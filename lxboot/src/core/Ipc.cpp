#include "core/Ipc.hpp"
#include "core/Engine.hpp"

#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>

static QByteArray pack(const QJsonObject &o)
{
    const QByteArray payload = QJsonDocument(o).toJson(QJsonDocument::Compact);
    QByteArray buf;
    QDataStream ds(&buf, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);
    ds << quint32(payload.size());
    buf.append(payload);
    return buf;
}

static QJsonObject unpack(QIODevice *dev, int timeoutMs)
{
    QDataStream ds(dev);
    ds.setByteOrder(QDataStream::BigEndian);
    const auto wait = [&](int n) {
        QElapsedTimer t;
        t.start();
        while (dev->bytesAvailable() < n && t.elapsed() < timeoutMs)
            dev->waitForReadyRead(200);
        return dev->bytesAvailable() >= n;
    };
    if (!wait(4))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("ipc timeout")}};
    quint32 size = 0;
    ds >> size;
    if (size > 8 * 1024 * 1024)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("ipc too large")}};
    if (!wait(int(size)))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("ipc payload timeout")}};
    const QByteArray payload = dev->read(size);
    const auto doc = QJsonDocument::fromJson(payload);
    return doc.object();
}

IpcServer::IpcServer(Engine *engine, const QString &path, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_path(path)
{
}

bool IpcServer::listen(QString *error)
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QLocalServer::removeServer(m_path);
    connect(&m_server, &QLocalServer::newConnection, this, &IpcServer::onNew);
    if (!m_server.listen(m_path)) {
        if (error)
            *error = m_server.errorString();
        return false;
    }
    return true;
}

void IpcServer::onNew()
{
    while (QLocalSocket *s = m_server.nextPendingConnection()) {
        connect(s, &QLocalSocket::readyRead, this, &IpcServer::onReady);
        connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
    }
}

void IpcServer::onReady()
{
    auto *s = qobject_cast<QLocalSocket *>(sender());
    if (!s)
        return;
    while (s->bytesAvailable() >= 4) {
        const QJsonObject req = unpack(s, 30000);
        const QString method = req.value(QStringLiteral("method")).toString();
        const QJsonObject params = req.value(QStringLiteral("params")).toObject();
        QJsonObject res = m_engine->call(method, params);
        res.insert(QStringLiteral("id"), req.value(QStringLiteral("id")));
        s->write(pack(res));
        s->flush();
        if (!s->bytesAvailable())
            break;
    }
}

IpcClient::IpcClient(QObject *parent)
    : QObject(parent)
{
}

bool IpcClient::connectTo(const QString &path, QString *error)
{
    m_sock.connectToServer(path);
    if (!m_sock.waitForConnected(1500)) {
        if (error)
            *error = m_sock.errorString();
        return false;
    }
    return true;
}

bool IpcClient::isConnected() const
{
    return m_sock.state() == QLocalSocket::ConnectedState;
}

QJsonObject IpcClient::call(const QString &method, const QJsonObject &params, int timeoutMs)
{
    if (!isConnected())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("not connected")}};
    const quint32 id = m_id++;
    QJsonObject req{{QStringLiteral("id"), int(id)}, {QStringLiteral("method"), method}, {QStringLiteral("params"), params}};
    m_sock.write(pack(req));
    m_sock.flush();
    return unpack(&m_sock, timeoutMs);
}
