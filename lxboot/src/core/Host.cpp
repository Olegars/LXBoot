#include "core/Host.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
#include <unistd.h>

bool Host::privileged() const
{
    return ::geteuid() == 0;
}

bool Host::applyEnabled() const
{
    return privileged() && (qEnvironmentVariableIntValue("LXBOOT_APPLY") != 0
                            || qEnvironmentVariable("LXBOOT_APPLY") == QLatin1String("1"));
}

CommandResult Host::run(const QString &program, const QStringList &args, int timeoutMs) const
{
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(program, args);
    if (!p.waitForStarted(3000)) {
        CommandResult r;
        r.code = 127;
        r.err = QStringLiteral("cannot start %1").arg(program);
        return r;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        CommandResult r;
        r.code = 124;
        r.err = QStringLiteral("timeout");
        return r;
    }
    CommandResult r;
    r.code = p.exitCode();
    r.out = QString::fromUtf8(p.readAllStandardOutput());
    r.err = QString::fromUtf8(p.readAllStandardError());
    return r;
}

CommandResult Host::shell(const QString &script, int timeoutMs) const
{
    return run(QStringLiteral("/bin/sh"), QStringList{QStringLiteral("-c"), script}, timeoutMs);
}

QJsonArray Host::nics() const
{
    const auto r = run(QStringLiteral("ip"), QStringList{QStringLiteral("-j"), QStringLiteral("addr")});
    if (r.ok()) {
        const auto doc = QJsonDocument::fromJson(r.out.toUtf8());
        if (doc.isArray()) {
            QJsonArray out;
            for (const auto &v : doc.array()) {
                const auto o = v.toObject();
                const QString ifname = o.value(QStringLiteral("ifname")).toString();
                if (ifname == QLatin1String("lo"))
                    continue;
                QString ipv4, mask;
                for (const auto &av : o.value(QStringLiteral("addr_info")).toArray()) {
                    const auto a = av.toObject();
                    if (a.value(QStringLiteral("family")).toString() == QLatin1String("inet")) {
                        ipv4 = a.value(QStringLiteral("local")).toString();
                        mask = QString::number(a.value(QStringLiteral("prefixlen")).toInt());
                        break;
                    }
                }
                QJsonObject n;
                n.insert(QStringLiteral("name"), ifname);
                n.insert(QStringLiteral("operstate"), o.value(QStringLiteral("operstate")).toString());
                n.insert(QStringLiteral("mac"), o.value(QStringLiteral("address")).toString());
                n.insert(QStringLiteral("mtu"), o.value(QStringLiteral("mtu")).toInt());
                n.insert(QStringLiteral("ipv4"), ipv4);
                n.insert(QStringLiteral("prefix"), mask);
                out.append(n);
            }
            return out;
        }
    }
    return {};
}

QJsonArray Host::blockDevices() const
{
    const auto r = run(QStringLiteral("lsblk"),
                       QStringList{QStringLiteral("-J"), QStringLiteral("-b"), QStringLiteral("-o"),
                                   QStringLiteral("NAME,PATH,SIZE,TYPE,TRAN,ROTA,MOUNTPOINT,MODEL,PKNAME")});
    if (!r.ok())
        return {};
    const auto doc = QJsonDocument::fromJson(r.out.toUtf8());
    QJsonArray out;
    const auto block = doc.object().value(QStringLiteral("blockdevices")).toArray();
    for (const auto &v : block) {
        const auto o = v.toObject();
        if (o.value(QStringLiteral("type")).toString() != QLatin1String("disk"))
            continue;
        out.append(o);
    }
    return out;
}

QJsonObject Host::zfsPools() const
{
    QJsonObject o;
    o.insert(QStringLiteral("available"), hasZfs());
    if (!hasZfs())
        return o;
    const auto r = run(QStringLiteral("zpool"), QStringList{QStringLiteral("list"), QStringLiteral("-H"), QStringLiteral("-o"), QStringLiteral("name,size,free,health")});
    QJsonArray pools;
    for (const auto &line : r.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const auto p = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (p.size() < 4)
            continue;
        QJsonObject z;
        z.insert(QStringLiteral("name"), p[0]);
        z.insert(QStringLiteral("size"), p[1]);
        z.insert(QStringLiteral("free"), p[2]);
        z.insert(QStringLiteral("health"), p[3]);
        pools.append(z);
    }
    o.insert(QStringLiteral("pools"), pools);
    return o;
}

QJsonArray Host::iscsiSessions() const
{
    QJsonArray out;
    const auto r = run(QStringLiteral("targetcli"), QStringList{QStringLiteral("ls"), QStringLiteral("/iscsi")});
    if (r.ok() && !r.out.trimmed().isEmpty()) {
        QJsonObject n;
        n.insert(QStringLiteral("raw"), r.out);
        out.append(n);
    }
    QFile sess(QStringLiteral("/proc/net/tcp"));
    Q_UNUSED(sess);
    return out;
}

QJsonArray Host::dnsmasqLeases(const QString &leaseFile) const
{
    QJsonArray out;
    QFile f(leaseFile);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;
        const auto p = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (p.size() < 3)
            continue;
        QJsonObject l;
        l.insert(QStringLiteral("expiry"), p.value(0));
        l.insert(QStringLiteral("mac"), p.value(1).toLower());
        l.insert(QStringLiteral("ip"), p.value(2));
        l.insert(QStringLiteral("hostname"), p.value(3));
        out.append(l);
    }
    return out;
}

bool Host::hasZfs() const
{
    return QFile::exists(QStringLiteral("/sbin/zfs")) || QFile::exists(QStringLiteral("/usr/sbin/zfs"));
}

bool Host::hasLvm() const
{
    return QFile::exists(QStringLiteral("/sbin/lvm")) || QFile::exists(QStringLiteral("/usr/sbin/lvcreate"));
}

bool Host::hasQemuImg() const
{
    return QFile::exists(QStringLiteral("/usr/bin/qemu-img"));
}

bool Host::hasTargetcli() const
{
    return QFile::exists(QStringLiteral("/usr/bin/targetcli")) || QFile::exists(QStringLiteral("/sbin/targetcli"));
}
