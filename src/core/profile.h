#pragma once
#include <QtCore>

namespace rs {
struct Profile {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name, host, user, group, environment = "测试", directory = ".", keyPath, notes;
    QString authentication = "password";
    int port = 22, timeout = 15;
    bool favorite = false, rememberSecret = false;
    QDateTime lastUsed;
    QJsonObject json() const;
    QJsonObject exportedJson() const;
    Profile duplicate() const;
    QString credentialKey() const;
    static Profile fromJson(const QJsonObject &object);
    QString identity() const;
};
QString dataPath();
void setDataPath(const QString &path);
QString remoteJoin(const QString &directory, const QString &name);
QString shellQuote(const QString &value);
QString humanSize(quint64 bytes);
QVector<Profile> loadProfiles();
bool saveProfiles(const QVector<Profile> &profiles, QString *error = nullptr);
} // namespace rs
Q_DECLARE_METATYPE(rs::Profile)
