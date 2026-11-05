#pragma once

#include "core/Engine.hpp"
#include "core/Ipc.hpp"

#include <QMainWindow>
#include <memory>

class QStackedWidget;
class QListWidget;
class QLabel;
class Pages;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    QJsonObject call(const QString &method, const QJsonObject &params = {});
    void showLogin();
    void setupKiosk();
    void refreshStatusBar();

    Engine m_local;
    IpcClient m_ipc;
    bool m_useIpc = false;
    QStackedWidget *m_stack = nullptr;
    QListWidget *m_nav = nullptr;
    QLabel *m_banner = nullptr;
    Pages *m_pages = nullptr;
};
