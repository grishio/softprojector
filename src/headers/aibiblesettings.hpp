#ifndef AIBIBLESETTINGS_HPP
#define AIBIBLESETTINGS_HPP

#include <QString>

// Settings for the AI Bible assistant: a live transcript (SSE) feed is
// classified by an OpenRouter Decisions model, which preselects the Bible
// book, chapter and verse in the Bible tab. Nothing is ever sent live
// automatically; the operator confirms with F5 or the Live button.
class AiBibleSettings
{
public:
    static QString defaultModel() { return QStringLiteral("cloudflare/clef-flash"); }

    bool enabled = false;
    QString apiKey;
    QString model = defaultModel();
    QString transcriptUrl;
    // Minimum probability of the chosen label before a selection is changed.
    double minConfidence = 0.6;
    // Seconds the assistant stays quiet after the operator changes the selection.
    int pauseSeconds = 10;

    static bool ensureTable(QString *error = nullptr);
    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr) const;
    bool validate(QString *error = nullptr) const;
    bool isUsable() const;
};

#endif
