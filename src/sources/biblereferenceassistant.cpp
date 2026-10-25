#include "../headers/biblereferenceassistant.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUuid>

namespace {
const char DecisionsEndpoint[] = "https://openrouter.ai/api/alpha/decisions";
const int RequestTimeoutMs = 20000;
const int DebounceMs = 150;
// Keep the state small: some Clef providers only read the first ~2K tokens.
const int MaxNewChars = 700;
const int MaxEarlierChars = 450;
const int MaxEarlierLines = 6;
const int MaxStoredLines = 200;
const double ReferenceThreshold = 0.5;
const qint64 ReferenceMemoryMs = 5 * 60 * 1000;
const int MaxReferenceLines = 3;

// Joins lines from the end backwards until the character budget is used.
QString tail(const QStringList &source, int from, int to, int maxChars, int maxLines)
{
    QStringList picked;
    int used = 0;
    for (int i = to - 1; i >= from && picked.size() < maxLines; --i) {
        const QString &line = source.at(i);
        if (used + line.size() > maxChars && !picked.isEmpty())
            break;
        picked.prepend(line.size() > maxChars ? line.right(maxChars) : line);
        used += line.size() + 1;
    }
    return picked.join('\n');
}

// Appends to ai-bible-assistant.log next to the executable so missed or wrong
// selections can be diagnosed (what was sent, what the model answered).
void writeLog(const QString &message)
{
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath("ai-bible-assistant.log");
    if (QFileInfo(path).size() > 2 * 1024 * 1024) {
        QFile::remove(path + ".old");
        QFile::rename(path, path + ".old");
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    file.write((QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz ") + message + '\n').toUtf8());
}

QStringList numberLabels(int count)
{
    QStringList labels;
    for (int i = 1; i <= count; ++i)
        labels << QString::number(i);
    return labels;
}
}

const QString BibleReferenceAssistant::KeepLabel = QStringLiteral("keep");

QString BibleSelection::caption() const
{
    if (book.isEmpty())
        return QString();
    if (chapter <= 0)
        return book;
    if (verse <= 0)
        return QString("%1 %2").arg(book).arg(chapter);
    return QString("%1 %2:%3").arg(book).arg(chapter).arg(verse);
}

BibleReferenceAssistant::BibleReferenceAssistant(BibleSelectionProvider *selectionProvider, QObject *parent,
                                                 QNetworkAccessManager *manager) : QObject(parent),
    provider(selectionProvider),
    network(manager ? manager : new QNetworkAccessManager(this)),
    debounce(new QTimer(this))
{
    debounce->setSingleShot(true);
    debounce->setInterval(DebounceMs);
    connect(debounce, &QTimer::timeout, this, &BibleReferenceAssistant::evaluate);
}

BibleReferenceAssistant::~BibleReferenceAssistant()
{
    cancel();
}

void BibleReferenceAssistant::setSettings(const AiBibleSettings &newSettings)
{
    cancel();
    settings = newSettings;
    halted = false;
    lines.clear();
    consumed = 0;
    referenceLines.clear();
    sessionId = "softprojector-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void BibleReferenceAssistant::cancel()
{
    debounce->stop();
    ++generation;
    if (activeReply) {
        QNetworkReply *reply = activeReply.data();
        activeReply.clear();
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void BibleReferenceAssistant::addLine(const QString &text)
{
    if (consumed >= lines.size())
        pendingSinceMs = QDateTime::currentMSecsSinceEpoch();
    lines << text;
    if (lines.size() > MaxStoredLines) {
        const int drop = lines.size() - MaxStoredLines;
        lines.erase(lines.begin(), lines.begin() + drop);
        consumed = qMax(0, consumed - drop);
    }
    if (!settings.isUsable() || halted)
        return;
    if (!isBusy())
        debounce->start();
}

void BibleReferenceAssistant::operatorTookOver()
{
    cancel();
    consumed = lines.size();
    referenceLines.clear(); // the operator's choice replaces earlier hints
}

void BibleReferenceAssistant::evaluate()
{
    if (isBusy() || halted || !settings.isUsable() || consumed >= lines.size())
        return;
    if (provider->assistantPaused()) {
        // The operator is in control; their change already reflects these lines.
        consumed = lines.size();
        emit statusChanged(tr("Paused after manual change."));
        writeLog("skip (paused after manual change): " + lines.last());
        return;
    }
    const QList<BibleBookInfo> books = provider->assistantBooks();
    if (books.isEmpty()) {
        consumed = lines.size();
        emit statusChanged(tr("No operator Bible is loaded."));
        return;
    }

    const int from = consumed;
    consumed = lines.size();
    job = Job();
    job.generation = ++generation;
    job.runStartMs = QDateTime::currentMSecsSinceEpoch();
    job.lineArrivedMs = pendingSinceMs > 0 ? pendingSinceMs : job.runStartMs;
    job.start = provider->assistantSelection();
    job.book = job.start.book;
    job.chapter = job.start.chapter;
    job.verse = job.start.verse;
    job.state.insert("current_selection", job.start.book.isEmpty() ? QStringLiteral("none") : job.start.caption());
    job.state.insert("current_book", job.start.book);
    job.state.insert("current_chapter", job.start.chapter);
    job.state.insert("current_verse", job.start.verse);
    job.state.insert("earlier_lines", tail(lines, 0, from, MaxEarlierChars, MaxEarlierLines));
    job.state.insert("new_lines", tail(lines, from, lines.size(), MaxNewChars, 1000));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    while (!referenceLines.isEmpty() && now - referenceLines.first().first > ReferenceMemoryMs)
        referenceLines.removeFirst();
    QStringList hints;
    for (const auto &hint : referenceLines)
        hints << hint.second;
    if (!hints.isEmpty())
        job.state.insert("recent_reference_lines", hints.join(" / "));
    emit statusChanged(tr("Analyzing transcript..."));
    writeLog(QString("run current=%1 | earlier=%2 | new=%3")
             .arg(job.state.value("current_selection").toString(), job.state.value("earlier_lines").toString(),
                  job.state.value("new_lines").toString()).replace('\n', " / "));
    sendRequest();
}

QJsonObject BibleReferenceAssistant::choiceQuestion(const QString &instructions, const QStringList &labels,
                                                    const QString &keepGuidance)
{
    QJsonObject criteria;
    criteria.insert(KeepLabel, keepGuidance);
    for (const QString &label : labels)
        if (label != KeepLabel)
            criteria.insert(label, QJsonValue()); // null guidance: the label speaks for itself
    return QJsonObject{{"type", "choice"}, {"instructions", instructions}, {"criteria", criteria}};
}

QJsonObject BibleReferenceAssistant::noulQuestion(const QString &instructions, const QString &trueGuidance,
                                                  const QString &falseGuidance)
{
    return QJsonObject{{"type", "noul"}, {"instructions", instructions},
                       {"criteria", QJsonObject{{"true", trueGuidance}, {"false", falseGuidance}}}};
}

QString BibleReferenceAssistant::chosenLabel(const QJsonObject &answers, const QString &question, double *probability)
{
    const QJsonObject answer = answers.value(question).toObject();
    const QString choice = answer.value("choice").toString();
    double p = answer.value("probabilities").toObject().value(choice).toDouble(-1.0);
    if (p < 0.0)
        p = answer.value("confidence").toDouble(0.0);
    if (probability)
        *probability = p;
    return choice;
}

int BibleReferenceAssistant::bestNumber(const QJsonObject &answers, const QString &question, int max, double *probability)
{
    // The labels cover the largest chapter/verse in the Bible; only 1..max is
    // valid for the chosen book/chapter, so take the most probable valid label.
    const QJsonObject answer = answers.value(question).toObject();
    const QJsonObject probabilities = answer.value("probabilities").toObject();
    QString best;
    double bestP = -1.0;
    for (auto it = probabilities.constBegin(); it != probabilities.constEnd(); ++it) {
        const int n = it.key().toInt();
        const bool valid = it.key() == KeepLabel || (n >= 1 && n <= max);
        if (valid && it.value().toDouble() > bestP) {
            best = it.key();
            bestP = it.value().toDouble();
        }
    }
    if (best.isEmpty()) { // no probabilities returned: fall back to the chosen label
        best = chosenLabel(answers, question, &bestP);
        const int n = best.toInt();
        if (best != KeepLabel && (n < 1 || n > max))
            best.clear();
    }
    if (probability)
        *probability = qMax(0.0, bestP);
    return best == KeepLabel || best.isEmpty() ? 0 : best.toInt();
}

void BibleReferenceAssistant::sendRequest()
{
    // One request per batch of transcript lines: Clef answers all four
    // questions in a single forward pass. Chapter and verse labels are plain
    // numbers up to the largest in the Bible and are validated locally against
    // the chosen book, so they do not depend on the book answer.
    const QList<BibleBookInfo> books = provider->assistantBooks();
    QStringList names;
    int maxChapters = 1;
    for (const BibleBookInfo &book : books) {
        names << book.name;
        maxChapters = qMax(maxChapters, book.chapterCount);
    }
    const int maxVerses = qMax(1, provider->assistantMaxVerses());
    const QString asr = QStringLiteral(
        "The text is automatic speech recognition of a sermon, possibly mixing Russian and Ukrainian, so words may be "
        "misspelled or split (for example 'Ниемия' or 'Неемии' for 'Неемия', 'от Иоана' for 'От Иоанна'). ");
    const QString numbers = QStringLiteral(
        "Numbers may be spoken as words or ordinals in any language (for example 'сто тридцать восьмой' = 138, "
        "'третья глава' = 3, 'chapter twelve' = 12). Two numbers right after a book name are chapter then verse "
        "(for example 'Иоанна три шестнадцать' = chapter 3, verse 16). A number said as a verse ('стих пятый', "
        "'verse 5') is not a chapter, and a number said as a chapter or psalm ('пятая глава', 'псалом пятый') is not "
        "a verse. If the speaker corrects a number, use the corrected one. ");
    const QString structure = QStringLiteral(
        "available_books lists every book of the Bible in use, with the number of verses in each of its chapters "
        "(the first number is chapter 1). Only choose a chapter and verse that exist for the chosen book. ");
    const QString context = QStringLiteral(
        "The speaker may announce the book, comment for a while, then give the chapter and verse; use earlier_lines "
        "and recent_reference_lines for the book, but only numbers spoken in new_lines. ");

    QJsonObject questions;
    questions.insert("reference", noulQuestion(
        "Does new_lines (continuing from earlier_lines) contain a spoken Bible reference: a book name, a chapter or "
        "psalm number, or a verse number? " + asr,
        "Yes: a book, chapter, psalm, or verse reference is spoken, for example 'Psalm 138', 'сто тридцать восьмой "
        "псалом', 'Евангелие от Иоанна, третья глава', 'Неемии четыре семнадцать', 'verse 16', 'читаем от Матфея'.",
        "No: only quotation or paraphrase of verses, or general talk with no book name and no chapter or verse numbers."));
    questions.insert("book", choiceQuestion(
        "Which Bible book is the speaker referring to? The spoken name may be in another language or grammatical form "
        "than the labels (for example 'псалом' or 'Psalm' = the book of Psalms, 'от Иоанна' = John, 'Неемии' = Неемия); "
        "choose the closest-sounding book. " + structure + asr + context,
        names,
        "No book is referred to, or it is the book already in current_selection."));
    questions.insert("chapter", choiceQuestion(
        "Which chapter or psalm number does the speaker say in new_lines? " + numbers + structure + asr,
        numberLabels(maxChapters),
        "No chapter or psalm number is spoken in new_lines, or it is the chapter already in current_selection."));
    questions.insert("verse", choiceQuestion(
        "Which verse number does the speaker say in new_lines? For a range such as 'verses 3 to 5' choose the first "
        "verse. " + numbers + structure + asr,
        numberLabels(maxVerses),
        "No verse number is spoken in new_lines, or it is the verse already in current_selection."));

    // Full Bible structure (operator Bible's own book names, verses per chapter).
    // Added to this request only; the log keeps the shorter job.state.
    QJsonObject available;
    for (const BibleBookInfo &book : books) {
        QJsonArray verses;
        for (int count : book.verseCounts)
            verses.append(count);
        available.insert(book.name, verses);
    }
    QJsonObject state = job.state;
    state.insert("available_books", available);
    const QJsonObject body{{"model", settings.model.trimmed()}, {"questions", questions},
                           {"state", state}, {"session_id", sessionId}};

    // Keep credentials on the official HTTPS endpoint, including on redirects.
    QNetworkRequest request(QUrl(QString::fromLatin1(DecisionsEndpoint)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Authorization", "Bearer " + settings.apiKey.trimmed().toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("User-Agent", "SoftProjector");
    request.setRawHeader("X-Title", "SoftProjector");
    QNetworkReply *reply = network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    activeReply = reply;
    reply->setProperty("sentAtMs", QDateTime::currentMSecsSinceEpoch());
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("decisionTimedOut", true);
        reply->abort();
    });
    timeout->start(RequestTimeoutMs);
    const int jobGeneration = job.generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, jobGeneration]() {
        handleReply(reply, jobGeneration);
    });
}

void BibleReferenceAssistant::handleReply(QNetworkReply *reply, int jobGeneration)
{
    reply->deleteLater();
    if (activeReply == reply)
        activeReply.clear();
    if (jobGeneration != generation)
        return; // cancelled or superseded
    const QByteArray data = reply->readAll();
    const qint64 roundTripMs = QDateTime::currentMSecsSinceEpoch() - reply->property("sentAtMs").toLongLong();
    job.networkMs += roundTripMs;
    ++job.requests;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->property("decisionTimedOut").toBool() || reply->error() != QNetworkReply::NoError ||
        status < 200 || status >= 300) {
        const QJsonObject error = QJsonDocument::fromJson(data).object().value("error").toObject();
        const QString apiError = error.value("message").toString();
        QString message;
        if (reply->property("decisionTimedOut").toBool())
            message = tr("The OpenRouter decision request timed out.");
        else if (status == 401) {
            message = tr("OpenRouter rejected the API key. Update it in Settings > AI Bible Assistant.");
            halted = true;
        } else if (status == 402) {
            message = tr("OpenRouter reports insufficient credits for this API key.");
            halted = true;
        } else if (status == 429)
            message = tr("OpenRouter rate limit reached; waiting for the next transcript line.");
        else
            message = tr("OpenRouter decision request failed: %1").arg(apiError.isEmpty() ? reply->errorString() : apiError);
        message.replace(settings.apiKey.trimmed(), "[redacted]");
        writeLog(QString("request %1 ms HTTP %2 error: %3 | body: %4").arg(roundTripMs).arg(status)
                 .arg(message, QString::fromUtf8(data.left(500))));
        emit failed(message);
        job.reported = true;
        finishJob();
        return;
    }
    const QJsonObject response = QJsonDocument::fromJson(data).object();
    const QJsonObject answers = response.value("answers").toObject();
    writeLog(QString("request %1 ms provider=%2 answers %3").arg(roundTripMs)
             .arg(response.value("provider").toString(),
                  QString::fromUtf8(QJsonDocument(answers).toJson(QJsonDocument::Compact))));
    if (answers.isEmpty()) {
        emit failed(tr("OpenRouter returned an invalid decision response."));
        job.reported = true;
        finishJob();
        return;
    }

    const double reference = answers.value("reference").toObject().value("noul").toDouble(0.0);
    if (reference < ReferenceThreshold) {
        emit statusChanged(tr("Listening... (no reference in the latest lines)"));
        job.reported = true;
        finishJob();
        return;
    }
    referenceLines.append(qMakePair(QDateTime::currentMSecsSinceEpoch(), job.state.value("new_lines").toString()));
    while (referenceLines.size() > MaxReferenceLines)
        referenceLines.removeFirst();

    const double threshold = settings.minConfidence;
    // Book.
    double p = 0.0;
    const QString book = chosenLabel(answers, "book", &p);
    int chapterCount = 0;
    for (const BibleBookInfo &b : provider->assistantBooks())
        if (b.name == book)
            chapterCount = b.chapterCount;
    if (chapterCount > 0 && book != job.start.book && p >= threshold) {
        job.book = book;
        job.bookChanged = true;
        job.chapter = 1;
        job.verse = 1;
        job.confidence = qMin(job.confidence, p);
    }
    if (job.book.isEmpty()) {
        emit statusChanged(tr("Listening... (no book selected yet)"));
        job.reported = true;
        finishJob();
        return;
    }
    if (!job.bookChanged)
        for (const BibleBookInfo &b : provider->assistantBooks())
            if (b.name == job.book)
                chapterCount = b.chapterCount;

    // Chapter: a number valid for the (possibly new) book.
    const int chapter = bestNumber(answers, "chapter", chapterCount, &p);
    if (chapter > 0 && p >= threshold && (job.bookChanged || chapter != job.start.chapter)) {
        job.chapter = chapter;
        job.chapterChanged = true;
        job.verse = 1;
        job.confidence = qMin(job.confidence, p);
    }

    // Verse: a number valid for that chapter.
    if (job.chapter > 0) {
        const int verseCount = provider->assistantVerseCount(job.book, job.chapter);
        const int verse = bestNumber(answers, "verse", verseCount, &p);
        if (verse > 0 && p >= threshold && verse != job.verse) {
            job.verse = verse;
            job.verseChanged = true;
            job.confidence = qMin(job.confidence, p);
        }
    }
    finishJob();
}

void BibleReferenceAssistant::finishJob()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 sinceLineMs = job.lineArrivedMs > 0 ? now - job.lineArrivedMs : 0;
    if (job.runStartMs > 0)
        writeLog(QString("timing: %1 request(s), OpenRouter %2 ms, run %3 ms, line-to-result %4 ms")
                 .arg(job.requests).arg(job.networkMs).arg(now - job.runStartMs).arg(sinceLineMs));
    if (job.bookChanged || job.chapterChanged || job.verseChanged) {
        BibleSelection selection;
        selection.book = job.book;
        selection.chapter = job.chapter;
        selection.verse = job.verse;
        if (selection != provider->assistantSelection() && !provider->assistantPaused()) {
            emit suggestion(selection, job.confidence);
            writeLog(QString("preselect %1 confidence %2").arg(selection.caption()).arg(job.confidence, 0, 'f', 2));
            emit statusChanged(tr("Preselected %1 (%2% confident, %3 s). Press F5 to show.")
                               .arg(selection.caption()).arg(qRound(job.confidence * 100))
                               .arg(sinceLineMs / 1000.0, 0, 'f', 1));
        }
    } else if (!job.reported)
        emit statusChanged(tr("Listening... (no change)"));
    job = Job();
    // Lines that arrived while this request ran are evaluated next.
    if (consumed < lines.size() && !halted)
        debounce->start();
}
