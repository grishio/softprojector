#include "../headers/openaiclient.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <algorithm>

namespace {
bool hasShutDown(const QJsonObject &model)
{
    const QDate date = QDate::fromString(model.value("shutdown_date").toString().left(10), Qt::ISODate);
    return date.isValid() && date <= QDateTime::currentDateTimeUtc().date();
}

bool unavailableModel(int status, const QJsonObject &error)
{
    // Authentication, quota, and temporary server errors do not retire a selection.
    if (status == 401 || status == 429 || status >= 500)
        return false;
    const QString code = error.value("code").toString();
    const QString param = error.value("param").toString();
    return status == 404 || code == "model_not_found" || code == "model_not_available" ||
        code == "model_retired" || code == "model_deprecated" || code == "model_access_denied" ||
        code == "invalid_model" || code == "unsupported_model" ||
        ((status == 400 || status == 403) && param == "model");
}
}

OpenAiClient::OpenAiClient(QObject *parent, QNetworkAccessManager *manager) : QObject(parent),
    network(manager ? manager : new QNetworkAccessManager(this))
{
}

OpenAiClient::~OpenAiClient()
{
    cancel();
}

void OpenAiClient::cancel()
{
    if (activeReply) {
        QNetworkReply *reply = activeReply.data();
        activeReply.clear();
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void OpenAiClient::fetchModels(const QString &apiKey)
{
    send(apiKey, {}, ListModels);
}

void OpenAiClient::translate(const OpenAiSettings &settings, const QString &songText,
                             const QString &title)
{
    if (isBusy()) {
        emit failed(tr("An OpenAI request is already running."));
        return;
    }
    QString error;
    if (!settings.validate(&error) || !translation.parse(songText, &error)) {
        emit failed(error);
        return;
    }
    // Verify the remembered model before sending lyrics or making a billable request.
    send(settings.apiKey, QJsonDocument(translation.request(settings, title))
         .toJson(QJsonDocument::Compact), CheckModel, settings.model.trimmed());
}

void OpenAiClient::requireModelSelection(const QString &apiKey, const QString &model)
{
    QString storageError;
    QString message = tr("The selected OpenAI model '%1' is no longer available to this API key. "
                         "Open Settings > OpenAI Translation, refresh available models, and select a replacement.").arg(model);
    if (!OpenAiSettings::clearUnavailableModel(apiKey, model, &storageError))
        message += tr(" Could not clear the saved selection: %1").arg(storageError);
    emit modelUnavailable(model);
    emit failed(message);
}

void OpenAiClient::send(const QString &apiKey, const QByteArray &body, Operation operation,
                        const QString &selectedModel)
{
    if (isBusy()) {
        emit failed(tr("An OpenAI request is already running."));
        return;
    }
    if (apiKey.trimmed().isEmpty() || apiKey.contains('\r') || apiKey.contains('\n')) {
        emit failed(tr("Enter a valid API key in Settings > OpenAI Translation."));
        return;
    }
    // Keep credentials on the official HTTPS endpoint, including on redirects.
    QByteArray endpoint = "https://api.openai.com/v1/";
    endpoint += operation == TranslateSong ? "responses" : "models";
    if (operation == CheckModel)
        endpoint += '/' + QUrl::toPercentEncoding(selectedModel);
    QNetworkRequest request(QUrl::fromEncoded(endpoint));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Authorization", "Bearer " + apiKey.trimmed().toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("User-Agent", "SoftProjector");
    QNetworkReply *reply = operation == TranslateSong ? network->post(request, body) : network->get(request);
    activeReply = reply;
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("openaiTimedOut", true);
        reply->abort();
    });
    timeout->start(operation == TranslateSong ? 300000 : 30000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, timeout, operation, apiKey, selectedModel, body]() {
        timeout->stop();
        activeReply.clear();
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;
        if (reply->property("openaiTimedOut").toBool())
            error = tr("The OpenAI request timed out. Try again or select a lower reasoning effort.");
        else if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            const QJsonObject details = QJsonDocument::fromJson(data).object().value("error").toObject();
            const QString apiError = details.value("message").toString();
            if (operation != ListModels && unavailableModel(status, details)) {
                reply->deleteLater();
                requireModelSelection(apiKey, selectedModel);
                return;
            }
            if (status == 401)
                error = tr("OpenAI rejected the API key. Update it in Settings > OpenAI Translation.");
            else if (status == 429)
                error = tr("OpenAI quota or rate limit reached. Check API billing or try again later.");
            else {
                error = tr("OpenAI request failed: %1").arg(apiError.isEmpty() ? reply->errorString() : apiError);
                const QString param = details.value("param").toString();
                if (status == 400 && (param.startsWith("reasoning") || apiError.contains("effort", Qt::CaseInsensitive)))
                    error += tr(" This model may not support the selected thinking effort. Choose Model default "
                                "or a supported effort in Settings > OpenAI Translation.");
                else if (status == 400 && param.startsWith("text.format"))
                    error += tr(" Choose a text model that supports structured output in Settings > OpenAI Translation.");
            }
        }
        reply->deleteLater();
        if (!error.isEmpty()) {
            error.replace(apiKey.trimmed(), "[redacted]");
            emit failed(error);
            return;
        }
        if (operation == ListModels) {
            const QJsonDocument document = QJsonDocument::fromJson(data);
            if (!document.isObject() || !document.object().value("data").isArray()) {
                emit failed(tr("OpenAI returned an invalid model list."));
                return;
            }
            QMap<QString, qint64> creationDates;
            for (const QJsonValue &value : document.object().value("data").toArray()) {
                const QString model = value.toObject().value("id").toString();
                if (model.isEmpty() || !value.isObject()) {
                    emit failed(tr("OpenAI returned an invalid model list."));
                    return;
                }
                if (!hasShutDown(value.toObject())) {
                    const qint64 created = value.toObject().value("created").toVariant().toLongLong();
                    creationDates[model] = qMax(creationDates.value(model), created);
                }
            }
            QStringList list = creationDates.keys();
            // Use API metadata instead of guessing releases from model names.
            // Missing dates go last; equal dates use names for a stable order.
            std::sort(list.begin(), list.end(), [&creationDates](const QString &left, const QString &right) {
                const qint64 leftDate = creationDates.value(left);
                const qint64 rightDate = creationDates.value(right);
                return leftDate == rightDate ? left < right : leftDate > rightDate;
            });
            emit modelsReady(list);
        } else if (operation == CheckModel) {
            const QJsonDocument document = QJsonDocument::fromJson(data);
            if (!document.isObject() || document.object().value("id").toString() != selectedModel) {
                emit failed(tr("OpenAI returned an invalid model availability response. Try again."));
                return;
            }
            if (hasShutDown(document.object())) {
                requireModelSelection(apiKey, selectedModel);
                return;
            }
            send(apiKey, body, TranslateSong, selectedModel);
        } else {
            QString proposal;
            if (translation.readResponse(data, &proposal, &error))
                emit translationReady(proposal);
            else
                emit failed(error);
        }
    });
}
