#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

struct CommandResult {
    int code = -1;
    QString out;
    QString err;
    bool ok() const { return code == 0; }
};

class Host {
public:
    bool privileged() const;
    bool applyEnabled() const;

    CommandResult run(const QString &program, const QStringList &args,
                      int timeoutMs = 30000) const;
    CommandResult shell(const QString &script, int timeoutMs = 30000) const;

    QJsonArray nics() const;
    QJsonArray blockDevices() const;
    QJsonObject zfsPools() const;
    QJsonArray iscsiSessions() const;
    QJsonArray dnsmasqLeases(const QString &leaseFile) const;
    bool hasZfs() const;
    bool hasLvm() const;
    bool hasQemuImg() const;
    bool hasTargetcli() const;
};
