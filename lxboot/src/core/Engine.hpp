#pragma once

#include "ConfigGen.hpp"
#include "Host.hpp"
#include "Metrics.hpp"
#include "Paths.hpp"
#include "Store.hpp"

#include <QJsonObject>
#include <QObject>

class Engine : public QObject {
    Q_OBJECT
public:
    explicit Engine(QObject *parent = nullptr);
    bool init(QString *error);
    QJsonObject call(const QString &method, const QJsonObject &params = {});
    const LxPaths &paths() const { return m_paths; }

signals:
    void eventLogged(const QString &level, const QString &event, const QString &detail);

private:
    QJsonObject ok(const QJsonObject &extra = {}) const;
    QJsonObject fail(const QString &msg) const;

    QJsonObject status() const;
    QJsonObject netList() const;
    QJsonObject netApply(const QJsonObject &p);
    QJsonObject dhcpGet() const;
    QJsonObject dhcpApply(const QJsonObject &p);
    QJsonObject diskList() const;
    QJsonObject diskSetRole(const QJsonObject &p);
    QJsonObject imageList() const;
    QJsonObject imageImport(const QJsonObject &p);
    QJsonObject imageDefault(const QJsonObject &p);
    QJsonObject imageRestore(const QJsonObject &p);
    QJsonObject clientList() const;
    QJsonObject clientUpsert(const QJsonObject &p);
    QJsonObject clientSuper(const QJsonObject &p);
    QJsonObject clientResetWb(const QJsonObject &p);
    QJsonObject groupList() const;
    QJsonObject groupUpsert(const QJsonObject &p);
    QJsonObject logList(const QJsonObject &p) const;
    QJsonObject service(const QString &method, const QJsonObject &p);
    QJsonObject leaseEvent(const QJsonObject &p);
    QJsonObject applyRuntime();
    QJsonObject ensureClientStorage(const QJsonObject &client, bool super);
    QJsonObject commitSuper(const QJsonObject &p);
    bool writeFile(const QString &path, const QString &data, QString *err) const;
    QString storageBackend() const;
    QString normalizeMac(QString mac) const;
    QString clientIqn(const QString &mac) const;
    QString clientWbPath(const QString &mac) const;
    QString imagePath(qint64 imageId) const;

    LxPaths m_paths;
    Store m_store;
    Host m_host;
    Metrics m_metrics;
    ConfigGen m_gen;
};
