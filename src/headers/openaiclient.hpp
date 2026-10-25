#ifndef OPENAICLIENT_HPP
#define OPENAICLIENT_HPP

#include <QObject>
#include <QPointer>
#include "songtranslation.hpp"

class QNetworkAccessManager;
class QNetworkReply;

class OpenAiClient : public QObject
{
    Q_OBJECT
public:
    explicit OpenAiClient(QObject *parent = nullptr, QNetworkAccessManager *manager = nullptr);
    ~OpenAiClient() override;
    bool isBusy() const { return !activeReply.isNull(); }
    void fetchModels(const QString &apiKey);
    void translate(const OpenAiSettings &settings, const QString &songText,
                   const QString &title);
    void cancel();

signals:
    void modelsReady(const QStringList &models);
    void modelUnavailable(const QString &model);
    void translationReady(const QString &proposal);
    void failed(const QString &message);

private:
    QNetworkAccessManager *network;
    QPointer<QNetworkReply> activeReply;
    SongTranslation translation;
    enum Operation { ListModels, CheckModel, TranslateSong };
    void send(const QString &apiKey, const QByteArray &body, Operation operation,
              const QString &selectedModel = QString());
    void requireModelSelection(const QString &apiKey, const QString &model);
};

#endif
