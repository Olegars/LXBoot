#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>
#include <functional>

class QTableWidget;
class QPlainTextEdit;
class QLabel;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;

using Api = std::function<QJsonObject(const QString &, const QJsonObject &)>;

class Pages : public QObject {
    Q_OBJECT
public:
    explicit Pages(Api api, QObject *parent = nullptr);
    QWidget *statusPage();
    QWidget *netPage();
    QWidget *dhcpPage();
    QWidget *diskPage();
    QWidget *imagePage();
    QWidget *clientPage();
    QWidget *logPage();
    QWidget *servicePage();
    void refreshAll();

private:
    void fillTable(QTableWidget *t, const QJsonArray &rows, const QStringList &keys, const QStringList &headers);
    Api m_api;
    QWidget *m_status = nullptr;
    QWidget *m_net = nullptr;
    QWidget *m_dhcp = nullptr;
    QWidget *m_disk = nullptr;
    QWidget *m_image = nullptr;
    QWidget *m_client = nullptr;
    QWidget *m_log = nullptr;
    QWidget *m_svc = nullptr;
    QLabel *m_cpu = nullptr;
    QLabel *m_ram = nullptr;
    QLabel *m_netlbl = nullptr;
    QLabel *m_clients = nullptr;
    QLabel *m_cfg = nullptr;
    QTableWidget *m_nicTable = nullptr;
    QTableWidget *m_diskTable = nullptr;
    QTableWidget *m_imgTable = nullptr;
    QTableWidget *m_rpTable = nullptr;
    QTableWidget *m_cliTable = nullptr;
    QPlainTextEdit *m_logBox = nullptr;
    QLineEdit *m_if = nullptr, *m_ip = nullptr, *m_mask = nullptr, *m_gw = nullptr, *m_dns = nullptr;
    QCheckBox *m_lacp = nullptr;
    QLineEdit *m_dhcpIf = nullptr, *m_dhcpSip = nullptr, *m_poolA = nullptr, *m_poolB = nullptr, *m_dhcpGw = nullptr, *m_dhcpDns = nullptr;
    QSpinBox *m_lease = nullptr;
    QCheckBox *m_dhcpOn = nullptr;
};
