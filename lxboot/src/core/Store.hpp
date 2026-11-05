#pragma once

#include "Paths.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

class Store {
public:
    explicit Store(const LxPaths &paths);
    bool open(QString *error);

    QJsonArray query(const QString &sql, const QJsonArray &binds = {}) const;
    bool exec(const QString &sql, const QJsonArray &binds = {}, QString *error = nullptr);
    qint64 lastId() const;

    void log(const QString &level, const QString &event, const QString &detail = {});
    QJsonObject settings() const;
    QString setting(const QString &key, const QString &fallback = {}) const;
    void setSetting(const QString &key, const QString &value);

private:
    bool migrate(QString *error);
    LxPaths m_paths;
    QSqlDatabase m_db;
};
