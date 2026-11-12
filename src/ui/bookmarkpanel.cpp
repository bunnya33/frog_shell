#include "bookmarkpanel.h"
#include "theme.h"
#include "contentfont.h"
#include "icons.h"
#include "fileicons.h"
#include "uizoom.h"
#include "layoutmetrics.h"
namespace rs {
void bookmarkMenu(QWidget *owner,DirectoryBookmarkStore *store,const QString &profile,const QString &id,const QPoint &global,const std::function<void(BookmarkOpen)> &open) {
    const auto b=store->entry(profile,id);if(!b)return;
    QMenu menu(owner);
    menu.addAction("打开目录和终端组",owner,[open]{open(BookmarkOpen::Both);});
    menu.addAction("打开目录",owner,[open]{open(BookmarkOpen::Directory);});
    menu.addAction("在此处新建终端",owner,[open]{open(BookmarkOpen::Terminal);});
    menu.addSeparator();
    QPointer<DirectoryBookmarkStore> guarded=store;
    menu.addAction("设置别名…",owner,[owner,guarded,profile,id,b]{
        bool ok=false;const auto alias=QInputDialog::getText(owner,"目录收藏别名","别名（留空恢复默认名称）",QLineEdit::Normal,b->alias,&ok);
        if(ok&&guarded)guarded->rename(profile,id,alias);
    });
    menu.addSeparator();
    menu.addAction("移除收藏",owner,[guarded,profile,id]{if(guarded)guarded->remove(profile,id);});
    menu.exec(global);
}
BookmarkTree::BookmarkTree(DirectoryBookmarkStore *s,const QString &p,QWidget *parent):SafeTreeWidget(parent),store(s),profile(p) {
    setProperty("sideTree",true);
    setColumnCount(2);setHeaderLabels({"名称","路径"});setAccessibleName("目录收藏");
    setRootIsDecorated(false);setUniformRowHeights(true);setSelectionBehavior(SelectRows);setSelectionMode(SingleSelection);
    setEditTriggers(NoEditTriggers);setDragEnabled(true);setAcceptDrops(true);setDragDropMode(InternalMove);setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(false);header()->setStretchLastSection(false);header()->setSectionResizeMode(QHeaderView::Interactive);
    setItemDelegate(new DensityDelegate(28,this));
    UiZoom::instance().bind(header(),"headerHeight",[this]{header()->setFixedHeight(qMax(uiPx(28),header()->fontMetrics().height()+uiPx(8)));setIconSize(uiSize(18,18));});
    UiZoom::instance().bind(this,"columns",[this]{scalingColumns=true;for(int c=0;c<2;++c)setColumnWidth(c,uiPx(baseColumns[c]));if(!customColumns)fitColumns();setIndentation(uiPx(18));scalingColumns=false;});
    connect(header(),&QHeaderView::sectionResized,this,[this](int c,int,int size){if(!scalingColumns&&!UiZoom::instance().changing()&&c<2){baseColumns[c]=uiBase(size);customColumns=true;}});
    setContextMenuPolicy(Qt::CustomContextMenu);connect(this,&QWidget::customContextMenuRequested,this,&BookmarkTree::menu);
    connect(this,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem *item,int){emit openRequested(item->data(0,BookmarkIdRole).toString(),BookmarkOpen::Both);});
    auto enter=new QShortcut(QKeySequence(Qt::Key_Return),this);enter->setContext(Qt::WidgetWithChildrenShortcut);
    connect(enter,&QShortcut::activated,this,[this]{if(auto item=currentItem())emit openRequested(item->data(0,BookmarkIdRole).toString(),BookmarkOpen::Both);});
    connect(s,&DirectoryBookmarkStore::changed,this,[this](const QString &p){if(p==profile)refresh();});refresh();
}
void BookmarkTree::fitColumns() {
    if(customColumns)return;
    QScopedValueRollback<bool> guard(scalingColumns,true);
    setColumnWidth(0,uiPx(105));setColumnWidth(1,qMax(uiPx(80),viewport()->width()-columnWidth(0)));
}
void BookmarkTree::resizeEvent(QResizeEvent *event){SafeTreeWidget::resizeEvent(event);fitColumns();}
void BookmarkTree::refresh() {
    const auto selected=currentItem()?currentItem()->data(0,BookmarkIdRole).toString():QString();
    const int v=verticalScrollBar()->value(),h=horizontalScrollBar()->value();
    insertion=-1;invalidateAccessibleRows();QSignalBlocker block(this);
    QHash<QString,QTreeWidgetItem*> existing;for(int i=0;i<topLevelItemCount();++i)existing[topLevelItem(i)->data(0,BookmarkIdRole).toString()]=topLevelItem(i);
    int index=0;for(const auto &b:store->entries(profile)) {
        auto item=existing.take(b.id);if(!item)item=new QTreeWidgetItem;
        const int old=indexOfTopLevelItem(item);if(old!=index){if(old>=0)takeTopLevelItem(old);insertTopLevelItem(index,item);}
        item->setText(0,b.name());item->setText(1,b.path);item->setToolTip(0,Qt::convertFromPlainText(b.name()));item->setToolTip(1,Qt::convertFromPlainText(b.path));item->setIcon(0,FileIconProvider::directory());
        item->setData(0,BookmarkIdRole,b.id);item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsDragEnabled);
        if(b.id==selected)setCurrentItem(item);++index;
    }
    qDeleteAll(existing);
    // Reinsertions can leave QTreeView's visible-row layout deferred while tree
    // signals are blocked. Finish this local layout before exposing fresh cells.
    doItemsLayout();invalidateAccessibleRows();
    verticalScrollBar()->setValue(v);horizontalScrollBar()->setValue(h);viewport()->update();
}
void BookmarkTree::menu(QPoint p){if(auto item=itemAt(p)){const auto id=item->data(0,BookmarkIdRole).toString();bookmarkMenu(this,store,profile,id,viewport()->mapToGlobal(p),[this,id](BookmarkOpen mode){emit openRequested(id,mode);});}}
void BookmarkTree::startDrag(Qt::DropActions) {
    if(!currentItem())return;
    const auto id=currentItem()->data(0,BookmarkIdRole).toString();
    // Leave QAbstractItemView's mouse/startDrag stack before the native nested loop.
    QTimer::singleShot(0,this,[this,id]{
        QPointer<BookmarkTree> owner=this;QPointer<QDrag> drag=new QDrag(this);
        auto mime=new QMimeData;mime->setData("application/x-rabbitshell-bookmark",QJsonDocument(QJsonObject{{"token",dragToken},{"id",id}}).toJson());drag->setMimeData(mime);
        drag->exec(Qt::MoveAction);
        if(drag)drag->deleteLater();if(owner){owner->insertion=-1;owner->viewport()->update();}
    });
}
int BookmarkTree::insertionAt(QPoint p) const {auto item=itemAt(p);if(!item)return topLevelItemCount();const auto rect=visualItemRect(item);return indexOfTopLevelItem(item)+(p.y()>rect.center().y()?1:0);}
void BookmarkTree::dragEnterEvent(QDragEnterEvent *e){if(e->source()==this&&e->mimeData()->hasFormat("application/x-rabbitshell-bookmark")){e->setDropAction(Qt::MoveAction);e->accept();}else e->ignore();}
void BookmarkTree::dragMoveEvent(QDragMoveEvent *e){if(e->source()==this&&(e->possibleActions()&Qt::MoveAction)){insertion=insertionAt(e->position().toPoint());e->setDropAction(Qt::MoveAction);e->accept();viewport()->update();}else e->ignore();}
void BookmarkTree::dropEvent(QDropEvent *e) {
    if(e->source()!=this||!store){e->ignore();return;}
    const int to=insertionAt(e->position().toPoint());insertion=-1;
    if(dropMimeData(nullptr,to,e->mimeData(),Qt::MoveAction)){e->setDropAction(Qt::MoveAction);e->accept();}else e->ignore();viewport()->update();
}
QMimeData *BookmarkTree::mimeData(const QList<QTreeWidgetItem*> &items) const {
    auto mime=new QMimeData;
    if(items.size()==1)mime->setData("application/x-rabbitshell-bookmark",QJsonDocument(QJsonObject{{"token",dragToken},{"id",items.first()->data(0,BookmarkIdRole).toString()}}).toJson());
    return mime;
}
bool BookmarkTree::dropMimeData(QTreeWidgetItem *parent,int index,const QMimeData *mime,Qt::DropAction action) {
    if(parent||action!=Qt::MoveAction||!store)return false;
    const auto o=QJsonDocument::fromJson(mime->data("application/x-rabbitshell-bookmark")).object();if(o["token"].toString()!=dragToken)return false;
    QStringList ids;for(const auto &b:store->entries(profile))ids<<b.id;
    const auto id=o["id"].toString();const int old=ids.indexOf(id);int to=index<0?ids.size():index;
    if(old<0)return false;ids.removeAt(old);if(old<to)--to;ids.insert(qBound(0,to,int(ids.size())),id);
    return store->reorder(profile,ids);
}
void BookmarkTree::dragLeaveEvent(QDragLeaveEvent *e){insertion=-1;viewport()->update();e->accept();}
void BookmarkTree::paintEvent(QPaintEvent *e) {
    SafeTreeWidget::paintEvent(e);QPainter p(viewport());
    if(!topLevelItemCount()){p.setPen(colors().muted);p.drawText(viewport()->rect().adjusted(uiPx(8),uiPx(8),-uiPx(8),-uiPx(8)),Qt::AlignTop|Qt::TextWordWrap,"右键资源树中的目录，添加到目录收藏");}
    if(insertion>=0){const int y=insertion<topLevelItemCount()?visualItemRect(topLevelItem(insertion)).top():topLevelItemCount()?visualItemRect(topLevelItem(topLevelItemCount()-1)).bottom():0;p.setPen(QPen(colors().accent,uiReal(2)));p.drawLine(0,y,viewport()->width(),y);}
}
}
