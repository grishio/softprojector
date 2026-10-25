#ifndef OPENAISETTINGSWIDGET_HPP
#define OPENAISETTINGSWIDGET_HPP

#include <QWidget>
#include "openaisettings.hpp"

class QLineEdit;
class QComboBox;
class QLabel;
class QPushButton;
class OpenAiClient;
class QNetworkAccessManager;

class OpenAiSettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit OpenAiSettingsWidget(QWidget *parent = nullptr, QNetworkAccessManager *network = nullptr);
    void loadSettings();
    bool saveSettings();
    void cancelRequest();

private:
    QLineEdit *apiKey;
    QComboBox *model;
    QComboBox *effort;
    QLabel *status;
    QPushButton *refresh;
    OpenAiClient *client;
    OpenAiSettings savedSettings;
    QString catalogKey;
    bool catalogReady = false;
    bool loaded = false;
    void refreshModels();
    void apiKeyChanged();
    void updateEfforts();
};

#endif
