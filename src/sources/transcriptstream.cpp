#include "../headers/transcriptstream.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace {
// The feed sends keep-alive comments; silence this long means a dead connection.
const int WatchdogMs = 90000;
const int MaxBackoffMs = 30000;
const int MaxSeenIds = 5000;
}

TranscriptStream::TranscriptStream(QObject *parent, QNetworkAccessManager *manager) : QObject(parent),
    network(manager ? manager : new QNetworkAccessManager(this)),
    reconnectTimer(new QTimer(this)),
    watchdog(new QTimer(this))
{
    reconnectTimer->setSingleShot(true);
    connect(reconnectTimer, &QTimer::timeout, this, &TranscriptStream::connectNow);
    watchdog->setSingleShot(true);
    watchdog->setInterval(WatchdogMs);
    connect(watchdog, &QTimer::timeout, this, [this]() {
        dropReply();
        scheduleReconnect(tr("No data from the transcript feed for %1 seconds.").arg(WatchdogMs / 1000));
    });
}

TranscriptStream::~TranscriptStream()
{
    stop();
}

void TranscriptStream::start(const QUrl &newUrl)
{
    stop();
    url = newUrl;
    running = true;
    backoffMs = 1000;
    serverRetryMs = 0;
    seenIds.clear();
    seenOrder.clear();
    connectNow();
}

void TranscriptStream::stop()
{
    running = false;
    reconnectTimer->stop();
    watchdog->stop();
    dropReply();
}

void TranscriptStream::dropReply()
{
    watchdog->stop();
    if (reply) {
        QNetworkReply *old = reply.data();
        reply.clear();
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }
    buffer.clear();
    eventData.clear();
}

void TranscriptStream::connectNow()
{
    if (!running)
        return;
    dropReply();
    sawData = false;
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setRawHeader("User-Agent", "SoftProjector");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *r = network->get(request);
    reply = r;
    watchdog->start();
    connect(r, &QNetworkReply::metaDataChanged, this, [this, r]() {
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 0 && (status < 200 || status >= 300)) {
            dropReply();
            scheduleReconnect(tr("Transcript feed returned HTTP %1.").arg(status));
        }
    });
    connect(r, &QNetworkReply::readyRead, this, [this, r]() {
        const QByteArray bytes = r->readAll();
        if (bytes.isEmpty())
            return;
        watchdog->start();
        if (!sawData) {
            sawData = true;
            backoffMs = 1000;
            emit connected();
        }
        feed(bytes);
    });
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (reply != r)
            return;
        const QString reason = r->error() == QNetworkReply::NoError
            ? tr("The transcript feed closed the connection.")
            : tr("Transcript feed error: %1").arg(r->errorString());
        dropReply();
        scheduleReconnect(reason);
    });
}

void TranscriptStream::scheduleReconnect(const QString &reason)
{
    emit disconnected(reason);
    if (!running)
        return;
    const int delay = serverRetryMs > 0 ? serverRetryMs : backoffMs;
    backoffMs = qMin(backoffMs * 2, MaxBackoffMs);
    reconnectTimer->start(delay);
}

void TranscriptStream::feed(const QByteArray &bytes)
{
    buffer += bytes;
    int start = 0;
    while (true) {
        const int newline = buffer.indexOf('\n', start);
        if (newline < 0)
            break;
        QByteArray line = buffer.mid(start, newline - start);
        if (line.endsWith('\r'))
            line.chop(1);
        processLine(line);
        start = newline + 1;
    }
    buffer.remove(0, start);
    // Guard against a server that never sends newlines.
    if (buffer.size() > 1024 * 1024)
        buffer.clear();
}

void TranscriptStream::processLine(const QByteArray &line)
{
    if (line.isEmpty()) {
        dispatchEvent();
        return;
    }
    if (line.startsWith(':'))
        return; // comment / keep-alive
    const int colon = line.indexOf(':');
    const QByteArray field = colon < 0 ? line : line.left(colon);
    QByteArray value = colon < 0 ? QByteArray() : line.mid(colon + 1);
    if (value.startsWith(' '))
        value.remove(0, 1);
    if (field == "data") {
        if (!eventData.isEmpty())
            eventData += '\n';
        eventData += value;
    } else if (field == "retry") {
        bool ok = false;
        const int ms = value.toInt(&ok);
        if (ok && ms > 0)
            serverRetryMs = qMin(ms, MaxBackoffMs);
    }
}

void TranscriptStream::dispatchEvent()
{
    if (eventData.isEmpty())
        return;
    const QByteArray data = eventData;
    eventData.clear();
    qint64 id = -1;
    QString text;
    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (document.isObject()) {
        const QJsonObject object = document.object();
        text = object.value("text").toString();
        if (object.contains("id"))
            id = object.value("id").toVariant().toLongLong();
    } else
        text = QString::fromUtf8(data);
    text = text.simplified();
    if (text.isEmpty())
        return;
    if (id >= 0) {
        if (seenIds.contains(id))
            return;
        seenIds.insert(id);
        seenOrder.enqueue(id);
        while (seenOrder.size() > MaxSeenIds)
            seenIds.remove(seenOrder.dequeue());
    }
    emit lineReceived(id, text);
}
