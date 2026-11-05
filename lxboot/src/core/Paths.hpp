#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>

struct LxPaths {
    QString prefix;      // writable data root
    QString etc;         // generated configs
    QString db;
    QString tftp;
    QString images;
    QString writeback;
    QString logDir;
    QString socket;
    QString hook;

    static LxPaths resolve()
    {
        LxPaths p;
        const QByteArray env = qgetenv("LXBOOT_PREFIX");
        p.prefix = env.isEmpty() ? QStringLiteral("/var/lib/lxboot") : QString::fromUtf8(env);
        p.etc = qEnvironmentVariable("LXBOOT_ETC", QStringLiteral("/etc/lxboot"));
        p.db = p.prefix + QStringLiteral("/lxboot.db");
        p.tftp = p.prefix + QStringLiteral("/tftp");
        p.images = p.prefix + QStringLiteral("/images");
        p.writeback = p.prefix + QStringLiteral("/writeback");
        p.logDir = QStringLiteral("/var/log/lxboot");
        if (!QDir(p.logDir).exists())
            p.logDir = p.prefix + QStringLiteral("/log");
        p.socket = qEnvironmentVariable("LXBOOT_SOCK", QStringLiteral("/run/lxboot/lxbootd.sock"));
        p.hook = p.prefix + QStringLiteral("/dhcp-hook.sh");
        return p;
    }

    void ensureDirs() const
    {
        for (const auto &d : {prefix, etc, tftp, images, writeback, logDir,
                              prefix + QStringLiteral("/clients"),
                              QFileInfo(socket).absolutePath()}) {
            QDir().mkpath(d);
        }
    }
};
