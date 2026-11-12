#include "fileclipboard.h"
#include "../core/profile.h"
#include <QClipboard>
#include <QThreadPool>

namespace rs {
namespace {
QString spoolRoot() { return QDir(dataPath()).filePath("clipboard-spool"); }
bool safeBatchName(const QString &name) {
    static const QRegularExpression id("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    return id.match(name).hasMatch();
}
void cleanup(const QString &root) {
    QDir dir(root);
    const auto now=QDateTime::currentDateTimeUtc();
    for (const auto &info : dir.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System)) {
        if (!safeBatchName(info.fileName()) || info.isSymLink() || info.isJunction() ||
            info.lastModified().toUTC().secsTo(now)<24*60*60) continue;
        QLockFile lock(dir.filePath(info.fileName()+".lock"));
        if (!lock.tryLock(0)) continue;
        if (QFileInfo(info.absoluteFilePath()).canonicalPath()==QFileInfo(root).canonicalFilePath())
            QDir(info.absoluteFilePath()).removeRecursively();
        lock.unlock();
    }
}
}
FileClipboardBatch::~FileClipboardBatch() {
    discard();
}
void FileClipboardBatch::discard() {
    if (spool.isEmpty() || published) return;
    const auto path=spool; const auto held=lock; spool.clear(); lock.reset();
    QThreadPool::globalInstance()->start([path,held] {
        const QFileInfo info(path); const QFileInfo parent(info.absolutePath());
        if (safeBatchName(info.fileName()) && !info.isSymLink() && !info.isJunction() &&
            info.canonicalPath()==parent.canonicalFilePath()) QDir(path).removeRecursively();
    });
}
FileClipboard &FileClipboard::instance() {
    static auto *clipboard=new FileClipboard;
    return *clipboard;
}
QByteArray FileClipboard::mimeType() { return "application/x-rabbitshell-file-token"; }
std::shared_ptr<FileClipboardBatch> FileClipboard::create(FileClipboardBatch::Kind kind,QString *error) {
    auto b=std::make_shared<FileClipboardBatch>(); b->kind=kind;
    b->token=QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1();
    if (kind==FileClipboardBatch::Kind::Copy) {
        const auto root=spoolRoot();
        if (!QDir().mkpath(root) || QFileInfo(root).isSymLink() || QFileInfo(root).isJunction()) {
            if(error)*error="无法创建私有剪贴板暂存目录"; return {};
        }
        static QSet<QString> checkedRoots;
        if (!checkedRoots.contains(root)) { checkedRoots.insert(root); QThreadPool::globalInstance()->start([root]{cleanup(root);}); }
        b->lock=std::make_shared<QLockFile>(QDir(root).filePath(QString::fromLatin1(b->token)+".lock"));
        if (!b->lock->tryLock(0)) { if(error)*error="无法锁定剪贴板暂存目录"; return {}; }
        b->spool=QDir(root).filePath(QString::fromLatin1(b->token));
        if (!QDir().mkdir(b->spool)) { if(error)*error="无法创建剪贴板暂存目录"; b->spool.clear(); return {}; }
    }
    return b;
}
std::shared_ptr<FileClipboardBatch> FileClipboard::current() const {
    const auto mime=QApplication::clipboard()->mimeData();
    if (!offered || !mime || mime->data(mimeType())!=offered->token) return {};
    return offered;
}
void FileClipboard::offer(const std::shared_ptr<FileClipboardBatch> &b) {
    offered=b; auto mime=new QMimeData; mime->setData(mimeType(),b->token);
    QApplication::clipboard()->setMimeData(mime);
}
bool FileClipboard::publish(const std::shared_ptr<FileClipboardBatch> &b) {
    if (!b || current()!=b || b->failed || b->remaining!=0 || b->staged.isEmpty() ||
        b->kind!=FileClipboardBatch::Kind::Copy) return false;
    const QFileInfo spool(b->spool);
    const auto canonicalSpool=spool.canonicalFilePath();
    if (canonicalSpool.isEmpty() || spool.isSymLink() || spool.isJunction()) return false;
    for (const auto &path:b->staged) {
        const QFileInfo item(path), slot(item.absolutePath());
        if (!item.exists() || item.isSymLink() || item.isJunction() || (!item.isFile() && !item.isDir()) ||
            slot.isSymLink() || slot.isJunction() || slot.canonicalPath()!=canonicalSpool ||
            item.canonicalPath()!=slot.canonicalFilePath()) return false;
    }
    QList<QUrl> urls; for (const auto &path:b->staged) urls<<QUrl::fromLocalFile(path);
    auto mime=new QMimeData; mime->setData(mimeType(),b->token); mime->setUrls(urls);
    b->ready=true; b->published=true; QApplication::clipboard()->setMimeData(mime); return true;
}
} // namespace rs
