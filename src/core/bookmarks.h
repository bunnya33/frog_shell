#pragma once
#include <QtCore>
#include <optional>
namespace rs {
struct DirectoryBookmark {
    QString id, path, alias;
    QString name() const { return alias.isEmpty() ? (path == "/" ? QStringLiteral("根目录") : path.section('/', -1)) : alias; }
};
enum class BookmarkOpen { Both, Directory, Terminal };
class DirectoryBookmarkStore : public QObject {
    Q_OBJECT
  public:
    explicit DirectoryBookmarkStore(const QString &file = {}, QObject *parent = nullptr);
    static QString normalizePath(const QString &path);
    QList<DirectoryBookmark> entries(const QString &profile) const;
    std::optional<DirectoryBookmark> entry(const QString &profile, const QString &id) const;
    void setProfiles(const QSet<QString> &ids) { profiles=ids; restricted=true; }
    bool accepts(const QString &profile) const { return !profile.isEmpty() && (!restricted || profiles.contains(profile)); }
    bool add(const QString &profile, const QString &path);
    bool rename(const QString &profile, const QString &id, const QString &alias);
    bool remove(const QString &profile, const QString &id);
    bool reorder(const QString &profile, const QStringList &ids);
    QString error() const { return lastError; }
  signals:
    void changed(const QString &profile);
    void saveFailed(const QString &message);
  private:
    QString fileName, lastError;
    bool writable=true, restricted=false;
    QSet<QString> profiles;
    QMap<QString,QList<DirectoryBookmark>> data;
    bool fail(const QString &message);
    bool commit(const QString &profile, const QList<DirectoryBookmark> &items);
};
}
