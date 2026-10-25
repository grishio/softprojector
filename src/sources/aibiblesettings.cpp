#include "../headers/aibiblesettings.hpp"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>

namespace {
bool databaseResult(bool ok, const QSqlQuery &query, QString *error)
{
    if (!ok && error)
        *error = query.lastError().text();
    return ok;
}

// Qt's null QString binds as SQL NULL; the text columns require empty text.
QString text(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}
}

bool AiBibleSettings::ensureTable(QString *error)
{
    QSqlQuery query;
    if (!databaseResult(query.exec(
            "CREATE TABLE IF NOT EXISTS AiBibleSettings ("
            "id INTEGER PRIMARY KEY CHECK (id = 1), "
            "enabled INTEGER NOT NULL DEFAULT 0, "
            "api_key TEXT NOT NULL DEFAULT '', "
            "model TEXT NOT NULL DEFAULT 'cloudflare/clef-flash', "
            "transcript_url TEXT NOT NULL DEFAULT '', "
            "min_confidence REAL NOT NULL DEFAULT 0.6, "
            "pause_seconds INTEGER NOT NULL DEFAULT 10)"), query, error))
        return false;
    return databaseResult(query.exec(
        "INSERT OR IGNORE INTO AiBibleSettings (id, enabled, api_key, model, transcript_url, min_confidence, pause_seconds) "
        "VALUES (1, 0, '', 'cloudflare/clef-flash', '', 0.6, 10)"), query, error);
}

bool AiBibleSettings::load(QString *error)
{
    QSqlQuery query;
    if (!databaseResult(query.exec("SELECT enabled, api_key, model, transcript_url, min_confidence, pause_seconds "
                                   "FROM AiBibleSettings WHERE id = 1"), query, error))
        return false;
    if (!query.next()) {
        if (error)
            *error = QCoreApplication::translate("AiBibleSettings", "AI Bible assistant settings are missing from the database.");
        return false;
    }
    enabled = query.value(0).toInt() != 0;
    apiKey = query.value(1).toString();
    model = query.value(2).toString();
    transcriptUrl = query.value(3).toString();
    minConfidence = query.value(4).toDouble();
    pauseSeconds = query.value(5).toInt();
    return true;
}

bool AiBibleSettings::save(QString *error) const
{
    if (!validate(error))
        return false;
    QSqlQuery query;
    query.prepare("INSERT OR REPLACE INTO AiBibleSettings (id, enabled, api_key, model, transcript_url, min_confidence, pause_seconds) "
                  "VALUES (1, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(enabled ? 1 : 0);
    query.addBindValue(text(apiKey.trimmed()));
    query.addBindValue(text(model.trimmed()));
    query.addBindValue(text(transcriptUrl.trimmed()));
    query.addBindValue(minConfidence);
    query.addBindValue(pauseSeconds);
    return databaseResult(query.exec(), query, error);
}

bool AiBibleSettings::validate(QString *error) const
{
    QString message;
    const QUrl url(transcriptUrl.trimmed(), QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (apiKey.contains(QRegularExpression("[\\r\\n]")))
        message = QCoreApplication::translate("AiBibleSettings", "The OpenRouter API key must be a single line.");
    else if (model.contains(QRegularExpression("\\s")))
        message = QCoreApplication::translate("AiBibleSettings", "The model ID must not contain spaces.");
    else if (!transcriptUrl.trimmed().isEmpty() && (!url.isValid() || (scheme != "http" && scheme != "https") || url.host().isEmpty()))
        message = QCoreApplication::translate("AiBibleSettings", "The transcript URL must be a full http:// or https:// address.");
    else if (minConfidence < 0.0 || minConfidence > 1.0)
        message = QCoreApplication::translate("AiBibleSettings", "Minimum confidence must be between 0 and 1.");
    else if (pauseSeconds < 0 || pauseSeconds > 600)
        message = QCoreApplication::translate("AiBibleSettings", "The pause after manual changes must be between 0 and 600 seconds.");
    else if (enabled && (apiKey.trimmed().isEmpty() || model.trimmed().isEmpty() || transcriptUrl.trimmed().isEmpty()))
        message = QCoreApplication::translate("AiBibleSettings", "To enable the AI Bible assistant, enter an OpenRouter API key, a model, and a transcript URL.");
    if (error)
        *error = message;
    return message.isEmpty();
}

bool AiBibleSettings::isUsable() const
{
    return enabled && validate();
}
