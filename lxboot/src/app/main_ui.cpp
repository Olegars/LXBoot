#include "ui/MainWindow.hpp"

#include <QApplication>
#include <QFont>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LXBoot"));
    app.setOrganizationName(QStringLiteral("LXBoot"));
    QFont f = app.font();
    f.setPointSize(11);
    app.setFont(f);
    app.setStyleSheet(QStringLiteral(
        "QWidget{background:#12161c;color:#e8edf4;font-family:'DejaVu Sans';}"
        "QMainWindow{background:#12161c;}"
        "QListWidget{background:#0d1117;border:none;padding:8px;outline:none;}"
        "QListWidget::item{padding:12px 16px;margin:2px 0;border-radius:6px;}"
        "QListWidget::item:selected{background:#2b6cb0;color:#fff;}"
        "QTableWidget{background:#171c24;gridline-color:#2a3340;alternate-background-color:#1c222c;}"
        "QHeaderView::section{background:#1b2430;color:#9fb3c8;padding:6px;border:0;}"
        "QLineEdit,QSpinBox,QComboBox,QPlainTextEdit{background:#1b222c;border:1px solid #334155;border-radius:4px;padding:6px;}"
        "QPushButton{background:#2b6cb0;color:#fff;border:none;border-radius:4px;padding:8px 14px;}"
        "QPushButton:hover{background:#3182ce;}"
        "QPushButton#danger{background:#c53030;}"
        "QPushButton#ghost{background:#2d3748;}"
        "QLabel#banner{background:#1a365d;color:#bee3f8;padding:8px 16px;}"
        "QLabel#warn{color:#f6e05e;}"));
    MainWindow w;
    w.show();
    return app.exec();
}
