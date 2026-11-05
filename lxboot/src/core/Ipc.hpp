#pragma once

#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>

class Engine;

class IpcServer : public QObject {
    Q_OBJECT
public:
    IpcServer(Engine *engine, const QString &path, QObject *parent = nullptr);
    bool listen(QString *error);

private:
    void onNew();
    void onReady();
    Engine *m_engine = nullptr;
    QString m_path;
    QLocalServer m_server;
};

class IpcClient : public QObject {
    Q_OBJECT
public:
    explicit IpcClient(QObject *parent = nullptr);
    bool connectTo(const QString &path, QString *error);
    QJsonObject call(const QString &method, const QJsonObject &params = {}, int timeoutMs = 30000);
    bool isConnected() const;

private:
    QLocalSocket m_sock;
    quint32 m_id = 1;
};
