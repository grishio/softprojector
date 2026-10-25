#include "../headers/openaisettings.hpp"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

namespace {
bool databaseResult(bool ok, const QSqlQuery &query, QString *error)
{
    if (!ok && error)
        *error = query.lastError().text();
    return ok;
}

}

bool OpenAiSettings::ensureTable(QString *error)
{
    QSqlQuery query;
    if (!databaseResult(query.exec(
            "CREATE TABLE IF NOT EXISTS OpenAiSettings ("
            "id INTEGER PRIMARY KEY CHECK (id = 1), "
            "api_key TEXT NOT NULL DEFAULT '', "
            "model TEXT NOT NULL DEFAULT '', "
            "reasoning_effort TEXT NOT NULL DEFAULT '')"), query, error))
        return false;
    // Specify empty values explicitly, including databases created by earlier builds
    // whose model column had a preset default. Existing selections are preserved.
    return databaseResult(query.exec("INSERT OR IGNORE INTO OpenAiSettings (id, api_key, model, reasoning_effort) VALUES (1, '', '', '')"), query, error);
}

bool OpenAiSettings::load(QString *error)
{
    QSqlQuery query;
    if (!databaseResult(query.exec("SELECT api_key, model, reasoning_effort FROM OpenAiSettings WHERE id = 1"), query, error))
        return false;
    if (!query.next()) {
        if (error)
            *error = QCoreApplication::translate("OpenAiSettings", "OpenAI settings are missing from the database.");
        return false;
    }
    apiKey = query.value(0).toString();
    model = query.value(1).toString();
    reasoningEffort = query.value(2).toString();
    return true;
}

bool OpenAiSettings::save(QString *error) const
{
    if (!validate(error))
        return false;
    QSqlQuery query;
    query.prepare("INSERT OR REPLACE INTO OpenAiSettings (id, api_key, model, reasoning_effort) VALUES (1, ?, ?, ?)");
    const QString key = apiKey.trimmed();
    // Qt's null QString binds as SQL NULL; these columns require empty text.
    query.addBindValue(key.isEmpty() ? QStringLiteral("") : key);
    const QString selectedModel = model.trimmed();
    query.addBindValue(selectedModel.isEmpty() ? QStringLiteral("") : selectedModel);
    query.addBindValue(reasoningEffort.isEmpty() ? QStringLiteral("") : reasoningEffort);
    return databaseResult(query.exec(), query, error);
}

bool OpenAiSettings::clearUnavailableModel(const QString &apiKey, const QString &model, QString *error)
{
    QSqlQuery query;
    // Do not erase a replacement saved by another settings dialog or API key.
    query.prepare("UPDATE OpenAiSettings SET model = '', reasoning_effort = '' WHERE id = 1 AND api_key = ? AND model = ?");
    query.addBindValue(apiKey.trimmed());
    query.addBindValue(model.trimmed());
    return databaseResult(query.exec(), query, error);
}

QStringList OpenAiSettings::reasoningEfforts()
{
    // API enum values, not model capabilities. /models does not expose the latter;
    // the API validates an explicitly selected effort for the chosen model.
    return {"none", "minimal", "low", "medium", "high", "xhigh", "max"};
}

bool OpenAiSettings::validate(QString *error) const
{
    QString message;
    if (apiKey.contains(QRegularExpression("[\\r\\n]")))
        message = QCoreApplication::translate("OpenAiSettings", "The API key must be a single line.");
    else if (!apiKey.trimmed().isEmpty() && model.trimmed().isEmpty())
        message = QCoreApplication::translate("OpenAiSettings", "Choose an OpenAI model.");
    else if (!reasoningEffort.isEmpty() && !reasoningEfforts().contains(reasoningEffort))
        message = QCoreApplication::translate("OpenAiSettings", "Choose a valid thinking effort or Model default.");
    if (error)
        *error = message;
    return message.isEmpty();
}
