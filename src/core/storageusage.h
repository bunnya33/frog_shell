#pragma once
#include <QString>
#include <QtGlobal>
#include <functional>

namespace rs {
struct UsageAmount { quint64 bytes = 0; bool partial = false; bool canceled = false; bool observed = false; };
struct StorageUsage { UsageAmount data, logs; };
// Read-only accounting of application-owned entries; never follows links.
StorageUsage scanStorageUsage(const QString &dataDirectory, const QString &settingsFile,
                              const QString &logDirectory, const std::function<bool()> &canceled = {});
}
