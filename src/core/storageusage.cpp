#include "storageusage.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <limits>

namespace rs {
namespace {
bool safeAncestors(const QString &path) {
    QFileInfo part(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    for (;;) {
        if (part.isSymLink() || part.isJunction()) return false;
        const auto parent = part.dir().absolutePath();
        if (parent == part.absoluteFilePath()) break;
        part.setFile(parent);
    }
    return true;
}
void addTree(const QString &path, UsageAmount &result, int &remaining, int depth,
             const std::function<bool()> &canceled) {
    if (canceled && canceled()) { result.canceled = true; return; }
    if (remaining-- <= 0 || depth > 64) { result.partial = true; return; }
    const QString current = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QFileInfo info(current);
    if (info.isSymLink() || info.isJunction() || !safeAncestors(current)) { result.partial = true; return; }
    if (!info.exists()) return;
    if (!info.isReadable()) { result.partial = true; return; }
    result.observed = true;
    if (info.isFile()) {
        const auto size = quint64(qMax<qint64>(0, info.size()));
        if (size > (std::numeric_limits<quint64>::max)() - result.bytes) result.partial = true;
        else result.bytes += size;
    } else if (info.isDir()) {
        QDir dir(current);
        if (!dir.isReadable()) { result.partial = true; return; }
        QDirIterator it(current, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        while (it.hasNext()) {
            if (remaining <= 0) { result.partial = true; return; }
            addTree(it.next(), result, remaining, depth + 1, canceled);
            if (result.canceled) return;
        }
    } else result.partial = true;
}
QString pathKey(const QString &path) {
    auto key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}
}
StorageUsage scanStorageUsage(const QString &dataDirectory, const QString &settingsFile,
                              const QString &logDirectory, const std::function<bool()> &canceled) {
    StorageUsage out; QSet<QString> roots; int dataBudget = 100000, logBudget = 100000;
    const QDir data(dataDirectory), logs(logDirectory);
    for (const auto &name : {"connections.json", "directory-bookmarks.json", "tunnels.json", "known_hosts",
                             "application-lock.json", "application-lock.required", "drafts", "clipboard-spool", "settings", "file-operation-locks"}) {
        const auto path = data.filePath(QLatin1String(name)); roots.insert(pathKey(path));
        addTree(path, out.data, dataBudget, 0, canceled);
        if (out.data.canceled) return out;
    }
    if (!settingsFile.isEmpty() && QFileInfo(settingsFile).isAbsolute()) {
        const auto key = pathKey(settingsFile);
        bool covered = roots.contains(key);
        for (const auto &root : roots) if (key.startsWith(root + '/')) { covered = true; break; }
        if (!covered) addTree(settingsFile, out.data, dataBudget, 0, canceled);
    }
    if (out.data.canceled) return out;
    for (const auto &name : {"diagnostic.jsonl", "diagnostic.1.jsonl", "diagnostic.2.jsonl"})
        addTree(logs.filePath(QLatin1String(name)), out.logs, logBudget, 0, canceled);
    for (int i = 0; i < 16 && !out.logs.canceled; ++i) {
        addTree(logs.filePath(QString("crash-%1.log").arg(i, 2, 10, QLatin1Char('0'))), out.logs, logBudget, 0, canceled);
        addTree(logs.filePath(QString("exit-%1.log").arg(i, 2, 10, QLatin1Char('0'))), out.logs, logBudget, 0, canceled);
    }
    return out;
}
}
