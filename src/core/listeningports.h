#pragma once
#include "services.h"
namespace rs::listening {
inline constexpr int maxRows=4096, outputLimit=512*1024;
struct Endpoint {
    QString protocol, address;
    quint16 port=0;
    qint64 pid=0;
    QString process;
    QString key() const {return protocol+"|"+address+"|"+QString::number(port);}
};
struct Snapshot {
    QVector<Endpoint> endpoints;
    bool available=false, complete=false, truncated=false;
    QString source, error;
};
services::Command command();
Snapshot parse(const services::Result &result);
}
