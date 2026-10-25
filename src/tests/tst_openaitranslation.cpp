#include <QtTest>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <cstring>

#include "../headers/editwidget.hpp"
#include "../headers/openaiclient.hpp"
#include "../headers/openaisettingswidget.hpp"
#include "../headers/songtranslationdialog.hpp"

namespace {
const QString original = QStringLiteral("Verse 1\nGod is love\nSing His praise\n=Old first line\nOld second line\n\nChorus\nAlleluia");
const QString proposal = QString::fromUtf8("Verse 1\nGod is love\nSing His praise\n=\nБог есть любовь\nВоспой Ему хвалу\n\nChorus\nAlleluia\n=\nАллилуйя");

QJsonObject translationPayload()
{
    return {{"stanzas", QJsonArray{
        QJsonObject{{"id", 0}, {"lines", QJsonArray{QString::fromUtf8("Бог есть любовь"), QString::fromUtf8("Воспой Ему хвалу")}}},
        QJsonObject{{"id", 1}, {"lines", QJsonArray{QString::fromUtf8("Аллилуйя")}}}}}};
}

QByteArray response(const QJsonObject &payload = translationPayload())
{
    const QJsonObject content{{"type", "output_text"},
        {"text", QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))}};
    const QJsonObject message{{"type", "message"}, {"role", "assistant"}, {"content", QJsonArray{content}}};
    const QJsonArray output{QJsonObject{{"type", "reasoning"}, {"summary", QJsonArray{}}}, message};
    return QJsonDocument(QJsonObject{{"status", "completed"}, {"output", output}}).toJson(QJsonDocument::Compact);
}

OpenAiSettings configuration()
{
    OpenAiSettings settings;
    settings.apiKey = "test-key-with-'quote";
    settings.model = "future-text-model";
    settings.reasoningEffort = "low";
    return settings;
}

QByteArray modelList(const QStringList &models)
{
    QJsonArray data;
    for (const QString &model : models)
        data.append(QJsonObject{{"id", model}});
    return QJsonDocument(QJsonObject{{"data", data}}).toJson();
}

class MockReply : public QNetworkReply
{
public:
    MockReply(const QNetworkRequest &request, const QByteArray &data, int status,
              NetworkError error, bool delayed, QObject *parent) : QNetworkReply(parent), bytes(data)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (error != NoError)
            setError(error, "Simulated failure");
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

class MockNetwork : public QNetworkAccessManager
{
public:
    QByteArray nextResponse = response();
    int nextStatus = 200;
    QNetworkReply::NetworkError nextError = QNetworkReply::NoError;
    QByteArray modelCheckResponse;
    int modelCheckStatus = 200;
    QNetworkReply::NetworkError modelCheckError = QNetworkReply::NoError;
    bool delayed = false;
    int calls = 0;
    QNetworkRequest lastRequest;
    QByteArray lastBody;
    Operation lastOperation = GetOperation;
    QPointer<MockReply> lastReply;
    QList<QNetworkRequest> requests;
    QList<QByteArray> bodies;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *body) override
    {
        ++calls;
        lastRequest = request;
        lastBody = body ? body->readAll() : QByteArray();
        lastOperation = operation;
        requests.append(request);
        bodies.append(lastBody);
        const bool checkModel = operation == GetOperation && request.url().path().startsWith("/v1/models/");
        QByteArray data = nextResponse;
        if (checkModel) {
            data = modelCheckResponse.isEmpty() ? QJsonDocument(QJsonObject{
                {"id", request.url().path().mid(QString("/v1/models/").size())}}).toJson() : modelCheckResponse;
        }
        lastReply = new MockReply(request, data, checkModel ? modelCheckStatus : nextStatus,
            checkModel ? modelCheckError : nextError, delayed, this);
        return lastReply;
    }
};

QString storedSong()
{
    QSqlQuery query("SELECT song_text FROM Songs WHERE id = 1");
    return query.next() ? query.value(0).toString() : QString();
}
}

class OpenAiTranslationTest : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        auto database = QSqlDatabase::addDatabase("QSQLITE");
        database.setDatabaseName(":memory:");
        QVERIFY(database.open());
        QString error;
        QVERIFY2(OpenAiSettings::ensureTable(&error), qPrintable(error));
    }

    void cleanup()
    {
        QSqlDatabase::database(QSqlDatabase::defaultConnection, false).close();
        QSqlDatabase::removeDatabase(QSqlDatabase::defaultConnection);
    }

    void existingDatabaseMigrationAndPersistence()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto database = QSqlDatabase::database();
        database.close();
        database.setDatabaseName(directory.filePath("existing.sqlite"));
        QVERIFY(database.open());
        {
            QSqlQuery query;
            QVERIFY(query.exec("CREATE TABLE Songs (id INTEGER, song_text TEXT)"));
            QVERIFY(query.exec("INSERT INTO Songs VALUES (1, 'original song')"));
            QVERIFY(query.exec("PRAGMA user_version = 2"));
        }
        QString error;
        QVERIFY2(OpenAiSettings::ensureTable(&error), qPrintable(error));
        OpenAiSettings settings;
        QVERIFY(settings.load(&error));
        QVERIFY(settings.apiKey.isEmpty());
        QVERIFY(settings.model.isEmpty());
        settings = configuration();
        QVERIFY2(settings.save(&error), qPrintable(error));
        database.close();
        QVERIFY(database.open());
        QVERIFY(OpenAiSettings::ensureTable(&error));
        QVERIFY(OpenAiSettings::ensureTable(&error));
        OpenAiSettings reloaded;
        QVERIFY(reloaded.load(&error));
        QCOMPARE(reloaded.apiKey, settings.apiKey);
        QCOMPARE(reloaded.model, settings.model);
        QCOMPARE(reloaded.reasoningEffort, settings.reasoningEffort);
        QCOMPARE(storedSong(), QString("original song"));
        QSqlQuery count("SELECT COUNT(*) FROM OpenAiSettings");
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 1);
        QSqlQuery version("PRAGMA user_version");
        QVERIFY(version.next());
        QCOMPARE(version.value(0).toInt(), 2);
        database.close();
    }

    void reasoningChoicesAndValidation()
    {
        QCOMPARE(OpenAiSettings::reasoningEfforts(), QStringList({"none", "minimal", "low", "medium", "high", "xhigh", "max"}));
        OpenAiSettings settings = configuration();
        QString error;
        QVERIFY(settings.save(&error)); // Future API model IDs need no application update.
        settings.reasoningEffort = "invented effort";
        QVERIFY(!settings.save(&error));
        settings.reasoningEffort.clear();
        QVERIFY(settings.save(&error));
        settings.model.clear();
        QVERIFY(!settings.save(&error));
        settings.apiKey.clear();
        QVERIFY(settings.save(&error)); // Unconfigured OpenAI must not block ordinary Settings.
        settings.apiKey = "bad\nkey";
        QVERIFY(!settings.validate(&error));
    }

    void existingTableDefaultDoesNotChooseAModel()
    {
        QSqlQuery query;
        QVERIFY(query.exec("DROP TABLE OpenAiSettings"));
        QVERIFY(query.exec("CREATE TABLE OpenAiSettings (id INTEGER PRIMARY KEY, api_key TEXT DEFAULT '', "
                           "model TEXT DEFAULT 'obsolete-preset', reasoning_effort TEXT DEFAULT '')"));
        QString error;
        QVERIFY(OpenAiSettings::ensureTable(&error));
        OpenAiSettings settings;
        QVERIFY(settings.load(&error));
        QVERIFY(settings.model.isEmpty());
    }

    void clearingUnavailableModelPreservesReplacement()
    {
        OpenAiSettings settings = configuration();
        QString error;
        QVERIFY(settings.save(&error));
        const QString oldModel = settings.model;
        settings.model = "replacement-model";
        QVERIFY(settings.save(&error));
        QVERIFY(OpenAiSettings::clearUnavailableModel(settings.apiKey, oldModel, &error));
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.model, QString("replacement-model"));
        QVERIFY(OpenAiSettings::clearUnavailableModel("different-key", settings.model, &error));
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.model, QString("replacement-model"));
    }

    void sourceAndRequestPreserveStoredStructure()
    {
        SongTranslation translation;
        QString error;
        QVERIFY2(translation.parse(original, &error), qPrintable(error));
        QCOMPARE(translation.stanzas().count(), 2);
        QCOMPARE(translation.stanzas().first().lines.count(), 2);
        OpenAiSettings settings = configuration();
        const QJsonObject request = translation.request(settings, "Song title");
        QCOMPARE(request.value("store").toBool(), false);
        QVERIFY(!QJsonDocument(request).toJson().contains(settings.apiKey.toUtf8()));
        QVERIFY(!QJsonDocument(request).toJson().contains("Old first line"));
        QCOMPARE(request.value("reasoning").toObject().value("effort").toString(), QString("low"));
        const QString input = request.value("input").toArray().first().toObject().value("content").toString();
        const QJsonObject source = QJsonDocument::fromJson(input.toUtf8()).object();
        QCOMPARE(source.value("stanzas").toArray().count(), 2);
        QVERIFY(!source.contains("target_language"));
        QVERIFY(!request.value("instructions").toString().contains("target_language"));
        const QJsonObject format = request.value("text").toObject().value("format").toObject();
        QCOMPARE(format.value("type").toString(), QString("json_schema"));
        QVERIFY(format.value("strict").toBool());
        QCOMPARE(format.value("schema").toObject().value("additionalProperties").toBool(), false);
        settings.reasoningEffort.clear();
        QVERIFY(!translation.request(settings, "Title").contains("reasoning"));
        settings.model = "another-future-model";
        QCOMPARE(translation.request(settings, "Title").value("max_output_tokens").toInt(), 16384);
    }

    void proposalMatchesProjectionAndRetranslatesOriginal()
    {
        SongTranslation translation;
        QString error, text;
        QVERIFY(translation.parse(original, &error));
        QVERIFY2(translation.readResponse(response(), &text, &error), qPrintable(error));
        QCOMPARE(text, proposal);
        Song song;
        song.songText = text;
        song.useTranslation = true;
        QCOMPARE(song.getSongTextList(true).count(), 2);
        for (const QString &block : song.getSongTextList(true)) {
            const QStringList halves = block.split('=');
            QCOMPARE(halves.count(), 2);
            QCOMPARE(halves.first().trimmed().split('\n').count() - 1,
                     halves.last().trimmed().split('\n').count());
        }
        QVERIFY(translation.parse(text, &error));
        QCOMPARE(translation.stanzas().first().lines, QStringList({"God is love", "Sing His praise"}));
        QVERIFY(translation.parse("Chorus\nAlleluia\n\nVerse 1\nGod is love", &error));
        QCOMPARE(translation.stanzas().count(), 2);
        song.songText = "Chorus\nAlleluia\n\nVerse 1\nGod is love";
        QCOMPARE(song.getSongTextList(true).count(), 3); // Source blocks are not expanded for the API.
        QVERIFY(translation.parse(QString::fromUtf8("Куплет 1\nБог є любов\n\n&Куплет 1\nСпівай\n\nПриспів\nАлілуя"), &error));
        QCOMPARE(translation.stanzas().count(), 3);
        QCOMPARE(translation.stanzas().at(1).heading, QString::fromUtf8("&Куплет 1"));
    }

    void mixedLanguageReferenceKeepsHeadersAndParentheticalLyrics()
    {
        const QString lyrics = QString::fromUtf8(
            "Куплет 1.\nБлагодарю, Тебя я,\nГосподь из Назарета,\nЗа счастье, что Тобою мне дано,\n"
            "3а то, что вот однажды\nМеня Ты в жизни встретил,\nИ стало на путях моих светло.\n\n"
            "Припев:\nБлагослови Господа (душа моя)!\nБлагослови Господа (душа моя)!\nGod is my Savior!\n(2 раза)\n\n"
            "Verse\nI am here for you.\n\nКуплет 2\nБог є любов.");
        const QStringList verseTranslation{"I thank you,", "Lord of Nazareth,",
            "For the happiness that You have given me,", "That one day", "You met me in life,",
            "And it became light on my paths."};
        const QStringList chorusTranslation{"Bless the Lord, (O my soul)!", "Bless the Lord, (O my soul)!",
            QString::fromUtf8("Бог мой Спаситель!"), "(2 times)"};
        const QJsonObject payload{{"stanzas", QJsonArray{
            QJsonObject{{"id", 0}, {"lines", QJsonArray::fromStringList(verseTranslation)}},
            QJsonObject{{"id", 1}, {"lines", QJsonArray::fromStringList(chorusTranslation)}},
            QJsonObject{{"id", 2}, {"lines", QJsonArray{QString::fromUtf8("Я здесь для тебя.")}}},
            QJsonObject{{"id", 3}, {"lines", QJsonArray{"God is love."}}}}}};
        MockNetwork network;
        network.nextResponse = response(payload);
        OpenAiClient client(nullptr, &network);
        QSignalSpy translated(&client, &OpenAiClient::translationReady);
        QSignalSpy failed(&client, &OpenAiClient::failed);
        client.translate(configuration(), lyrics, "Reference hymn");
        QTRY_COMPARE(translated.count(), 1);
        QCOMPARE(failed.count(), 0);
        const QString text = translated.first().first().toString();
        const QStringList originals = lyrics.split("\n\n");
        const QStringList expectedBlocks{
            originals.at(0) + "\n=\n" + verseTranslation.join('\n'),
            originals.at(1) + "\n=\n" + chorusTranslation.join('\n'),
            originals.at(2) + QString::fromUtf8("\n=\nЯ здесь для тебя."),
            originals.at(3) + "\n=\nGod is love."};
        QCOMPARE(text, expectedBlocks.join("\n\n"));
        QCOMPARE(text.count(QString::fromUtf8("Куплет 1.")), 1);
        QCOMPARE(text.count(QString::fromUtf8("Припев:")), 1);
        QVERIFY(!text.contains("```"));
        Song song;
        song.songText = text;
        song.useTranslation = true;
        for (const QString &block : song.getSongTextList(true)) {
            const QStringList halves = block.split('=');
            QCOMPARE(halves.count(), 2);
            QCOMPARE(halves.first().trimmed().split('\n').count() - 1,
                     halves.last().trimmed().split('\n').count());
        }
        // Re-translation still receives original mixed-language lyrics only.
        SongTranslation again;
        QString error;
        QVERIFY2(again.parse(text, &error), qPrintable(error));
        QCOMPARE(again.stanzas().count(), 4);
        QCOMPARE(again.stanzas().at(1).lines.at(2), QString("God is my Savior!"));
        QCOMPARE(again.stanzas().at(3).lines.first(), QString::fromUtf8("Бог є любов."));
        const QJsonObject request = QJsonDocument::fromJson(network.lastBody).object();
        const QJsonObject input = QJsonDocument::fromJson(request.value("input").toArray()
            .first().toObject().value("content").toString().toUtf8()).object();
        QVERIFY(!input.contains("target_language"));
        QCOMPARE(input.value("stanzas").toArray().at(1).toObject().value("lines").toArray().at(2).toString(),
                 QString("God is my Savior!"));
    }

    void pastedWindowsNewlinesRemoveBlankLinesWithoutChangingLyrics()
    {
        const QString lyrics = QString::fromUtf8(
            "\r\nVerse 1.\r\n\r\n  I am here for you.  \r\n \t\r\n(2 times)\r\n"
            "\r\n\r\nChorus:\r\nБог є любов.\r\n\r\n");
        SongTranslation translation;
        QString error, text;
        QVERIFY2(translation.parse(lyrics, &error), qPrintable(error));
        QCOMPARE(translation.stanzas().count(), 2);
        QCOMPARE(translation.stanzas().first().heading, QString("Verse 1."));
        QCOMPARE(translation.stanzas().first().lines, QStringList({"  I am here for you.  ", "(2 times)"}));
        const QJsonObject payload{{"stanzas", QJsonArray{
            QJsonObject{{"id", 0}, {"lines", QJsonArray{QString::fromUtf8("Я здесь для тебя."), QString::fromUtf8("(2 раза)")}}},
            QJsonObject{{"id", 1}, {"lines", QJsonArray{"God is love."}}}}}};
        QVERIFY2(translation.readResponse(response(payload), &text, &error), qPrintable(error));
        QCOMPARE(text, QString::fromUtf8("Verse 1.\n  I am here for you.  \n(2 times)\n=\n"
            "Я здесь для тебя.\n(2 раза)\n\nChorus:\nБог є любов.\n=\nGod is love."));
        QVERIFY(!text.contains('\r'));
        SongTranslation again;
        QVERIFY(again.parse(text, &error));
        QCOMPARE(again.stanzas().first().lines, translation.stanzas().first().lines);
    }

    void omittedParenthesesCannotBeAccepted_data()
    {
        QTest::addColumn<QStringList>("translatedLines");
        QTest::newRow("omitted backing vocal") << QStringList{QString::fromUtf8("Благослови Господа!"), QString::fromUtf8("(2 раза)")};
        QTest::newRow("unwrapped repetition instruction") << QStringList{QString::fromUtf8("Благослови Господа (душа моя)!"), QString::fromUtf8("2 раза")};
        QTest::newRow("unclosed parenthesis") << QStringList{QString::fromUtf8("Благослови Господа (душа моя)!"), QString::fromUtf8("(2 раза")};
    }

    void omittedParenthesesCannotBeAccepted()
    {
        QFETCH(QStringList, translatedLines);
        SongTranslation translation;
        QString error, text;
        QVERIFY(translation.parse("Chorus\nBless the Lord (O my soul)!\n(2 times)", &error));
        const QJsonObject payload{{"stanzas", QJsonArray{QJsonObject{
            {"id", 0}, {"lines", QJsonArray::fromStringList(translatedLines)}}}}};
        QVERIFY(!translation.readResponse(response(payload), &text, &error));
        QVERIFY(text.isEmpty());
        QVERIFY(error.contains("parentheses"));
    }

    void invalidSource_data()
    {
        QTest::addColumn<QString>("lyrics");
        QTest::newRow("empty") << QString();
        QTest::newRow("missing heading") << QString("God is love");
        QTest::newRow("empty stanza") << QString("Verse 1\n\nChorus\nAlleluia");
        QTest::newRow("inline separator") << QString("Verse 1\nLyrics=translation");
    }

    void invalidSource()
    {
        QFETCH(QString, lyrics);
        SongTranslation translation;
        QString error;
        QVERIFY(!translation.parse(lyrics, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(translation.stanzas().isEmpty());
    }

    void malformedResponse_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::newRow("invalid JSON") << QByteArray("not JSON");
        QTest::newRow("incomplete") << QByteArray("{\"status\":\"incomplete\",\"output\":[]}");
        QTest::newRow("refusal") << QByteArray("{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"role\":\"assistant\",\"content\":[{\"type\":\"refusal\",\"refusal\":\"No\"}]}]}");
        QTest::newRow("missing stanzas") << response(QJsonObject{});
        for (const QString &kind : QStringList{"wrong order", "line count", "newline", "separator", "heading", "empty line", "nonstring", "fractional id"}) {
            QJsonObject payload = translationPayload();
            QJsonArray stanzas = payload.value("stanzas").toArray();
            QJsonObject first = stanzas.first().toObject();
            QJsonArray lines = first.value("lines").toArray();
            if (kind == "wrong order") first.insert("id", 1);
            else if (kind == "fractional id") first.insert("id", 0.5);
            else if (kind == "line count") lines.removeLast();
            else if (kind == "newline") lines[0] = "one\ntwo";
            else if (kind == "separator") lines[0] = "bad=text";
            else if (kind == "heading") lines[0] = "Verse 3";
            else if (kind == "empty line") lines[0] = "";
            else if (kind == "nonstring") lines[0] = 7;
            first.insert("lines", lines);
            stanzas[0] = first;
            payload.insert("stanzas", stanzas);
            QTest::newRow(qPrintable(kind)) << response(payload);
        }
    }

    void malformedResponse()
    {
        QFETCH(QByteArray, data);
        SongTranslation translation;
        QString error, text = "previous proposal";
        QVERIFY(translation.parse(original, &error));
        QVERIFY(!translation.readResponse(data, &text, &error));
        QVERIFY(text.isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void networkRequestAndModelList()
    {
        MockNetwork network;
        OpenAiClient client(nullptr, &network);
        QSignalSpy translated(&client, &OpenAiClient::translationReady);
        client.translate(configuration(), original, "Title");
        QVERIFY(client.isBusy());
        QTRY_COMPARE(translated.count(), 1);
        QCOMPARE(translated.first().first().toString(), proposal);
        QCOMPARE(network.calls, 2);
        QCOMPARE(network.requests.first().url(), QUrl("https://api.openai.com/v1/models/future-text-model"));
        QVERIFY(network.bodies.first().isEmpty()); // Availability lookup sends no lyrics.
        QCOMPARE(network.lastOperation, QNetworkAccessManager::PostOperation);
        QCOMPARE(network.lastRequest.url(), QUrl("https://api.openai.com/v1/responses"));
        QCOMPARE(network.lastRequest.rawHeader("Authorization"), "Bearer " + configuration().apiKey.toUtf8());
        QCOMPARE(network.lastRequest.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(), int(QNetworkRequest::ManualRedirectPolicy));
        QVERIFY(!client.isBusy());
        network.nextResponse = "{\"data\":[{\"id\":\"future-text-model\",\"created\":300},{\"id\":\"future-text-model\",\"created\":300},"
            "{\"id\":\"arbitrary-api-model\",\"created\":200},{\"id\":\"unknown-family-2099\",\"created\":100},"
            "{\"id\":\"retired-model\",\"shutdown_date\":\"2000-01-01\"}]}";
        QSignalSpy models(&client, &OpenAiClient::modelsReady);
        client.fetchModels(configuration().apiKey);
        QTRY_COMPARE(models.count(), 1);
        QCOMPARE(models.first().first().toStringList(), QStringList({"future-text-model", "arbitrary-api-model", "unknown-family-2099"}));
        QCOMPARE(network.lastOperation, QNetworkAccessManager::GetOperation);
    }

    void modelDatesSortNewestFirstWithStableUndatedFallback()
    {
        MockNetwork network;
        network.nextResponse = "{\"data\":[{\"id\":\"z-undated\"},{\"id\":\"z-old\",\"created\":100},"
            "{\"id\":\"z-same-day\",\"created\":200},{\"id\":\"a-newest\",\"created\":300},"
            "{\"id\":\"a-undated\"},{\"id\":\"a-same-day\",\"created\":200}]}";
        OpenAiClient client(nullptr, &network);
        QSignalSpy models(&client, &OpenAiClient::modelsReady);
        client.fetchModels(configuration().apiKey);
        QTRY_COMPARE(models.count(), 1);
        QCOMPARE(models.first().first().toStringList(), QStringList({"a-newest", "a-same-day", "z-same-day", "z-old", "a-undated", "z-undated"}));
    }

    void failuresCancelAndTimeout()
    {
        MockNetwork network;
        OpenAiClient client(nullptr, &network);
        QSignalSpy failed(&client, &OpenAiClient::failed);
        QSignalSpy translated(&client, &OpenAiClient::translationReady);
        client.fetchModels("");
        QCOMPARE(network.calls, 0);
        QCOMPARE(failed.count(), 1);
        network.nextStatus = 401;
        network.nextError = QNetworkReply::AuthenticationRequiredError;
        client.fetchModels(configuration().apiKey);
        QTRY_COMPARE(failed.count(), 2);
        QVERIFY(failed.last().first().toString().contains("API key"));
        network.nextStatus = 400;
        network.nextResponse = QJsonDocument(QJsonObject{{"error", QJsonObject{{"message", configuration().apiKey}}}}).toJson();
        client.fetchModels(configuration().apiKey);
        QTRY_COMPARE(failed.count(), 3);
        QVERIFY(!failed.last().first().toString().contains(configuration().apiKey));
        network.nextStatus = 200;
        network.nextError = QNetworkReply::NoError;
        network.nextResponse = response();
        network.delayed = true;
        client.translate(configuration(), original, "Title");
        QPointer<MockReply> oldReply = network.lastReply;
        client.cancel();
        QVERIFY(!client.isBusy());
        if (oldReply) oldReply->complete();
        QCOMPARE(translated.count(), 0);
        QCOMPARE(failed.count(), 3);
        client.translate(configuration(), original, "Title");
        QTimer *timeout = network.lastReply->findChild<QTimer *>();
        QVERIFY(timeout);
        QVERIFY(QMetaObject::invokeMethod(timeout, "timeout"));
        QCOMPARE(failed.count(), 4);
        QVERIFY(failed.last().first().toString().contains("timed out"));
        QVERIFY(!client.isBusy());
    }

    void unavailableModelRequiresReplacement_data()
    {
        QTest::addColumn<int>("httpStatus");
        QTest::addColumn<QByteArray>("errorBody");
        QTest::addColumn<bool>("duringTranslation");
        QTest::newRow("missing on lookup") << 404 << QByteArray("{\"error\":{\"code\":\"model_not_found\"}}") << false;
        QTest::newRow("model access revoked") << 403 << QByteArray("{\"error\":{\"param\":\"model\"}}") << false;
        QTest::newRow("retired between lookup and translation") << 404 << QByteArray("{\"error\":{\"code\":\"model_not_found\"}}") << true;
        QTest::newRow("unsupported model") << 400 << QByteArray("{\"error\":{\"code\":\"unsupported_model\"}}") << true;
    }

    void unavailableModelRequiresReplacement()
    {
        QFETCH(int, httpStatus);
        QFETCH(QByteArray, errorBody);
        QFETCH(bool, duringTranslation);
        QString error;
        QVERIFY(configuration().save(&error));
        MockNetwork network;
        if (duringTranslation) {
            network.nextStatus = httpStatus;
            network.nextResponse = errorBody;
        } else {
            network.modelCheckStatus = httpStatus;
            network.modelCheckResponse = errorBody;
        }
        SongTranslationDialog dialog(original, "Title", configuration(), nullptr, &network);
        auto *generate = dialog.findChild<QPushButton *>("generateTranslation");
        auto *accept = dialog.findChild<QPushButton *>("acceptTranslation");
        auto *client = dialog.findChild<OpenAiClient *>();
        QSignalSpy failed(client, &OpenAiClient::failed);
        QSignalSpy unavailable(client, &OpenAiClient::modelUnavailable);
        generate->click();
        QTRY_COMPARE(unavailable.count(), 1);
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed.first().first().toString().contains("select a replacement"));
        QCOMPARE(network.calls, duringTranslation ? 2 : 1);
        QVERIFY(!generate->isEnabled());
        QVERIFY(!accept->isEnabled());
        QVERIFY(dialog.proposedSongText().isEmpty());
        OpenAiSettings stored;
        QVERIFY(stored.load(&error));
        QCOMPARE(stored.apiKey, configuration().apiKey);
        QVERIFY(stored.model.isEmpty());
        QVERIFY(stored.reasoningEffort.isEmpty());
        QVERIFY(OpenAiSettings::ensureTable(&error));
        QVERIFY(stored.load(&error));
        QVERIFY(stored.model.isEmpty()); // Starting again cannot restore a retired selection.
    }

    void shutdownDatePreventsTranslation()
    {
        QString error;
        QVERIFY(configuration().save(&error));
        MockNetwork network;
        network.modelCheckResponse = QJsonDocument(QJsonObject{
            {"id", configuration().model}, {"shutdown_date", "2000-01-01"}}).toJson();
        OpenAiClient client(nullptr, &network);
        QSignalSpy unavailable(&client, &OpenAiClient::modelUnavailable);
        client.translate(configuration(), original, "Title");
        QTRY_COMPARE(unavailable.count(), 1);
        QCOMPARE(network.calls, 1);
        OpenAiSettings settings;
        QVERIFY(settings.load(&error));
        QVERIFY(settings.model.isEmpty());
    }

    void temporaryFailuresPreserveModel_data()
    {
        QTest::addColumn<int>("httpStatus");
        QTest::addColumn<bool>("duringTranslation");
        for (const int status : {401, 429, 500}) {
            QTest::newRow(qPrintable(QString("lookup %1").arg(status))) << status << false;
            QTest::newRow(qPrintable(QString("translation %1").arg(status))) << status << true;
        }
    }

    void temporaryFailuresPreserveModel()
    {
        QFETCH(int, httpStatus);
        QFETCH(bool, duringTranslation);
        QString error;
        QVERIFY(configuration().save(&error));
        MockNetwork network;
        if (duringTranslation)
            network.nextStatus = httpStatus;
        else
            network.modelCheckStatus = httpStatus;
        OpenAiClient client(nullptr, &network);
        QSignalSpy failed(&client, &OpenAiClient::failed);
        QSignalSpy unavailable(&client, &OpenAiClient::modelUnavailable);
        client.translate(configuration(), original, "Title");
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(unavailable.count(), 0);
        OpenAiSettings settings;
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.model, configuration().model);
        QCOMPARE(settings.reasoningEffort, configuration().reasoningEffort);
    }

    void unsupportedEffortPreservesModelAndExplainsDefault()
    {
        QString error;
        QVERIFY(configuration().save(&error));
        MockNetwork network;
        network.nextStatus = 400;
        network.nextResponse = "{\"error\":{\"message\":\"Unsupported effort\",\"param\":\"reasoning.effort\",\"code\":\"unsupported_value\"}}";
        OpenAiClient client(nullptr, &network);
        QSignalSpy failed(&client, &OpenAiClient::failed);
        QSignalSpy unavailable(&client, &OpenAiClient::modelUnavailable);
        client.translate(configuration(), original, "Title");
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(failed.first().first().toString().contains("Choose Model default"));
        QCOMPARE(unavailable.count(), 0);
        OpenAiSettings settings;
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.model, configuration().model);
    }

    void settingsWidgetAppliesAndDiscardsDrafts()
    {
        MockNetwork network;
        network.nextResponse = modelList({configuration().model, "replacement-model"});
        OpenAiSettingsWidget widget(nullptr, &network);
        widget.loadSettings();
        auto *key = widget.findChild<QLineEdit *>("openAiApiKey");
        auto *model = widget.findChild<QComboBox *>("openAiModel");
        auto *effort = widget.findChild<QComboBox *>("openAiReasoningEffort");
        auto *refresh = widget.findChild<QPushButton *>("openAiRefreshModels");
        QCOMPARE(key->echoMode(), QLineEdit::Password);
        QVERIFY(model->isEditable());
        QCOMPARE(model->insertPolicy(), QComboBox::NoInsert);
        QCOMPARE(model->count(), 0);
        key->setText("draft-key");
        OpenAiSettings settings;
        QString error;
        QVERIFY(settings.load(&error));
        QVERIFY(settings.apiKey.isEmpty());
        widget.loadSettings();
        QVERIFY(key->text().isEmpty());
        key->setText(configuration().apiKey);
        refresh->click();
        QTRY_COMPARE(model->count(), 2);
        QCOMPARE(model->currentIndex(), -1); // Never pick the first API model automatically.
        model->setCurrentIndex(model->findText(configuration().model));
        effort->setCurrentIndex(effort->findData("max"));
        QVERIFY(widget.saveSettings());
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.reasoningEffort, QString("max"));
        QCOMPARE(settings.model, configuration().model);
        model->setCurrentIndex(model->findText("replacement-model"));
        widget.loadSettings(); // Discards draft replacement and verifies the saved model.
        QTRY_VERIFY(model->isEnabled());
        QCOMPARE(model->currentText(), configuration().model);
        QCOMPARE(effort->currentData().toString(), QString("max"));
        model->setCurrentIndex(model->findText("replacement-model"));
        QVERIFY(widget.saveSettings());
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.model, QString("replacement-model"));
        key->setText("a-different-account-key");
        QCOMPARE(model->count(), 0);
        QVERIFY(!model->isEnabled());
        QVERIFY(!effort->isEnabled());
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.apiKey, configuration().apiKey);
        refresh->click();
        QTRY_COMPARE(model->count(), 2);
        QCOMPARE(model->currentIndex(), -1);
    }

    void modelSearchFiltersWithoutSavingOrRetiringSearchText()
    {
        QString error;
        QVERIFY(configuration().save(&error));
        MockNetwork network;
        network.nextResponse = "{\"data\":[{\"id\":\"future-text-model\",\"created\":100},"
            "{\"id\":\"new-text-family\",\"created\":300},{\"id\":\"other-model\",\"created\":200}]}";
        OpenAiSettingsWidget widget(nullptr, &network);
        widget.loadSettings();
        auto *model = widget.findChild<QComboBox *>("openAiModel");
        auto *effort = widget.findChild<QComboBox *>("openAiReasoningEffort");
        auto *refresh = widget.findChild<QPushButton *>("openAiRefreshModels");
        QTRY_VERIFY(model->isEnabled());
        QCOMPARE(model->currentText(), configuration().model);
        widget.show();
        auto *search = model->lineEdit();
        search->setFocus();
        search->selectAll();
        QTest::keyClicks(search, "TEXT");
        auto *completer = model->completer();
        QTRY_COMPARE(completer->completionCount(), 2);
        QCOMPARE(completer->completionModel()->index(0, 0).data().toString(), QString("new-text-family"));
        QCOMPARE(completer->completionModel()->index(1, 0).data().toString(), configuration().model);
        QCOMPARE(model->currentData(Qt::DisplayRole).toString(), configuration().model);
        QCOMPARE(effort->currentData().toString(), configuration().reasoningEffort);
        QVERIFY(widget.saveSettings());
        OpenAiSettings stored;
        QVERIFY(stored.load(&error));
        QCOMPARE(stored.model, configuration().model);
        search->selectAll();
        QTest::keyClicks(search, "not-an-api-model");
        QTRY_COMPARE(completer->completionCount(), 0);
        QCOMPARE(model->count(), 3);
        // A refresh during a search must check the actual selection, not the query.
        refresh->click();
        QTRY_VERIFY(model->isEnabled());
        QCOMPARE(model->currentText(), configuration().model);
        QVERIFY(stored.load(&error));
        QCOMPARE(stored.model, configuration().model);
        search->setFocus();
        search->selectAll();
        QTest::keyClicks(search, "TEXT");
        QTRY_COMPARE(completer->completionCount(), 2);
        auto *popup = completer->popup();
        QTRY_VERIFY(popup->isVisible());
        const QModelIndex match = completer->completionModel()->index(0, 0);
        QTest::mouseClick(popup->viewport(), Qt::LeftButton, Qt::NoModifier, popup->visualRect(match).center());
        QTRY_COMPARE(model->currentData(Qt::DisplayRole).toString(), QString("new-text-family"));
        QVERIFY(widget.saveSettings());
        QVERIFY(stored.load(&error));
        QCOMPARE(stored.model, QString("new-text-family"));
        QCOMPARE(model->count(), 3);
    }

    void settingsRefreshRequiresReplacement_data()
    {
        QTest::addColumn<QStringList>("availableModels");
        QTest::newRow("saved model absent") << QStringList{"replacement-model"};
        QTest::newRow("account has no models") << QStringList{};
    }

    void settingsRefreshRequiresReplacement()
    {
        QFETCH(QStringList, availableModels);
        QString error;
        QVERIFY(configuration().save(&error));
        MockNetwork network;
        network.nextResponse = modelList(availableModels);
        OpenAiSettingsWidget widget(nullptr, &network);
        widget.loadSettings();
        auto *model = widget.findChild<QComboBox *>("openAiModel");
        auto *status = widget.findChild<QLabel *>("openAiStatus");
        QTRY_VERIFY(status->text().contains("no longer available"));
        QCOMPARE(model->currentIndex(), -1);
        OpenAiSettings stored;
        QVERIFY(stored.load(&error));
        QVERIFY(stored.model.isEmpty());
        QVERIFY(stored.reasoningEffort.isEmpty());
        QCOMPARE(stored.apiKey, configuration().apiKey);
        // Apply must require an explicit replacement even when other models exist.
        QTimer::singleShot(0, &widget, []() {
            for (QWidget *window : QApplication::topLevelWidgets()) {
                if (auto *message = qobject_cast<QMessageBox *>(window))
                    message->accept();
            }
        });
        QVERIFY(!widget.saveSettings());
        network.nextResponse = modelList({"replacement-model"});
        widget.loadSettings();
        QTRY_VERIFY(model->isEnabled());
        QCOMPARE(model->currentIndex(), -1);
        model->setCurrentIndex(0);
        QVERIFY(widget.saveSettings());
        QVERIFY(stored.load(&error));
        QCOMPARE(stored.model, QString("replacement-model"));
        QVERIFY(stored.reasoningEffort.isEmpty());
    }

    void failedModelRefreshPreservesSavedChoice_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<int>("httpStatus");
        QTest::newRow("network error") << QByteArray("{}") << 500;
        QTest::newRow("malformed list") << QByteArray("{\"data\":[{}]}") << 200;
        QTest::newRow("invalid JSON") << QByteArray("not JSON") << 200;
    }

    void failedModelRefreshPreservesSavedChoice()
    {
        QFETCH(QByteArray, data);
        QFETCH(int, httpStatus);
        QString error;
        QVERIFY(configuration().save(&error));
        MockNetwork network;
        network.nextResponse = data;
        network.nextStatus = httpStatus;
        OpenAiSettingsWidget widget(nullptr, &network);
        widget.loadSettings();
        auto *refresh = widget.findChild<QPushButton *>("openAiRefreshModels");
        QTRY_VERIFY(refresh->isEnabled());
        QCOMPARE(widget.findChild<QComboBox *>("openAiModel")->currentText(), configuration().model);
        QVERIFY(widget.saveSettings()); // Unrelated settings remain usable during outages.
        OpenAiSettings settings;
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.model, configuration().model);
        QCOMPARE(settings.reasoningEffort, configuration().reasoningEffort);
    }

    void previewRequiresAcceptanceAndClearsOnRetranslateOrCancel()
    {
        MockNetwork network;
        SongTranslationDialog dialog(original, "Title", configuration(), nullptr, &network);
        auto *generate = dialog.findChild<QPushButton *>("generateTranslation");
        auto *accept = dialog.findChild<QPushButton *>("acceptTranslation");
        auto *cancel = dialog.findChild<QPushButton *>("cancelTranslationRequest");
        auto *preview = dialog.findChild<QPlainTextEdit *>("translationPreview");
        QVERIFY(!dialog.findChild<QComboBox *>("translationLanguage"));
        QVERIFY(preview->isReadOnly());
        QVERIFY(!accept->isEnabled());
        generate->click();
        QVERIFY(!generate->isEnabled());
        QTRY_VERIFY(accept->isEnabled());
        QVERIFY(!preview->isReadOnly());
        QCOMPARE(dialog.proposedSongText(), proposal);
        // Clearing a draft disables acceptance while leaving it editable for undo.
        preview->selectAll();
        preview->insertPlainText(QString());
        QVERIFY(!accept->isEnabled());
        QVERIFY(dialog.proposedSongText().isEmpty());
        QVERIFY(!preview->isReadOnly());
        preview->undo();
        QVERIFY(accept->isEnabled());
        QCOMPARE(dialog.proposedSongText(), proposal);
        preview->moveCursor(QTextCursor::Start);
        QVERIFY(preview->find(QString::fromUtf8("Воспой")));
        preview->insertPlainText(QString::fromUtf8("Воздай"));
        QVERIFY(dialog.proposedSongText().contains(QString::fromUtf8("Воздай")));
        network.delayed = true;
        generate->click();
        QVERIFY(!accept->isEnabled());
        QVERIFY(dialog.proposedSongText().isEmpty());
        QVERIFY(preview->isReadOnly());
        QVERIFY(cancel->isEnabled());
        cancel->click();
        QVERIFY(!accept->isEnabled());
        QVERIFY(generate->isEnabled());
        QVERIFY(dialog.proposedSongText().isEmpty());
        QVERIFY(preview->isReadOnly());
        network.delayed = false;
        generate->click();
        QTRY_VERIFY(accept->isEnabled());
        QVERIFY(!preview->isReadOnly());
        const QJsonObject request = QJsonDocument::fromJson(network.lastBody).object();
        const QJsonObject source = QJsonDocument::fromJson(request.value("input").toArray()
            .first().toObject().value("content").toString().toUtf8()).object();
        QVERIFY(!source.contains("target_language"));
        const QByteArray sourceJson = QJsonDocument(source).toJson();
        QVERIFY(!sourceJson.contains("Old first line"));
        QVERIFY(!sourceJson.contains(QString::fromUtf8("Бог есть любовь").toUtf8()));
        accept->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.proposedSongText(), proposal);
    }

    void acceptanceChangesEditorOnlyUntilSave()
    {
        QString editedProposal = proposal;
        editedProposal.replace(QString::fromUtf8("Воспой"), QString::fromUtf8("Воздай"));
        QSqlQuery query;
        QVERIFY(query.exec("CREATE TABLE Songbooks (id INTEGER, name TEXT)"));
        QVERIFY(query.exec("INSERT INTO Songbooks VALUES (1, 'Test book')"));
        QVERIFY(query.exec("CREATE TABLE Songs (id INTEGER PRIMARY KEY, songbook_id TEXT, number INTEGER, title TEXT, category INTEGER, "
                           "tune TEXT, words TEXT, music TEXT, song_text TEXT, notes TEXT, use_translation BOOL, use_private BOOL, "
                           "alignment_v INTEGER, alignment_h INTEGER, color INTEGER, font TEXT, info_color INTEGER, info_font TEXT, "
                           "ending_color INTEGER, ending_font TEXT, use_background BOOL, background_name TEXT, background BLOB)"));
        Song song;
        song.songbook_id = "1";
        song.songbook_name = "Test book";
        song.title = "Test song";
        song.number = 1;
        song.songText = original;
        song.saveNew();
        song.songID = 1;
        QString error;
        QVERIFY(configuration().save(&error));
        EditWidget editor;
        editor.setEdit(song);
        auto *lyrics = editor.findChild<QTextEdit *>("textEditSong");
        auto *translate = editor.findChild<QPushButton *>("pushButtonTranslate");
        QVERIFY(lyrics && translate);
        // Drive the modal review with an already-validated proposal; no HTTP request is started.
        QTimer::singleShot(0, &editor, [&editor]() {
            auto *dialog = editor.findChild<SongTranslationDialog *>();
            QVERIFY(dialog);
            auto *client = dialog->findChild<OpenAiClient *>();
            QVERIFY(QMetaObject::invokeMethod(client, "translationReady", Q_ARG(QString, proposal)));
            auto *preview = dialog->findChild<QPlainTextEdit *>("translationPreview");
            QVERIFY(preview->find(QString::fromUtf8("Воспой")));
            preview->insertPlainText(QString::fromUtf8("Воздай"));
            dialog->findChild<QPushButton *>("cancelTranslation")->click();
            QVERIFY(dialog->proposedSongText().isEmpty());
        });
        translate->click();
        QCOMPARE(lyrics->toPlainText(), original);
        QCOMPARE(storedSong(), original);
        QTimer::singleShot(0, &editor, [&editor, &editedProposal]() {
            auto *dialog = editor.findChild<SongTranslationDialog *>();
            QVERIFY(dialog);
            auto *client = dialog->findChild<OpenAiClient *>();
            QVERIFY(client);
            QVERIFY(QMetaObject::invokeMethod(client, "translationReady", Q_ARG(QString, proposal)));
            auto *preview = dialog->findChild<QPlainTextEdit *>("translationPreview");
            QVERIFY(!preview->isReadOnly());
            QVERIFY(preview->find(QString::fromUtf8("Воспой")));
            preview->insertPlainText(QString::fromUtf8("Воздай"));
            QCOMPARE(dialog->proposedSongText(), editedProposal);
            auto *cancel = dialog->findChild<QPushButton *>("cancelTranslation");
            auto *accept = dialog->findChild<QPushButton *>("acceptTranslation");
            QVERIFY(cancel->mapTo(dialog, QPoint()).x() < accept->mapTo(dialog, QPoint()).x());
            accept->click();
        });
        translate->click();
        QCOMPARE(lyrics->toPlainText(), editedProposal);
        QVERIFY(editor.findChild<QCheckBox *>("checkBoxUseTranslation")->isChecked());
        QCOMPARE(storedSong(), original);
        // Cancelling the editor discards the accepted proposal.
        editor.findChild<QPushButton *>("btnCancel")->click();
        QCOMPARE(storedSong(), original);
        editor.setEdit(song);
        QTimer::singleShot(0, &editor, [&editor, &editedProposal]() {
            auto *dialog = editor.findChild<SongTranslationDialog *>();
            auto *client = dialog->findChild<OpenAiClient *>();
            QMetaObject::invokeMethod(client, "translationReady", Q_ARG(QString, proposal));
            auto *preview = dialog->findChild<QPlainTextEdit *>("translationPreview");
            QVERIFY(preview->find(QString::fromUtf8("Воспой")));
            preview->insertPlainText(QString::fromUtf8("Воздай"));
            QCOMPARE(dialog->proposedSongText(), editedProposal);
            dialog->findChild<QPushButton *>("acceptTranslation")->click();
        });
        translate->click();
        QTimer saveWatchdog;
        connect(&saveWatchdog, &QTimer::timeout, &editor, []() {
            for (QWidget *window : QApplication::topLevelWidgets()) {
                if (auto *message = qobject_cast<QMessageBox *>(window)) {
                    QTest::qFail(qPrintable(message->text()), __FILE__, __LINE__);
                    message->reject();
                }
            }
        });
        saveWatchdog.start(20);
        editor.findChild<QPushButton *>("btnSave")->click();
        saveWatchdog.stop();
        QCOMPARE(storedSong(), editedProposal);
        QSqlQuery saved("SELECT use_translation FROM Songs WHERE id = 1");
        QVERIFY(saved.next());
        QVERIFY(saved.value(0).toBool());
    }
};

QTEST_MAIN(OpenAiTranslationTest)
#include "tst_openaitranslation.moc"
