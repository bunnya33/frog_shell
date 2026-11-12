#pragma once
#include "profile.h"
#include <QtCore>
#include <memory>

namespace rs {
// Bounded session metadata only. Remote content never enters the journal.
class FileOperationJournal final {
  public:
    FileOperationJournal(const Profile &profile, const QString &authenticatedHostKey);
    ~FileOperationJournal();
    FileOperationJournal(const FileOperationJournal &) = delete;
    FileOperationJournal &operator=(const FileOperationJournal &) = delete;
    bool valid() const { return !identity.isEmpty(); }
    QString mutationLockPath() const { return path; }
    // Held for the full lifetime of a remote mutation, including a whole undo batch.
    std::unique_ptr<QLockFile> acquireMutationGuard(QString *error) const;
    bool snapshot(QJsonObject *out, QString *error) const;
    bool prepare(const QString &batchId, const QString &kind, const QString &source,
                 const QString &target, const QVariantMap &attributes,
                 QString *effectId, QString *error, bool keepOpen = false) const;
    bool sealBatch(const QString &batchId, QString *error) const;
    bool beginUndo(const QString &batchId, QVariantMap *effect, QString *error) const;
    bool transition(const QString &effectId, const QString &expected, const QString &state,
                    const QVariantMap &changes, QString *error) const;
    static QString newId();
  private:
    QString identity, path;
    bool mutate(const std::function<bool(QJsonObject &, QString *)> &fn, QString *error) const;
};
} // namespace rs
