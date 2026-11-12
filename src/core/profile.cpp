#include "profile.h"

namespace rs {
QJsonObject Profile::json() const {
    return {{"id", id},
            {"name", name},
            {"host", host},
            {"user", user},
            {"port", port},
            {"group", group},
            {"environment", environment},
            {"directory", directory},
            {"authentication", authentication},
            {"keyPath", keyPath},
            {"notes", notes},
            {"favorite", favorite},
            {"rememberSecret", rememberSecret && authentication != "agent"},
            {"timeout", timeout},
            {"lastUsed", lastUsed.toString(Qt::ISODate)}};
}
Profile Profile::fromJson(const QJsonObject &o) {
    Profile p;
    p.id = o["id"].toString(p.id);
    p.name = o["name"].toString();
    p.host = o["host"].toString();
    p.user = o["user"].toString();
    p.port = o["port"].toInt(22);
    p.group = o["group"].toString();
    p.environment = o["environment"].toString("测试");
    p.directory = o["directory"].toString(".");
    p.authentication = o["authentication"].toString("password");
    p.keyPath = o["keyPath"].toString();
    p.notes = o["notes"].toString();
    p.favorite = o["favorite"].toBool();
    p.rememberSecret = o["rememberSecret"].toBool() && p.authentication != "agent";
    p.timeout = qBound(3, o["timeout"].toInt(15), 60);
    p.lastUsed = QDateTime::fromString(o["lastUsed"].toString(), Qt::ISODate);
    return p;
}
QString Profile::identity() const {
    return QString("[%1] %2 · %3@%4:%5").arg(environment, name, user, host).arg(port);
}
QJsonObject Profile::exportedJson() const {
    auto object = json();
    object.remove("rememberSecret");
    return object;
}
Profile Profile::duplicate() const {
    Profile copy = *this;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.rememberSecret = false;
    copy.lastUsed = {};
    return copy;
}
QString Profile::credentialKey() const {
    const QJsonArray identity = {id,   host,           user,
                                 port, authentication, authentication == "key" ? keyPath : QString()};
    return "io.rabbitshell.desktop/ssh/" +
           QString::fromLatin1(
               QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),
                                        QCryptographicHash::Sha256)
                   .toHex());
}
namespace { QString selectedDataPath; }
void setDataPath(const QString &path) { selectedDataPath = path; }
QString dataPath() {
    auto p = selectedDataPath.isEmpty() ? qEnvironmentVariable("RABBITSHELL_DATA_DIR") : selectedDataPath;
    if (p.isEmpty())
        p = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(p);
    return p;
}
QString remoteJoin(const QString &d, const QString &n) {
    return QDir::cleanPath(d + "/" + n);
}
QString shellQuote(const QString &s) {
    auto r = s;
    r.replace("'", "'\"'\"'");
    return "'" + r + "'";
}
QString humanSize(quint64 n) {
    double v = double(n);
    QStringList u = {"B", "KiB", "MiB", "GiB", "TiB"};
    int i = 0;
    while (v >= 1024 && i < 4) {
        v /= 1024;
        ++i;
    }
    return QString::number(v, 'f', i ? 1 : 0) + " " + u[i];
}
QVector<Profile> loadProfiles() {
    QFile f(dataPath() + "/connections.json");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    auto a = QJsonDocument::fromJson(f.readAll()).object()["connections"].toArray();
    QVector<Profile> r;
    for (auto v : a)
        r << Profile::fromJson(v.toObject());
    return r;
}
bool saveProfiles(const QVector<Profile> &p, QString *error) {
    QJsonArray a;
    for (const auto &v : p)
        a << v.json();
    QSaveFile f(dataPath() + "/connections.json");
    if (!f.open(QIODevice::WriteOnly) ||
        f.write(QJsonDocument(QJsonObject{{"version", 1}, {"connections", a}}).toJson()) < 0 || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QFile::setPermissions(f.fileName(), QFile::ReadOwner | QFile::WriteOwner);
    return true;
}
} // namespace rs
