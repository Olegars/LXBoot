#include "core/Store.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>
#include <QJsonDocument>

Store::Store(const LxPaths &paths)
    : m_paths(paths)
{
}

bool Store::open(QString *error)
{
    m_paths.ensureDirs();
    if (QSqlDatabase::contains(QStringLiteral("lxboot")))
        m_db = QSqlDatabase::database(QStringLiteral("lxboot"));
    else {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("lxboot"));
        m_db.setDatabaseName(m_paths.db);
    }
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }
    QSqlQuery prag(m_db);
    prag.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    prag.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    return migrate(error);
}

QJsonArray Store::query(const QString &sql, const QJsonArray &binds) const
{
    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const auto &b : binds)
        q.addBindValue(b.toVariant());
    QJsonArray rows;
    if (!q.exec())
        return rows;
    while (q.next()) {
        QJsonObject row;
        const QSqlRecord rec = q.record();
        for (int i = 0; i < rec.count(); ++i) {
            const QVariant v = q.value(i);
            const QString name = rec.fieldName(i);
            if (v.isNull())
                row.insert(name, QJsonValue::Null);
            else if (v.typeId() == QMetaType::Bool)
                row.insert(name, v.toBool());
            else if (v.typeId() == QMetaType::LongLong || v.typeId() == QMetaType::Int)
                row.insert(name, v.toLongLong());
            else if (v.typeId() == QMetaType::Double)
                row.insert(name, v.toDouble());
            else
                row.insert(name, v.toString());
        }
        rows.append(row);
    }
    return rows;
}

bool Store::exec(const QString &sql, const QJsonArray &binds, QString *error)
{
    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const auto &b : binds)
        q.addBindValue(b.toVariant());
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    return true;
}

qint64 Store::lastId() const
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT last_insert_rowid()"));
    if (q.next())
        return q.value(0).toLongLong();
    return 0;
}

void Store::log(const QString &level, const QString &event, const QString &detail)
{
    exec(QStringLiteral("INSERT INTO event_log(ts, level, event, detail) VALUES(?,?,?,?)"),
         QJsonArray{QDateTime::currentDateTimeUtc().toString(Qt::ISODate), level, event, detail});
}

QJsonObject Store::settings() const
{
    QJsonObject o;
    for (const auto &v : query(QStringLiteral("SELECT key, value FROM settings"))) {
        const auto row = v.toObject();
        o.insert(row.value(QStringLiteral("key")).toString(), row.value(QStringLiteral("value")).toString());
    }
    return o;
}

QString Store::setting(const QString &key, const QString &fallback) const
{
    const auto rows = query(QStringLiteral("SELECT value FROM settings WHERE key=?"), QJsonArray{key});
    if (rows.isEmpty())
        return fallback;
    return rows.first().toObject().value(QStringLiteral("value")).toString();
}

void Store::setSetting(const QString &key, const QString &value)
{
    exec(QStringLiteral("INSERT INTO settings(key, value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"),
         QJsonArray{key, value});
}

bool Store::migrate(QString *error)
{
    const char *ddl[] = {
        "CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT)",
        "CREATE TABLE IF NOT EXISTS groups("
        "id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL, image_id INTEGER, wb_policy TEXT DEFAULT 'reset')",
        "CREATE TABLE IF NOT EXISTS images("
        "id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL, path TEXT, size_bytes INTEGER DEFAULT 0,"
        "kind TEXT DEFAULT 'qcow2', is_default INTEGER DEFAULT 0, created_ts TEXT)",
        "CREATE TABLE IF NOT EXISTS restore_points("
        "id INTEGER PRIMARY KEY, image_id INTEGER NOT NULL, note TEXT, path TEXT, created_ts TEXT,"
        "FOREIGN KEY(image_id) REFERENCES images(id))",
        "CREATE TABLE IF NOT EXISTS disks("
        "name TEXT PRIMARY KEY, role TEXT DEFAULT 'offline', ram_cache_mb INTEGER DEFAULT 0, path TEXT)",
        "CREATE TABLE IF NOT EXISTS clients("
        "mac TEXT PRIMARY KEY, hostname TEXT, ip TEXT, group_id INTEGER, image_id INTEGER,"
        "wb_policy TEXT DEFAULT 'reset', super_client INTEGER DEFAULT 0, note TEXT,"
        "last_seen TEXT, wb_bytes INTEGER DEFAULT 0, rx_bps INTEGER DEFAULT 0, tx_bps INTEGER DEFAULT 0,"
        "online INTEGER DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS dhcp("
        "id INTEGER PRIMARY KEY CHECK(id=1), enabled INTEGER DEFAULT 1, interface TEXT,"
        "server_ip TEXT, pool_start TEXT, pool_end TEXT, netmask TEXT DEFAULT '255.255.255.0',"
        "gateway TEXT, dns TEXT DEFAULT '8.8.8.8', lease_time INTEGER DEFAULT 3600)",
        "CREATE TABLE IF NOT EXISTS event_log("
        "id INTEGER PRIMARY KEY, ts TEXT, level TEXT, event TEXT, detail TEXT)",
        nullptr
    };
    for (int i = 0; ddl[i]; ++i) {
        if (!exec(QString::fromUtf8(ddl[i]), {}, error))
            return false;
    }

    const auto n = query(QStringLiteral("SELECT COUNT(*) AS n FROM dhcp"));
    if (n.first().toObject().value(QStringLiteral("n")).toInteger() == 0) {
        exec(QStringLiteral("INSERT INTO dhcp(id, interface, server_ip, pool_start, pool_end, gateway) "
                            "VALUES(1,'eth0','10.0.0.1','10.0.0.100','10.0.0.200','10.0.0.1')"));
    }
    if (query(QStringLiteral("SELECT COUNT(*) AS n FROM groups")).first().toObject().value(QStringLiteral("n")).toInteger() == 0)
        exec(QStringLiteral("INSERT INTO groups(name, wb_policy) VALUES('default','reset')"));

    if (setting(QStringLiteral("admin_hash")).isEmpty()) {
        const QByteArray salt = QByteArrayLiteral("lxboot-v1");
        const QByteArray hash = QCryptographicHash::hash(salt + QByteArrayLiteral("lxboot"), QCryptographicHash::Sha256).toHex();
        setSetting(QStringLiteral("admin_hash"), QString::fromLatin1(hash));
        setSetting(QStringLiteral("admin_salt"), QString::fromLatin1(salt));
    }
    if (setting(QStringLiteral("apply")).isEmpty())
        setSetting(QStringLiteral("apply"), qEnvironmentVariableIsSet("LXBOOT_APPLY") ? QStringLiteral("1") : QStringLiteral("0"));
    if (setting(QStringLiteral("ssh")).isEmpty())
        setSetting(QStringLiteral("ssh"), QStringLiteral("0"));
    return true;
}
