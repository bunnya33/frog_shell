#pragma once
#include <QtWidgets>
#include <atomic>
#include <memory>
namespace rs {
struct DragFile { QString remotePath, name; quint64 size=0; qint64 modified=0; };
// One bounded metadata set and a private on-disk spool. No remote data in MIME.
class FileDragBatch final : public std::enable_shared_from_this<FileDragBatch> {
  public:
    using Done=std::function<void(QString)>;
    using Fetch=std::function<void(int,const QString &,Done)>;
    static std::shared_ptr<FileDragBatch> create(const QList<DragFile> &,QString *error=nullptr);
    QList<DragFile> files;
    QByteArray internalMoveToken; // Opaque lowercase UUID; set before creating the data object.
    Qt::DropAction performedAction=Qt::IgnoreAction; // GUI thread, after DoDragDrop returns.
    Fetch fetch; // Invoked only on the GUI thread, after release + first Read.
    std::function<void()> cancelJobs; // Native download jobs only; GUI only, safe owner guard required.
    std::function<void(bool)> systemFinished; // Native download batch; independent of internal move confirmation.
    std::atomic_bool released=false,cancelled=false;
    void cancel();
    void finish(bool accepted);
    QString stagePath(int index) const;
    // A GUI caller pumps a small nested event loop; a COM thread waits on a condition.
    // Neither path performs a blocking GUI invoke or touches a Workspace off-thread.
    QString awaitFile(int index);
    bool ready(int index) const;
  private:
    QTemporaryDir spool;
    struct Entry { bool requested=false,done=false;QString error; };
    mutable QMutex mutex;
    QWaitCondition changed;
    QList<Entry> entries;
    void request(int index);
};
QByteArray internalFileMoveMimeType();
bool isValidInternalFileMoveToken(const QByteArray &);
QByteArray internalFileMoveToken(const QMimeData *);
QPixmap fileDragBadge(const QIcon &,const QString &,int count,qreal dpr=1);
bool dragRemoteFiles(QWidget *source,const std::shared_ptr<FileDragBatch> &,const QPixmap &);
}
