#pragma once

#include "Paths.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

class ConfigGen {
public:
    explicit ConfigGen(const LxPaths &paths);
    QString dnsmasq(const QJsonObject &dhcp, const QJsonArray &reservations,
                    const QString &listenNic, const QString &serverIp) const;
    QString ipxeScript(const QString &serverIp) const;
    QString dhcpHook() const;
    QString netdevBond(const QString &name, const QStringList &slaves) const;
    QString networkd(const QString &iface, const QString &ip, const QString &mask,
                     const QString &gw, const QString &dns) const;

private:
    LxPaths m_paths;
};
