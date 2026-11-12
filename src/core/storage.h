#pragma once
#include <QtCore>
namespace rs {
QString defaultDataPath();
bool storageOverride();
QString storagePointerFile();
bool resolveStoragePath(QString *path, QString *error);
void configureStorageSettings(const QString &path);
bool validateStorageTarget(const QString &source, const QString &target, QString *error);
// Call only after stopping storage users while holding the source lock; targetLocked
// indicates the caller also holds the target's instance.lock. pointerOverride is for isolated tests.
bool migrateStorage(const QString &source, const QString &target, QString *error, bool targetLocked = false, const QString &pointerOverride = {});
}
