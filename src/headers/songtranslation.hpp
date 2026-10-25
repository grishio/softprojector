#ifndef SONGTRANSLATION_HPP
#define SONGTRANSLATION_HPP

#include <QJsonObject>
#include <QList>
#include <QStringList>
#include "openaisettings.hpp"

// Translates stored stanza blocks, never the expanded performance order.
class SongTranslation
{
public:
    struct Stanza {
        QString heading;
        QStringList lines;
    };

    bool parse(const QString &songText, QString *error);
    QJsonObject request(const OpenAiSettings &settings, const QString &title) const;
    bool readResponse(const QByteArray &response, QString *proposal, QString *error) const;
    QList<Stanza> stanzas() const { return source; }

private:
    QList<Stanza> source;
};

#endif
