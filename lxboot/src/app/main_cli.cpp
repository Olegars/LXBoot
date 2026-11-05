#include "core/Engine.hpp"
#include "core/Ipc.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <cstdio>

static void printJson(const QJsonObject &o)
{
    std::fwrite(QJsonDocument(o).toJson(QJsonDocument::Indented).constData(), 1,
                size_t(QJsonDocument(o).toJson(QJsonDocument::Indented).size()), stdout);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty() || args[0] == QLatin1String("-h") || args[0] == QLatin1String("--help")) {
        std::puts("lxboot <method> [k=v ...]\n"
                  "  status | net.list | dhcp.get | disk.list | image.list | client.list | log.list\n"
                  "  client.upsert mac=aa:bb:cc:dd:ee:ff hostname=pc01 ip=10.0.0.10\n"
                  "  client.super mac=... on=1\n"
                  "  client.commitSuper mac=... save=1\n"
                  "  image.import path=/tmp/win.vhdx name=win11\n"
                  "  disk.setRole name=sdb role=image ram_cache_mb=4096\n"
                  "  runtime.apply\n"
                  "  lease add|old|del MAC IP HOSTNAME\n");
        return 0;
    }

    QString method = args[0];
    QJsonObject params;
    if (method == QLatin1String("lease") && args.size() >= 3) {
        params.insert(QStringLiteral("action"), args.value(1));
        params.insert(QStringLiteral("mac"), args.value(2));
        params.insert(QStringLiteral("ip"), args.value(3));
        params.insert(QStringLiteral("hostname"), args.value(4));
        method = QStringLiteral("lease");
    } else {
        for (int i = 1; i < args.size(); ++i) {
            const int eq = args[i].indexOf(QLatin1Char('='));
            if (eq < 0)
                continue;
            const QString k = args[i].left(eq);
            const QString v = args[i].mid(eq + 1);
            if (v == QLatin1String("1") || v == QLatin1String("true"))
                params.insert(k, true);
            else if (v == QLatin1String("0") || v == QLatin1String("false"))
                params.insert(k, false);
            else {
                bool okNum = false;
                const qint64 n = v.toLongLong(&okNum);
                if (okNum)
                    params.insert(k, n);
                else
                    params.insert(k, v);
            }
        }
    }

    IpcClient ipc;
    QString err;
    if (ipc.connectTo(qEnvironmentVariable("LXBOOT_SOCK", QStringLiteral("/run/lxboot/lxbootd.sock")), &err)) {
        printJson(ipc.call(method, params));
        return 0;
    }
    Engine engine;
    if (!engine.init(&err)) {
        std::fprintf(stderr, "%s\n", qPrintable(err));
        return 1;
    }
    printJson(engine.call(method, params));
    return 0;
}
