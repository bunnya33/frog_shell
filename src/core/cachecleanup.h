#pragma once
#include <QtCore>
#include <functional>

namespace rs {
// The plan is an opaque, bounded snapshot. Only roots supplied by the settings
// page and the application's fixed whitelist are ever considered.
struct CleanupEntry {
    QString relative;
    quint64 volume = 0, index = 0, size = 0, modified = 0;
    bool directory = false;
};
struct CleanupBatch { QString token; QList<CleanupEntry> entries; };
struct CleanupPlan {
    QString dataRoot, logRoot;
    quint64 dataVolume = 0, dataIndex = 0, logVolume = 0, logIndex = 0;
    QList<CleanupEntry> logs;
    QList<CleanupBatch> batches;
    quint64 bytes = 0;
    int items = 0, skipped = 0, failures = 0;
    bool partial = false, canceled = false, supported = true;
};
struct CleanupResult {
    quint64 bytes = 0;
    int items = 0, skipped = 0, failures = 0;
    bool partial = false, canceled = false;
};
using CleanupCanceled = std::function<bool()>;
CleanupPlan planCacheCleanup(const QString &dataRoot, const QString &logRoot,
                             const QSet<QString> &protectedTokens = {}, const CleanupCanceled &canceled = {},
                             bool preserveCurrentLog = true, bool preserveRotatingLogs = false);
CleanupResult executeCacheCleanup(const CleanupPlan &plan, const QSet<QString> &protectedTokens = {},
                                  const CleanupCanceled &canceled = {});
bool isCleanupToken(const QString &token);
}
