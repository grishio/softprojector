#include "../headers/aibiblesettingswidget.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

AiBibleSettingsWidget::AiBibleSettingsWidget(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("AI Bible Assistant"), this);
    QFont font = heading->font();
    font.setBold(true);
    font.setPointSize(12);
    heading->setFont(font);
    layout->addWidget(heading);
    auto *description = new QLabel(tr("Listens to a live sermon transcript and preselects the Bible book, chapter, and verse "
                                      "the speaker names. Selections are never shown automatically: press F5 or Live to show them."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *form = new QFormLayout;
    enabled = new QCheckBox(tr("Enable AI Bible assistant"), this);
    enabled->setObjectName("aiBibleEnabled");
    form->addRow(QString(), enabled);

    apiKey = new QLineEdit(this);
    apiKey->setObjectName("aiBibleApiKey");
    apiKey->setEchoMode(QLineEdit::Password);
    apiKey->setPlaceholderText(tr("Enter your OpenRouter API key"));
    form->addRow(tr("OpenRouter API key:"), apiKey);
    showKey = new QCheckBox(tr("Show API key"), this);
    form->addRow(QString(), showKey);
    connect(showKey, &QCheckBox::toggled, this, [this](bool show) {
        apiKey->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
    });

    model = new QLineEdit(this);
    model->setObjectName("aiBibleModel");
    model->setPlaceholderText(AiBibleSettings::defaultModel());
    auto *resetModel = new QPushButton(tr("Default"), this);
    auto *modelRow = new QHBoxLayout;
    modelRow->addWidget(model);
    modelRow->addWidget(resetModel);
    form->addRow(tr("Decision model:"), modelRow);
    connect(resetModel, &QPushButton::clicked, this, [this]() { model->setText(AiBibleSettings::defaultModel()); });

    transcriptUrl = new QLineEdit(this);
    transcriptUrl->setObjectName("aiBibleTranscriptUrl");
    transcriptUrl->setPlaceholderText(tr("https://example.com/api/data/gettranscript?organization=...&translationsession=..."));
    form->addRow(tr("Transcript feed URL (SSE):"), transcriptUrl);

    minConfidence = new QDoubleSpinBox(this);
    minConfidence->setObjectName("aiBibleMinConfidence");
    minConfidence->setRange(0.0, 1.0);
    minConfidence->setSingleStep(0.05);
    minConfidence->setDecimals(2);
    form->addRow(tr("Minimum confidence:"), minConfidence);

    pauseSeconds = new QSpinBox(this);
    pauseSeconds->setObjectName("aiBiblePauseSeconds");
    pauseSeconds->setRange(0, 600);
    pauseSeconds->setSuffix(tr(" s"));
    form->addRow(tr("Pause after manual change:"), pauseSeconds);
    layout->addLayout(form);

    auto *help = new QLabel(tr("The transcript feed must be a Server-Sent Events stream whose data lines are JSON objects such as "
                               "{\"id\": 3322, \"text\": \"...\"}. Each new line is classified by an OpenRouter Decisions model "
                               "(default cloudflare/clef-flash) in the background; the operator Bible's book list, chapter and verse "
                               "numbers, and the current selection are sent with the most recent transcript lines. "
                               "Higher minimum confidence means fewer, safer changes. After you change the Bible selection "
                               "yourself, the assistant waits the configured number of seconds before changing it again."), this);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto *storage = new QLabel(tr("The API key is saved in the application's SQLite database without encryption. "
                                  "Transcript text is sent to OpenRouter."), this);
    storage->setWordWrap(true);
    layout->addWidget(storage);
    status = new QLabel(this);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    layout->addStretch();
}

void AiBibleSettingsWidget::loadSettings()
{
    QString error;
    savedSettings = AiBibleSettings();
    loaded = savedSettings.load(&error);
    enabled->setChecked(savedSettings.enabled);
    apiKey->setText(savedSettings.apiKey);
    apiKey->setEchoMode(QLineEdit::Password);
    showKey->setChecked(false);
    model->setText(savedSettings.model);
    transcriptUrl->setText(savedSettings.transcriptUrl);
    minConfidence->setValue(savedSettings.minConfidence);
    pauseSeconds->setValue(savedSettings.pauseSeconds);
    setEnabled(loaded);
    status->setText(loaded ? QString() : tr("Could not load AI Bible assistant settings: %1").arg(error));
}

AiBibleSettings AiBibleSettingsWidget::currentValues() const
{
    AiBibleSettings settings;
    settings.enabled = enabled->isChecked();
    settings.apiKey = apiKey->text().trimmed();
    settings.model = model->text().trimmed().isEmpty() ? AiBibleSettings::defaultModel() : model->text().trimmed();
    settings.transcriptUrl = transcriptUrl->text().trimmed();
    settings.minConfidence = minConfidence->value();
    settings.pauseSeconds = pauseSeconds->value();
    return settings;
}

bool AiBibleSettingsWidget::saveSettings(bool *changed)
{
    if (changed)
        *changed = false;
    if (!loaded) {
        QMessageBox::warning(this, tr("AI Bible assistant"), tr("AI Bible assistant settings could not be loaded. Reopen Settings to try again."));
        return false;
    }
    const AiBibleSettings settings = currentValues();
    QString error;
    if (!settings.save(&error)) {
        QMessageBox::warning(this, tr("AI Bible assistant"), tr("Could not save AI Bible assistant settings: %1").arg(error));
        return false;
    }
    if (changed)
        *changed = settings.enabled != savedSettings.enabled || settings.apiKey != savedSettings.apiKey ||
            settings.model != savedSettings.model || settings.transcriptUrl != savedSettings.transcriptUrl ||
            !qFuzzyCompare(1.0 + settings.minConfidence, 1.0 + savedSettings.minConfidence) ||
            settings.pauseSeconds != savedSettings.pauseSeconds;
    savedSettings = settings;
    model->setText(settings.model);
    return true;
}
