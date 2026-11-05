#pragma once

#include <QJsonObject>

class Metrics {
public:
    QJsonObject snapshot() const;
};
