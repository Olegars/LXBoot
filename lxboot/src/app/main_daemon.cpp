#include "core/Engine.hpp"
#include "core/Ipc.hpp"

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("lxbootd"));
    Engine engine;
    QString err;
    if (!engine.init(&err)) {
        std::fprintf(stderr, "init: %s\n", qPrintable(err));
        return 1;
    }
    IpcServer server(&engine, engine.paths().socket);
    if (!server.listen(&err)) {
        std::fprintf(stderr, "listen: %s\n", qPrintable(err));
        return 2;
    }
    engine.call(QStringLiteral("runtime.apply"));
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &engine, [&] { engine.call(QStringLiteral("client.list")); });
    poll.start(4000);
    std::fprintf(stderr, "lxbootd socket %s\n", qPrintable(engine.paths().socket));
    return app.exec();
}
