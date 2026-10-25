#ifndef AIBIBLESETTINGSWIDGET_HPP
#define AIBIBLESETTINGSWIDGET_HPP

#include <QWidget>
#include "aibiblesettings.hpp"

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class AiBibleSettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AiBibleSettingsWidget(QWidget *parent = nullptr);
    void loadSettings();
    // Returns false (and warns) if the settings are invalid or could not be saved.
    // Sets *changed when the stored settings differ from what was loaded.
    bool saveSettings(bool *changed = nullptr);

private:
    QCheckBox *enabled;
    QLineEdit *apiKey;
    QCheckBox *showKey;
    QLineEdit *model;
    QLineEdit *transcriptUrl;
    QDoubleSpinBox *minConfidence;
    QSpinBox *pauseSeconds;
    QLabel *status;
    AiBibleSettings savedSettings;
    bool loaded = false;
    AiBibleSettings currentValues() const;
};

#endif
