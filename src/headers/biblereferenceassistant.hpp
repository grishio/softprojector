#ifndef BIBLEREFERENCEASSISTANT_HPP
#define BIBLEREFERENCEASSISTANT_HPP

#include <QJsonObject>
#include <QMetaType>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QStringList>
#include "aibiblesettings.hpp"

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

struct BibleSelection
{
    QString book;
    int chapter = 0;
    int verse = 0;
    bool operator==(const BibleSelection &o) const { return book == o.book && chapter == o.chapter && verse == o.verse; }
    bool operator!=(const BibleSelection &o) const { return !(*this == o); }
    QString caption() const;
};

Q_DECLARE_METATYPE(BibleSelection)

struct BibleBookInfo
{
    QString name;
    int chapterCount = 0;
    QList<int> verseCounts; // verses per chapter; index 0 = chapter 1
};

// Implemented by the Bible tab so the assistant can see what is selectable
// without depending on the widget.
class BibleSelectionProvider
{
public:
    virtual ~BibleSelectionProvider() = default;
    virtual QList<BibleBookInfo> assistantBooks() const = 0;
    virtual int assistantVerseCount(const QString &book, int chapter) const = 0;
    // Largest verse number in any chapter (sizes the verse question's labels).
    virtual int assistantMaxVerses() const = 0;
    virtual BibleSelection assistantSelection() const = 0;
    virtual bool assistantPaused() const = 0;
};

// Classifies the live transcript with an OpenRouter Decisions model (Clef)
// and suggests a book/chapter/verse. Decision models answer typed
// multiple-choice questions instead of generating text; each batch of
// transcript lines is one request asking four questions at once (reference
// spoken? / book / chapter number / verse number), validated locally.
// Requests are asynchronous; only one runs at a time and lines that arrive
// meanwhile are evaluated together on the next run.
class BibleReferenceAssistant : public QObject
{
    Q_OBJECT
public:
    explicit BibleReferenceAssistant(BibleSelectionProvider *provider, QObject *parent = nullptr,
                                     QNetworkAccessManager *manager = nullptr);
    ~BibleReferenceAssistant() override;
    void setSettings(const AiBibleSettings &settings);
    void addLine(const QString &text);
    // The operator changed the selection: forget unprocessed lines and drop
    // any in-flight result so the assistant does not undo the change.
    void operatorTookOver();
    void cancel();
    bool isBusy() const { return !activeReply.isNull(); }

    static const QString KeepLabel;
    // Request helpers, public for tests.
    static QJsonObject choiceQuestion(const QString &instructions, const QStringList &labels,
                                      const QString &keepGuidance);
    static QJsonObject noulQuestion(const QString &instructions, const QString &trueGuidance,
                                    const QString &falseGuidance);
    // Returns the chosen label and its probability, or an empty label.
    static QString chosenLabel(const QJsonObject &answers, const QString &question, double *probability);
    // Most probable number label in 1..max (or keep); returns 0 for keep/none.
    static int bestNumber(const QJsonObject &answers, const QString &question, int max, double *probability);

signals:
    void suggestion(const BibleSelection &selection, double confidence);
    void statusChanged(const QString &status);
    void failed(const QString &message);

private:
    struct Job
    {
        int generation = 0;
        BibleSelection start;
        QJsonObject state;
        QString book;
        bool bookChanged = false;
        int chapter = 0;
        bool chapterChanged = false;
        int verse = 0;
        bool verseChanged = false;
        double confidence = 1.0;
        bool reported = false; // a status or error was already shown
        // Timing (ms since epoch / ms): when the oldest new line arrived, when the
        // chain started, and total time spent waiting on OpenRouter.
        qint64 lineArrivedMs = 0;
        qint64 runStartMs = 0;
        qint64 networkMs = 0;
        int requests = 0;
    };

    BibleSelectionProvider *provider;
    QNetworkAccessManager *network;
    QPointer<QNetworkReply> activeReply;
    QTimer *debounce;
    AiBibleSettings settings;
    QString sessionId;
    QStringList lines;
    int consumed = 0;
    qint64 pendingSinceMs = 0; // arrival time of the oldest unprocessed line
    int generation = 0;
    bool halted = false;
    Job job;
    // Recent lines that contained a reference (e.g. "I will read from Matthew"),
    // kept for a few minutes so a chapter or verse spoken later is matched to
    // that book even after many lines of commentary.
    QList<QPair<qint64, QString>> referenceLines;

    void evaluate();
    void sendRequest();
    void finishJob();
    void handleReply(QNetworkReply *reply, int jobGeneration);
};

#endif
