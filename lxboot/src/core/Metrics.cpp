#include "core/Metrics.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>

static QByteArray slurp(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

QJsonObject Metrics::snapshot() const
{
    QJsonObject o;
    qint64 total = 0, avail = 0, memfree = 0;
    for (const auto &raw : slurp(QStringLiteral("/proc/meminfo")).split('\n')) {
        const QString line = QString::fromLatin1(raw);
        const auto parts = line.split(QLatin1Char(':'), Qt::SkipEmptyParts);
        if (parts.size() < 2)
            continue;
        const QString key = parts[0].trimmed();
        qint64 val = 0;
        for (const QChar c : parts[1]) {
            if (c.isDigit())
                val = val * 10 + c.digitValue();
            else if (val > 0)
                break;
        }
        if (key == QLatin1String("MemTotal"))
            total = val;
        else if (key == QLatin1String("MemAvailable"))
            avail = val;
        else if (key == QLatin1String("MemFree"))
            memfree = val;
    }
    if (!avail)
        avail = memfree;
    o.insert(QStringLiteral("ram_total_kb"), total);
    o.insert(QStringLiteral("ram_avail_kb"), avail);
    o.insert(QStringLiteral("ram_used_pct"), total ? int(100.0 * double(total - avail) / double(total)) : 0);

    const auto stat = slurp(QStringLiteral("/proc/stat")).split('\n').value(0);
    {
        const auto p = QString::fromLatin1(stat).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        qint64 idle = 0, totalc = 0;
        for (int i = 1; i < p.size(); ++i) {
            const qint64 v = p[i].toLongLong();
            totalc += v;
            if (i == 4)
                idle = v;
        }
        o.insert(QStringLiteral("cpu_idle"), idle);
        o.insert(QStringLiteral("cpu_total"), totalc);
    }

    QJsonArray nics;
    int netLine = 0;
    for (const auto &raw : slurp(QStringLiteral("/proc/net/dev")).split('\n')) {
        ++netLine;
        if (netLine <= 2)
            continue;
        const QString line = QString::fromLatin1(raw);
        const int c = line.indexOf(QLatin1Char(':'));
        if (c < 0)
            continue;
        const QString name = line.left(c).trimmed();
        if (name.isEmpty() || name == QLatin1String("lo"))
            continue;
        const auto f = line.mid(c + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (f.size() < 10)
            continue;
        nics.append(QJsonObject{{QStringLiteral("name"), name},
                                {QStringLiteral("rx_bytes"), f[0].toLongLong()},
                                {QStringLiteral("rx_packets"), f[1].toLongLong()},
                                {QStringLiteral("tx_bytes"), f[8].toLongLong()},
                                {QStringLiteral("tx_packets"), f[9].toLongLong()}});
    }
    o.insert(QStringLiteral("nics"), nics);

    QJsonArray disks;
    for (const auto &raw : slurp(QStringLiteral("/proc/diskstats")).split('\n')) {
        const auto f = QString::fromLatin1(raw).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (f.size() < 14)
            continue;
        const QString name = f[2];
        if (name.startsWith(QLatin1String("loop")) || name.startsWith(QLatin1String("ram"))
            || name.startsWith(QLatin1String("dm-")))
            continue;
        const bool partition = name.back().isDigit() && !name.startsWith(QLatin1String("nvme"));
        const bool nvmePart = name.startsWith(QLatin1String("nvme")) && name.contains(QLatin1Char('p'));
        if (partition || nvmePart)
            continue;
        disks.append(QJsonObject{{QStringLiteral("name"), name},
                                 {QStringLiteral("read_ios"), f[3].toLongLong()},
                                 {QStringLiteral("read_sectors"), f[5].toLongLong()},
                                 {QStringLiteral("write_ios"), f[7].toLongLong()},
                                 {QStringLiteral("write_sectors"), f[9].toLongLong()},
                                 {QStringLiteral("io_ticks"), f[12].toLongLong()}});
    }
    o.insert(QStringLiteral("disks"), disks);

    const auto load = QString::fromLatin1(slurp(QStringLiteral("/proc/loadavg"))).split(QLatin1Char(' '));
    o.insert(QStringLiteral("load1"), load.value(0).toDouble());
    o.insert(QStringLiteral("load5"), load.value(1).toDouble());
    o.insert(QStringLiteral("load15"), load.value(2).toDouble());
    o.insert(QStringLiteral("nproc"), int(QDir(QStringLiteral("/sys/devices/system/cpu"))
                                              .entryList(QStringList{QStringLiteral("cpu[0-9]*")}, QDir::Dirs).size()));
    return o;
}
