#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <cstring>

#include "../headers/aibiblesettings.hpp"
#include "../headers/biblereferenceassistant.hpp"
#include "../headers/transcriptstream.hpp"

namespace {
class MockReply : public QNetworkReply
{
public:
    MockReply(const QNetworkRequest &request, const QByteArray &data, int status, bool delayed, QObject *parent)
        : QNetworkReply(parent), bytes(data)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (status >= 400)
            setError(ProtocolInvalidOperationError, "Simulated HTTP error");
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (!delayed)
            QTimer::singleShot(0, this, [this]() { complete(); });
    }
    void abort() override
    {
        setError(OperationCanceledError, "Cancelled");
        complete();
    }
    void complete()
    {
        if (isFinished())
            return;
        setFinished(true);
        emit readyRead();
        emit finished();
    }
    qint64 bytesAvailable() const override { return bytes.size() - position + QNetworkReply::bytesAvailable(); }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 count = qMin(maxSize, qint64(bytes.size() - position));
        if (count <= 0)
            return -1;
        std::memcpy(data, bytes.constData() + position, size_t(count));
        position += count;
        return count;
    }

private:
    QByteArray bytes;
    qint64 position = 0;
};

struct Scripted
{
    QByteArray body;
    int status = 200;
};

class MockNetwork : public QNetworkAccessManager
{
public:
    QList<Scripted> script;
    QList<QNetworkRequest> requests;
    QList<QJsonObject> bodies;
    bool delayed = false;
    QPointer<MockReply> lastReply;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *body) override
    {
        requests.append(request);
        bodies.append(QJsonDocument::fromJson(body ? body->readAll() : QByteArray()).object());
        const Scripted next = script.isEmpty() ? Scripted{QByteArray("{}"), 500} : script.takeFirst();
        auto *reply = new MockReply(request, next.body, next.status, delayed, this);
        lastReply = reply;
        return reply;
    }
};

class FakeBible : public BibleSelectionProvider
{
public:
    BibleSelection selection{QString::fromUtf8("Бытие"), 1, 1};
    bool paused = false;
    QList<BibleBookInfo> assistantBooks() const override
    {
        QList<BibleBookInfo> books{BibleBookInfo{QString::fromUtf8("Бытие"), 50, {}},
                                   BibleBookInfo{QString::fromUtf8("Псалтирь"), 150, {}},
                                   BibleBookInfo{QString::fromUtf8("От Иоанна"), 21, {}}};
        for (BibleBookInfo &book : books)
            for (int i = 0; i < book.chapterCount; ++i)
                book.verseCounts.append(24);
        books[1].verseCounts[117] = 176;
        return books;
    }
    int assistantVerseCount(const QString &, int) const override { return 24; }
    int assistantMaxVerses() const override { return 176; }
    BibleSelection assistantSelection() const override { return selection; }
    bool assistantPaused() const override { return paused; }
};

QByteArray answers(const QJsonObject &a)
{
    return QJsonDocument(QJsonObject{{"id", "dec-1"}, {"model", "cloudflare/clef-flash"}, {"answers", a},
                                     {"usage", QJsonObject{{"input_tokens", 10}, {"output_tokens", 1}}}}).toJson();
}

QJsonObject choice(const QString &label, double p)
{
    return QJsonObject{{"type", "choice"}, {"choice", label}, {"confidence", p},
                       {"probabilities", QJsonObject{{label, p}}}};
}

// One Decisions response answering all four questions.
QByteArray decide(double reference, const QString &book, double pBook,
                  const QString &chapter = "keep", double pChapter = 0.9,
                  const QString &verse = "keep", double pVerse = 0.9)
{
    return answers(QJsonObject{{"reference", QJsonObject{{"type", "noul"}, {"noul", reference}}},
                               {"book", choice(book, pBook)}, {"chapter", choice(chapter, pChapter)},
                               {"verse", choice(verse, pVerse)}});
}

AiBibleSettings usable()
{
    AiBibleSettings s;
    s.enabled = true;
    s.apiKey = "or-test-key";
    s.transcriptUrl = "https://example.com/api/data/gettranscript?organization=X";
    s.minConfidence = 0.6;
    return s;
}
}

class AiBibleAssistantTest : public QObject
{
    Q_OBJECT
private slots:
    void sseParsesFeedAcrossChunksAndSkipsDuplicates()
    {
        TranscriptStream stream;
        QSignalSpy spy(&stream, &TranscriptStream::lineReceived);
        const QByteArray feed = QString::fromUtf8(
            ": connected\n\n"
            "data: {\"id\":3322,\"text\":\"Давид также записал\"}\r\n\r\n"
            ": keep-alive\n\n"
            "data: {\"id\":3323,\"text\":\"тридцать восьмой псалом.\"}\n\n"
            "data: {\"id\":3323,\"text\":\"тридцать восьмой псалом.\"}\n\n"
            "data: {\"id\":3324,\"text\":\"Да, сто тридцать восьмой псалом.\"}\n\n").toUtf8();
        // Split inside a multi-byte character and inside a line.
        stream.feed(feed.left(41));
        stream.feed(feed.mid(41, 7));
        stream.feed(feed.mid(48));
        QCOMPARE(int(spy.count()), 3);
        QCOMPARE(spy.at(0).at(0).toLongLong(), qint64(3322));
        QCOMPARE(spy.at(0).at(1).toString(), QString::fromUtf8("Давид также записал"));
        QCOMPARE(spy.at(2).at(1).toString(), QString::fromUtf8("Да, сто тридцать восьмой псалом."));
    }

    void singleRequestSelectsBookChapterAndVerse()
    {
        MockNetwork network;
        FakeBible bible;
        BibleReferenceAssistant assistant(&bible, nullptr, &network);
        assistant.setSettings(usable());
        QSignalSpy spy(&assistant, &BibleReferenceAssistant::suggestion);
        network.script = {{decide(0.93, QString::fromUtf8("Псалтирь"), 0.91, "138", 0.82, "keep", 0.7)}};
        assistant.addLine(QString::fromUtf8("Да, сто тридцать восьмой псалом."));
        QTRY_COMPARE(int(spy.count()), 1);
        const BibleSelection s = spy.at(0).at(0).value<BibleSelection>();
        QCOMPARE(s.book, QString::fromUtf8("Псалтирь"));
        QCOMPARE(s.chapter, 138);
        QCOMPARE(s.verse, 1);
        QCOMPARE(int(network.requests.size()), 1);
        const QNetworkRequest &r = network.requests.first();
        QCOMPARE(r.url().toString(), QString("https://openrouter.ai/api/alpha/decisions"));
        QCOMPARE(r.rawHeader("Authorization"), QByteArray("Bearer or-test-key"));
        const QJsonObject first = network.bodies.first();
        QCOMPARE(first.value("model").toString(), QString("cloudflare/clef-flash"));
        QVERIFY(first.value("questions").toObject().value("book").toObject().value("criteria").toObject()
                .contains(QString::fromUtf8("От Иоанна")));
        QCOMPARE(first.value("state").toObject().value("new_lines").toString(),
                 QString::fromUtf8("Да, сто тридцать восьмой псалом."));
        const QJsonObject questions = first.value("questions").toObject();
        QCOMPARE(int(questions.size()), 4);
        const QJsonObject state = first.value("state").toObject();
        QCOMPARE(state.value("current_book").toString(), QString::fromUtf8("Бытие"));
        QCOMPARE(state.value("current_chapter").toInt(), 1);
        const QJsonObject available = state.value("available_books").toObject();
        QCOMPARE(int(available.size()), 3);
        QCOMPARE(int(available.value(QString::fromUtf8("Псалтирь")).toArray().size()), 150);
        QCOMPARE(available.value(QString::fromUtf8("Псалтирь")).toArray().at(117).toInt(), 176);
        QCOMPARE(int(questions.value("chapter").toObject().value("criteria").toObject().size()), 151); // keep + 1..150
        QCOMPARE(int(questions.value("verse").toObject().value("criteria").toObject().size()), 177);   // keep + 1..176
    }

    void bookThenCommentaryThenChapter()
    {
        MockNetwork network;
        FakeBible bible;
        BibleReferenceAssistant assistant(&bible, nullptr, &network);
        assistant.setSettings(usable());
        QSignalSpy spy(&assistant, &BibleReferenceAssistant::suggestion);
        connect(&assistant, &BibleReferenceAssistant::suggestion, this,
                [&bible](const BibleSelection &s, double) { bible.selection = s; });
        // 1. Book announced: John is preselected at 1:1.
        network.script = {{decide(0.9, QString::fromUtf8("От Иоанна"), 0.85)}};
        assistant.addLine(QString::fromUtf8("Сегодня я буду читать из Евангелия от Иоанна."));
        QTRY_COMPARE(int(spy.count()), 1);
        QCOMPARE(bible.selection.book, QString::fromUtf8("От Иоанна"));
        QCOMPARE(bible.selection.chapter, 1);
        // 2. Commentary: no reference, nothing changes.
        for (int i = 0; i < 8; ++i) {
            network.script = {{decide(0.05, "keep", 0.9)}};
            const int before = int(network.requests.size());
            assistant.addLine(QString::fromUtf8("Комментарий номер %1 о жизни и вере.").arg(i));
            QTRY_COMPARE(int(network.requests.size()), before + 1);
            QTest::qWait(20);
        }
        QCOMPARE(int(spy.count()), 1);
        // 3. Chapter spoken much later: the earlier hint is still in the state.
        network.script = {{decide(0.9, "keep", 0.8, "3", 0.8, "16", 0.75)}};
        const int before = int(network.requests.size());
        assistant.addLine(QString::fromUtf8("Третья глава, шестнадцатый стих."));
        QTRY_COMPARE(int(spy.count()), 2);
        QCOMPARE(bible.selection.book, QString::fromUtf8("От Иоанна"));
        QCOMPARE(bible.selection.chapter, 3);
        QCOMPARE(bible.selection.verse, 16);
        QVERIFY(network.bodies.at(before).value("state").toObject().value("recent_reference_lines").toString()
                .contains(QString::fromUtf8("от Иоанна")));
    }

    void numbersAreValidatedAgainstTheBook()
    {
        // "140" is the model's top pick but John has only 21 chapters.
        const QJsonObject a{{"chapter", QJsonObject{{"type", "choice"}, {"choice", "140"},
            {"probabilities", QJsonObject{{"140", 0.6}, {"14", 0.3}, {"keep", 0.1}}}}}};
        double p = 0.0;
        QCOMPARE(BibleReferenceAssistant::bestNumber(a, "chapter", 21, &p), 14);
        QCOMPARE(p, 0.3);
        QCOMPARE(BibleReferenceAssistant::bestNumber(a, "chapter", 150, &p), 140);
    }

    void noReferenceStopsAfterOneCall()
    {
        MockNetwork network;
        FakeBible bible;
        BibleReferenceAssistant assistant(&bible, nullptr, &network);
        assistant.setSettings(usable());
        QSignalSpy spy(&assistant, &BibleReferenceAssistant::suggestion);
        QSignalSpy status(&assistant, &BibleReferenceAssistant::statusChanged);
        network.script = {{decide(0.1, "keep", 0.9)}};
        assistant.addLine(QString::fromUtf8("Он всегда с нами."));
        QTRY_VERIFY(status.count() >= 2);
        QTest::qWait(50);
        QCOMPARE(int(network.requests.size()), 1);
        QCOMPARE(int(spy.count()), 0);
    }

    void lowConfidenceKeepsSelection()
    {
        MockNetwork network;
        FakeBible bible;
        BibleReferenceAssistant assistant(&bible, nullptr, &network);
        assistant.setSettings(usable());
        QSignalSpy spy(&assistant, &BibleReferenceAssistant::suggestion);
        network.script = {{decide(0.9, QString::fromUtf8("Псалтирь"), 0.4, "5", 0.3, "3", 0.2)}};
        assistant.addLine("Psalm five verse three, maybe");
        QTRY_COMPARE(int(network.requests.size()), 1);
        QTest::qWait(50);
        QCOMPARE(int(spy.count()), 0);
    }

    void operatorChangeDiscardsInFlightResult()
    {
        MockNetwork network;
        network.delayed = true;
        FakeBible bible;
        BibleReferenceAssistant assistant(&bible, nullptr, &network);
        assistant.setSettings(usable());
        QSignalSpy spy(&assistant, &BibleReferenceAssistant::suggestion);
        network.script = {{decide(0.95, QString::fromUtf8("От Иоанна"), 0.95)}};
        assistant.addLine(QString::fromUtf8("Откроем Евангелие от Иоанна"));
        QTRY_COMPARE(int(network.requests.size()), 1);
        assistant.operatorTookOver();
        QVERIFY(!assistant.isBusy());
        QTest::qWait(50);
        QCOMPARE(int(spy.count()), 0);
    }

    void rejectedKeyHaltsUntilSettingsChange()
    {
        MockNetwork network;
        FakeBible bible;
        BibleReferenceAssistant assistant(&bible, nullptr, &network);
        assistant.setSettings(usable());
        QSignalSpy failed(&assistant, &BibleReferenceAssistant::failed);
        network.script = {{QByteArray("{\"error\":{\"message\":\"bad key or-test-key\"}}"), 401}};
        assistant.addLine("Psalm 23");
        QTRY_COMPARE(int(failed.count()), 1);
        QVERIFY(!failed.at(0).at(0).toString().contains("or-test-key"));
        assistant.addLine("Psalm 24");
        QTest::qWait(400);
        QCOMPARE(int(network.requests.size()), 1);
    }

    void settingsRoundTrip()
    {
        QTemporaryDir dir;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE");
            db.setDatabaseName(dir.filePath("test.sqlite"));
            QVERIFY(db.open());
            QString error;
            QVERIFY2(AiBibleSettings::ensureTable(&error), qPrintable(error));
            AiBibleSettings defaults;
            QVERIFY(defaults.load(&error));
            QCOMPARE(defaults.model, AiBibleSettings::defaultModel());
            QVERIFY(!defaults.enabled);
            AiBibleSettings s = usable();
            s.pauseSeconds = 7;
            QVERIFY2(s.save(&error), qPrintable(error));
            AiBibleSettings loaded;
            QVERIFY(loaded.load(&error));
            QCOMPARE(loaded.apiKey, s.apiKey);
            QCOMPARE(loaded.transcriptUrl, s.transcriptUrl);
            QCOMPARE(loaded.pauseSeconds, 7);
            QVERIFY(loaded.enabled);
            AiBibleSettings bad = usable();
            bad.transcriptUrl = "ftp://example.com/feed";
            QVERIFY(!bad.save(&error));
            db.close();
        }
        QSqlDatabase::removeDatabase(QSqlDatabase::defaultConnection);
    }
};

QTEST_MAIN(AiBibleAssistantTest)
#include "tst_aibibleassistant.moc"
