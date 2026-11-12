#pragma once
#include "safetree.h"
#include "../core/bookmarks.h"
namespace rs {
inline constexpr int BookmarkIdRole=Qt::UserRole+20;
class BookmarkTree : public SafeTreeWidget {
    Q_OBJECT
  public:
    BookmarkTree(DirectoryBookmarkStore *store,const QString &profile,QWidget *parent=nullptr);
    void refresh();
  signals:
    void openRequested(const QString &id,BookmarkOpen mode);
  protected:
    void startDrag(Qt::DropActions) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    QStringList mimeTypes() const override {return {"application/x-rabbitshell-bookmark"};}
    QMimeData *mimeData(const QList<QTreeWidgetItem*> &items) const override;
    bool dropMimeData(QTreeWidgetItem *,int,const QMimeData *,Qt::DropAction) override;
    Qt::DropActions supportedDropActions() const override {return Qt::MoveAction;}
  private:
    QPointer<DirectoryBookmarkStore> store;
    QString profile;
    int insertion=-1;
    QString dragToken=QUuid::createUuid().toString();
    qreal baseColumns[2]={105,195};
    bool customColumns=false;
    void fitColumns();
    bool scalingColumns=false;
    int insertionAt(QPoint) const;
    void menu(QPoint);
};
void bookmarkMenu(QWidget *owner,DirectoryBookmarkStore *store,const QString &profile,const QString &id,
                  const QPoint &global,const std::function<void(BookmarkOpen)> &open);
}
