#include "ui/MainWindow.hpp"
#include "ui/Pages.hpp"

#include <QApplication>
#include <QDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("LXBoot — diskless appliance"));
    resize(1280, 800);

    QString err;
    if (!m_local.init(&err)) {
        QMessageBox::critical(this, QStringLiteral("LXBoot"), err);
    }
    if (m_ipc.connectTo(m_local.paths().socket, &err))
        m_useIpc = true;

    auto *root = new QWidget(this);
    auto *hl = new QHBoxLayout(root);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);

    m_nav = new QListWidget;
    m_nav->setFixedWidth(210);
    m_nav->addItems({QStringLiteral("Статус"), QStringLiteral("Сеть"), QStringLiteral("DHCP / PXE"),
                     QStringLiteral("Диски"), QStringLiteral("Образы"), QStringLiteral("Клиенты"),
                     QStringLiteral("Журнал"), QStringLiteral("Службы")});
    m_nav->setCurrentRow(0);

    auto *right = new QWidget;
    auto *vl = new QVBoxLayout(right);
    vl->setContentsMargins(0, 0, 0, 0);
    m_banner = new QLabel;
    m_banner->setObjectName(QStringLiteral("banner"));
    m_stack = new QStackedWidget;

    m_pages = new Pages(
        [this](const QString &m, const QJsonObject &p) { return call(m, p); }, this);
    m_stack->addWidget(m_pages->statusPage());
    m_stack->addWidget(m_pages->netPage());
    m_stack->addWidget(m_pages->dhcpPage());
    m_stack->addWidget(m_pages->diskPage());
    m_stack->addWidget(m_pages->imagePage());
    m_stack->addWidget(m_pages->clientPage());
    m_stack->addWidget(m_pages->logPage());
    m_stack->addWidget(m_pages->servicePage());

    vl->addWidget(m_banner);
    vl->addWidget(m_stack, 1);
    hl->addWidget(m_nav);
    hl->addWidget(right, 1);
    setCentralWidget(root);

    connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    connect(m_nav, &QListWidget::currentRowChanged, this, [this] { m_pages->refreshAll(); });

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        refreshStatusBar();
        if (m_stack->currentIndex() == 0 || m_stack->currentIndex() == 5)
            m_pages->refreshAll();
    });
    timer->start(3000);

    showLogin();
    setupKiosk();
    refreshStatusBar();
    m_pages->refreshAll();
}

QJsonObject MainWindow::call(const QString &method, const QJsonObject &params)
{
    if (m_useIpc && m_ipc.isConnected())
        return m_ipc.call(method, params);
    return m_local.call(method, params);
}

void MainWindow::showLogin()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("LXBoot"));
    d.setModal(true);
    auto *l = new QVBoxLayout(&d);
    l->addWidget(new QLabel(QStringLiteral("Пароль администратора (по умолчанию: lxboot)")));
    auto *e = new QLineEdit;
    e->setEchoMode(QLineEdit::Password);
    l->addWidget(e);
    auto *ok = new QPushButton(QStringLiteral("Войти"));
    l->addWidget(ok);
    connect(ok, &QPushButton::clicked, &d, [&] {
        const auto r = call(QStringLiteral("auth.check"), {{QStringLiteral("password"), e->text()}});
        if (r.value(QStringLiteral("ok_auth")).toBool())
            d.accept();
        else
            QMessageBox::warning(&d, QStringLiteral("LXBoot"), QStringLiteral("Неверный пароль"));
    });
    connect(e, &QLineEdit::returnPressed, ok, &QPushButton::click);
    if (d.exec() != QDialog::Accepted)
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void MainWindow::setupKiosk()
{
    if (qEnvironmentVariableIntValue("LXBOOT_WINDOWED"))
        return;
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    showFullScreen();
}

void MainWindow::refreshStatusBar()
{
    const auto st = call(QStringLiteral("status"));
    const auto m = st.value(QStringLiteral("metrics")).toObject();
    QString mode = m_useIpc ? QStringLiteral("демон") : QStringLiteral("локально");
    if (!st.value(QStringLiteral("apply")).toBool())
        mode += QStringLiteral(" · dry-run (LXBOOT_APPLY=1 для записи в систему)");
    const QString cfg = st.value(QStringLiteral("cfg_ok")).toBool()
                            ? QStringLiteral("конфиг OK")
                            : st.value(QStringLiteral("cfg_error")).toString();
    m_banner->setText(QStringLiteral("  LXBoot  ·  клиенты %1/%2  ·  RAM %3%  ·  load %4  ·  %5  ·  %6")
                          .arg(st.value(QStringLiteral("clients_online")).toInteger())
                          .arg(st.value(QStringLiteral("clients_total")).toInteger())
                          .arg(m.value(QStringLiteral("ram_used_pct")).toInt())
                          .arg(m.value(QStringLiteral("load1")).toDouble(), 0, 'f', 2)
                          .arg(cfg, mode));
    statusBar()->showMessage(st.value(QStringLiteral("prefix")).toString());
}
