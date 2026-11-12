#pragma once
#include <QIcon>
#include <QString>
namespace rs {
// Finite Seti definitions/outlines shared by every file surface. Never caches paths.
class FileIconProvider {
  public:
    static QIcon file(const QString &path);
    static QIcon directory();
    static QString definition(const QString &path);
    static bool resourcesAvailable();
};
} // namespace rs
