#include "core/Engine.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <unistd.h>

Engine::Engine(QObject *parent)
    : QObject(parent)
    , m_paths(LxPaths::resolve())
    , m_store(m_paths)
    , m_gen(m_paths)
{
}

bool Engine::init(QString *error)
{
    m_paths.ensureDirs();
    if (!m_store.open(error))
        return false;
    m_store.log(QStringLiteral("info"), QStringLiteral("engine.start"),
                QStringLiteral("prefix=%1 apply=%2 uid=%3")
                    .arg(m_paths.prefix)
                    .arg(m_host.applyEnabled() ? 1 : 0)
                    .arg(::geteuid()));
    return true;
}

QJsonObject Engine::ok(const QJsonObject &extra) const
{
    QJsonObject o = extra;
    o.insert(QStringLiteral("ok"), true);
    return o;
}

QJsonObject Engine::fail(const QString &msg) const
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), msg}};
}

QString Engine::normalizeMac(QString mac) const
{
    mac = mac.trimmed().toLower();
    mac.replace(QLatin1Char('-'), QLatin1Char(':'));
    mac.remove(QLatin1Char('.'));
    if (mac.contains(QLatin1Char(':')) && mac.length() == 17)
        return mac;
    mac.remove(QLatin1Char(':'));
    if (mac.length() != 12)
        return {};
    QString out;
    for (int i = 0; i < 12; i += 2) {
        if (i)
            out += QLatin1Char(':');
        out += mac.mid(i, 2);
    }
    return out;
}

QString Engine::clientIqn(const QString &mac) const
{
    QString m = mac;
    m.replace(QLatin1Char(':'), QLatin1Char('-'));
    return QStringLiteral("iqn.2026-11.lxboot:client-%1").arg(m);
}

QString Engine::clientWbPath(const QString &mac) const
{
    QString m = mac;
    m.replace(QLatin1Char(':'), QLatin1Char('-'));
    return m_paths.writeback + QLatin1Char('/') + m + QStringLiteral(".qcow2");
}

QString Engine::imagePath(qint64 imageId) const
{
    const auto rows = m_store.query(QStringLiteral("SELECT path FROM images WHERE id=?"), QJsonArray{imageId});
    if (rows.isEmpty())
        return {};
    return rows.first().toObject().value(QStringLiteral("path")).toString();
}

QString Engine::storageBackend() const
{
    if (m_host.hasZfs())
        return QStringLiteral("zfs");
    if (m_host.hasLvm())
        return QStringLiteral("lvm");
    return QStringLiteral("qcow2");
}

bool Engine::writeFile(const QString &path, const QString &data, QString *err) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (err)
            *err = QStringLiteral("write %1 failed").arg(path);
        return false;
    }
    f.write(data.toUtf8());
    f.close();
    return true;
}

QJsonObject Engine::call(const QString &method, const QJsonObject &params)
{
    if (method == QLatin1String("ping"))
        return ok({{QStringLiteral("version"), QStringLiteral("0.1.0")}});
    if (method == QLatin1String("status"))
        return status();
    if (method == QLatin1String("net.list"))
        return netList();
    if (method == QLatin1String("net.apply"))
        return netApply(params);
    if (method == QLatin1String("dhcp.get"))
        return dhcpGet();
    if (method == QLatin1String("dhcp.apply"))
        return dhcpApply(params);
    if (method == QLatin1String("disk.list"))
        return diskList();
    if (method == QLatin1String("disk.setRole"))
        return diskSetRole(params);
    if (method == QLatin1String("image.list"))
        return imageList();
    if (method == QLatin1String("image.import"))
        return imageImport(params);
    if (method == QLatin1String("image.setDefault"))
        return imageDefault(params);
    if (method == QLatin1String("image.restore"))
        return imageRestore(params);
    if (method == QLatin1String("client.list"))
        return clientList();
    if (method == QLatin1String("client.upsert"))
        return clientUpsert(params);
    if (method == QLatin1String("client.delete")) {
        const QString mac = normalizeMac(params.value(QStringLiteral("mac")).toString());
        m_store.exec(QStringLiteral("DELETE FROM clients WHERE mac=?"), QJsonArray{mac});
        m_store.log(QStringLiteral("info"), QStringLiteral("client.delete"), mac);
        return ok();
    }
    if (method == QLatin1String("client.super"))
        return clientSuper(params);
    if (method == QLatin1String("client.resetWb"))
        return clientResetWb(params);
    if (method == QLatin1String("client.commitSuper"))
        return commitSuper(params);
    if (method == QLatin1String("group.list"))
        return groupList();
    if (method == QLatin1String("group.upsert"))
        return groupUpsert(params);
    if (method == QLatin1String("log.list"))
        return logList(params);
    if (method == QLatin1String("lease"))
        return leaseEvent(params);
    if (method == QLatin1String("runtime.apply"))
        return applyRuntime();
    if (method.startsWith(QLatin1String("service.")))
        return service(method.mid(8), params);
    if (method == QLatin1String("auth.check")) {
        const QString pass = params.value(QStringLiteral("password")).toString();
        const QByteArray salt = m_store.setting(QStringLiteral("admin_salt")).toUtf8();
        const QByteArray hash = QCryptographicHash::hash(salt + pass.toUtf8(), QCryptographicHash::Sha256).toHex();
        const bool good = QString::fromLatin1(hash) == m_store.setting(QStringLiteral("admin_hash"));
        return ok({{QStringLiteral("ok_auth"), good}});
    }
    return fail(QStringLiteral("unknown method %1").arg(method));
}

QJsonObject Engine::status() const
{
    QJsonObject o = ok();
    o.insert(QStringLiteral("metrics"), m_metrics.snapshot());
    o.insert(QStringLiteral("zfs"), m_host.zfsPools());
    o.insert(QStringLiteral("backend"), storageBackend());
    o.insert(QStringLiteral("privileged"), m_host.privileged());
    o.insert(QStringLiteral("apply"), m_host.applyEnabled());
    o.insert(QStringLiteral("qemu_img"), m_host.hasQemuImg());
    o.insert(QStringLiteral("targetcli"), m_host.hasTargetcli());
    o.insert(QStringLiteral("prefix"), m_paths.prefix);
    const auto online = m_store.query(QStringLiteral("SELECT COUNT(*) AS n FROM clients WHERE online=1"));
    const auto total = m_store.query(QStringLiteral("SELECT COUNT(*) AS n FROM clients"));
    o.insert(QStringLiteral("clients_online"), online.first().toObject().value(QStringLiteral("n")).toInteger());
    o.insert(QStringLiteral("clients_total"), total.first().toObject().value(QStringLiteral("n")).toInteger());
    const auto img = m_store.query(QStringLiteral("SELECT COUNT(*) AS n FROM images"));
    o.insert(QStringLiteral("images"), img.first().toObject().value(QStringLiteral("n")).toInteger());
    const auto roles = m_store.query(QStringLiteral("SELECT role, COUNT(*) AS n FROM disks GROUP BY role"));
    o.insert(QStringLiteral("disk_roles"), roles);
    const bool hasImage = !m_store.query(QStringLiteral("SELECT name FROM disks WHERE role='image'")).isEmpty();
    const bool hasWb = !m_store.query(QStringLiteral("SELECT name FROM disks WHERE role='writeback'")).isEmpty();
    o.insert(QStringLiteral("cfg_ok"), hasImage && hasWb);
    o.insert(QStringLiteral("cfg_error"),
             hasImage && hasWb ? QString()
                               : QStringLiteral("Назначьте диски Image и Write-back. На DOM/USB системы писать нельзя."));
    o.insert(QStringLiteral("ssh"), m_store.setting(QStringLiteral("ssh")) == QLatin1String("1"));
    o.insert(QStringLiteral("dhcp_enabled"),
             m_store.query(QStringLiteral("SELECT enabled FROM dhcp WHERE id=1"))
                 .first()
                 .toObject()
                 .value(QStringLiteral("enabled"))
                 .toInteger()
                 != 0);
    return o;
}

QJsonObject Engine::netList() const
{
    return ok({{QStringLiteral("nics"), m_host.nics()}});
}

QJsonObject Engine::netApply(const QJsonObject &p)
{
    const QString iface = p.value(QStringLiteral("iface")).toString();
    const QString ip = p.value(QStringLiteral("ip")).toString();
    const QString mask = p.value(QStringLiteral("mask")).toString(QStringLiteral("255.255.255.0"));
    const QString gw = p.value(QStringLiteral("gateway")).toString();
    const QString dns = p.value(QStringLiteral("dns")).toString();
    if (iface.isEmpty() || ip.isEmpty())
        return fail(QStringLiteral("iface and ip required"));

    const QString net = m_gen.networkd(iface, ip, mask, gw, dns);
    QString err;
    const QString path = m_paths.etc + QStringLiteral("/10-lxboot.network");
    if (!writeFile(path, net, &err))
        return fail(err);

    if (p.value(QStringLiteral("lacp")).toBool()) {
        const auto slaves = p.value(QStringLiteral("slaves")).toArray();
        QStringList sl;
        for (const auto &v : slaves)
            sl << v.toString();
        writeFile(m_paths.etc + QStringLiteral("/20-bond.netdev"), m_gen.netdevBond(QStringLiteral("bond0"), sl), &err);
        QString dropins;
        for (const auto &s : sl) {
            dropins += QStringLiteral("[Match]\nName=%1\n\n[Network]\nBond=bond0\n").arg(s);
            writeFile(m_paths.etc + QStringLiteral("/30-%1.network").arg(s), dropins, &err);
            dropins.clear();
        }
        writeFile(m_paths.etc + QStringLiteral("/35-bond0.network"),
                  m_gen.networkd(QStringLiteral("bond0"), ip, mask, gw, dns), &err);
    }

    m_store.setSetting(QStringLiteral("net.iface"), iface);
    m_store.setSetting(QStringLiteral("net.ip"), ip);
    m_store.setSetting(QStringLiteral("net.mask"), mask);
    m_store.setSetting(QStringLiteral("net.gw"), gw);
    m_store.setSetting(QStringLiteral("net.dns"), dns);
    m_store.exec(QStringLiteral("UPDATE dhcp SET interface=?, server_ip=?, gateway=? WHERE id=1"),
                 QJsonArray{iface, ip, gw.isEmpty() ? ip : gw});

    if (m_host.applyEnabled()) {
        auto r = m_host.run(QStringLiteral("ip"),
                            QStringList{QStringLiteral("addr"), QStringLiteral("flush"), QStringLiteral("dev"), iface});
        Q_UNUSED(r);
        int prefix = 24;
        if (mask == QLatin1String("255.255.0.0"))
            prefix = 16;
        m_host.run(QStringLiteral("ip"),
                   QStringList{QStringLiteral("addr"), QStringLiteral("add"),
                               QStringLiteral("%1/%2").arg(ip).arg(prefix), QStringLiteral("dev"), iface});
        m_host.run(QStringLiteral("ip"), QStringList{QStringLiteral("link"), QStringLiteral("set"), iface, QStringLiteral("up")});
        if (!gw.isEmpty())
            m_host.run(QStringLiteral("ip"),
                       QStringList{QStringLiteral("route"), QStringLiteral("replace"), QStringLiteral("default"),
                                   QStringLiteral("via"), gw, QStringLiteral("dev"), iface});
    }
    m_store.log(QStringLiteral("info"), QStringLiteral("net.apply"), QStringLiteral("%1 %2").arg(iface, ip));
    return applyRuntime();
}

QJsonObject Engine::dhcpGet() const
{
    const auto rows = m_store.query(QStringLiteral("SELECT * FROM dhcp WHERE id=1"));
    QJsonObject o = ok();
    if (!rows.isEmpty())
        o.insert(QStringLiteral("dhcp"), rows.first());
    o.insert(QStringLiteral("leases"),
             m_host.dnsmasqLeases(m_paths.prefix + QStringLiteral("/dnsmasq.leases")));
    return o;
}

QJsonObject Engine::dhcpApply(const QJsonObject &p)
{
    m_store.exec(QStringLiteral("UPDATE dhcp SET enabled=?, interface=?, server_ip=?, pool_start=?, pool_end=?, "
                                "netmask=?, gateway=?, dns=?, lease_time=? WHERE id=1"),
                 QJsonArray{p.value(QStringLiteral("enabled")).toBool(true) ? 1 : 0,
                            p.value(QStringLiteral("interface")).toString(),
                            p.value(QStringLiteral("server_ip")).toString(),
                            p.value(QStringLiteral("pool_start")).toString(),
                            p.value(QStringLiteral("pool_end")).toString(),
                            p.value(QStringLiteral("netmask")).toString(QStringLiteral("255.255.255.0")),
                            p.value(QStringLiteral("gateway")).toString(),
                            p.value(QStringLiteral("dns")).toString(QStringLiteral("8.8.8.8")),
                            p.value(QStringLiteral("lease_time")).toInt(3600)});
    m_store.log(QStringLiteral("info"), QStringLiteral("dhcp.apply"),
                p.value(QStringLiteral("pool_start")).toString() + QStringLiteral("-")
                    + p.value(QStringLiteral("pool_end")).toString());
    return applyRuntime();
}

QJsonObject Engine::diskList() const
{
    const auto live = m_host.blockDevices();
    const auto saved = m_store.query(QStringLiteral("SELECT * FROM disks"));
    QHash<QString, QJsonObject> roles;
    for (const auto &v : saved) {
        const auto o = v.toObject();
        roles.insert(o.value(QStringLiteral("name")).toString(), o);
    }
    QJsonArray disks;
    for (const auto &v : live) {
        auto o = v.toObject();
        const QString name = o.value(QStringLiteral("name")).toString();
        const auto role = roles.value(name);
        o.insert(QStringLiteral("role"), role.value(QStringLiteral("role")).toString(QStringLiteral("offline")));
        o.insert(QStringLiteral("ram_cache_mb"), role.value(QStringLiteral("ram_cache_mb")).toInteger());
        disks.append(o);
    }
    QJsonObject o = ok();
    o.insert(QStringLiteral("disks"), disks);
    o.insert(QStringLiteral("metrics"), m_metrics.snapshot().value(QStringLiteral("disks")));
    return o;
}

QJsonObject Engine::diskSetRole(const QJsonObject &p)
{
    const QString name = p.value(QStringLiteral("name")).toString();
    const QString role = p.value(QStringLiteral("role")).toString();
    const int ram = p.value(QStringLiteral("ram_cache_mb")).toInt();
    const QString path = p.value(QStringLiteral("path")).toString();
    if (name.isEmpty())
        return fail(QStringLiteral("disk name required"));
    static const QStringList allowed{QStringLiteral("offline"), QStringLiteral("image"), QStringLiteral("writeback"),
                                     QStringLiteral("ssd"), QStringLiteral("game"), QStringLiteral("system")};
    if (!allowed.contains(role))
        return fail(QStringLiteral("unknown role"));

    const auto mem = m_metrics.snapshot();
    const qint64 totalKb = mem.value(QStringLiteral("ram_total_kb")).toInteger();
    const qint64 reserveKb = 2ll * 1024 * 1024;
    const qint64 usedCache = m_store.query(QStringLiteral("SELECT COALESCE(SUM(ram_cache_mb),0) AS n FROM disks WHERE name!=?"))
                                 .first()
                                 .toObject()
                                 .value(QStringLiteral("n"))
                                 .toInteger();
    if ((usedCache + ram) * 1024ll > qMax<qint64>(0, totalKb - reserveKb) && ram > 0)
        return fail(QStringLiteral("RAM cache слишком большой: оставьте минимум 2 ГБ системе"));

    m_store.exec(QStringLiteral("INSERT INTO disks(name, role, ram_cache_mb, path) VALUES(?,?,?,?) "
                                "ON CONFLICT(name) DO UPDATE SET role=excluded.role, ram_cache_mb=excluded.ram_cache_mb, path=excluded.path"),
                 QJsonArray{name, role, ram, path});
    if (role == QLatin1String("image"))
        QDir().mkpath(m_paths.images);
    if (role == QLatin1String("writeback"))
        QDir().mkpath(m_paths.writeback);
    m_store.log(QStringLiteral("info"), QStringLiteral("disk.role"), QStringLiteral("%1=%2 cache=%3").arg(name, role).arg(ram));
    return diskList();
}

QJsonObject Engine::imageList() const
{
    QJsonObject o = ok();
    o.insert(QStringLiteral("images"), m_store.query(QStringLiteral("SELECT * FROM images ORDER BY id")));
    o.insert(QStringLiteral("restore_points"),
             m_store.query(QStringLiteral("SELECT * FROM restore_points ORDER BY id DESC")));
    return o;
}

QJsonObject Engine::imageImport(const QJsonObject &p)
{
    const QString src = p.value(QStringLiteral("path")).toString();
    QString name = p.value(QStringLiteral("name")).toString();
    if (src.isEmpty())
        return fail(QStringLiteral("path required"));
    QFileInfo fi(src);
    if (!fi.exists())
        return fail(QStringLiteral("file not found: %1").arg(src));
    if (name.isEmpty())
        name = fi.completeBaseName();

    const QString dest = m_paths.images + QLatin1Char('/') + name + QStringLiteral(".qcow2");
    QDir().mkpath(m_paths.images);
    CommandResult r;
    if (m_host.hasQemuImg()) {
        r = m_host.run(QStringLiteral("qemu-img"),
                       QStringList{QStringLiteral("convert"), QStringLiteral("-p"), QStringLiteral("-O"),
                                   QStringLiteral("qcow2"), src, dest},
                       3600000);
        if (!r.ok())
            return fail(r.err.isEmpty() ? r.out : r.err);
    } else {
        if (!QFile::copy(src, dest) && src != dest)
            return fail(QStringLiteral("copy failed (install qemu-img for vhd/img convert)"));
    }
    qint64 size = QFileInfo(dest).size();
    if (m_host.hasQemuImg()) {
        const auto info = m_host.run(QStringLiteral("qemu-img"), QStringList{QStringLiteral("info"), QStringLiteral("--output=json"), dest});
        const auto doc = QJsonDocument::fromJson(info.out.toUtf8());
        size = doc.object().value(QStringLiteral("virtual-size")).toInteger(size);
    }
    m_store.exec(QStringLiteral("INSERT INTO images(name, path, size_bytes, kind, is_default, created_ts) VALUES(?,?,?,?,?,?) "
                                "ON CONFLICT(name) DO UPDATE SET path=excluded.path, size_bytes=excluded.size_bytes"),
                 QJsonArray{name, dest, size, QStringLiteral("qcow2"), 0,
                            QDateTime::currentDateTimeUtc().toString(Qt::ISODate)});
    const auto idRow = m_store.query(QStringLiteral("SELECT id FROM images WHERE name=?"), QJsonArray{name});
    const qint64 id = idRow.first().toObject().value(QStringLiteral("id")).toInteger();
    const auto defs = m_store.query(QStringLiteral("SELECT COUNT(*) AS n FROM images WHERE is_default=1"));
    if (defs.first().toObject().value(QStringLiteral("n")).toInteger() == 0)
        m_store.exec(QStringLiteral("UPDATE images SET is_default=1 WHERE id=?"), QJsonArray{id});
    m_store.log(QStringLiteral("info"), QStringLiteral("image.import"), name);
    return imageList();
}

QJsonObject Engine::imageDefault(const QJsonObject &p)
{
    const qint64 id = p.value(QStringLiteral("id")).toInteger();
    m_store.exec(QStringLiteral("UPDATE images SET is_default=0"));
    m_store.exec(QStringLiteral("UPDATE images SET is_default=1 WHERE id=?"), QJsonArray{id});
    m_store.log(QStringLiteral("info"), QStringLiteral("image.default"), QString::number(id));
    return imageList();
}

QJsonObject Engine::imageRestore(const QJsonObject &p)
{
    const QString action = p.value(QStringLiteral("action")).toString();
    const qint64 imageId = p.value(QStringLiteral("image_id")).toInteger();
    const auto pts = m_store.query(QStringLiteral("SELECT * FROM restore_points WHERE image_id=? ORDER BY id DESC"),
                                   QJsonArray{imageId});
    if (action == QLatin1String("create")) {
        const QString img = imagePath(imageId);
        const QString note = p.value(QStringLiteral("note")).toString(QStringLiteral("manual"));
        const QString rp = m_paths.images + QStringLiteral("/rp-%1-%2.qcow2")
                               .arg(imageId)
                               .arg(QDateTime::currentSecsSinceEpoch());
        if (m_host.hasQemuImg())
            m_host.run(QStringLiteral("qemu-img"), QStringList{QStringLiteral("convert"), QStringLiteral("-O"), QStringLiteral("qcow2"), img, rp}, 3600000);
        else
            QFile::copy(img, rp);
        m_store.exec(QStringLiteral("INSERT INTO restore_points(image_id, note, path, created_ts) VALUES(?,?,?,?)"),
                     QJsonArray{imageId, note, rp, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)});
        m_store.log(QStringLiteral("info"), QStringLiteral("restore.create"), note);
        return imageList();
    }
    if (pts.isEmpty())
        return fail(QStringLiteral("no restore points"));
    if (action == QLatin1String("restore_last")) {
        const auto last = pts.first().toObject();
        const QString img = imagePath(imageId);
        QFile::remove(img);
        QFile::copy(last.value(QStringLiteral("path")).toString(), img);
        m_store.log(QStringLiteral("warn"), QStringLiteral("restore.to_last"), last.value(QStringLiteral("note")).toString());
        return imageList();
    }
    if (action == QLatin1String("merge_last")) {
        if (pts.size() < 1)
            return fail(QStringLiteral("nothing to merge"));
        const auto last = pts.first().toObject();
        m_store.exec(QStringLiteral("DELETE FROM restore_points WHERE id=?"), QJsonArray{last.value(QStringLiteral("id")).toInteger()});
        QFile::remove(last.value(QStringLiteral("path")).toString());
        m_store.log(QStringLiteral("info"), QStringLiteral("restore.merge_last"), {});
        return imageList();
    }
    if (action == QLatin1String("merge_all")) {
        for (const auto &v : pts) {
            QFile::remove(v.toObject().value(QStringLiteral("path")).toString());
        }
        m_store.exec(QStringLiteral("DELETE FROM restore_points WHERE image_id=?"), QJsonArray{imageId});
        m_store.log(QStringLiteral("warn"), QStringLiteral("restore.merge_all"), QString::number(imageId));
        return imageList();
    }
    return fail(QStringLiteral("unknown restore action"));
}

QJsonObject Engine::clientList() const
{
    const auto leases = m_host.dnsmasqLeases(m_paths.prefix + QStringLiteral("/dnsmasq.leases"));
    QSet<QString> live;
    for (const auto &v : leases) {
        const auto l = v.toObject();
        const QString mac = normalizeMac(l.value(QStringLiteral("mac")).toString());
        live.insert(mac);
        const auto exist = m_store.query(QStringLiteral("SELECT mac FROM clients WHERE mac=?"), QJsonArray{mac});
        if (exist.isEmpty()) {
            const_cast<Store &>(m_store).exec(
                QStringLiteral("INSERT INTO clients(mac, hostname, ip, last_seen, online) VALUES(?,?,?,?,1)"),
                QJsonArray{mac, l.value(QStringLiteral("hostname")).toString(), l.value(QStringLiteral("ip")).toString(),
                           QDateTime::currentDateTimeUtc().toString(Qt::ISODate)});
            const_cast<Store &>(m_store).log(QStringLiteral("info"), QStringLiteral("client.discovered"), mac);
        } else {
            const_cast<Store &>(m_store).exec(
                QStringLiteral("UPDATE clients SET ip=?, hostname=COALESCE(NULLIF(hostname,''), ?), last_seen=?, online=1 WHERE mac=?"),
                QJsonArray{l.value(QStringLiteral("ip")).toString(), l.value(QStringLiteral("hostname")).toString(),
                           QDateTime::currentDateTimeUtc().toString(Qt::ISODate), mac});
        }
    }
    const auto all = m_store.query(QStringLiteral("SELECT * FROM clients ORDER BY hostname, mac"));
    QJsonArray out;
    for (const auto &v : all) {
        auto o = v.toObject();
        const QString mac = o.value(QStringLiteral("mac")).toString();
        const bool on = live.contains(mac);
        o.insert(QStringLiteral("online"), on);
        const QString wb = clientWbPath(mac);
        o.insert(QStringLiteral("wb_bytes"), QFileInfo(wb).exists() ? QFileInfo(wb).size() : 0);
        out.append(o);
        if (!on)
            const_cast<Store &>(m_store).exec(QStringLiteral("UPDATE clients SET online=0 WHERE mac=?"), QJsonArray{mac});
    }
    QJsonObject r = ok();
    r.insert(QStringLiteral("clients"), out);
    r.insert(QStringLiteral("groups"), m_store.query(QStringLiteral("SELECT * FROM groups")));
    r.insert(QStringLiteral("images"), m_store.query(QStringLiteral("SELECT id, name, is_default FROM images")));
    r.insert(QStringLiteral("unknown"),
             m_store.query(QStringLiteral("SELECT * FROM clients WHERE hostname IS NULL OR hostname='' OR group_id IS NULL")));
    return r;
}

QJsonObject Engine::clientUpsert(const QJsonObject &p)
{
    const QString mac = normalizeMac(p.value(QStringLiteral("mac")).toString());
    if (mac.isEmpty())
        return fail(QStringLiteral("bad mac"));
    m_store.exec(QStringLiteral("INSERT INTO clients(mac, hostname, ip, group_id, image_id, wb_policy, note) "
                                "VALUES(?,?,?,?,?,?,?) "
                                "ON CONFLICT(mac) DO UPDATE SET hostname=excluded.hostname, ip=excluded.ip, "
                                "group_id=excluded.group_id, image_id=excluded.image_id, wb_policy=excluded.wb_policy, note=excluded.note"),
                 QJsonArray{mac, p.value(QStringLiteral("hostname")).toString(), p.value(QStringLiteral("ip")).toString(),
                            p.value(QStringLiteral("group_id")).toInteger(), p.value(QStringLiteral("image_id")).toInteger(),
                            p.value(QStringLiteral("wb_policy")).toString(QStringLiteral("reset")),
                            p.value(QStringLiteral("note")).toString()});
    m_store.log(QStringLiteral("info"), QStringLiteral("client.upsert"), mac);
    return applyRuntime();
}

QJsonObject Engine::clientSuper(const QJsonObject &p)
{
    const QString mac = normalizeMac(p.value(QStringLiteral("mac")).toString());
    const bool on = p.value(QStringLiteral("on")).toBool();
    m_store.exec(QStringLiteral("UPDATE clients SET super_client=? WHERE mac=?"), QJsonArray{on ? 1 : 0, mac});
    m_store.log(QStringLiteral("warn"), QStringLiteral("client.super"),
                QStringLiteral("%1 %2").arg(mac, on ? QStringLiteral("on") : QStringLiteral("off")));
    return clientList();
}

QJsonObject Engine::clientResetWb(const QJsonObject &p)
{
    const QString mac = normalizeMac(p.value(QStringLiteral("mac")).toString());
    const QString wb = clientWbPath(mac);
    QFile::remove(wb);
    m_store.exec(QStringLiteral("UPDATE clients SET wb_bytes=0 WHERE mac=?"), QJsonArray{mac});
    m_store.log(QStringLiteral("info"), QStringLiteral("client.reset_wb"), mac);
    return ok();
}

QJsonObject Engine::commitSuper(const QJsonObject &p)
{
    const QString mac = normalizeMac(p.value(QStringLiteral("mac")).toString());
    const bool save = p.value(QStringLiteral("save")).toBool(true);
    const auto rows = m_store.query(QStringLiteral("SELECT * FROM clients WHERE mac=?"), QJsonArray{mac});
    if (rows.isEmpty())
        return fail(QStringLiteral("unknown client"));
    const auto c = rows.first().toObject();
    if (!c.value(QStringLiteral("super_client")).toInteger())
        return fail(QStringLiteral("super client is not enabled"));
    qint64 imageId = c.value(QStringLiteral("image_id")).toInteger();
    if (!imageId) {
        const auto d = m_store.query(QStringLiteral("SELECT id FROM images WHERE is_default=1"));
        if (d.isEmpty())
            return fail(QStringLiteral("no default image"));
        imageId = d.first().toObject().value(QStringLiteral("id")).toInteger();
    }
    const QString gold = imagePath(imageId);
    const QString wb = clientWbPath(mac);
    if (save) {
        imageRestore(QJsonObject{{QStringLiteral("action"), QStringLiteral("create")},
                                 {QStringLiteral("image_id"), imageId},
                                 {QStringLiteral("note"), QStringLiteral("super %1").arg(mac)}});
        if (m_host.hasQemuImg() && QFile::exists(wb)) {
            const QString merged = gold + QStringLiteral(".merged");
            auto r = m_host.run(QStringLiteral("qemu-img"),
                                QStringList{QStringLiteral("convert"), QStringLiteral("-O"), QStringLiteral("qcow2"), wb, merged},
                                3600000);
            if (r.ok()) {
                QFile::remove(gold);
                QFile::rename(merged, gold);
            }
        }
        m_store.log(QStringLiteral("info"), QStringLiteral("image.commit_super"), mac);
    } else {
        QFile::remove(wb);
        m_store.log(QStringLiteral("info"), QStringLiteral("image.discard_super"), mac);
    }
    m_store.exec(QStringLiteral("UPDATE clients SET super_client=0 WHERE mac=?"), QJsonArray{mac});
    return applyRuntime();
}

QJsonObject Engine::groupList() const
{
    return ok({{QStringLiteral("groups"), m_store.query(QStringLiteral("SELECT * FROM groups"))}});
}

QJsonObject Engine::groupUpsert(const QJsonObject &p)
{
    const QString name = p.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        return fail(QStringLiteral("name required"));
    m_store.exec(QStringLiteral("INSERT INTO groups(name, image_id, wb_policy) VALUES(?,?,?) "
                                "ON CONFLICT(name) DO UPDATE SET image_id=excluded.image_id, wb_policy=excluded.wb_policy"),
                 QJsonArray{name, p.value(QStringLiteral("image_id")).toInteger(),
                            p.value(QStringLiteral("wb_policy")).toString(QStringLiteral("reset"))});
    return groupList();
}

QJsonObject Engine::logList(const QJsonObject &p) const
{
    const int limit = p.value(QStringLiteral("limit")).toInt(200);
    return ok({{QStringLiteral("events"),
                m_store.query(QStringLiteral("SELECT * FROM event_log ORDER BY id DESC LIMIT ?"), QJsonArray{limit})}});
}

QJsonObject Engine::ensureClientStorage(const QJsonObject &client, bool super)
{
    qint64 imageId = client.value(QStringLiteral("image_id")).toInteger();
    if (!imageId) {
        const auto d = m_store.query(QStringLiteral("SELECT id FROM images WHERE is_default=1"));
        if (d.isEmpty())
            return fail(QStringLiteral("no image"));
        imageId = d.first().toObject().value(QStringLiteral("id")).toInteger();
    }
    const QString gold = imagePath(imageId);
    if (gold.isEmpty() || !QFile::exists(gold))
        return fail(QStringLiteral("golden image missing"));
    const QString mac = client.value(QStringLiteral("mac")).toString();
    const QString policy = client.value(QStringLiteral("wb_policy")).toString(QStringLiteral("reset"));
    const QString wb = clientWbPath(mac);
    if (policy == QLatin1String("reset") && !super && QFile::exists(wb))
        QFile::remove(wb);
    if (!QFile::exists(wb)) {
        QDir().mkpath(m_paths.writeback);
        if (m_host.hasQemuImg()) {
            auto r = m_host.run(QStringLiteral("qemu-img"),
                                QStringList{QStringLiteral("create"), QStringLiteral("-f"), QStringLiteral("qcow2"),
                                            QStringLiteral("-F"), QStringLiteral("qcow2"), QStringLiteral("-b"), gold, wb});
            if (!r.ok())
                return fail(r.err);
        } else {
            QFile::copy(gold, wb);
        }
    }
    return ok({{QStringLiteral("lun"), wb}, {QStringLiteral("iqn"), clientIqn(mac)}});
}

QJsonObject Engine::leaseEvent(const QJsonObject &p)
{
    const QString action = p.value(QStringLiteral("action")).toString();
    const QString mac = normalizeMac(p.value(QStringLiteral("mac")).toString());
    const QString ip = p.value(QStringLiteral("ip")).toString();
    const QString host = p.value(QStringLiteral("hostname")).toString();
    if (mac.isEmpty())
        return fail(QStringLiteral("mac"));
    if (action == QLatin1String("del")) {
        const auto rows = m_store.query(QStringLiteral("SELECT * FROM clients WHERE mac=?"), QJsonArray{mac});
        if (!rows.isEmpty()) {
            const auto c = rows.first().toObject();
            if (c.value(QStringLiteral("wb_policy")).toString() == QLatin1String("reset")
                && !c.value(QStringLiteral("super_client")).toInteger())
                QFile::remove(clientWbPath(mac));
        }
        m_store.exec(QStringLiteral("UPDATE clients SET online=0 WHERE mac=?"), QJsonArray{mac});
        m_store.log(QStringLiteral("info"), QStringLiteral("lease.del"), mac);
        return ok();
    }
    clientUpsert(QJsonObject{{QStringLiteral("mac"), mac},
                             {QStringLiteral("ip"), ip},
                             {QStringLiteral("hostname"), host}});
    const auto rows = m_store.query(QStringLiteral("SELECT * FROM clients WHERE mac=?"), QJsonArray{mac});
    if (rows.isEmpty())
        return fail(QStringLiteral("client missing"));
    const auto c = rows.first().toObject();
    const auto st = ensureClientStorage(c, c.value(QStringLiteral("super_client")).toInteger() != 0);
    m_store.log(QStringLiteral("info"), QStringLiteral("lease.add"), QStringLiteral("%1 %2").arg(mac, ip));
    return st;
}

QJsonObject Engine::applyRuntime()
{
    const auto dhcpRows = m_store.query(QStringLiteral("SELECT * FROM dhcp WHERE id=1"));
    if (dhcpRows.isEmpty())
        return fail(QStringLiteral("dhcp row missing"));
    const auto dhcp = dhcpRows.first().toObject();
    const QString serverIp = dhcp.value(QStringLiteral("server_ip")).toString();
    const QString iface = dhcp.value(QStringLiteral("interface")).toString();
    const auto clients = m_store.query(QStringLiteral("SELECT mac, ip, hostname FROM clients WHERE ip IS NOT NULL AND ip!=''"));
    QString err;
    const QString conf = m_gen.dnsmasq(dhcp, clients, iface, serverIp);
    if (!writeFile(m_paths.etc + QStringLiteral("/dnsmasq.conf"), conf, &err))
        return fail(err);
    if (!writeFile(m_paths.tftp + QStringLiteral("/boot.ipxe"), m_gen.ipxeScript(serverIp), &err))
        return fail(err);
    const QString hook = m_gen.dhcpHook().arg(m_paths.prefix, m_paths.socket);
    if (!writeFile(m_paths.hook, hook, &err))
        return fail(err);
    QFile::setPermissions(m_paths.hook, QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther
                                            | QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup
                                            | QFileDevice::ReadOther);

    QString iscsi;
    iscsi += QStringLiteral("#!/bin/sh\nset -e\n");
    iscsi += QStringLiteral("# LXBoot iSCSI map — targetcli if present\n");
    const auto all = m_store.query(QStringLiteral("SELECT * FROM clients"));
    for (const auto &v : all) {
        const auto c = v.toObject();
        const QString mac = c.value(QStringLiteral("mac")).toString();
        const auto st = ensureClientStorage(c, c.value(QStringLiteral("super_client")).toInteger() != 0);
        if (!st.value(QStringLiteral("ok")).toBool())
            continue;
        const QString lun = st.value(QStringLiteral("lun")).toString();
        const QString iqn = st.value(QStringLiteral("iqn")).toString();
        const QString name = QString(mac).replace(QLatin1Char(':'), QLatin1Char('-'));
        iscsi += QStringLiteral("if command -v targetcli >/dev/null 2>&1; then\n");
        iscsi += QStringLiteral("  targetcli /backstores/fileio create %1 %2 2>/dev/null || true\n").arg(name, lun);
        iscsi += QStringLiteral("  targetcli /iscsi create %1 2>/dev/null || true\n").arg(iqn);
        iscsi += QStringLiteral("  targetcli /iscsi/%1/tpg1/luns create /backstores/fileio/%2 2>/dev/null || true\n")
                     .arg(iqn, name);
        iscsi += QStringLiteral("  targetcli /iscsi/%1/tpg1 set attribute authentication=0 generate_node_acls=1 cache_dynamic_acls=1\n")
                     .arg(iqn);
        iscsi += QStringLiteral("fi\n");
    }
    writeFile(m_paths.prefix + QStringLiteral("/apply-iscsi.sh"), iscsi, &err);
    QFile::setPermissions(m_paths.prefix + QStringLiteral("/apply-iscsi.sh"),
                          QFileDevice::ExeOwner | QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    if (m_host.applyEnabled()) {
        if (dhcp.value(QStringLiteral("enabled")).toInteger()) {
            m_host.run(QStringLiteral("systemctl"),
                       QStringList{QStringLiteral("restart"), QStringLiteral("lxboot-dnsmasq.service")});
            m_host.run(QStringLiteral("dnsmasq"),
                       QStringList{QStringLiteral("--test"), QStringLiteral("-C"),
                                   m_paths.etc + QStringLiteral("/dnsmasq.conf")});
        }
        m_host.shell(QStringLiteral("sh ") + m_paths.prefix + QStringLiteral("/apply-iscsi.sh"));
    }
    m_store.log(QStringLiteral("info"), QStringLiteral("runtime.apply"), serverIp);
    QJsonObject o = ok();
    o.insert(QStringLiteral("dnsmasq"), m_paths.etc + QStringLiteral("/dnsmasq.conf"));
    o.insert(QStringLiteral("ipxe"), m_paths.tftp + QStringLiteral("/boot.ipxe"));
    o.insert(QStringLiteral("applied"), m_host.applyEnabled());
    return o;
}

QJsonObject Engine::service(const QString &method, const QJsonObject &p)
{
    if (method == QLatin1String("password")) {
        const QString pass = p.value(QStringLiteral("password")).toString();
        if (pass.size() < 4)
            return fail(QStringLiteral("password too short"));
        const QByteArray salt = QByteArrayLiteral("lxboot-v1");
        const QByteArray hash = QCryptographicHash::hash(salt + pass.toUtf8(), QCryptographicHash::Sha256).toHex();
        m_store.setSetting(QStringLiteral("admin_hash"), QString::fromLatin1(hash));
        m_store.setSetting(QStringLiteral("admin_salt"), QString::fromLatin1(salt));
        m_store.log(QStringLiteral("info"), QStringLiteral("service.password"), {});
        return ok();
    }
    if (method == QLatin1String("ssh")) {
        const bool on = p.value(QStringLiteral("on")).toBool();
        m_store.setSetting(QStringLiteral("ssh"), on ? QStringLiteral("1") : QStringLiteral("0"));
        if (m_host.applyEnabled()) {
            m_host.run(QStringLiteral("systemctl"),
                       QStringList{on ? QStringLiteral("enable") : QStringLiteral("disable"), QStringLiteral("--now"),
                                   QStringLiteral("sshd")});
        }
        return ok();
    }
    if (method == QLatin1String("restart")) {
        applyRuntime();
        if (m_host.applyEnabled()) {
            m_host.run(QStringLiteral("systemctl"), QStringList{QStringLiteral("restart"), QStringLiteral("lxboot-dnsmasq.service")});
        }
        return ok();
    }
    if (method == QLatin1String("reboot")) {
        m_store.log(QStringLiteral("warn"), QStringLiteral("service.reboot"), {});
        if (m_host.applyEnabled())
            m_host.run(QStringLiteral("systemctl"), QStringList{QStringLiteral("reboot")});
        return ok({{QStringLiteral("queued"), true}});
    }
    if (method == QLatin1String("shutdown")) {
        m_store.log(QStringLiteral("warn"), QStringLiteral("service.shutdown"), {});
        if (m_host.applyEnabled())
            m_host.run(QStringLiteral("systemctl"), QStringList{QStringLiteral("poweroff")});
        return ok({{QStringLiteral("queued"), true}});
    }
    if (method == QLatin1String("shell")) {
        const QString pass = p.value(QStringLiteral("password")).toString();
        const QByteArray salt = m_store.setting(QStringLiteral("admin_salt")).toUtf8();
        const QByteArray hash = QCryptographicHash::hash(salt + pass.toUtf8(), QCryptographicHash::Sha256).toHex();
        if (QString::fromLatin1(hash) != m_store.setting(QStringLiteral("admin_hash")))
            return fail(QStringLiteral("bad password"));
        return ok({{QStringLiteral("shell"), true}, {QStringLiteral("hint"), QStringLiteral("Ctrl+Alt+F2 or lxboot-ui --shell")}});
    }
    return fail(QStringLiteral("unknown service action"));
}
