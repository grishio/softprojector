#ifndef TRANSCRIPTSTREAM_HPP
#define TRANSCRIPTSTREAM_HPP

#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Minimal Server-Sent Events client for a live transcript feed. Each event's
// data is expected to be a JSON object {"id": <number>, "text": "<line>"}.
// Reconnects automatically with backoff and drops duplicate ids on replay.
class TranscriptStream : public QObject
{
    Q_OBJECT
public:
    explicit TranscriptStream(QObject *parent = nullptr, QNetworkAccessManager *manager = nullptr);
    ~TranscriptStream() override;
    void start(const QUrl &url);
    void stop();
    bool isRunning() const { return running; }

    // Parses a chunk of raw stream bytes. Public for tests.
    void feed(const QByteArray &bytes);

signals:
    void lineReceived(qint64 id, const QString &text);
    void connected();
    void disconnected(const QString &reason);

private:
    QNetworkAccessManager *network;
    QPointer<QNetworkReply> reply;
    QTimer *reconnectTimer;
    QTimer *watchdog;
    QUrl url;
    bool running = false;
    bool sawData = false;
    int backoffMs = 1000;
    int serverRetryMs = 0;
    QByteArray buffer;
    QByteArray eventData;
    QSet<qint64> seenIds;
    QQueue<qint64> seenOrder;
    void connectNow();
    void dropReply();
    void scheduleReconnect(const QString &reason);
    void processLine(const QByteArray &line);
    void dispatchEvent();
};

#endif
