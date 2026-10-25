#ifndef OPENAISETTINGS_HPP
#define OPENAISETTINGS_HPP

#include <QStringList>

class OpenAiSettings
{
public:
    QString apiKey;
    QString model;
    // Empty means use the model's default, omitting reasoning from the request.
    QString reasoningEffort;

    static bool ensureTable(QString *error = nullptr);
    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr) const;
    static QStringList reasoningEfforts();
    static bool clearUnavailableModel(const QString &apiKey, const QString &model, QString *error = nullptr);
    bool validate(QString *error = nullptr) const;
};

#endif
