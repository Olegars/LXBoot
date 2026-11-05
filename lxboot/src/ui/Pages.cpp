#include "ui/Pages.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

Pages::Pages(Api api, QObject *parent)
    : QObject(parent)
    , m_api(std::move(api))
{
}

static QWidget *wrap(QLayout *l)
{
    auto *w = new QWidget;
    w->setLayout(l);
    return w;
}

void Pages::fillTable(QTableWidget *t, const QJsonArray &rows, const QStringList &keys, const QStringList &headers)
{
    t->clear();
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->setRowCount(rows.size());
    t->horizontalHeader()->setStretchLastSection(true);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    for (int r = 0; r < rows.size(); ++r) {
        const auto o = rows[r].toObject();
        for (int c = 0; c < keys.size(); ++c) {
            QString v;
            const auto val = o.value(keys[c]);
            if (val.isBool())
                v = val.toBool() ? QStringLiteral("yes") : QStringLiteral("no");
            else if (val.isDouble())
                v = QString::number(val.toDouble());
            else
                v = val.toVariant().toString();
            t->setItem(r, c, new QTableWidgetItem(v));
        }
    }
}

QWidget *Pages::statusPage()
{
    auto *l = new QVBoxLayout;
    m_cpu = new QLabel;
    m_ram = new QLabel;
    m_netlbl = new QLabel;
    m_clients = new QLabel;
    m_cfg = new QLabel;
    m_cfg->setObjectName(QStringLiteral("warn"));
    m_cfg->setWordWrap(true);
    l->addWidget(new QLabel(QStringLiteral("<h2>Статус системы</h2>")));
    l->addWidget(m_cpu);
    l->addWidget(m_ram);
    l->addWidget(m_netlbl);
    l->addWidget(m_clients);
    l->addWidget(m_cfg);
    l->addStretch();
    m_status = wrap(l);
    return m_status;
}

QWidget *Pages::netPage()
{
    auto *l = new QVBoxLayout;
    l->addWidget(new QLabel(QStringLiteral("<h2>Сетевые интерфейсы</h2>")));
    m_nicTable = new QTableWidget;
    l->addWidget(m_nicTable, 1);
    auto *form = new QFormLayout;
    m_if = new QLineEdit(QStringLiteral("eth0"));
    m_ip = new QLineEdit(QStringLiteral("10.0.0.1"));
    m_mask = new QLineEdit(QStringLiteral("255.255.255.0"));
    m_gw = new QLineEdit(QStringLiteral("10.0.0.1"));
    m_dns = new QLineEdit(QStringLiteral("8.8.8.8"));
    m_lacp = new QCheckBox(QStringLiteral("LACP bond0 (802.3ad)"));
    form->addRow(QStringLiteral("Интерфейс"), m_if);
    form->addRow(QStringLiteral("IP"), m_ip);
    form->addRow(QStringLiteral("Маска"), m_mask);
    form->addRow(QStringLiteral("Шлюз"), m_gw);
    form->addRow(QStringLiteral("DNS"), m_dns);
    form->addRow(m_lacp);
    l->addLayout(form);
    auto *btn = new QPushButton(QStringLiteral("Применить сеть"));
    l->addWidget(btn);
    connect(btn, &QPushButton::clicked, this, [this] {
        QJsonObject p{{QStringLiteral("iface"), m_if->text()},
                      {QStringLiteral("ip"), m_ip->text()},
                      {QStringLiteral("mask"), m_mask->text()},
                      {QStringLiteral("gateway"), m_gw->text()},
                      {QStringLiteral("dns"), m_dns->text()},
                      {QStringLiteral("lacp"), m_lacp->isChecked()}};
        if (m_lacp->isChecked())
            p.insert(QStringLiteral("slaves"), QJsonArray{m_if->text()});
        const auto r = m_api(QStringLiteral("net.apply"), p);
        if (!r.value(QStringLiteral("ok")).toBool())
            QMessageBox::warning(m_net, QStringLiteral("Сеть"), r.value(QStringLiteral("error")).toString());
        refreshAll();
    });
    m_net = wrap(l);
    return m_net;
}

QWidget *Pages::dhcpPage()
{
    auto *l = new QVBoxLayout;
    l->addWidget(new QLabel(QStringLiteral("<h2>DHCP / PXE / TFTP</h2>")));
    l->addWidget(new QLabel(QStringLiteral("Выключите DHCP на роутере. Сервер должен быть единственным DHCP в boot-LAN.")));
    auto *form = new QFormLayout;
    m_dhcpOn = new QCheckBox(QStringLiteral("DHCP включён"));
    m_dhcpOn->setChecked(true);
    m_dhcpIf = new QLineEdit;
    m_dhcpSip = new QLineEdit;
    m_poolA = new QLineEdit;
    m_poolB = new QLineEdit;
    m_dhcpGw = new QLineEdit;
    m_dhcpDns = new QLineEdit;
    m_lease = new QSpinBox;
    m_lease->setRange(60, 86400);
    m_lease->setValue(3600);
    form->addRow(m_dhcpOn);
    form->addRow(QStringLiteral("Интерфейс"), m_dhcpIf);
    form->addRow(QStringLiteral("IP сервера"), m_dhcpSip);
    form->addRow(QStringLiteral("Пул с"), m_poolA);
    form->addRow(QStringLiteral("Пул по"), m_poolB);
    form->addRow(QStringLiteral("Шлюз"), m_dhcpGw);
    form->addRow(QStringLiteral("DNS"), m_dhcpDns);
    form->addRow(QStringLiteral("Lease, сек"), m_lease);
    l->addLayout(form);
    auto *btn = new QPushButton(QStringLiteral("Сохранить DHCP и пересобрать iPXE"));
    l->addWidget(btn);
    l->addStretch();
    connect(btn, &QPushButton::clicked, this, [this] {
        const auto r = m_api(QStringLiteral("dhcp.apply"),
                             QJsonObject{{QStringLiteral("enabled"), m_dhcpOn->isChecked()},
                                         {QStringLiteral("interface"), m_dhcpIf->text()},
                                         {QStringLiteral("server_ip"), m_dhcpSip->text()},
                                         {QStringLiteral("pool_start"), m_poolA->text()},
                                         {QStringLiteral("pool_end"), m_poolB->text()},
                                         {QStringLiteral("gateway"), m_dhcpGw->text()},
                                         {QStringLiteral("dns"), m_dhcpDns->text()},
                                         {QStringLiteral("lease_time"), m_lease->value()},
                                         {QStringLiteral("netmask"), QStringLiteral("255.255.255.0")}});
        if (!r.value(QStringLiteral("ok")).toBool())
            QMessageBox::warning(m_dhcp, QStringLiteral("DHCP"), r.value(QStringLiteral("error")).toString());
    });
    m_dhcp = wrap(l);
    return m_dhcp;
}

QWidget *Pages::diskPage()
{
    auto *l = new QVBoxLayout;
    l->addWidget(new QLabel(QStringLiteral("<h2>Диски и роли</h2>")));
    l->addWidget(new QLabel(QStringLiteral("Роли: image, writeback, ssd, game, system, offline. RAM cache — МБ.")));
    m_diskTable = new QTableWidget;
    l->addWidget(m_diskTable, 1);
    auto *row = new QHBoxLayout;
    auto *role = new QComboBox;
    role->addItems({QStringLiteral("image"), QStringLiteral("writeback"), QStringLiteral("ssd"),
                    QStringLiteral("game"), QStringLiteral("system"), QStringLiteral("offline")});
    auto *ram = new QSpinBox;
    ram->setRange(0, 256 * 1024);
    ram->setSingleStep(1024);
    auto *btn = new QPushButton(QStringLiteral("Назначить выбранному"));
    row->addWidget(new QLabel(QStringLiteral("Роль")));
    row->addWidget(role);
    row->addWidget(new QLabel(QStringLiteral("RAM cache МБ")));
    row->addWidget(ram);
    row->addWidget(btn);
    l->addLayout(row);
    connect(btn, &QPushButton::clicked, this, [this, role, ram] {
        const int r = m_diskTable->currentRow();
        if (r < 0)
            return;
        const QString name = m_diskTable->item(r, 0)->text();
        const auto res = m_api(QStringLiteral("disk.setRole"),
                               QJsonObject{{QStringLiteral("name"), name},
                                           {QStringLiteral("role"), role->currentText()},
                                           {QStringLiteral("ram_cache_mb"), ram->value()}});
        if (!res.value(QStringLiteral("ok")).toBool())
            QMessageBox::warning(m_disk, QStringLiteral("Диски"), res.value(QStringLiteral("error")).toString());
        refreshAll();
    });
    m_disk = wrap(l);
    return m_disk;
}

QWidget *Pages::imagePage()
{
    auto *l = new QVBoxLayout;
    l->addWidget(new QLabel(QStringLiteral("<h2>Образы (золотые)</h2>")));
    m_imgTable = new QTableWidget;
    l->addWidget(m_imgTable, 1);
    auto *btns = new QHBoxLayout;
    auto *imp = new QPushButton(QStringLiteral("Импорт IMG/VHD/VHDX"));
    auto *def = new QPushButton(QStringLiteral("Сделать образом по умолчанию"));
    auto *rp = new QPushButton(QStringLiteral("Создать restore point"));
    auto *rpl = new QPushButton(QStringLiteral("Restore to last"));
    auto *ml = new QPushButton(QStringLiteral("Merge to last"));
    auto *ma = new QPushButton(QStringLiteral("Merge all"));
    btns->addWidget(imp);
    btns->addWidget(def);
    btns->addWidget(rp);
    btns->addWidget(rpl);
    btns->addWidget(ml);
    btns->addWidget(ma);
    l->addLayout(btns);
    m_rpTable = new QTableWidget;
    l->addWidget(new QLabel(QStringLiteral("Restore points")));
    l->addWidget(m_rpTable, 1);
    auto selectedId = [this]() -> qint64 {
        const int r = m_imgTable->currentRow();
        if (r < 0)
            return 0;
        return m_imgTable->item(r, 0)->text().toLongLong();
    };
    connect(imp, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(m_image, QStringLiteral("Образ"), QString(),
                                                          QStringLiteral("Images (*.img *.raw *.vhd *.vhdx *.qcow2 *.vmdk)"));
        if (path.isEmpty())
            return;
        const auto r = m_api(QStringLiteral("image.import"), QJsonObject{{QStringLiteral("path"), path}});
        if (!r.value(QStringLiteral("ok")).toBool())
            QMessageBox::warning(m_image, QStringLiteral("Импорт"), r.value(QStringLiteral("error")).toString());
        refreshAll();
    });
    connect(def, &QPushButton::clicked, this, [this, selectedId] {
        m_api(QStringLiteral("image.setDefault"), {{QStringLiteral("id"), selectedId()}});
        refreshAll();
    });
    auto restore = [this, selectedId](const QString &action) {
        m_api(QStringLiteral("image.restore"),
              QJsonObject{{QStringLiteral("action"), action}, {QStringLiteral("image_id"), selectedId()}});
        refreshAll();
    };
    connect(rp, &QPushButton::clicked, this, [restore] { restore(QStringLiteral("create")); });
    connect(rpl, &QPushButton::clicked, this, [restore] { restore(QStringLiteral("restore_last")); });
    connect(ml, &QPushButton::clicked, this, [restore] { restore(QStringLiteral("merge_last")); });
    connect(ma, &QPushButton::clicked, this, [restore] { restore(QStringLiteral("merge_all")); });
    m_image = wrap(l);
    return m_image;
}

QWidget *Pages::clientPage()
{
    auto *l = new QVBoxLayout;
    l->addWidget(new QLabel(QStringLiteral("<h2>Клиенты</h2>")));
    m_cliTable = new QTableWidget;
    l->addWidget(m_cliTable, 1);
    auto *form = new QHBoxLayout;
    auto *mac = new QLineEdit;
    mac->setPlaceholderText(QStringLiteral("MAC"));
    auto *host = new QLineEdit;
    host->setPlaceholderText(QStringLiteral("имя ПК"));
    auto *ip = new QLineEdit;
    ip->setPlaceholderText(QStringLiteral("IP"));
    auto *save = new QPushButton(QStringLiteral("Добавить / обновить"));
    form->addWidget(mac);
    form->addWidget(host);
    form->addWidget(ip);
    form->addWidget(save);
    l->addLayout(form);
    auto *ops = new QHBoxLayout;
    auto *supOn = new QPushButton(QStringLiteral("Super Client ON"));
    auto *supOff = new QPushButton(QStringLiteral("Super Client OFF"));
    auto *commit = new QPushButton(QStringLiteral("Сохранить в золотой образ"));
    auto *discard = new QPushButton(QStringLiteral("Отбросить Super"));
    auto *reset = new QPushButton(QStringLiteral("Сбросить write-back"));
    reset->setObjectName(QStringLiteral("danger"));
    ops->addWidget(supOn);
    ops->addWidget(supOff);
    ops->addWidget(commit);
    ops->addWidget(discard);
    ops->addWidget(reset);
    l->addLayout(ops);
    auto currentMac = [this] {
        const int r = m_cliTable->currentRow();
        return r < 0 ? QString() : m_cliTable->item(r, 0)->text();
    };
    connect(save, &QPushButton::clicked, this, [this, mac, host, ip] {
        m_api(QStringLiteral("client.upsert"),
              QJsonObject{{QStringLiteral("mac"), mac->text()},
                          {QStringLiteral("hostname"), host->text()},
                          {QStringLiteral("ip"), ip->text()}});
        refreshAll();
    });
    connect(supOn, &QPushButton::clicked, this, [this, currentMac] {
        m_api(QStringLiteral("client.super"), {{QStringLiteral("mac"), currentMac()}, {QStringLiteral("on"), true}});
        refreshAll();
    });
    connect(supOff, &QPushButton::clicked, this, [this, currentMac] {
        m_api(QStringLiteral("client.super"), {{QStringLiteral("mac"), currentMac()}, {QStringLiteral("on"), false}});
        refreshAll();
    });
    connect(commit, &QPushButton::clicked, this, [this, currentMac] {
        m_api(QStringLiteral("client.commitSuper"), {{QStringLiteral("mac"), currentMac()}, {QStringLiteral("save"), true}});
        refreshAll();
    });
    connect(discard, &QPushButton::clicked, this, [this, currentMac] {
        m_api(QStringLiteral("client.commitSuper"), {{QStringLiteral("mac"), currentMac()}, {QStringLiteral("save"), false}});
        refreshAll();
    });
    connect(reset, &QPushButton::clicked, this, [this, currentMac] {
        m_api(QStringLiteral("client.resetWb"), {{QStringLiteral("mac"), currentMac()}});
        refreshAll();
    });
    m_client = wrap(l);
    return m_client;
}

QWidget *Pages::logPage()
{
    auto *l = new QVBoxLayout;
    l->addWidget(new QLabel(QStringLiteral("<h2>Журнал</h2>")));
    m_logBox = new QPlainTextEdit;
    m_logBox->setReadOnly(true);
    l->addWidget(m_logBox);
    m_log = wrap(l);
    return m_log;
}

QWidget *Pages::servicePage()
{
    auto *l = new QVBoxLayout;
    l->addWidget(new QLabel(QStringLiteral("<h2>Службы appliance</h2>")));
    auto *apply = new QPushButton(QStringLiteral("Применить runtime (dnsmasq + iSCSI map)"));
    auto *restart = new QPushButton(QStringLiteral("Перезапустить службы"));
    auto *ssh = new QPushButton(QStringLiteral("Включить SSH"));
    ssh->setObjectName(QStringLiteral("ghost"));
    auto *pass = new QPushButton(QStringLiteral("Сменить пароль"));
    auto *shell = new QPushButton(QStringLiteral("Техническая консоль (пароль)"));
    shell->setObjectName(QStringLiteral("ghost"));
    auto *reboot = new QPushButton(QStringLiteral("Reboot сервера"));
    reboot->setObjectName(QStringLiteral("danger"));
    auto *off = new QPushButton(QStringLiteral("Shutdown"));
    off->setObjectName(QStringLiteral("danger"));
    l->addWidget(apply);
    l->addWidget(restart);
    l->addWidget(ssh);
    l->addWidget(pass);
    l->addWidget(shell);
    l->addWidget(reboot);
    l->addWidget(off);
    l->addStretch();
    connect(apply, &QPushButton::clicked, this, [this] { m_api(QStringLiteral("runtime.apply"), {}); });
    connect(restart, &QPushButton::clicked, this, [this] { m_api(QStringLiteral("service.restart"), {}); });
    connect(ssh, &QPushButton::clicked, this, [this] { m_api(QStringLiteral("service.ssh"), {{QStringLiteral("on"), true}}); });
    connect(pass, &QPushButton::clicked, this, [this] {
        const QString p = QInputDialog::getText(m_svc, QStringLiteral("Пароль"), QStringLiteral("Новый пароль"),
                                                QLineEdit::Password);
        if (!p.isEmpty())
            m_api(QStringLiteral("service.password"), {{QStringLiteral("password"), p}});
    });
    connect(shell, &QPushButton::clicked, this, [this] {
        const QString p = QInputDialog::getText(m_svc, QStringLiteral("Консоль"), QStringLiteral("Пароль"),
                                                QLineEdit::Password);
        const auto r = m_api(QStringLiteral("service.shell"), {{QStringLiteral("password"), p}});
        QMessageBox::information(m_svc, QStringLiteral("Консоль"),
                                 r.value(QStringLiteral("ok")).toBool()
                                     ? r.value(QStringLiteral("hint")).toString()
                                     : r.value(QStringLiteral("error")).toString());
    });
    connect(reboot, &QPushButton::clicked, this, [this] { m_api(QStringLiteral("service.reboot"), {}); });
    connect(off, &QPushButton::clicked, this, [this] { m_api(QStringLiteral("service.shutdown"), {}); });
    m_svc = wrap(l);
    return m_svc;
}

void Pages::refreshAll()
{
    const auto st = m_api(QStringLiteral("status"), {});
    const auto m = st.value(QStringLiteral("metrics")).toObject();
    if (m_cpu)
        m_cpu->setText(QStringLiteral("CPU load: %1 / %2 / %3   nproc=%4")
                           .arg(m.value(QStringLiteral("load1")).toDouble(), 0, 'f', 2)
                           .arg(m.value(QStringLiteral("load5")).toDouble(), 0, 'f', 2)
                           .arg(m.value(QStringLiteral("load15")).toDouble(), 0, 'f', 2)
                           .arg(m.value(QStringLiteral("nproc")).toInt()));
    if (m_ram)
        m_ram->setText(QStringLiteral("RAM: %1% занято  (avail %2 МБ / total %3 МБ)  backend=%4")
                           .arg(m.value(QStringLiteral("ram_used_pct")).toInt())
                           .arg(m.value(QStringLiteral("ram_avail_kb")).toInteger() / 1024)
                           .arg(m.value(QStringLiteral("ram_total_kb")).toInteger() / 1024)
                           .arg(st.value(QStringLiteral("backend")).toString()));
    if (m_netlbl) {
        QString n;
        for (const auto &v : m.value(QStringLiteral("nics")).toArray()) {
            const auto o = v.toObject();
            n += QStringLiteral("%1  rx=%2  tx=%3\n")
                     .arg(o.value(QStringLiteral("name")).toString())
                     .arg(o.value(QStringLiteral("rx_bytes")).toInteger())
                     .arg(o.value(QStringLiteral("tx_bytes")).toInteger());
        }
        m_netlbl->setText(n);
    }
    if (m_clients)
        m_clients->setText(QStringLiteral("Клиенты online %1 / %2, образов %3")
                               .arg(st.value(QStringLiteral("clients_online")).toInteger())
                               .arg(st.value(QStringLiteral("clients_total")).toInteger())
                               .arg(st.value(QStringLiteral("images")).toInteger()));
    if (m_cfg)
        m_cfg->setText(st.value(QStringLiteral("cfg_ok")).toBool()
                           ? QStringLiteral("Image и Write-back назначены.")
                           : st.value(QStringLiteral("cfg_error")).toString());

    const auto nets = m_api(QStringLiteral("net.list"), {});
    if (m_nicTable)
        fillTable(m_nicTable, nets.value(QStringLiteral("nics")).toArray(),
                  {QStringLiteral("name"), QStringLiteral("operstate"), QStringLiteral("mac"), QStringLiteral("ipv4"),
                   QStringLiteral("prefix"), QStringLiteral("mtu")},
                  {QStringLiteral("NIC"), QStringLiteral("state"), QStringLiteral("MAC"), QStringLiteral("IPv4"),
                   QStringLiteral("mask"), QStringLiteral("MTU")});

    const auto dhcp = m_api(QStringLiteral("dhcp.get"), {}).value(QStringLiteral("dhcp")).toObject();
    if (m_dhcpIf && m_dhcpIf->text().isEmpty()) {
        m_dhcpIf->setText(dhcp.value(QStringLiteral("interface")).toString());
        m_dhcpSip->setText(dhcp.value(QStringLiteral("server_ip")).toString());
        m_poolA->setText(dhcp.value(QStringLiteral("pool_start")).toString());
        m_poolB->setText(dhcp.value(QStringLiteral("pool_end")).toString());
        m_dhcpGw->setText(dhcp.value(QStringLiteral("gateway")).toString());
        m_dhcpDns->setText(dhcp.value(QStringLiteral("dns")).toString());
        m_lease->setValue(dhcp.value(QStringLiteral("lease_time")).toInt(3600));
        m_dhcpOn->setChecked(dhcp.value(QStringLiteral("enabled")).toInteger() != 0);
    }

    const auto disks = m_api(QStringLiteral("disk.list"), {});
    if (m_diskTable)
        fillTable(m_diskTable, disks.value(QStringLiteral("disks")).toArray(),
                  {QStringLiteral("name"), QStringLiteral("size"), QStringLiteral("tran"), QStringLiteral("rota"),
                   QStringLiteral("role"), QStringLiteral("ram_cache_mb"), QStringLiteral("model")},
                  {QStringLiteral("Имя"), QStringLiteral("Размер"), QStringLiteral("Шина"), QStringLiteral("HDD"),
                   QStringLiteral("Роль"), QStringLiteral("RAM cache"), QStringLiteral("Модель")});

    const auto imgs = m_api(QStringLiteral("image.list"), {});
    if (m_imgTable)
        fillTable(m_imgTable, imgs.value(QStringLiteral("images")).toArray(),
                  {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("size_bytes"), QStringLiteral("kind"),
                   QStringLiteral("is_default"), QStringLiteral("path")},
                  {QStringLiteral("ID"), QStringLiteral("Имя"), QStringLiteral("Размер"), QStringLiteral("Тип"),
                   QStringLiteral("Default"), QStringLiteral("Путь")});
    if (m_rpTable)
        fillTable(m_rpTable, imgs.value(QStringLiteral("restore_points")).toArray(),
                  {QStringLiteral("id"), QStringLiteral("image_id"), QStringLiteral("note"), QStringLiteral("created_ts")},
                  {QStringLiteral("ID"), QStringLiteral("Image"), QStringLiteral("Заметка"), QStringLiteral("Время")});

    const auto cli = m_api(QStringLiteral("client.list"), {});
    if (m_cliTable)
        fillTable(m_cliTable, cli.value(QStringLiteral("clients")).toArray(),
                  {QStringLiteral("mac"), QStringLiteral("hostname"), QStringLiteral("ip"), QStringLiteral("online"),
                   QStringLiteral("super_client"), QStringLiteral("wb_policy"), QStringLiteral("wb_bytes"),
                   QStringLiteral("last_seen")},
                  {QStringLiteral("MAC"), QStringLiteral("Имя"), QStringLiteral("IP"), QStringLiteral("Online"),
                   QStringLiteral("Super"), QStringLiteral("WB"), QStringLiteral("Write-back"), QStringLiteral("Seen")});

    const auto log = m_api(QStringLiteral("log.list"), {{QStringLiteral("limit"), 150}});
    if (m_logBox) {
        QString t;
        for (const auto &v : log.value(QStringLiteral("events")).toArray()) {
            const auto o = v.toObject();
            t += QStringLiteral("%1  [%2]  %3  %4\n")
                     .arg(o.value(QStringLiteral("ts")).toString(), o.value(QStringLiteral("level")).toString(),
                          o.value(QStringLiteral("event")).toString(), o.value(QStringLiteral("detail")).toString());
        }
        m_logBox->setPlainText(t);
    }
}
