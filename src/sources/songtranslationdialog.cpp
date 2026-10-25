#include "../headers/songtranslationdialog.hpp"
#include "../headers/openaiclient.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

SongTranslationDialog::SongTranslationDialog(const QString &songText, const QString &title,
    const OpenAiSettings &settings, QWidget *parent, QNetworkAccessManager *network) : QDialog(parent),
    original(songText), songTitle(title), configuration(settings)
{
    setWindowTitle(tr("Translate song"));
    resize(1000, 650);
    auto *layout = new QVBoxLayout(this);
    auto *controls = new QHBoxLayout;
    auto *direction = new QLabel(tr("Russian or Ukrainian to English; English to Russian, line by line."), this);
    direction->setWordWrap(true);
    controls->addWidget(direction, 1);
    translateButton = new QPushButton(tr("Translate"), this);
    translateButton->setObjectName("generateTranslation");
    controls->addWidget(translateButton);
    cancelRequestButton = new QPushButton(tr("Cancel request"), this);
    cancelRequestButton->setObjectName("cancelTranslationRequest");
    controls->addWidget(cancelRequestButton);
    layout->addLayout(controls);
    auto *info = new QLabel(tr("Model: %1 | Thinking effort: %2").arg(configuration.model,
        configuration.reasoningEffort.isEmpty() ? tr("Model default") : configuration.reasoningEffort), this);
    info->setTextFormat(Qt::PlainText);
    layout->addWidget(info);
    auto *splitter = new QSplitter(this);
    auto *left = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->addWidget(new QLabel(tr("Current song"), left));
    auto *current = new QPlainTextEdit(original, left);
    current->setReadOnly(true);
    leftLayout->addWidget(current);
    auto *right = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->addWidget(new QLabel(tr("Proposed song with translation (editable)"), right));
    preview = new QPlainTextEdit(right);
    preview->setObjectName("translationPreview");
    preview->setReadOnly(true);
    rightLayout->addWidget(preview);
    layout->addWidget(splitter, 1);
    status = new QLabel(tr("Review and edit the proposed translation before accepting. Keep the stanza headings, '=' separators, and matching line counts. "
                            "Accept applies it to the editor; Save commits the song. "
                            "Existing parallel translations will be replaced."), this);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    // Explicit order keeps Cancel to the left of Accept on every platform.
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *cancelButton = new QPushButton(tr("Cancel"), this);
    cancelButton->setObjectName("cancelTranslation");
    buttons->addWidget(cancelButton);
    acceptButton = new QPushButton(tr("Accept translation"), this);
    acceptButton->setObjectName("acceptTranslation");
    acceptButton->setEnabled(false);
    acceptButton->setAutoDefault(false);
    translateButton->setDefault(true);
    buttons->addWidget(acceptButton);
    layout->addLayout(buttons);
    client = new OpenAiClient(this, network);
    connect(preview, &QPlainTextEdit::textChanged, this, [this]() {
        proposal = preview->toPlainText();
        acceptButton->setEnabled(!client->isBusy() && !proposal.trimmed().isEmpty());
    });
    connect(acceptButton, &QPushButton::clicked, this, [this]() {
        if (!proposal.trimmed().isEmpty() && !client->isBusy())
            accept();
    });
    connect(cancelButton, &QPushButton::clicked, this, &SongTranslationDialog::reject);
    connect(cancelRequestButton, &QPushButton::clicked, this, [this]() {
        client->cancel();
        clearProposal();
        setBusy(false);
        status->setText(tr("Translation cancelled. The song has not changed."));
    });
    connect(translateButton, &QPushButton::clicked, this, [this]() {
        clearProposal();
        setBusy(true);
        status->setText(tr("Translating..."));
        client->translate(configuration, original, songTitle);
    });
    connect(client, &OpenAiClient::translationReady, this, [this](const QString &text) {
        proposal = text;
        preview->setPlainText(proposal);
        setBusy(false);
        acceptButton->setEnabled(true);
        translateButton->setText(tr("Re-translate"));
        status->setText(tr("Edit any words in the proposal before accepting. Keep the stanza headings, '=' separators, and matching line counts. "
                           "Accept applies your edits to the editor; Save commits the song. "
                           "Re-translate replaces the proposal with a new translation of the original lyrics."));
    });
    connect(client, &OpenAiClient::failed, this, [this](const QString &error) {
        clearProposal();
        setBusy(false);
        status->setText(error);
    });
    connect(client, &OpenAiClient::modelUnavailable, this, [this]() {
        modelRequiresSelection = true;
        clearProposal();
        setBusy(false);
    });
    setBusy(false);
}

void SongTranslationDialog::setBusy(bool busy)
{
    translateButton->setEnabled(!busy && !modelRequiresSelection);
    cancelRequestButton->setEnabled(busy);
    preview->setReadOnly(busy || proposal.isEmpty());
    acceptButton->setEnabled(!busy && !proposal.trimmed().isEmpty());
}

void SongTranslationDialog::clearProposal()
{
    proposal.clear();
    preview->clear();
    preview->setReadOnly(true);
    acceptButton->setEnabled(false);
}

void SongTranslationDialog::reject()
{
    client->cancel();
    clearProposal();
    QDialog::reject();
}
