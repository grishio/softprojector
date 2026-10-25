#include "../headers/openaisettingswidget.hpp"
#include "../headers/openaiclient.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

OpenAiSettingsWidget::OpenAiSettingsWidget(QWidget *parent, QNetworkAccessManager *network) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("OpenAI Song Translation"), this);
    QFont font = heading->font();
    font.setBold(true);
    font.setPointSize(12);
    heading->setFont(font);
    layout->addWidget(heading);
    auto *description = new QLabel(tr("Configure the OpenAI API used by Translate in the song editor."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *form = new QFormLayout;
    apiKey = new QLineEdit(this);
    apiKey->setObjectName("openAiApiKey");
    apiKey->setEchoMode(QLineEdit::Password);
    apiKey->setPlaceholderText(tr("Enter your OpenAI API key"));
    form->addRow(tr("API key:"), apiKey);
    auto *showKey = new QCheckBox(tr("Show API key"), this);
    form->addRow(QString(), showKey);
    connect(showKey, &QCheckBox::toggled, this, [this](bool show) {
        apiKey->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
    });
    model = new QComboBox(this);
    model->setObjectName("openAiModel");
    model->setEditable(true);
    model->setInsertPolicy(QComboBox::NoInsert);
    model->lineEdit()->setPlaceholderText(tr("Type to filter models, then select a match"));
    model->lineEdit()->setClearButtonEnabled(true);
    auto *completer = new QCompleter(model->model(), model);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setModelSorting(QCompleter::UnsortedModel);
    model->setCompleter(completer);
    connect(model->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
        model->setEditText(model->currentData(Qt::DisplayRole).toString());
    });
    form->addRow(tr("Model:"), model);
    refresh = new QPushButton(tr("Refresh available models"), this);
    refresh->setObjectName("openAiRefreshModels");
    form->addRow(QString(), refresh);
    effort = new QComboBox(this);
    effort->setObjectName("openAiReasoningEffort");
    form->addRow(tr("Thinking effort:"), effort);
    layout->addLayout(form);
    auto *help = new QLabel(tr("Models are loaded from OpenAI for your API key, newest first by creation date. Type in the model field to filter by name. "
                               "Choose a text model that supports structured output. "
                               "The API does not list supported thinking efforts; Model default uses the model's own setting. "
                               "If an effort is unsupported, choose Model default or another effort. Higher effort can take longer and cost more."), this);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto *storage = new QLabel(tr("The API key is saved in the application's SQLite database without encryption. "
                                  "Translation sends the song's title and original lyrics to OpenAI."), this);
    storage->setWordWrap(true);
    layout->addWidget(storage);
    status = new QLabel(this);
    status->setObjectName("openAiStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    layout->addStretch();
    client = new OpenAiClient(this, network);
    connect(model, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updateEfforts(); });
    connect(apiKey, &QLineEdit::textChanged, this, &OpenAiSettingsWidget::apiKeyChanged);
    connect(refresh, &QPushButton::clicked, this, &OpenAiSettingsWidget::refreshModels);
    connect(client, &OpenAiClient::modelsReady, this, [this](const QStringList &models) {
        const QString current = model->currentData(Qt::DisplayRole).toString();
        catalogKey = apiKey->text().trimmed();
        catalogReady = true;
        {
            QSignalBlocker blocker(model);
            model->clear();
            model->addItems(models);
            // Never choose a default or silently replace a remembered model.
            model->setCurrentIndex(current.isEmpty() ? -1 : model->findText(current));
        }
        updateEfforts();
        model->setEnabled(loaded && !models.isEmpty());
        refresh->setEnabled(loaded && !catalogKey.isEmpty());
        if (!current.isEmpty() && model->currentIndex() < 0) {
            QString error;
            const bool cleared = OpenAiSettings::clearUnavailableModel(catalogKey, current, &error);
            if (catalogKey == savedSettings.apiKey && current == savedSettings.model) {
                savedSettings.model.clear();
                savedSettings.reasoningEffort.clear();
            }
            status->setText(tr("The previously selected model '%1' is no longer available. Select a replacement before translating.").arg(current));
            if (!cleared)
                status->setText(status->text() + tr(" Could not clear the saved selection: %1").arg(error));
        } else if (models.isEmpty())
            status->setText(tr("No available models were returned for this API key. Check the account's model access and refresh again."));
        else
            status->setText(model->currentIndex() < 0 ? tr("Available models loaded. Select a model.") : tr("Available models refreshed."));
    });
    connect(client, &OpenAiClient::failed, this, [this](const QString &error) {
        refresh->setEnabled(loaded && !apiKey->text().trimmed().isEmpty());
        model->setEnabled(loaded && catalogReady);
        status->setText(error);
    });
    updateEfforts();
}

void OpenAiSettingsWidget::updateEfforts()
{
    const QString previous = effort->currentData().toString();
    effort->clear();
    effort->addItem(tr("Model default"), QString());
    const QStringList choices = model->currentIndex() < 0 ? QStringList() : OpenAiSettings::reasoningEfforts();
    for (const QString &choice : choices)
        effort->addItem(choice, choice);
    const int index = effort->findData(previous);
    effort->setCurrentIndex(index < 0 ? 0 : index);
    effort->setEnabled(!choices.isEmpty());
}

void OpenAiSettingsWidget::refreshModels()
{
    refresh->setEnabled(false);
    model->setEnabled(false);
    status->setText(tr("Loading models..."));
    client->fetchModels(apiKey->text());
}

void OpenAiSettingsWidget::apiKeyChanged()
{
    cancelRequest();
    catalogReady = false;
    catalogKey.clear();
    {
        QSignalBlocker blocker(model);
        model->clear();
        if (apiKey->text().trimmed() == savedSettings.apiKey && !savedSettings.model.isEmpty()) {
            model->addItem(savedSettings.model);
            model->setCurrentIndex(0);
        } else
            model->setCurrentIndex(-1);
    }
    model->setEnabled(false);
    updateEfforts();
    status->setText(apiKey->text().trimmed().isEmpty() ? QString() : tr("Refresh available models for this API key, then select a model."));
}

void OpenAiSettingsWidget::cancelRequest()
{
    client->cancel();
    refresh->setEnabled(loaded && !apiKey->text().trimmed().isEmpty());
    status->clear();
}

void OpenAiSettingsWidget::loadSettings()
{
    cancelRequest();
    QString error;
    savedSettings = OpenAiSettings();
    loaded = savedSettings.load(&error);
    {
        QSignalBlocker blocker(apiKey);
        apiKey->setText(savedSettings.apiKey);
    }
    apiKeyChanged();
    apiKey->setEchoMode(QLineEdit::Password);
    for (QCheckBox *check : findChildren<QCheckBox *>())
        check->setChecked(false);
    const int index = effort->findData(savedSettings.reasoningEffort);
    effort->setCurrentIndex(index < 0 ? 0 : index);
    apiKey->setEnabled(loaded);
    if (!loaded)
        status->setText(tr("Could not load OpenAI settings: %1").arg(error));
    else if (!savedSettings.apiKey.isEmpty())
        refreshModels();
}

bool OpenAiSettingsWidget::saveSettings()
{
    if (!loaded) {
        QMessageBox::warning(this, tr("OpenAI settings"), tr("OpenAI settings could not be loaded. Reopen Settings to try again."));
        return false;
    }
    OpenAiSettings settings;
    settings.apiKey = apiKey->text().trimmed();
    // The editor text is a search query. Save only an actual API-list selection.
    settings.model = model->currentData(Qt::DisplayRole).toString();
    settings.reasoningEffort = effort->currentData().toString();
    const bool unchangedSelection = settings.apiKey == savedSettings.apiKey &&
        settings.model == savedSettings.model && !settings.model.isEmpty();
    if (!settings.apiKey.isEmpty() && !unchangedSelection &&
        (!catalogReady || catalogKey != settings.apiKey || model->currentIndex() < 0)) {
        QMessageBox::warning(this, tr("OpenAI settings"), tr("Refresh available models for this API key and select a model before saving."));
        return false;
    }
    QString error;
    if (!settings.save(&error)) {
        QMessageBox::warning(this, tr("OpenAI settings"), tr("Could not save OpenAI settings: %1").arg(error));
        return false;
    }
    savedSettings = settings;
    model->setEditText(settings.model);
    return true;
}
