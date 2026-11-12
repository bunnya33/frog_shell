#pragma once
#include <QtWidgets>
#include <memory>

namespace rs {
class Workspace;
class Session;
struct FileClipboardBatch {
    enum class Kind { Copy, Cut };
    Kind kind = Kind::Copy;
    QByteArray token;
    QString spool;
    QString sourceIdentity;
    QStringList roots, staged;
    QPointer<Workspace> owner;
    QPointer<Session> origin;
    quint64 generation = 0, revision = 0;
    int remaining = 0;
    bool failed = false, ready = false, published = false, moving = false;
    QString error;
    std::shared_ptr<QLockFile> lock;
    void discard();
    ~FileClipboardBatch();
};
class FileClipboard final {
  public:
    static FileClipboard &instance();
    std::shared_ptr<FileClipboardBatch> create(FileClipboardBatch::Kind kind, QString *error);
    std::shared_ptr<FileClipboardBatch> current() const;
    void offer(const std::shared_ptr<FileClipboardBatch> &batch);
    bool publish(const std::shared_ptr<FileClipboardBatch> &batch);
    static QByteArray mimeType();
  private:
    FileClipboard() = default;
    std::shared_ptr<FileClipboardBatch> offered;
};
} // namespace rs
