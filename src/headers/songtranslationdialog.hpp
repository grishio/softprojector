#ifndef SONGTRANSLATIONDIALOG_HPP
#define SONGTRANSLATIONDIALOG_HPP

#include <QDialog>
#include "openaisettings.hpp"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class OpenAiClient;
class QNetworkAccessManager;

class SongTranslationDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SongTranslationDialog(const QString &songText, const QString &title,
                                   const OpenAiSettings &settings, QWidget *parent = nullptr,
                                   QNetworkAccessManager *network = nullptr);
    QString proposedSongText() const { return proposal; }
    void reject() override;

private:
    QString original;
    QString songTitle;
    QString proposal;
    OpenAiSettings configuration;
    OpenAiClient *client;
    QPlainTextEdit *preview;
    QPushButton *translateButton;
    QPushButton *acceptButton;
    QPushButton *cancelRequestButton;
    QLabel *status;
    bool modelRequiresSelection = false;
    void setBusy(bool busy);
    void clearProposal();
};

#endif
