#include "messagebox.h"
#include "applock.h"
#include "uipreferences.h"
#include "connectionnotice.h"
#include "workspace.h"
#include "editorcompare.h"
#include "uploadconflictdialog.h"
#include "roundedsurface.h"
#include "sshprompts.h"
#include "fileicons.h"
#include "filecontextmenu.h"
#include "fileclipboard.h"
#include "theme.h"
#include "icons.h"
#include "windowstyle.h"
#include "styledcombobox.h"
#include "layoutmetrics.h"
#include "../core/diagnostics.h"
#include <algorithm>
#include <cmath>
namespace rs {
void NetworkPlot::paintEvent(QPaintEvent *) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);auto c=colors();p.setFont(uiFont(12));
    const auto values=series.value(selected);const auto now=QDateTime::currentMSecsSinceEpoch();
    double peak=0;for(const auto &v:values)if(v.first>=now-60000)peak=qMax(peak,qMax(v.second.first,v.second.second));
    double unit=peak>=1024*1024?1024*1024:peak>=1024?1024:1;QString suffix=unit==1024*1024?"MiB/s":unit==1024?"KiB/s":"B/s";
    const double maximum=qMax(unit,std::ceil(peak/unit)*unit);
    const QString top=values.isEmpty()?"—":QString::number(maximum/unit,'g',3)+" "+suffix;
    const int gutter=qMax(uiPx(48),p.fontMetrics().horizontalAdvance(top)+uiPx(6));
    QRectF plot(gutter,uiPx(9),qMax(1,width()-gutter-uiPx(4)),qMax(1,height()-uiPx(29)));
    for(int n=0;n<3;++n){const qreal y=plot.bottom()-plot.height()*n/2;p.setPen(QPen(c.line,uiReal(.7)));p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));p.setPen(c.muted);
        const QString tick=values.isEmpty()?QString("—"):n==2?top:QString::number(maximum/unit*n/2,'g',3);
        p.drawText(QRectF(0,y-uiReal(8),gutter-uiPx(6),uiPx(16)),Qt::AlignRight|Qt::AlignVCenter,tick);
    }
    p.setPen(c.muted);p.drawText(QRectF(plot.left(),height()-uiPx(18),uiPx(80),uiPx(18)),Qt::AlignLeft|Qt::AlignVCenter,"60 秒前");
    p.drawText(QRectF(plot.right()-uiPx(42),height()-uiPx(18),uiPx(42),uiPx(18)),Qt::AlignRight|Qt::AlignVCenter,"现在");
    for(int which=0;which<2;++which){QPainterPath path;bool first=true;
        for(const auto &v:values){if(v.first<now-60000)continue;const double value=which?v.second.second:v.second.first;
            QPointF point(plot.left()+plot.width()*qBound(0.,double(v.first-(now-60000))/60000.,1.),plot.bottom()-plot.height()*value/maximum);
            if(first)path.moveTo(point);else path.lineTo(point);first=false;
        }
        p.setPen(QPen(which?networkSendColor():networkReceiveColor(),uiReal(1.5),which?Qt::DashLine:Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.drawPath(path);
    }
}

namespace {
QString safeTerminalTooltip(const QString &text) {
    return "<qt>"+text.toHtmlEscaped().replace('\n',"<br>")+"</qt>";
}
class BreadcrumbButton final : public QPushButton {
  public:
    BreadcrumbButton(const QString &label, const QString &path, bool current)
        : QPushButton(QString(label).replace('&', "&&")), label(label) {
        setProperty("breadcrumb", true);
        setToolTip(path);
        setAccessibleName(label);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        uiMaximumWidth(this,current ? 200 : 88);
        bindUiFont(this,13,current?QFont::DemiBold:QFont::Normal);
        UiZoom::instance().bind(this,"breadcrumbMinimum",[this,current]{setMinimumWidth(qMin(QPushButton::sizeHint().width(),uiPx(current?72:24)));});
    }
    QSize minimumSizeHint() const override { return QSize(minimumWidth(), sizeHint().height()); }

  protected:
    void paintEvent(QPaintEvent *) override {
        QStyleOptionButton option;
        initStyleOption(&option);
        const auto content = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this);
        // Elide the literal name before escaping Qt's mnemonic marker. The
        // navigation target and accessible name always retain the original.
        option.text = fontMetrics().elidedText(label, Qt::ElideMiddle, content.width()).replace('&', "&&");
        QStylePainter painter(this);
        painter.drawControl(QStyle::CE_PushButton, option);
    }

  private:
    QString label;
};
class BreadcrumbSeparator final : public QWidget {
  public:
    BreadcrumbSeparator() {
        uiFixedWidth(this,12);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(colors().muted, uiReal(1.3), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const qreal y = height() / 2.0;
        QPainterPath chevron;
        chevron.moveTo(uiReal(4), y - uiReal(3));
        chevron.lineTo(uiReal(7), y);
        chevron.lineTo(uiReal(4), y + uiReal(3));
        p.drawPath(chevron);
    }
};
class FileItem : public QTreeWidgetItem {
  public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override {
        const auto rank = [](const QTreeWidgetItem &item) {
            if (item.data(0, ParentNavigationRole).toBool()) return 0;
            if (item.data(0, FileDraftRole).toBool()) return 1;
            return item.data(0, Qt::UserRole).toMap()["directory"].toBool() ? 2 : 3;
        };
        const int leftRank = rank(*this), rightRank = rank(other);
        if (leftRank != rightRank)
            return (leftRank < rightRank) ==
                   (!treeWidget() || treeWidget()->header()->sortIndicatorOrder() == Qt::AscendingOrder);
        const int column = treeWidget() ? treeWidget()->sortColumn() : 0;
        const auto left = data(0, Qt::UserRole).toMap(), right = other.data(0, Qt::UserRole).toMap();
        if (column == 1 && leftRank == 3 && left["size"].toULongLong() != right["size"].toULongLong())
            return left["size"].toULongLong() < right["size"].toULongLong();
        if (column == 2 && left["modified"].toLongLong() != right["modified"].toLongLong())
            return left["modified"].toLongLong() < right["modified"].toLongLong();
        return QString::localeAwareCompare(text(0), other.text(0)) < 0;
    }
};
QWidget *rowWidget(QHBoxLayout **out) {
    auto w = new PanelBand;
    auto l = new QHBoxLayout(w);
    uiMargins(l,12,3,8,3);
    uiSpacing(l,3);
    *out = l;
    return w;
}
void itemText(QTreeWidgetItem *i, int col, const QString &s) {
    i->setText(col, s);
    i->setToolTip(col, s);
}
QString basename(const QString &p) {
    return p.section('/', -1);
}
} // namespace
FileView::FileView(QWidget *parent)
    : AnimatedTreeWidget(parent), rubber(new QRubberBand(QRubberBand::Rectangle, viewport())) {
    setSelectionMode(ExtendedSelection);
    setSelectionBehavior(SelectRows);
    setAllColumnsShowFocus(true);
    setUniformRowHeights(true);
    setTreeMode(TreeMode::Hierarchy);
    setExpandsOnDoubleClick(false);
    UiZoom::instance().bind(this,"fileGeometry",[this]{header()->setFixedHeight(qMax(uiPx(28),header()->fontMetrics().height()+uiPx(8)));setIndentation(uiPx(24));setIconSize(uiSize(18,18));});
    setAcceptDrops(true);
    setDragDropMode(DragDrop);
    setDragEnabled(true);
    setDefaultDropAction(Qt::CopyAction);
    setDropIndicatorShown(true);
    setRootIsDecorated(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setColumnCount(3);
    setHeaderLabels({"名称", "大小", "修改时间"});
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    setColumnHidden(2, true);
    setSortingEnabled(true);
    sortItems(0, Qt::AscendingOrder);
    setAccessibleName("远程文件列表");
    auto changing=[this]{invalidateAccessibleRows();clearUploadPreview();};
    connect(model(),&QAbstractItemModel::rowsAboutToBeRemoved,this,changing);
    connect(model(),&QAbstractItemModel::rowsAboutToBeInserted,this,changing);
    connect(model(),&QAbstractItemModel::layoutAboutToBeChanged,this,changing);
    connect(model(),&QAbstractItemModel::modelAboutToBeReset,this,changing);
    connect(model(),&QAbstractItemModel::dataChanged,this,[this]{if(uploadPreview)clearUploadPreview();});
    connect(this,&QTreeWidget::itemExpanded,this,changing);
    connect(this,&QTreeWidget::itemCollapsed,this,changing);
}
FileView::~FileView() {
    // QTreeWidget destroys its model after FileView's value members. Do not let
    // that reset call derived preview/cache code during base-class teardown.
    disconnect(model(),nullptr,this,nullptr);
    disconnect(this,nullptr,this,nullptr);
}
void FileView::startDrag(Qt::DropActions){
    if (currentItem() && currentItem()->data(0, ParentNavigationRole).toBool()) return;
    QVariantList rows;
    for(auto item:selectedItems()){
        auto d=item->data(0,Qt::UserRole).toMap();
        if(item->data(0,ParentNavigationRole).toBool()||item->data(0,FileDraftRole).toBool()||d.isEmpty()||d["link"].toBool()){
            QToolTip::showText(QCursor::pos(),"链接和新建占位项不能移动。",this);return;
        }
        rows<<d;
    }
    if(!rows.isEmpty())emit remoteDragRequested(rows);
}
void FileView::mousePressEvent(QMouseEvent *e) {
    DiagnosticScope press(DiagEvent::FilePressBegin,DiagEvent::FilePressEnd,0,0,{},int(e->button()));
    auto row = rowAt(e->position().toPoint());
    if (row && row->data(0, ParentNavigationRole).toBool() &&
        (e->button() == Qt::LeftButton || e->button() == Qt::RightButton)) {
        navigationPress = true;
        clearSelection();
        selectionModel()->setCurrentIndex(indexFromItem(row), QItemSelectionModel::NoUpdate);
        e->accept();
        return;
    }
    bool blank=false;
    if(e->button()==Qt::LeftButton){
        DiagnosticScope hit(DiagEvent::FileHitBegin,DiagEvent::FileHitEnd,0,0,{});
        blank=!itemAt(e->position().toPoint());
    }
    if (e->button() == Qt::LeftButton && blank) {
        selecting = true;
        origin = e->position().toPoint();
        if (!(e->modifiers() & Qt::ControlModifier))
            clearSelection();
        rubber->setGeometry(QRect(origin, QSize()));
        rubber->show();
    } else {
        DiagnosticScope base(DiagEvent::FileBasePressBegin,DiagEvent::FileBasePressEnd,0,0,{});
        AnimatedTreeWidget::mousePressEvent(e);
    }
}
void FileView::mouseDoubleClickEvent(QMouseEvent *e) {
    auto row = rowAt(e->position().toPoint());
    if (e->button() == Qt::LeftButton && row && row->data(0, ParentNavigationRole).toBool()) {
        navigationPress = true;
        clearSelection();
        selectionModel()->setCurrentIndex(indexFromItem(row), QItemSelectionModel::NoUpdate);
        emit parentNavigationActivated();
        e->accept();
        return;
    }
    AnimatedTreeWidget::mouseDoubleClickEvent(e);
}
void FileView::mouseMoveEvent(QMouseEvent *e) {
    if (navigationPress) {e->accept();return;}
    if (selecting) {
        QRect rect = QRect(origin, e->position().toPoint()).normalized();
        rubber->setGeometry(rect);
        QTreeWidgetItemIterator it(this);
        for (; *it; ++it) {
            if ((*it)->data(0, FileDraftRole).toBool() || (*it)->data(0, ParentNavigationRole).toBool())
                continue;
            auto row=visualItemRect(*it);
            if(!row.isEmpty()) { row.setLeft(0); row.setRight(viewport()->width()-1); }
            if (!row.isEmpty() && row.intersects(rect))
                (*it)->setSelected(true);
            else if (!(e->modifiers() & Qt::ControlModifier))
                (*it)->setSelected(false);
        }
    } else
        AnimatedTreeWidget::mouseMoveEvent(e);
}
void FileView::mouseReleaseEvent(QMouseEvent *e) {
    DiagnosticScope release(DiagEvent::FileReleaseBegin,DiagEvent::FileReleaseEnd,0,0,{});
    selecting = false;
    rubber->hide();
    if (navigationPress) {navigationPress=false;e->accept();return;}
    AnimatedTreeWidget::mouseReleaseEvent(e);
}
QTreeWidgetItem *FileView::rowAt(QPoint point) const {
    if (!viewport()->rect().contains(point)) return nullptr;
    auto index = indexAt(point);
    // indexAt handles every column. Also cover branch indentation and the row's
    // unused horizontal area when no section is under the pointer.
    if (!index.isValid()) for (int col = 0; col < columnCount(); ++col) {
        const int left = qMax(0, header()->sectionViewportPosition(col));
        const int right = qMin(viewport()->width()-1, header()->sectionViewportPosition(col)+columnWidth(col)-1);
        if (left <= right) { index = indexAt(QPoint((left+right)/2, point.y())); if(index.isValid()) break; }
    }
    return itemFromIndex(index);
}
UploadTarget FileView::uploadTarget(QPoint point) const {
    if (!viewport()->rect().contains(point)) return {{}, "不在文件列表内"};
    auto item = rowAt(point);
    if (item) {
        if (item->data(0, ParentNavigationRole).toBool())
            return {{}, "上一级为导航项，不能作为上传目标"};
        const auto info = item->data(0, Qt::UserRole).toMap();
        if (item->data(0, FileDraftRole).toBool() || !info.contains("directory") || info["path"].toString().isEmpty())
            return {{}, "此项尚未载入或正在新建，不能作为上传目标"};
        if (info["link"].toBool()) return {{}, "链接不能作为上传目标，请打开实际目录"};
        const auto path=info["path"].toString();
        if (!path.startsWith('/')) return {{}, "无法识别远端目标路径"};
        return {info["directory"].toBool() ? path : remoteJoin(path, ".."), {}};
    }
    if (!uploadRoot.startsWith('/')) return {{}, "当前目录尚未载入，不能上传"};
    return {uploadRoot,{}};
}
void FileView::setUploadRoot(const QString &path) {
    uploadRoot=path; clearUploadPreview();
}
UploadTarget FileView::internalMoveTarget(QPoint point) const {
    auto row=rowAt(point);
    if(row && row->data(0,ParentNavigationRole).toBool()) {
        if(uploadRoot=="/" || !uploadRoot.startsWith('/') || QDir::cleanPath(uploadRoot)!=uploadRoot)
            return {{},"当前目录尚未载入，不能移动到上一级"};
        return {remoteJoin(uploadRoot,".."),{}};
    }
    return uploadTarget(point);
}
QStringList FileView::localUploadPaths(const QMimeData *mime) const {
    if (!mime || !mime->hasUrls() || mime->urls().isEmpty() || mime->urls().size()>256) return {};
    QStringList paths;
    for (const auto &url : mime->urls()) {
        const QFileInfo f(url.toLocalFile());
        if (!url.isLocalFile() || !f.exists() || f.isSymLink() || f.isJunction() || (!f.isFile() && !f.isDir())) return {};
        if (!paths.contains(f.absoluteFilePath())) paths << f.absoluteFilePath();
    }
    return paths;
}
void FileView::clearUploadPreview() {
    const bool hadPreview=uploadPreview;
    uploadPreview=false; uploadAllowed=false; internalPreview=false; uploadRow={}; uploadMessage.clear(); viewport()->update();
    if(hadPreview)emit uploadPreviewTarget({},false);
}
bool FileView::previewUpload(QPoint point, const QMimeData *mime, Qt::DropActions actions) {
    const auto target=uploadTarget(point);
    uploadPreview=true;
    internalPreview=false;
    uploadAllowed=actions.testFlag(Qt::CopyAction) && !localUploadPaths(mime).isEmpty() && target.valid();
    uploadMessage=uploadAllowed ? "上传到 " + target.path : !target.error.isEmpty() ? target.error : "仅支持复制本地普通文件或目录";
    auto item=rowAt(point);
    if(item && !item->data(0,Qt::UserRole).toMap()["directory"].toBool())item=item->parent();
    uploadRow=uploadAllowed && item && target.path!=uploadRoot &&
              item->data(0,Qt::UserRole).toMap()["path"].toString()==target.path
              ? QPersistentModelIndex(indexFromItem(item)) : QPersistentModelIndex();
    emit uploadPreviewTarget(target.path,uploadAllowed);
    viewport()->update(); return uploadAllowed;
}
bool FileView::previewInternalMove(QPoint point,const QMimeData *mime,Qt::DropActions actions) {
    const auto target=internalMoveTarget(point);const auto token=internalFileMoveToken(mime);
    if(actions.testFlag(Qt::MoveAction) && target.valid() && internalMoveValid && internalMoveValid(token,target.path) &&
       internalMoveNoop && internalMoveNoop(token,target.path)) {clearUploadPreview();return true;}
    uploadPreview=true;internalPreview=true;
    uploadAllowed=actions.testFlag(Qt::MoveAction) && target.valid() && internalMoveValid && internalMoveValid(token,target.path);
    uploadMessage=uploadAllowed?"移动到 "+target.path:target.valid()?"不能移动到此目标":target.error;
    auto item=rowAt(point);if(item && !item->data(0,Qt::UserRole).toMap()["directory"].toBool())item=item->parent();
    auto nav=rowAt(point);
    if(nav && nav->data(0,ParentNavigationRole).toBool())item=nav;
    uploadRow=uploadAllowed && item && (item->data(0,ParentNavigationRole).toBool() || item->data(0,Qt::UserRole).toMap()["path"].toString()==target.path)
        ?QPersistentModelIndex(indexFromItem(item)):QPersistentModelIndex();
    emit uploadPreviewTarget(target.path,uploadAllowed);viewport()->update();return uploadAllowed;
}
static bool hasInternalMoveFormat(const QMimeData *mime) {
    return mime && (mime->hasFormat(QString::fromLatin1(internalFileMoveMimeType())) ||
                    mime->hasFormat(QStringLiteral("application/x-qt-windows-mime;value=\"RabbitShell.InternalFileMove.v1\"")));
}
void FileView::dragEnterEvent(QDragEnterEvent *e) {
    const auto token=internalFileMoveToken(e->mimeData());
    if(hasInternalMoveFormat(e->mimeData())) {
        if(token.isEmpty() || !e->possibleActions().testFlag(Qt::MoveAction) || !internalMoveSourceValid || !internalMoveSourceValid(token)) {
            clearUploadPreview();e->ignore();return;
        }
        // Establish Qt's viewport drag target independently of the entry row.
        // Hover/drop reject strict descendants and invalid paths. Directory-self
        // and same-parent no-ops accept without preview or a business operation.
        previewInternalMove(e->position().toPoint(),e->mimeData(),e->possibleActions());
        e->setDropAction(Qt::MoveAction);e->accept();return;
    }
    if (previewUpload(e->position().toPoint(),e->mimeData(),e->possibleActions())) { e->setDropAction(Qt::CopyAction); e->accept(); } else e->ignore();
}
void FileView::dragMoveEvent(QDragMoveEvent *e) {
    if(hasInternalMoveFormat(e->mimeData()) && internalFileMoveToken(e->mimeData()).isEmpty()){clearUploadPreview();e->ignore();return;}
    if(!internalFileMoveToken(e->mimeData()).isEmpty()) {if(previewInternalMove(e->position().toPoint(),e->mimeData(),e->possibleActions())){e->setDropAction(Qt::MoveAction);e->accept();}else e->ignore();return;}
    if (previewUpload(e->position().toPoint(),e->mimeData(),e->possibleActions())) { e->setDropAction(Qt::CopyAction); e->accept(); } else e->ignore();
}
void FileView::dragLeaveEvent(QDragLeaveEvent *e) { clearUploadPreview(); e->accept(); }
bool FileView::viewportEvent(QEvent *event) {
    if (event->type()==QEvent::Leave || event->type()==QEvent::Hide || event->type()==QEvent::Destroy) clearUploadPreview();
    return AnimatedTreeWidget::viewportEvent(event);
}
QRect FileView::uploadTargetRect() const {
    if(!uploadPreview || !uploadAllowed)return {};
    if(internalPreview) {if(!uploadRow.isValid())return {};auto row=visualRect(uploadRow);row.setLeft(1);row.setRight(viewport()->width()-2);return row.intersected(viewport()->rect());}
    if(!uploadRow.isValid())return viewport()->rect().adjusted(1,1,-1,-1);
    const QModelIndex root=uploadRow;
    auto inside=[&](QModelIndex index){
        for(;index.isValid();index=index.parent())if(index.siblingAtColumn(0)==root)return true;
        return false;
    };
    QModelIndex index=root;QRect bounds=visualRect(index);
    if(bounds.isEmpty())return {};
    // If the parent is above the viewport, start at the first visible row.
    // indexBelow walks Qt's visible rows only; never scan an offscreen subtree.
    if(bounds.bottom()<0) {auto first=rowAt(QPoint(1,0));if(!first)return {};index=indexFromItem(first);bounds={};}
    for(;index.isValid() && inside(index);index=indexBelow(index)) {
        const auto row=visualRect(index);if(row.top()>=viewport()->height())break;
        bounds=bounds.isEmpty()?row:bounds.united(row);
    }
    bounds.setLeft(1);bounds.setRight(viewport()->width()-2);
    return bounds.intersected(viewport()->rect().adjusted(1,1,-1,-1));
}
void FileView::paintEvent(QPaintEvent *event) {
    AnimatedTreeWidget::paintEvent(event);
    if (!uploadPreview) return;
    const auto color=uploadAllowed ? colors().accent : colors().red;
    QPainter p(viewport()); p.setRenderHint(QPainter::Antialiasing);
    const auto target=uploadTargetRect();
    if(!target.isEmpty()) {
        p.setPen(QPen(color,uiPx(2)));auto fill=color;fill.setAlpha(16);p.setBrush(fill);p.drawRoundedRect(target,uiPx(4),uiPx(4));
    }
    QRect label( uiPx(6),viewport()->height()-uiPx(36),viewport()->width()-uiPx(12),uiPx(30));
    p.setBrush(colors().background);p.setPen(color);p.drawRoundedRect(label,uiPx(4),uiPx(4));
    p.drawText(label.adjusted(uiPx(7),0,-uiPx(7),0),Qt::AlignVCenter,fontMetrics().elidedText(uploadMessage,Qt::ElideMiddle,label.width()-uiPx(14)));
}
void FileView::dropEvent(QDropEvent *e) {
    const auto token=internalFileMoveToken(e->mimeData());
    if(hasInternalMoveFormat(e->mimeData()) && token.isEmpty()){clearUploadPreview();e->ignore();return;}
    if(!token.isEmpty()) {
        const auto target=internalMoveTarget(e->position().toPoint());
        const bool allowed=target.valid() && e->possibleActions().testFlag(Qt::MoveAction) && internalMoveValid && internalMoveValid(token,target.path);
        const bool noop=allowed && internalMoveNoop && internalMoveNoop(token,target.path);
        clearUploadPreview();if(allowed){if(!noop)emit internalMoveDropped(token,target.path);e->setDropAction(Qt::MoveAction);e->accept();}else e->ignore();return;
    }
    const auto paths=localUploadPaths(e->mimeData()); const auto target=uploadTarget(e->position().toPoint());
    clearUploadPreview();
    if (!paths.isEmpty() && target.valid() && e->possibleActions().testFlag(Qt::CopyAction)) {
        e->setDropAction(Qt::CopyAction);e->accept();emit uploadDropped(paths,target.path);
    } else { e->ignore(); QToolTip::showText(mapToGlobal(e->position().toPoint()),target.valid()?"仅支持复制本地普通文件或目录":target.error,this); }
}
Workspace::Workspace(Profile p, std::optional<QByteArray> secret, bool demonstration, QWidget *parent,
                     ConnectionRepository *store, DirectoryBookmarkStore *bookmarks)
    : QWidget(parent), profile(std::move(p)), credential(std::move(secret)), repository(store), bookmarkStore(bookmarks),
      rememberCredential(profile.rememberSecret), demo(demonstration) {
    setProperty("rabbitUi",true);
    layoutPersistenceEnabled = !demonstration;
    buildUi();
    if(repository){
        connect(repository,&ConnectionRepository::invalidated,this,[this](const QString &id){
            if(id==profile.id)stopServerClock();
        });
        connect(repository,&ConnectionRepository::idle,this,[this]{ensureServerClock();});
    }
    if(bookmarkStore){
        connect(bookmarkStore,&DirectoryBookmarkStore::saveFailed,this,[this](const QString &error){bookmarkStatus->setText(error);bookmarkStatus->show();});
        connect(bookmarkStore,&DirectoryBookmarkStore::changed,this,[this](const QString &p){if(p!=profile.id)return;if(bookmarkIntent){const auto b=bookmarkStore->entry(p,bookmarkIntent->id);if(!b||b->path!=bookmarkIntent->path)cancelBookmarkIntent();}syncBookmarkGroups();});
    }
    connect(files,&FileView::remoteDragRequested,this,&Workspace::dragFiles,Qt::QueuedConnection);
    files->internalMoveValid=[this](const QByteArray &token,const QString &target){return validInternalDrag(token,target);};
    files->internalMoveNoop=[this](const QByteArray &token,const QString &target){return internalDragNoop(token,target);};
    files->internalMoveSourceValid=[this](const QByteArray &token){return validInternalDragSource(token);};
    connect(files,&FileView::internalMoveDropped,this,&Workspace::acceptInternalDrag);
    connect(&UiZoom::instance(),&UiZoom::changed,this,[this]{
        sizeTerminalManager();updateTerminalRowActions();
        for(QTreeWidgetItemIterator it(terminalManager);*it;++it)if((*it)->data(0,Qt::UserRole).isValid())(*it)->setSizeHint(0,QSize(0,uiPx(28)));
        if(terminals.contains(focusedTerm))currentTerminal->setText(currentTerminal->fontMetrics().elidedText(terminalLabel(terminals[focusedTerm],true),Qt::ElideRight,uiPx(200)).replace("&","&&"));
        if(entryDraft.editor && entryDraft.row.isValid())files->itemFromIndex(entryDraft.row)->setSizeHint(0,QSize(0,qMax(uiPx(40),entryDraft.editor->sizeHint().height()+uiPx(12))));
        QResizeEvent event(size(),size());resizeEvent(&event);
    });
    connect(&UiPreferences::instance(),&UiPreferences::defaultsRestored,this,&Workspace::resetLocalPreferences);
    transferIdleTimer.setSingleShot(true);
    transferIdleTimer.setInterval(60000);
    connect(&transferIdleTimer, &QTimer::timeout, this, [this] {
        if (!activeTransfer && transferSession) {
            transferRetiring = true;
            transferStopping = true;
            transferReady = false;
            transferSession->stop();
        }
    });
    if(auto lock=AppLock::instance())connect(lock,&AppLock::lockedChanged,this,[this](bool locked){
        if(!locked){
            ensureServerClock();
            if(deferredConflict){auto pending=*deferredConflict;deferredConflict.reset();if(pending.worker)showUploadConflict(pending.worker,pending.id,pending.nonce,pending.path,pending.canOverwrite);}
            if(!transferQueue.isEmpty())ensureTransferSession();return;
        }
        cancelBookmarkIntent();for(auto weak:fileDrags)if(auto batch=weak.lock())batch->cancel();cancelQueuedTunnels("应用已锁定，请解锁后手动启动。");
        servicesReading=false;preparingCredentials=false;
        if(mainPending&&!online)cancelMainConnection();
    });
    refreshTheme();
    if (demo)
        demoData();
    else
        QTimer::singleShot(0, this, &Workspace::reconnect);
    restoreWorkspaceLayout();
    restoringWorkspaceLayout = false;
    for (auto group : editArea->groups()) hadEditorPages |= group->count() > 0;
}
Workspace::~Workspace() {
    saveWorkspaceLayout();
    emit aboutToShutdown();
    stopListening("工作区已关闭");if(listeningSession)listeningSession->wait();
    cancelFileDrags();
    cancelClipboardJobs();
    stopServices();stopListening("连接已断开或身份已变化；上次端口结果已过期");
    for(auto s:serviceWorkers)if(s)s->wait();
    cancelEntryDraft();
    for (auto w : floating)
        delete w;
    if (session) {
        session->stop();
        session->wait();
    }
    if (transferSession) {
        transferSession->stop();
        transferSession->wait();
    }
    for (auto id : clipboardCompletions.keys()) completeClipboardJob(id, {}, "工作区已关闭");
    if (tunnelService) {tunnelService->stopAll();delete tunnelService;tunnelService=nullptr;}
    if (credential)
        credential->fill('\0');
}
void Workspace::buildUi() {
    auto outer = new QVBoxLayout(this);
    uiMargins(outer,0,0,0,0);
    uiSpacing(outer,0);
    auto body = new PanelSplitter;
    outer->addWidget(body, 1);
    auto left = new QWidget;
    auto leftLayout = new QHBoxLayout(left);
    uiMargins(leftLayout,0,0,0,0);
    uiSpacing(leftLayout,0);
    auto rail = new QFrame;
    rail->setObjectName("rail");
    uiFixedWidth(rail,44);
    setFrameRole(rail, "rail");
    auto railLayout = new QVBoxLayout(rail);
    uiMargins(railLayout,0,2,0,0);uiSpacing(railLayout,0);
    auto status = iconButton(Icon::Status, "服务器状态", nullptr, IconSize::Activity);
    auto explorer = iconButton(Icon::Explorer, "资源管理", nullptr, IconSize::Activity);
    auto activity = new QButtonGroup(this);
    activity->setExclusive(true);
    for (auto b : {status, explorer}) { b->setCheckable(true); activity->addButton(b); }
    explorer->setChecked(true);
    status->setAccessibleName("服务器状态");
    explorer->setAccessibleName("资源管理");
    railLayout->addWidget(status);
    railLayout->addWidget(explorer);
    railLayout->addStretch();
    leftLayout->addWidget(rail);
    sidebar = new QStackedWidget;
    connect(sidebar,&QStackedWidget::currentChanged,this,[status,explorer](int index) {
        status->setChecked(index==0); explorer->setChecked(index==1);
    });
    auto railEdge=new QWidget;setFrameRole(railEdge,"divider");uiFixedWidth(railEdge,1);leftLayout->addWidget(railEdge);
    leftLayout->addWidget(sidebar, 1);
    body->addWidget(left);
    auto statusPage = new QWidget;
    statusPage->setObjectName("side");
    auto sl = new QVBoxLayout(statusPage);
    uiMargins(sl,12,12,12,12);uiSpacing(sl,10);
    auto card = [](QWidget *content) {
        auto frame = new QFrame;setFrameRole(frame,"card");frame->setProperty("statusCard",true);
        auto layout = new QVBoxLayout(frame);uiMargins(layout,12,9,12,9);uiSpacing(layout,0);
        layout->addWidget(content);return frame;
    };
    serverSummary = new QLabel(profile.name);
    serverSummary->setWordWrap(true);serverSummary->setTextFormat(Qt::PlainText);
    serverSummary->setObjectName("serverIdentity");
    bindUiFont(serverSummary,13,QFont::Medium);
    auto identityBody=new QWidget;auto identityLayout=new QVBoxLayout(identityBody);uiMargins(identityLayout,0,0,0,0);uiSpacing(identityLayout,3);identityLayout->addWidget(serverSummary);
    serverPlatform=new QLabel("等待服务器信息");serverArchitecture=new QLabel("—");
    for(auto label:{serverPlatform,serverArchitecture}){label->setTextFormat(Qt::PlainText);label->setWordWrap(true);label->setProperty("muted",true);bindUiFont(label,12);identityLayout->addWidget(label);}
    auto identity=card(identityBody);uiMinimumHeight(identity,76);sl->addWidget(identity);
    resourceSummary = new ResourceSummary;sl->addWidget(resourceSummary);
    auto networkCard=new QFrame;setFrameRole(networkCard,"card");networkCard->setProperty("statusCard",true);
    auto networkLayout=new QVBoxLayout(networkCard);uiMargins(networkLayout,12,8,12,8);uiSpacing(networkLayout,4);
    auto networkTitle=new QLabel("网络传输");bindUiFont(networkTitle,13,QFont::Medium);networkLayout->addWidget(networkTitle);
    nic = new StyledComboBox;bandHeight(nic,28);
    preferredNic = QSettings().value("network/" + profile.id + "/interface").toString();
    nic->setAccessibleName("监测网卡");networkLayout->addWidget(nic);
    networkRate = new NetworkRateSummary;networkLayout->addWidget(networkRate);
    networkPlot = new NetworkPlot;networkLayout->addWidget(networkPlot);
    auto legend = new QLabel;
    legend->setObjectName("networkLegend");bindUiFont(legend,12);
    legend->setTextFormat(Qt::RichText);networkLayout->addWidget(legend);
    networkCard->setObjectName("serverNetworkCard");
    miniProcesses = new ProcessTable(true);
    auto miniHead=new QWidget;bandHeight(miniHead,32);auto processHeader=new QHBoxLayout(miniHead);
    uiMargins(processHeader,12,2,8,2);uiSpacing(processHeader,0);
    processHeader->addWidget(new QLabel("进程信息"),1);
    auto all = iconButton(Icon::Expand,"查看全部进程");processHeader->addWidget(all);
    auto miniCard=processCard(miniProcesses,miniHead);
    UiZoom::instance().bind(miniCard,"height",[this,miniCard]{miniCard->setFixedHeight(uiPx(32)+miniProcesses->horizontalHeader()->height()+5*miniProcesses->verticalHeader()->defaultSectionSize()+uiPx(2)+(miniProcesses->horizontalScrollBar()->maximum()>0?miniProcesses->style()->pixelMetric(QStyle::PM_ScrollBarExtent):0));});
    connect(miniProcesses->horizontalScrollBar(),&QScrollBar::rangeChanged,miniCard,[this,miniCard]{miniCard->setFixedHeight(uiPx(32)+miniProcesses->horizontalHeader()->height()+5*miniProcesses->verticalHeader()->defaultSectionSize()+uiPx(2)+(miniProcesses->horizontalScrollBar()->maximum()>0?miniProcesses->style()->pixelMetric(QStyle::PM_ScrollBarExtent):0));});
    sl->addWidget(miniCard);
    miniListening=new ListeningTable(true);
    auto portsHead=new QWidget;bandHeight(portsHead,32);auto portsLayout=new QHBoxLayout(portsHead);uiMargins(portsLayout,12,2,8,2);uiSpacing(portsLayout,0);
    portsLayout->addWidget(new QLabel("监听端口"),1);miniListeningState=new QLabel("待采集");miniListeningState->setProperty("muted",true);bindUiFont(miniListeningState,12);portsLayout->addWidget(miniListeningState);auto allListening=iconButton(Icon::Expand,"查看全部监听端口");portsLayout->addWidget(allListening);
    auto listeningCard=processCard(miniListening,portsHead);
    UiZoom::instance().bind(listeningCard,"height",[this,listeningCard]{listeningCard->setFixedHeight(uiPx(32)+miniListening->horizontalHeader()->height()+5*miniListening->verticalHeader()->defaultSectionSize()+uiPx(2));});
    sl->addWidget(listeningCard);sl->addWidget(networkCard);sl->addStretch();connect(allListening,&QToolButton::clicked,this,&Workspace::showListeningPage);
    auto statusScroll = new QScrollArea;
    statusScroll->setWidgetResizable(true);
    statusScroll->setFrameShape(QFrame::NoFrame);
    statusScroll->setWidget(statusPage);
    auto statusPanel=new QWidget;auto sp=new QVBoxLayout(statusPanel);uiMargins(sp,0,0,0,0);uiSpacing(sp,0);
    QHBoxLayout *statusHead;auto statusBand=rowWidget(&statusHead);statusBand->setObjectName("statusHeader");
    statusHead->addWidget(new QLabel("服务器状态"),1);sp->addWidget(statusBand);sp->addWidget(statusScroll,1);
    sidebar->addWidget(statusPanel);
    auto filePage = new QWidget;
    filePage->setObjectName("side");
    auto fl = new QVBoxLayout(filePage);
    uiMargins(fl,0,0,0,0);
    uiSpacing(fl,0);
    QHBoxLayout *head;
    auto resourceHead=rowWidget(&head);resourceHead->setObjectName("resourceHeader");fl->addWidget(resourceHead);
    head->addWidget(new QLabel("资源管理"), 1);
    auto newFile = iconButton(Icon::NewFile, "新建文件"), newFolder = iconButton(Icon::NewFolder, "新建目录");
    head->addWidget(newFile);
    head->addWidget(newFolder);
    connect(newFile, &QToolButton::clicked, this, [this] { createEntry(false); });
    connect(newFolder, &QToolButton::clicked, this, [this] { createEntry(true); });
    QHBoxLayout *nav;
    auto navBand=new QWidget;bandHeight(navBand,28);nav=new QHBoxLayout(navBand);uiMargins(nav,7,0,7,0);uiSpacing(nav,0);fl->addWidget(navBand);
    for (const auto &pair : QVector<QPair<Icon, QString>>{
             {Icon::Back, "返回"}, {Icon::Forward, "前进"}, {Icon::Up, "上一级"}, {Icon::Refresh, "刷新"}, {Icon::Home, "主目录"}}) {
        auto b = iconButton(pair.first, pair.second);
        b->setToolTip(pair.second);
        b->setAccessibleName(pair.second);
        nav->addWidget(b);
        connect(b, &QToolButton::clicked, this, [this, label = pair.second] {
            if (label == "返回" && historyIndex > 0) {
                --historyIndex;
                navigate(history[historyIndex], true);
            } else if (label == "前进" && historyIndex + 1 < history.size()) {
                ++historyIndex;
                navigate(history[historyIndex], true);
            } else if (label == "上一级" && directory.startsWith('/') && directory != "/")
                navigate(remoteJoin(directory, ".."));
            else if (label == "刷新")
                refreshFiles();
            else if (label == "主目录")
                navigate(homeDirectory.isEmpty() ? "." : homeDirectory);
        });
    }
    nav->addStretch();
    auto more=iconButton(Icon::More,"更多");more->setObjectName("resourceMore");nav->addWidget(more);
    connect(more,&QToolButton::clicked,this,[this,more] {
        FileContextMenu menu(this);menu.setObjectName("resourceViewMenu");
        auto hidden=menu.addAction(resourceMenuIcon(Icon::Eye),"显示隐藏文件");
        hidden->setCheckable(true);hidden->setChecked(showHiddenFiles);
        connect(hidden,&QAction::toggled,this,[this](bool value) {
            showHiddenFiles=value;QSettings().setValue("files/show-hidden",value);filterFiles();
        });
        auto modified=menu.addAction(resourceMenuIcon(Icon::CalendarClock),"显示修改时间列");
        modified->setCheckable(true);modified->setChecked(!files->isColumnHidden(2));
        connect(modified,&QAction::toggled,files,[this](bool value) {files->setColumnHidden(2,!value);});
        connect(&menu,&QMenu::aboutToShow,&menu,[this,hidden,modified] {
            const QSignalBlocker a(hidden),b(modified);
            hidden->setChecked(showHiddenFiles);modified->setChecked(!files->isColumnHidden(2));
        });
        menu.exec(more->mapToGlobal(QPoint(0,more->height())));
    });
    addressStack = new QStackedWidget;
    addressStack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    breadcrumbs = new QWidget;
    breadcrumbs->setObjectName("breadcrumbs");
    installRoundedSurface(breadcrumbs,RoundedRole::Path);
    breadcrumbs->installEventFilter(this);
    breadcrumbs->setAttribute(Qt::WA_StyledBackground);
    crumbLayout = new QHBoxLayout(breadcrumbs);
    uiMargins(crumbLayout,3,0,28,0);
    uiSpacing(crumbLayout,0);
    pathEdit = new QLineEdit;
    pathEdit->setAccessibleName("目录路径");
    pathEdit->installEventFilter(this);
    addressStack->addWidget(breadcrumbs);
    addressStack->addWidget(pathEdit);
    auto addressRow=new QWidget;auto addressLayout=new QVBoxLayout(addressRow);uiMargins(addressLayout,10,2,10,2);uiSpacing(addressLayout,0);addressLayout->addWidget(addressStack);fl->addWidget(addressRow);
    UiZoom::instance().bind(addressStack,"height",[this]{addressStack->setFixedHeight(qMax(uiPx(28),pathEdit->fontMetrics().height()+uiPx(8)));});
    connect(pathEdit, &QLineEdit::returnPressed, this, [this] {
        auto path = pathEdit->text().trimmed();
        addressStack->setCurrentWidget(breadcrumbs);
        pathEdit->setText(directory);
        if (!path.isEmpty()) navigate(path);
    });
    files = new FileView;
    files->setProperty("sideTree",true);
    files->installEventFilter(this);
    fileFilter = new QLineEdit;
    fileFilter->setPlaceholderText("筛选当前目录文件名");
    fileFilter->setClearButtonEnabled(true);
    showHiddenFiles = QSettings().value("files/show-hidden", true).toBool();
    bandHeight(fileFilter,28);auto filterRow=new QWidget;auto filterLayout=new QVBoxLayout(filterRow);uiMargins(filterLayout,10,2,10,2);uiSpacing(filterLayout,0);filterLayout->addWidget(fileFilter);fl->addWidget(filterRow);
    connect(fileFilter, &QLineEdit::textChanged, this, &Workspace::filterFiles);
    auto explorerSplit=new PanelSplitter(Qt::Vertical);
    explorerSplit->addWidget(files);
    auto bookmarksCard=new QFrame;bookmarksCard->setObjectName("bookmarkPanel");
    auto bookmarksLayout=new QVBoxLayout(bookmarksCard);uiMargins(bookmarksLayout,0,0,0,0);uiSpacing(bookmarksLayout,0);
    auto bookmarkTitle=new QLabel("目录收藏");bookmarkTitle->setTextFormat(Qt::PlainText);
    bandHeight(bookmarkTitle);UiZoom::instance().bind(bookmarkTitle,"margin",[bookmarkTitle]{bookmarkTitle->setContentsMargins(uiPx(12),0,uiPx(10),0);});
    bookmarksLayout->addWidget(bookmarkTitle);
    if(!bookmarkStore){bookmarkStore=new DirectoryBookmarkStore({},this);bookmarkStore->setProfiles({profile.id});}
    bookmarkTree=new BookmarkTree(bookmarkStore,profile.id);bookmarksLayout->addWidget(bookmarkTree,1);
    connect(bookmarkTree,&BookmarkTree::openRequested,this,&Workspace::openBookmark);
    bookmarkStatus=new QLabel;bookmarkStatus->setTextFormat(Qt::PlainText);bookmarkStatus->setWordWrap(true);bookmarkStatus->setProperty("muted",true);
    bookmarkStatus->setText(bookmarkStore->error());bookmarkStatus->setVisible(!bookmarkStore->error().isEmpty());bookmarksLayout->addWidget(bookmarkStatus);
    explorerSplit->addWidget(bookmarksCard);explorerSplit->setSizes({560,180});explorerSplit->setStretchFactor(0,1);
    uiMinimumHeight(bookmarksCard,95);fl->addWidget(explorerSplit,1);
    sidebar->addWidget(filePage);
    sidebar->setCurrentIndex(1);
    connect(status, &QToolButton::clicked, this, [this] { sidebar->setCurrentIndex(0); });
    connect(explorer, &QToolButton::clicked, this, [this] { sidebar->setCurrentIndex(1); });
    auto right = new QWidget;
    auto rl = new QVBoxLayout(right);
    uiMargins(rl,0,0,0,0);
    uiSpacing(rl,0);
    body->addWidget(right);
    body->setObjectName("workspaceSplit");
    body->setStretchFactor(1, 1);
    // Include the activity rail and status-card margins in the minimum. Keep
    // the splitter resizable while giving scaled metric text usable space.
    uiMinimumWidth(left,310);
    body->setSizes({uiPx(345),1094});
    verticalSplit = new PanelSplitter(Qt::Vertical);
    connectionBar=new ConnectionNotice(true,right);rl->addWidget(connectionBar);
    rl->addWidget(verticalSplit);
    connectionLoading=new ConnectionNotice(false,right);right->installEventFilter(this);
    for(auto notice:{connectionBar,connectionLoading}){
        connect(notice->retry,&QPushButton::clicked,this,&Workspace::reconnect);
        connect(notice->cancel,&QPushButton::clicked,this,[this]{if(mainPending)cancelMainConnection();else{connectionDismissed=true;syncConnectionNotice();}});
    }
    editorPanel = new QWidget;
    auto el = new QVBoxLayout(editorPanel);
    uiMargins(el,0,0,0,0);
    uiSpacing(el,0);
    editArea = new EditorArea;
    el->addWidget(editArea, 1);
    verticalSplit->addWidget(editorPanel);
    connect(editArea, &EditorArea::closePageRequested, this, &Workspace::closePage);
    connect(editArea, &EditorArea::movePageRequested, this, [this](QWidget *p) { movePage(p); });
    connect(editArea, &EditorArea::floatPageRequested, this, &Workspace::floatPage);
    connect(editArea, &EditorArea::pagePresented, this, &Workspace::pagePresented);
    connect(editArea, &EditorArea::layoutChanged, this, &Workspace::scheduleEditorEmptyCheck);
    terminalPanel = new QWidget;
    auto tl = new QVBoxLayout(terminalPanel);
    uiMargins(tl,0,0,0,0);
    uiSpacing(tl,0);
    QHBoxLayout *terminalHead;
    auto terminalBand=rowWidget(&terminalHead);terminalBand->setObjectName("terminalHeader");tl->addWidget(terminalBand);uiMargins(terminalHead,4,0,6,0);
    bottomTabs = new QTabBar;
    bottomTabs->setObjectName("bottomPanelTabs");bottomTabs->setExpanding(false);bottomTabs->setDrawBase(false);
    bottomTabs->setProperty("preservePortDraft",true);bottomTabs->addTab("终端");bottomTabs->addTab("端口");bottomTabs->addTab("服务");
    terminalHead->addWidget(bottomTabs);
    terminalHead->addStretch();
    auto newTerm = iconButton(Icon::Add, "新建终端");
    splitTerminal = iconButton(Icon::SplitHorizontal, "拆分终端");
    closeCurrentTerminal = iconButton(Icon::Trash, "关闭终端");
    closeCurrentTerminal->setToolTip("关闭终端（结束会话）");
    auto hide = iconButton(Icon::Collapse, "隐藏面板");
    currentTerminal = iconButton(Icon::Terminal, "当前终端");
    currentTerminal->setObjectName("currentTerminal");
    currentTerminal->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    currentTerminal->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    uiMinimumWidth(currentTerminal,40);
    uiMaximumWidth(currentTerminal,200);
    connect(currentTerminal, &QToolButton::clicked, this, [this] { activateTerminal(focusedTerm, true); });
    maximizeTerminal = iconButton(Icon::PanelMaximize, "最大化面板");
    maximizeTerminal->setObjectName("maximizeTerminalPanel");
    maximizeTerminal->setCheckable(true);
    terminalActions=new QWidget;
    auto actionsLayout=new QHBoxLayout(terminalActions);uiMargins(actionsLayout,0,0,0,0);uiSpacing(actionsLayout,0);
    for (auto b : {newTerm, currentTerminal, splitTerminal, closeCurrentTerminal}) actionsLayout->addWidget(b);
    terminalHead->addWidget(terminalActions);
    servicesPanel=new ServicesPanel(profile.identity(),demo);
    servicesPanel->mutationGuard=[this]{return shuttingDown?QString("工作区正在关闭"):tunnelStartError();};
    servicesPanel->sessionFactory=[this](QObject *owner,Session::Mode mode){return serviceSession(owner,mode);};
    servicesPanel->directory=[this]{return directory;};
    connect(servicesPanel,&ServicesPanel::connectionRequested,this,&Workspace::ensureServices);
    terminalHead->addWidget(servicesPanel->toolbar);servicesPanel->toolbar->hide();
    for (auto b : {maximizeTerminal, hide}) {b->setProperty("preservePortDraft",true);terminalHead->addWidget(b);}
    auto termBody = new PanelSplitter;
    terminalStack = new QStackedWidget;
    terminalManager = new AnimatedTreeWidget;
    terminalManager->setTreeMode(AnimatedTreeWidget::TreeMode::TerminalPeers);
    terminalManager->setObjectName("terminalManager");terminalManager->setProperty("sideTree",true);
    terminalManager->setHeaderHidden(true);
    terminalManager->setContextMenuPolicy(Qt::CustomContextMenu);
    terminalManager->setColumnCount(2);terminalManager->header()->setStretchLastSection(false);
    terminalManager->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    terminalManager->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    UiZoom::instance().bind(terminalManager,"actionsColumn",[this]{terminalManager->header()->setMinimumSectionSize(uiPx(28));terminalManager->header()->resizeSection(1,uiPx(60));});
    uiMaximumWidth(terminalManager,450);
    uiMinimumWidth(terminalManager,150);
    UiZoom::instance().bind(terminalManager,"treeMetrics",[this]{terminalManager->setIndentation(uiPx(24));terminalManager->setIconSize(uiSize(18,18));});
    terminalManager->setSelectionMode(QAbstractItemView::SingleSelection);
    terminalManager->setSelectionBehavior(QAbstractItemView::SelectRows);
    terminalManager->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    terminalManager->viewport()->installEventFilter(this);
    terminalRowActions = new QWidget(terminalManager->viewport());
    terminalRowActions->setObjectName("terminalRowActions");
    auto rowActions = new QHBoxLayout(terminalRowActions);
    uiMargins(rowActions,0,0,0,0);
    uiSpacing(rowActions,0);
    auto rowSplit = iconButton(Icon::SplitHorizontal, "拆分终端", terminalRowActions);
    auto rowClose = iconButton(Icon::Trash, "关闭终端", terminalRowActions);
    rowSplit->setObjectName("splitSelectedTerminal");
    rowClose->setObjectName("closeSelectedTerminal");
    rowClose->setToolTip("关闭终端（结束会话）");
    for (auto button : {rowSplit, rowClose}) {
        button->setFocusPolicy(Qt::NoFocus);
        uiFixedSize(button,28,28);
        rowActions->addWidget(button);
        connect(button, &QToolButton::pressed, this, [this, button] {
            button->setProperty("pressedTerminal", terminalRowId);
        });
    }
    connect(rowSplit, &QToolButton::clicked, this, [this, rowSplit] {
        const int id = rowSplit->property("pressedTerminal").toInt();
        if (terminals.contains(id)) addTerminal(terminals[id].group);
    });
    connect(rowClose, &QToolButton::clicked, this, [this, rowClose] {
        closeTerminal(rowClose->property("pressedTerminal").toInt());
    });
    connect(terminalManager, &QTreeWidget::itemExpanded, this, [this] { updateTerminalRowActions(); });
    connect(terminalManager, &QTreeWidget::itemCollapsed, this, [this] { updateTerminalRowActions(); });
    connect(terminalManager->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] { updateTerminalRowActions(); });
    termBody->addWidget(terminalStack);
    termBody->addWidget(terminalManager);
    termBody->setSizes({924,170});
    termBody->setStretchFactor(0, 1);termBody->setStretchFactor(1,0);
    connect(termBody,&QSplitter::splitterMoved,this,[this]{if(!UiZoom::instance().changing()&&terminalManager->isVisible())terminalManagerBaseWidth=uiBase(terminalManager->width());});
    terminalManager->hide();
    bottomContents=new QStackedWidget;bottomContents->addWidget(termBody);
    portsPanel=new PortsPanel(profile.id);
    connect(portsPanel,&PortsPanel::configurationChanged,this,&Workspace::tunnelCountsChanged);
    connect(portsPanel,&PortsPanel::tunnelRunningChanged,this,&Workspace::tunnelCountsChanged);
    portsPanel->startGuard=[this]{return tunnelStartError();};
    bottomContents->addWidget(portsPanel);tl->addWidget(bottomContents,1);
    bottomContents->addWidget(servicesPanel);
    connect(bottomTabs,&QTabBar::currentChanged,this,[this](int index){bottomContents->setCurrentIndex(index);terminalActions->setVisible(index==0);servicesPanel->toolbar->setVisible(index==2);});
    connect(portsPanel,&PortsPanel::startRequested,this,&Workspace::requestTunnelStart);
    connect(portsPanel,&PortsPanel::stopRequested,this,&Workspace::stopTunnel);
    connect(portsPanel,&PortsPanel::restartRequested,this,&Workspace::restartTunnel);
    verticalSplit->addWidget(terminalPanel);
    verticalSplit->setSizes({560,280});
    verticalSplit->setStretchFactor(0, 2);
    verticalSplit->setStretchFactor(1, 1);
    connect(verticalSplit, &QSplitter::splitterMoved, this, [this] {
        if (restoringWorkspaceLayout || UiZoom::instance().changing() || terminalMaximized || terminalPanel->isHidden()) return;
        const auto sizes = verticalSplit->sizes();
        if (sizes.size() == 2 && sizes[0] + sizes[1] > 0) {
            savedTerminalRatio = qreal(sizes[1]) / (sizes[0] + sizes[1]);
            savedTerminalHeight = sizes[1];
            layoutReset = false;
        }
    });
    connect(newTerm, &QToolButton::clicked, this, [this] {bottomTabs->setCurrentIndex(0);addTerminal();});
    connect(splitTerminal, &QToolButton::clicked, this,
            [this] { addTerminal(terminals.contains(focusedTerm) ? terminals[focusedTerm].group : 0); });
    connect(closeCurrentTerminal, &QToolButton::clicked, this, [this] { closeTerminal(focusedTerm); });
    connect(hide, &QToolButton::clicked, this, &Workspace::hideTerminalPanel);
    connect(maximizeTerminal, &QToolButton::clicked, this, &Workspace::setTerminalMaximized);
    terminalPanel->installEventFilter(this);
    connect(terminalManager, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        const int id = item ? item->data(0, Qt::UserRole).toInt() : 0;
        if (terminals.contains(id)) activateTerminal(id);
        else updateTerminalRowActions();
    });
    connect(terminalManager, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        const int id = item->data(0, Qt::UserRole).toInt();
        if (terminals.contains(id)) activateTerminal(id, true);
    });
    connect(terminalManager, &QWidget::customContextMenuRequested, this, &Workspace::terminalMenu);
    QHBoxLayout *footer;
    auto footerWidget = new PanelBand(24,true);footer=new QHBoxLayout(footerWidget);uiMargins(footer,10,0,10,0);uiSpacing(footer,8);
    setFrameRole(footerWidget, "footer");
    connectionStatus = new QLabel("SSH · 未连接");
    footer->addWidget(connectionStatus);
    footer->addStretch();
    serverClock = new ServerClock(this);
    footer->addWidget(serverClock);
    auto showTerm = iconButton(Icon::Terminal, "显示底部面板"), tasks = iconButton(Icon::Transfers, "文件任务中心");
    showTerm->setProperty("preservePortDraft",true);
    for(auto b:{showTerm,tasks}){uiFixedSize(b,22,22);uiIconSize(b,16,16);}
    auto scale=new QLabel;scale->setObjectName("footerZoom");bindUiFont(scale,12);UiZoom::instance().bind(scale,"value",[scale]{scale->setText(QString::number(qRound(UiZoom::instance().factor()*100))+"%");});
    footer->addWidget(showTerm);
    footer->addWidget(tasks);footer->addWidget(scale);
    outer->addWidget(footerWidget);
    connect(showTerm, &QToolButton::clicked, terminalPanel, &QWidget::show);
    transferPanel = new QFrame(this);
    transferPanel->setObjectName("side");
    setFrameRole(transferPanel, "transfer");
    auto tr = new QVBoxLayout(transferPanel);
    QHBoxLayout *taskHead;
    tr->addWidget(rowWidget(&taskHead));
    taskHead->addWidget(new QLabel("文件任务中心 · " + profile.name), 1);
    auto closeTasks = iconButton(Icon::Close, "关闭文件任务中心");
    taskHead->addWidget(closeTasks);
    auto taskTabs = new QTabWidget;
    uploadList = new QTreeWidget;
    downloadList = new QTreeWidget;
    for (auto list : {uploadList, downloadList}) {
        list->setHeaderLabels({"文件", "进度"});
        list->setRootIsDecorated(false);
        list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        list->header()->setSectionResizeMode(1, QHeaderView::Fixed);
        UiZoom::instance().bind(list,"progressColumn",[list]{list->header()->resizeSection(1,uiPx(178));});
        list->setTextElideMode(Qt::ElideMiddle);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(list, &QWidget::customContextMenuRequested, this,
                [this, list](QPoint p) { transferMenu(list, p); });
    }
    taskTabs->addTab(uploadList, "上传");
    taskTabs->addTab(downloadList, "下载");
    tr->addWidget(taskTabs, 1);
    taskSummary = new QLabel("暂无传输任务");
    tr->addWidget(taskSummary);
    transferPanel->hide();
    connect(tasks, &QToolButton::clicked, this, [this] {
        transferPanel->setVisible(!transferPanel->isVisible());
        transferPanel->raise();
    });
    connect(closeTasks, &QToolButton::clicked, transferPanel, &QWidget::hide);
    connect(files, &FileView::parentNavigationActivated, this, [this] {
        diagnose(DiagEvent::FileActivate,session?session->diagnosticTag:0,0,{},0);
        if (directory.startsWith('/') && directory != "/" && (demo || (online && session)))
            navigate(remoteJoin(directory, ".."));
    });
    connect(files, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *i, int) {
        if (i->data(0, ParentNavigationRole).toBool()) return;
        if (i->data(0, FileDraftRole).toBool() || !i->data(0, Qt::UserRole).isValid())
            return;
        auto d = i->data(0, Qt::UserRole).toMap();
        diagnose(DiagEvent::FileActivate,session?session->diagnosticTag:0,0,{},d["directory"].toBool()?1:2);
        if (d["directory"].toBool())
            navigate(d["path"].toString());
        else
            openFile(d["path"].toString());
    });
    connect(files, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *i) {
        DiagnosticScope expanded(DiagEvent::FileExpandedBegin,DiagEvent::FileExpandedEnd,session?session->diagnosticTag:0,0,{});
        if (!i->data(0, DirectoryLoadedRole).toBool())
            requestDirectory(i->data(0, Qt::UserRole).toMap()["path"].toString());
    });
    connect(files, &QWidget::customContextMenuRequested, this, &Workspace::fileMenu);
    connect(files, &FileView::uploadDropped, this,
            [this](const QStringList &p, const QString &d) { queueTransfers(p, true, d); });
    connect(files,&FileView::uploadPreviewTarget,this,[this](const QString &path,bool active){
        const bool root=active && path==directory;
        for(auto widget:{breadcrumbs,static_cast<QWidget *>(pathEdit)}) {
            if(widget->property("uploadTarget").toBool()==root)continue;
            widget->setProperty("uploadTarget",root);widget->style()->unpolish(widget);widget->style()->polish(widget);widget->update();
        }
    });
    auto rename = new QShortcut(QKeySequence(Qt::Key_F2), files);
    rename->setContext(Qt::WidgetWithChildrenShortcut);
    connect(rename, &QShortcut::activated, this, &Workspace::renameEntry);
    connect(all, &QToolButton::clicked, this, &Workspace::showProcessPage);
    connect(nic, &QComboBox::currentTextChanged, this, [this](const QString &n) {
        networkPlot->select(n);
        networkRate->setRates(networkRates.contains(n)?std::optional(networkRates.value(n)):std::nullopt);
    });
    connect(nic, &QComboBox::activated, this, [this](int index) {
        preferredNic = nic->itemText(index);
        QSettings().setValue("network/" + profile.id + "/interface", preferredNic);
    });
    monitorTimer.setInterval(2000);
    connect(&monitorTimer, &QTimer::timeout, this, [this] {
        requestMonitorSnapshot();
        requestListeningSnapshot();
    });
}
void Workspace::showProcessPage() {
        if(OperationGate::locked())return;
        if (!processPage) {
            processPage = new QWidget;
            auto l = new QVBoxLayout(processPage);uiMargins(l,12,8,12,12);uiSpacing(l,8);
            processes = new ProcessTable(false);
            auto filterRow=new QWidget;auto filterLayout=new QHBoxLayout(filterRow);uiMargins(filterLayout,0,0,0,0);uiSpacing(filterLayout,8);
            auto count=new QLabel;count->setProperty("muted",true);bindUiFont(count,12);
            auto filter=new QLineEdit;filter->setObjectName("processFilter");filter->setAccessibleName("筛选已采集进程");filter->setPlaceholderText("筛选名称 / PID / 用户");filter->setClearButtonEnabled(true);bandHeight(filter,28);uiMaximumWidth(filter,360);filterLayout->addWidget(filter,1);filterLayout->addWidget(count);filterLayout->addStretch();l->addWidget(filterRow);
            filter->setToolTip("本地纯文本、不区分大小写；仅筛选本次已采集的前 200 个 CPU 进程");
            processes->countsChanged=[count](int shown,int total){count->setText(QString("已采集进程 %1 / %2").arg(shown).arg(total));};processes->countsChanged(0,0);
            connect(filter,&QLineEdit::textChanged,processes,&ProcessTable::setFilterText);
            l->addWidget(processCard(processes), 1);
        }
        if (groupFor(processPage)) editArea->activatePage(processPage);
        else if (auto window = floating.value(processPage)) {
            pagePresented(processPage);
            window->show(); window->raise(); window->activateWindow();
        } else editArea->addPage(processPage, "进程信息");
        if (demo)
            demoData();
        else if (online && !monitorPending) requestMonitorSnapshot();
}
void Workspace::attachSession(Session *s, bool transfer) {
    if (!transfer) {
        authenticatedFingerprint.clear();
        connect(s,&Session::disconnected,this,[this,s](const QString &reason) {
            if(s!=session)return;
            const auto callbacks=std::exchange(journalCallbacks,{});
            for(const auto &callback:callbacks)callback({},"连接已断开，操作结果待核查："+reason);
        });
        connect(s, &Session::authenticatedHostIdentity, this, [this,s](const QString &fingerprint) {
            if (s == session) authenticatedFingerprint = fingerprint;
        });
        connect(s, &Session::journalUndoApplied, this, [this,s](int, const QVariantMap &effect) {
            if (s == session) applyJournalUndo(effect);
        });
    }
    const auto attempt = mainAttempt;
    const auto attempted = credential;
    const auto generation = credentialGeneration;
    const auto token = repository ? repository->revision(profile.id) : 0;
    const bool remember = rememberCredential;
    const bool shouldPersist = persistCredential;
    connect(s, &Session::authenticated, this, [this, s, transfer, attempt, attempted, generation, token, remember, shouldPersist] {
        if ((!transfer && (s!=session || attempt!=mainAttempt)) || generation != credentialGeneration || shuttingDown || !attempted)
            return;
        passwordWasRejected = false;
        keyNeedsReview = false;
        if (repository && shouldPersist && persistCredential) {
            persistCredential = false;
            auto current = repository->find(profile.id);
            if (current && (remember || current->rememberSecret))
                repository->authenticated(
                    profile, *attempted, remember, token, this, [this, token](bool ok, const QString &error) {
                        if (!ok && repository && token == repository->revision(profile.id))
                            AppMessageBox::warning(this, "密码未保存", error + "\n本次会话仍可使用。");
                    });
        }
    });
    auto rejected = std::make_shared<bool>(false);
    connect(s, &Session::keyAuthenticationNeedsInput, this, [this, s, transfer, attempt, rejected, generation] {
        if(!transfer && (s!=session || attempt!=mainAttempt || shuttingDown))return;
        *rejected = true;
        if (generation != credentialGeneration)
            return;
        keyNeedsReview = true;
        stopServerClock();
    });
    connect(s, &Session::passwordRejected, this, [this, s, transfer, attempt, rejected, generation] {
        if(!transfer && (s!=session || attempt!=mainAttempt || shuttingDown))return;
        *rejected = true;
        if (generation != credentialGeneration)
            return;
        if (credential)
            credential->fill('\0');
        credential.reset();
        passwordWasRejected = true;
        stopServerClock();
        ++credentialGeneration;
    });
    connect(s, &QThread::finished, this, [this, s, attempt, rejected, transfer] {
        if ((!transfer && (s!=session || attempt!=mainAttempt)) || !*rejected || shuttingDown)
            return;
        QTimer::singleShot(0, this, [this, attempt, transfer] {
            if (shuttingDown || (!transfer && attempt!=mainAttempt))
                return;
            if (transfer)
                ensureTransferSession();
            else
                reconnect();
        });
    });
    connect(s, &Session::hostKey, this, [this, s, transfer, attempt, generation, token](const QString &fp, bool changed) {
        if(!transfer && (s!=session || attempt!=mainAttempt || shuttingDown))return;
        QPointer<Session> guarded=s;
        requestHostTrust(this,profile,fp,changed,[this,guarded,transfer,attempt,generation,token]{
            return guarded && (transfer || (guarded==session && attempt==mainAttempt)) && !shuttingDown && generation==credentialGeneration &&
                   (!repository || (repository->revision(profile.id)==token && repository->find(profile.id) &&
                                    repository->find(profile.id)->credentialKey()==profile.credentialKey()));
        },[guarded](bool accepted){if(guarded)guarded->trust(accepted);});
    });
    if (transfer) {
        transferFingerprint.clear();
        connect(s,&Session::authenticatedHostIdentity,this,[this,s](const QString &fingerprint){if(s==transferSession)transferFingerprint=fingerprint;});
        connect(s,&Session::uploadConflict,this,[this,s](int id,quint64 nonce,const QString &path,bool overwrite){showUploadConflict(s,id,nonce,path,overwrite);});
        connect(s,&Session::uploadConflictClosed,this,[this,s](int id,quint64 nonce){
            if(s==transferSession && uploadConflictDialog && uploadConflictDialog->property("job").toInt()==id && uploadConflictDialog->property("nonce").toULongLong()==nonce)uploadConflictDialog->reject();
        });
        connect(s, &Session::connected, this, [this, s] {
            if (!online || shuttingDown || authenticatedFingerprint.isEmpty() || transferFingerprint!=authenticatedFingerprint) {
                if(online && !shuttingDown)taskSummary->setText("传输连接的服务器身份与当前工作区不一致，请重新连接。");
                s->stop();
                return;
            }
            transferReady = true;
            startTransfer();
        });
        connect(s, &Session::disconnected, this, [this](const QString &why) {
            cancelFileDrags();
            cancelClipboardJobs();
            transferReady = false;
            if (activeTransfer) {
                if(transfers.value(activeTransfer).type=="upload") {
                    finishUploadJournalRoot(activeTransfer,true);
                    cancelledTransfers.insert(activeTransfer);
                    if(transferItems.contains(activeTransfer))transferItems[activeTransfer]->setToolTip(1,"连接中断，上传结果待核查；未自动重试。");
                }
                if (clipboardCompletions.contains(activeTransfer)) {
                    const int clipboardId=activeTransfer;
                    QPointer<Session> worker=transferSession;
                    auto finish=[this,clipboardId]{completeClipboardJob(clipboardId,{},"传输连接断开，结果未确认");};
                    if(worker) {
                        connect(worker,&QThread::finished,this,finish,Qt::SingleShotConnection);
                        if(!worker->isRunning())finish();
                    } else finish();
                    cancelledTransfers.insert(clipboardId);
                }
                if(transfers[activeTransfer].options.value("nativeDrop").toBool()){
                    const int nativeId=activeTransfer;auto done=dragCompletions.take(nativeId);dragValid.remove(nativeId);
                    if(done){QPointer<Session> retiring=transferSession;if(retiring&&retiring->isRunning())connect(retiring,&QThread::finished,this,[done]{done("拖出连接已断开，请重新拖出");});else done("拖出连接已断开，请重新拖出");}
                    cancelledTransfers.insert(nativeId);
                }
                if (cancelledTransfers.remove(activeTransfer))
                    transferItems[activeTransfer]->setText(1, "已取消");
                else if (!transferQueue.contains(activeTransfer))
                    transferQueue.prepend(activeTransfer);
                activeTransfer = 0;
            }
            for (int id : transferQueue)
                transferItems[id]->setText(1, pausedTransfers.contains(id) ? "已暂停" : "等待连接");
            if (!transferRetiring)
                taskSummary->setText("传输连接已断开 · 右键可重新连接；未完成文件从头重试。" + why);
            updateTransferBatches();
            emit stateChanged();
        });
        connect(s, &QThread::finished, this, [this, s] {
            if (s != transferSession)
                return;
            transferSession = nullptr;
            transferStopping = false;
            s->deleteLater();
            bool resume = transferRetiring;
            transferRetiring = false;
            if (resume && online && !shuttingDown)
                ensureTransferSession();
        });
        connect(s, &Session::transferProgress, this,
                [this, s](int id, const QString &path, quint64 current, quint64 total) {
                    if (s != transferSession || id != activeTransfer || !transferItems.contains(id)) return;
                    auto item = transferItems[id];
                    auto stats = item->data(1, Qt::UserRole).toMap();
                    stats["progress"] = QString("%1 / %2").arg(humanSize(current), humanSize(total));
                    item->setData(1, Qt::UserRole, stats);
                    item->setToolTip(1, QString("%1\n当前文件 %2 / %3").arg(path, humanSize(current), humanSize(total)));
                    if (!pausedTransfers.contains(id)) item->setText(1, "当前文件 " + stats["progress"].toString() + "\n" +
                        stats.value("speed", "0 B/s").toString());
                });
        connect(s, &Session::transferStatistics, this,
                [this, s](int id, quint64 bytes, qint64 activeMs, double rate, bool) {
                    if (s != transferSession || id != activeTransfer || !transferItems.contains(id)) return;
                    const auto speed = humanSize(quint64(qMax(0.0, rate))) + "/s";
                    auto item = transferItems[id];
                    auto stats = item->data(1, Qt::UserRole).toMap();
                    stats["bytes"] = QVariant::fromValue(bytes); stats["activeMs"] = activeMs; stats["speed"] = speed;
                    item->setData(1, Qt::UserRole, stats);
                    const QString progress = stats.contains("progress") ? "当前文件 " + stats["progress"].toString() : "已传 " + humanSize(bytes);
                    item->setText(1, pausedTransfers.contains(id) ? "已暂停\n0 B/s" : progress + "\n" + speed);
                    taskSummary->setText((transfers[id].type == "upload" ? "↑ 上传 " : "↓ 下载 ") +
                                         (pausedTransfers.contains(id) ? "已暂停 · 0 B/s" : speed));
                });
        connect(s, &Session::transferEntry, this,
                [this,s](int id, const QString &path, const QString &state, const QString &reason) {
                    if(s!=transferSession || id!=activeTransfer)return;
                    auto item = transferItems.value(id);
                    if (!item || item->childCount() >= 50)
                        return;
                    auto child = new QTreeWidgetItem(item, {basename(path), state});
                    child->setToolTip(0, path);
                    child->setToolTip(1, reason);
                });
        connect(s, &Session::completed, this, [this, s](const QString &type, int id, const QVariantMap &result) {
            if(s==transferSession && type=="journalSeal" && uploadJournalSealJobs.remove(id))return;
            if (s != transferSession || id != activeTransfer || (type != "upload" && type != "download" && type != "move"))
                return;
            if (type == "move") {
                if (transferItems.contains(id)) transferItems[id]->setText(1,"已移动");
                completeClipboardJob(id,result,{}); pausedTransfers.remove(id);cancelledTransfers.remove(id);activeTransfer=0;
                trimTransferHistory();updateTransferBatches();startTransfer();emit stateChanged();return;
            }
            if (transferItems.contains(id)) {
                const auto bytes = result["payloadBytes"].toULongLong();
                const auto elapsed = qMax<qint64>(1, result["activeMs"].toLongLong());
                const auto average = humanSize(quint64(double(bytes) * 1000 / elapsed)) + "/s";
                const bool partial = result["failed"].toInt() > 0;
                QString state = partial ? "部分失败 · 可重试" : result["completed"].toInt() == 0 && result["skipped"].toInt() > 0
                    ? "全部跳过" : "已完成";
                transferItems[id]->setText(1, state + "\n" + (partial ? "本次已传平均 " : "平均 ") + average);
                transferItems[id]->setToolTip(
                    1, QString("完成 %1 · 跳过 %2 · 失败 %3\n%4\n最多展示 50 条子文件明细。")
                           .arg(result["completed"].toInt())
                           .arg(result["skipped"].toInt())
                           .arg(result["failed"].toInt())
                           .arg(result["errors"].toStringList().join('\n')) +
                        QString("\n本次实际传输 %1 · 有效耗时 %2 ms（含枚举及提交，不含暂停/排队）")
                            .arg(humanSize(bytes)).arg(result["activeMs"].toLongLong()));
            }
            const bool native=transfers[id].options.value("nativeDrop").toBool();
            if(native&&transferItems.contains(id))transferItems[id]->setText(1,(result["failed"].toInt()?QString("远端下载失败 · 请重新拖出"):QString("远端下载已完成 · 系统负责最终落盘"))+"\n平均下载 "+humanSize(quint64(double(result["payloadBytes"].toULongLong())*1000/qMax<qint64>(1,result["activeMs"].toLongLong())))+"/s");
            auto done=dragCompletions.take(id);dragValid.remove(id);
            if(done)done(result["failed"].toInt()?"远端下载部分失败":QString());
            const bool clipboardTask=clipboardCompletions.contains(id);
            if (!clipboardTask) taskSummary->setText("任务处理完成");
            completeClipboardJob(id,result,{});
            if(type=="upload")finishUploadJournalRoot(id);
            pausedTransfers.remove(id);
            cancelledTransfers.remove(id);
            activeTransfer = 0;
            trimTransferHistory();
            updateTransferBatches();
            startTransfer();
            if(type=="upload")refreshUploaded(transfers[id]);
            emit stateChanged();
        });
        connect(s, &Session::failed, this, [this, s](const QString &, int id, const QString &reason) {
            if(s==transferSession && uploadJournalSealJobs.remove(id)) {taskSummary->setText("上传批次记录待核查："+reason);return;}
            if (s != transferSession || id != activeTransfer) return;
            if(transfers.value(id).type=="upload")refreshUploaded(transfers[id]);
            if (transferItems.contains(id)) {
                transferItems[id]->setText(1, cancelledTransfers.remove(id) ? "已取消" : transfers.value(id).options.value("nativeDrop").toBool()?"拖出下载失败 · 请重新拖出":"失败 · 可重试");
                transferItems[id]->setToolTip(1, reason);
            }
            auto done=dragCompletions.take(id);dragValid.remove(id);if(done)done(reason);
            taskSummary->setText(reason);
            completeClipboardJob(id,{},reason);
            if(transfers.value(id).type=="upload")finishUploadJournalRoot(id);
            pausedTransfers.remove(id);
            activeTransfer = 0;
            trimTransferHistory();
            updateTransferBatches();
            startTransfer();
            emit stateChanged();
        });
        return;
    }
    connect(s,&QThread::finished,this,[this,s]{if(s==session&&!shuttingDown)syncConnectionNotice();});
    connect(s,&Session::phase,this,[this,s,attempt](const QString &phase){
        if(s==session && attempt==mainAttempt && !shuttingDown && !closing)mainConnectionState(phase,true);
    });
    connect(s, &Session::connected, this, [this, s, attempt] {
        if(s!=session || attempt!=mainAttempt || shuttingDown || closing){s->stop();return;}
        const bool restoring=everConnected;
        mainResponseWaiting=false;
        online = true;everConnected=true;listeningAuthBlocked=false;listeningClock.invalidate();
        mainConnectionState("● SSH: " + profile.name + " · 已连接",false);
        if(restoring){
            navigate(directory.isEmpty()?(s->profile.directory.isEmpty()?QString("."):s->profile.directory):directory);
            restoringDirectoryRequest=latestNavigation;restoreTerminalSessions();if(bookmarkIntent)dispatchBookmark();
        }else if(bookmarkIntent)dispatchBookmark();
        else {navigate(s->profile.directory.isEmpty() ? "." : s->profile.directory);addTerminal();}
        monitorTimer.start();
        ensureServerClock();
        ensureTransferSession();
        restorePortRules();
        ensureTunnels();
        emit stateChanged();
    });
    connect(s, &Session::disconnected, this, [this, s, attempt](const QString &why) {
        if (s != session || attempt!=mainAttempt || shuttingDown) return;
        online = false;
        stopServerClock();
        mainResponseWaiting=false;
        cancelClipboardJobs();
        cancelBookmarkIntent();
        rebuildTerminals();
        cancelFileDrags();
        files->setUploadRoot({});
        refreshDirectories.clear();
        cancelEntryDraft();
        ++directoryGeneration;
        directoryRequests.clear();
        newestDirectoryRequest.clear();
        fileMutations.clear();
        transferReady = false;
        if (transferSession) {
            transferStopping = true;
            transferSession->stop();
        }
        monitorTimer.stop();
        stopListening("SSH 已断开；上次端口结果已过期");
        monitorPending = false;
        mainConnectionState("连接失败："+why,false);
        for (auto &t : terminals) {
            if (t.running) {
                t.running = false;
                t.restorePending=true;
                t.view->feed("\r\n[SSH 已断开，历史输出保留；重连不会恢复原进程]\r\n");
            }
        }
        for (auto editor : pendingSaves)
            if (editor)
                editor->saving = false;
        pendingSaves.clear();
        pendingReads.clear();
        pendingConflicts.clear();
        const bool interruptedClose=closing;closing = false;if(interruptedClose)emit closeCancelled();
        closeAfterSave.clear();
        rebuildTerminals();
        emit stateChanged();
    });
    connect(s, &Session::responseWaiting,this,[this,s,attempt](bool waiting){
        if(s!=session||attempt!=mainAttempt||!online)return;
        if(waiting==mainResponseWaiting)return;mainResponseWaiting=waiting;
        if(waiting){connectionStatus->setProperty("beforeWaitText",connectionStatus->text());connectionStatus->setProperty("beforeWaitTip",connectionStatus->toolTip());connectionStatus->setProperty("beforeWaitStyle",connectionStatus->styleSheet());connectionStatus->setText("● SSH · 响应缓慢，等待恢复（会话保留）");connectionStatus->setStyleSheet("color:"+colors().orange.name());connectionStatus->setToolTip("本地等待超时不触发断线；已有终端继续处理，未完成操作不重放。");}
        else {connectionStatus->setText(connectionStatus->property("beforeWaitText").toString());connectionStatus->setToolTip(connectionStatus->property("beforeWaitTip").toString());connectionStatus->setStyleSheet(connectionStatus->property("beforeWaitStyle").toString());}
    });
    connect(s, &Session::terminalOutput, this, [this, s,attempt](int id, const QByteArray &data) {
        if (s==session&&attempt==mainAttempt&&online&&terminals.contains(id))
            terminals[id].view->feed(data);
        s->acknowledge(data.size());
    });
    connect(s, &Session::terminalClosed, this, [this, s,attempt](int id) {
        if (s != session||attempt!=mainAttempt) return;
        if (terminals.contains(id)) {
            terminals[id].running = false;
            terminals[id].restorePending=false;
            terminals[id].view->feed("\r\n[会话已结束]\r\n");
            rebuildTerminals();
        }
    });
    connect(s, &Session::completed, this, [this, s](const QString &type, int id, const QVariantMap &r) {
        if (s != session) return;
        if(journalCallbacks.contains(id)) {auto callback=journalCallbacks.take(id);callback(r,{});return;}
        if (type == "list") {if(!completeBookmark(id,r))renderDirectory(r,id);}
        else if (type == "read") {
            if (pendingConflicts.contains(id)) {
                auto editor = pendingConflicts.take(id);
                if (editor)
                    resolveConflict(editor, r);
                return;
            }
            QString path = r["path"].toString();
            pendingReads.remove(id);
            if (editors.value(path))
                return;
            auto e = new Editor(profile.id, path, r["data"].toByteArray(), r["hash"].toByteArray());
            if (r["link"].toBool() || (r["permissions"].toUInt() & 0222) == 0)
                e->code->setReadOnly(true);
            editors[path] = e;
            editArea->addPage(e, basename(path));
            connect(e, &Editor::saveRequested, this, &Workspace::saveEditor);
            connect(e, &Editor::changed, this, [this, e] {
                updateEditorTitle(e);
                emit stateChanged();
            });
            emit searchDocumentOpened(e);
        } else if (type == "save") {
            auto e = pendingSaves.take(id);
            if (e) {
                e->saved(r["data"].toByteArray(), r["hash"].toByteArray());
                if (closeAfterSave.remove(e) && !e->dirty())
                    closePage(e);
            }
            if (closing && pendingSaves.isEmpty()) {
                closing = false;
                if (!dirty())
                    emit readyToClose();
                else
                    emit closeCancelled();
            }
        } else if (type == "monitor") {
            monitorPending = false;
            monitor(r["data"].toByteArray(), r["latency"].toLongLong());
        } else if (type == "stat")
            properties(r);
        else if (type == "rename") {
            auto old = r["old"].toString(), path = r["path"].toString();
            auto keys = editors.keys();
            for (const auto &key : keys)
                if (key == old || key.startsWith(old + "/")) {
                    auto e = editors.take(key);
                    if (e) {
                        e->setPath(path + key.mid(old.size()));
                        editors[e->path] = e;
                        updateEditorTitle(e);
                    }
                }
            refreshFiles();
        } else if (type == "touch" || type == "mkdir" || type == "delete") {
            const QString path = fileMutations.take(id);
            if (entryDraft.job == id)
                cancelEntryDraft();
            if (!path.isEmpty()) {
                if(type=="delete") {
                    DiagnosticScope mutation(DiagEvent::DeleteUiBegin,DiagEvent::DeleteUiEnd,s->diagnosticTag,id,path);
                    syncFileMutation(path,true,id);
                } else syncFileMutation(path,false,id);
            }
        } else if (type == "chmod")
            requestDirectory(directory);
    });
    connect(s, &Session::failed, this, [this, s](const QString &type, int id, const QString &reason) {
        if(s==session && journalCallbacks.contains(id)) {auto callback=journalCallbacks.take(id);callback({},reason);return;}
        if (s == session) fail(type, id, reason);
    });
}
void Workspace::resetLocalPreferences(){
    QSettings().remove("files/confirmInternalMove");
    preferredNic.clear();nic->setCurrentIndex(nic->count()?0:-1);
    layoutReset = true;
    showHiddenFiles=true;filterFiles();refreshTheme();
}
void Workspace::beginMainConnection(){
    ++mainAttempt;connectionDismissed=false;mainConnectionState("正在连接…",true);
}
void Workspace::mainConnectionState(const QString &phase,bool pending){
    if(!pending&&!online)cancelBookmarkIntent();
    mainPending=pending;mainPhase=phase;connectionStatus->setText(phase);connectionStatus->setToolTip(phase);
    connectionStatus->setStyleSheet("color:"+(pending?colors().orange:online?colors().green:colors().red).name());
    syncConnectionNotice();emit stateChanged();
}
void Workspace::cancelMainConnection(){
    cancelBookmarkIntent();
    ++mainAttempt;preparingCredentials=false;
    if(session)session->stop();
    mainConnectionState("已取消连接",false);
}
void Workspace::syncConnectionNotice(){
    if(!connectionLoading)return;
    const bool available=!shuttingDown&&!connectionDismissed;
    const bool large=available&&mainPending&&!everConnected&&editors.isEmpty()&&terminals.isEmpty();
    connectionLoading->setVisible(large);
    connectionBar->setVisible(available&&!large&&!online&&!mainPhase.isEmpty());
    const bool animate=mainPending&&active&&isVisible();
    connectionLoading->display(mainPhase,mainPending,animate&&large);
    connectionBar->display(mainPhase,mainPending,animate&&!large);
    for(auto notice:{connectionLoading,connectionBar})notice->retry->setEnabled(!preparingCredentials&&(!session||!session->isRunning()));
    if(large){connectionLoading->setGeometry(connectionLoading->parentWidget()->rect());connectionLoading->raise();}
}
void Workspace::showEvent(QShowEvent *e){QWidget::showEvent(e);syncConnectionNotice();if(serverClock)serverClock->setActive(active);ensureServerClock();}
void Workspace::hideEvent(QHideEvent *e){QWidget::hideEvent(e);if(serverClock)serverClock->setActive(false);connectionLoading->display(mainPhase,mainPending,false);connectionBar->display(mainPhase,mainPending,false);}
void Workspace::reconnect() {
    if(OperationGate::locked())return;
    if (demo || shuttingDown || preparingCredentials || (session && session->isRunning()))
        return;
    beginMainConnection();
    if (session) {
        delete session;
        session = nullptr;
    }
    if (repository) {
        if (auto current = repository->find(profile.id)) {
            if (current->credentialKey() != profile.credentialKey()) {
                mainConnectionState("连接身份已更改，请重新打开工作区。",false);
                AppMessageBox::information(
                    this, "连接身份已更改",
                    "请先关闭此旧工作区，再从连接管理打开新配置。旧文件草稿与传输任务仍属于原服务器。");
                return;
            }
            rememberCredential = current->rememberSecret;
        } else
            rememberCredential = false;
    }
    if (keyNeedsReview) {
        promptCredential();
        return;
    }
    if (!credential && profile.authentication != "agent") {
        if (repository && rememberCredential && !passwordWasRejected) {
            preparingCredentials = true;
            mainConnectionState("正在读取系统保存的密码…",true);
            const auto attempt=mainAttempt;
            const auto generation = credentialGeneration;const auto lockEpoch=OperationGate::token();
            repository->read(profile, this, [this, attempt, generation,lockEpoch](SecretResult result, quint64 token) {
                if(attempt!=mainAttempt || shuttingDown||!OperationGate::accepts(lockEpoch))return;
                preparingCredentials = false;
                if (shuttingDown)
                    return;
                if (!repository || repository->revision(profile.id) != token ||
                    generation != credentialGeneration) {
                    mainConnectionState("连接配置已更改，请重新连接。",false);
                    return;
                }
                if (result.status == SecretResult::Found) {
                    credential = std::move(result.value);
                    startSession();
                } else {
                    if (result.status == SecretResult::Error)
                        AppMessageBox::warning(this, "无法读取保存密码", result.error);
                    promptCredential();
                }
            });
        } else
            promptCredential();
        return;
    }
    startSession();
}
void Workspace::updateCredentials(const Profile &updated, const std::optional<QByteArray> &secret) {
    if (updated.credentialKey() != profile.credentialKey()) {
        cancelBookmarkIntent();
        bookmarkGroups.clear();rebuildTerminals();
        cancelFileDrags();
        stopServices();stopListening("连接已断开或身份已变化；上次端口结果已过期");
        ++credentialGeneration;
        cancelQueuedTunnels("连接身份已更改，请关闭旧工作区后重新打开。");
        for(const auto &rule:portsPanel->rules()) {
            if(portsPanel->state(rule.id)=="running")portsPanel->setRuleState(rule.id,"running","连接身份已更改；此转发仍属于旧服务器，请重新打开工作区。");
            else if(wantedTunnels.remove(rule.id) && tunnelService)tunnelService->stopRule(rule.id);
        }
        return;
    }
    cancelFileDrags();
    rememberCredential = updated.rememberSecret;
    persistCredential = false;
    if (secret) {
        listeningAuthBlocked=false;listeningClock.invalidate();
        if(!credential || *credential!=*secret) {
            stopServices();stopListening("连接已断开或身份已变化；上次端口结果已过期");
            if (credential) credential->fill('\0');
            credential = secret;
            ++credentialGeneration;
        }
        passwordWasRejected = false;
        keyNeedsReview = false;
    }
}
void Workspace::promptCredential(bool transfer, bool tunnel, bool service) {
    if(OperationGate::locked())return;
    if (preparingCredentials || shuttingDown)
        return;
    const bool main=!transfer&&!tunnel&&!service;
    if(main){if(!mainPending)beginMainConnection();mainConnectionState("等待输入 SSH 认证信息",true);}
    const auto attempt=mainAttempt;
    preparingCredentials = true;
    QDialog dialog(this);
    dialog.setWindowTitle(profile.authentication == "key" ? "SSH 私钥口令" : "SSH 密码");
    auto layout = new QVBoxLayout(&dialog);
    auto label = new QLabel(profile.identity());
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    layout->addWidget(label);
    if (passwordWasRejected)
        layout->addWidget(new QLabel("服务器拒绝了密码，请重新输入。"));
    if (keyNeedsReview) {
        auto explanation =
            new QLabel("私钥认证未完成。可重新输入口令；也可取消后检查私钥文件、账号与文件权限。");
        explanation->setWordWrap(true);
        layout->addWidget(explanation);
    }
    auto field = new QLineEdit;
    field->setObjectName("loginSecret");
    field->setEchoMode(QLineEdit::Password);
    field->setPlaceholderText(profile.authentication == "key" ? "无加密的私钥可留空" : "密码");
    layout->addWidget(field);
    auto remember = new QCheckBox(profile.authentication == "key" ? "保存私钥口令" : "保存密码");
    remember->setObjectName("loginRememberSecret");
    remember->setChecked(rememberCredential);
    auto current = repository ? repository->find(profile.id) : nullptr;
    remember->setEnabled(current && current->credentialKey() == profile.credentialKey());
    layout->addWidget(remember);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("登录");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(460, 180);
    field->setFocus();
    const auto lockEpoch=OperationGate::token();
    const bool accepted = dialog.exec() == QDialog::Accepted && OperationGate::accepts(lockEpoch);
    if(main && attempt!=mainAttempt)return;
    preparingCredentials = false;
    if (!accepted || shuttingDown) {
        if(main && !shuttingDown)cancelMainConnection();
        if(service)servicesPanel->connectionError("已取消服务连接认证；手动刷新可重新连接。");
        if(!accepted)cancelQueuedTunnels("已取消认证，转发未启动。");
        return;
    }
    if(repository) {
        const auto current=repository->find(profile.id);
        if(!current || current->credentialKey()!=profile.credentialKey()) {
            if(main)mainConnectionState("连接身份已更改，请重新打开工作区。",false);
            cancelQueuedTunnels("连接身份已更改，请重新打开工作区。");return;
        }
    }
    if (repository)
        repository->invalidate(profile.id);
    stopServerClock();
    credential = field->text().toUtf8();
    ++credentialGeneration;
    rememberCredential = remember->isEnabled() && remember->isChecked();
    persistCredential = true;
    if(!tunnel&&!service)startSession(transfer);
    if(service)ensureServices();
    ensureTunnels();
}
void Workspace::startSession(bool transfer) {
    if(OperationGate::locked())return;
    if (shuttingDown)
        return;
    if(!transfer && !mainPending)beginMainConnection();
    Profile target = profile;
    if (repository)
        if (auto current = repository->find(profile.id);
            current && current->credentialKey() == profile.credentialKey())
            target = *current;
    auto s = new Session(target, credential.value_or(QByteArray()), this);
    if (transfer) {
        transferSession = s;
        transferReady = false;
    } else {
        stopServerClock();
        session = s;
        connectionAutoStarted=false;
    }
    attachSession(s, transfer);
    s->start();
    ensureTunnels();
}
void Workspace::disconnectHost() {
    cancelBookmarkIntent();
    if (!hasMainConnection() && !preparingCredentials && !tunnelService && queuedTunnelStarts.isEmpty() && !hasServiceConnection())
        return;
    if (AppMessageBox::question(this, "断开 SSH",
                              profile.identity() + QString("\n终端会话及 %1 条端口转发将结束；未保存草稿和输出会保留。\n未完成传输等").arg(tunnelCount())
                                                   + "待重连；重连后从头重试，主动暂停的任务保持暂停。") !=
        QMessageBox::Yes)
        return;
    cancelFileDrags();
    if(mainPending || preparingCredentials)cancelMainConnection();
    stopTunnels();
    stopServices();stopListening("连接已断开或身份已变化；上次端口结果已过期");
    if (session)
        session->stop();
    if (transferSession)
        transferSession->stop();
    if (credential)
        credential->fill('\0');
    credential.reset();
    ++credentialGeneration;
}
void Workspace::navigate(const QString &path, bool historical) {
    if(OperationGate::locked())return;
    cancelBookmarkIntent();
    if (demo) {
        directory = path == "." ? "/home/demo" : QDir::cleanPath(path);
        if (!historical && (historyIndex < 0 || history.value(historyIndex) != directory)) {
            history = history.mid(0, historyIndex + 1);
            history << directory;
            historyIndex = history.size() - 1;
        }
        syncParentNavigation();
        updateBreadcrumbs();
        return;
    }
    if (!online || !session || path.isEmpty())
        return;
    if (!finishEntryDraft())
        return;
    ++directoryGeneration;
    directoryRequests.clear();
    newestDirectoryRequest.clear();
    refreshDirectories.clear();
    requestDirectory(path, true, historical);
}
void Workspace::refreshFiles() {
    cancelBookmarkIntent();
    if(!online || !session || directory.isEmpty() || demo) return;
    ++directoryGeneration;directoryRequests.clear();newestDirectoryRequest.clear();refreshDirectories.clear();
    for(QTreeWidgetItemIterator it(files);*it;++it) {
        auto item=*it;const auto info=item->data(0,Qt::UserRole).toMap();
        if(!info["directory"].toBool() || info["link"].toBool())continue;
        bool visible=!item->isHidden();
        for(auto parent=item->parent();parent;parent=parent->parent())visible &= parent->isExpanded() && !parent->isHidden();
        if(item->isExpanded() && visible)refreshDirectories.insert(info["path"].toString());
        item->setData(0,DirectoryLoadedRole,false);
    }
    if(entryDraft.waiting)refreshDirectories.insert(entryDraft.parentPath);
    requestDirectory(directory);
}
void Workspace::refreshUploaded(const Job &job) {
    diagnose(DiagEvent::RefreshUpload,session?session->diagnosticTag:0,job.id,job.path);
    QSet<QString> parents{remoteJoin(job.path,"..")};
    for(QTreeWidgetItemIterator it(files);*it;++it) {
        auto item=*it;const auto path=item->data(0,Qt::UserRole).toMap()["path"].toString();
        if((path==job.path || path.startsWith(job.path+'/')) && item->data(0,DirectoryLoadedRole).toBool()) {
            if(item->isExpanded())parents.insert(path);else item->setData(0,DirectoryLoadedRole,false);
        }
    }
    for(const auto &path:parents) if(path==directory || (fileItem(path) && fileItem(path)->data(0,DirectoryLoadedRole).toBool()))requestDirectory(path);
}
QTreeWidgetItem *Workspace::fileItem(const QString &path) const {
    if (path.isEmpty()) return nullptr;
    for (QTreeWidgetItemIterator it(files); *it; ++it)
        if (!(*it)->data(0, FileDraftRole).toBool() && !(*it)->data(0, ParentNavigationRole).toBool() &&
            (*it)->data(0, Qt::UserRole).toMap()["path"].toString() == path)
            return *it;
    return nullptr;
}
void Workspace::syncParentNavigation() {
    QTreeWidgetItem *navigation = nullptr;
    for (int n = files->topLevelItemCount() - 1; n >= 0; --n) {
        auto item = files->topLevelItem(n);
        if (!item->data(0, ParentNavigationRole).toBool()) continue;
        if (navigation) delete item;
        else navigation = item;
    }
    const bool hasParent = directory.startsWith('/') && QDir::cleanPath(directory) != "/";
    if (!hasParent) { delete navigation; return; }
    if (!navigation) {
        navigation = new FileItem(files);
        navigation->setData(0, ParentNavigationRole, true);
        navigation->setFlags(Qt::ItemIsEnabled);
        navigation->setText(0, "..");
        navigation->setToolTip(0, "上一级目录");
    }
    navigation->setIcon(0, icon(Icon::Up));
}
void Workspace::requestDirectory(const QString &path, bool navigation, bool historical) {
    if (!online || !session || path.isEmpty())
        return;
    diagnose(DiagEvent::ListRequest,session->diagnosticTag,0,path);
    auto parent = navigation || path == directory ? nullptr : fileItem(path);
    if (!navigation && path != directory && !parent)
        return;
    Job j;
    j.type = "list";
    j.path = path;
    j.id = nextJob++;
    directoryRequests[j.id] = {path, parent ? QPersistentModelIndex(files->indexFromItem(parent)) : QPersistentModelIndex(),
                                directoryGeneration, navigation, historical};
    newestDirectoryRequest[path] = j.id;
    if (navigation)
        latestNavigation = j.id;
    session->submit(j);
}
void Workspace::renderDirectory(const QVariantMap &r, int id) {
    diagnose(DiagEvent::ListResult,session?session->diagnosticTag:0,id,r["path"].toString(),r["entries"].toList().size());
    if (!directoryRequests.contains(id))
        return;
    const auto request = directoryRequests.take(id);
    if (request.generation != directoryGeneration || newestDirectoryRequest.value(request.path) != id ||
        (request.navigation && id != latestNavigation)) {
        diagnose(DiagEvent::ListDiscard,session?session->diagnosticTag:0,id,request.path);
        return;
    }
    newestDirectoryRequest.remove(request.path);
    QTreeWidgetItem *parent = request.parent.isValid() ? files->itemFromIndex(request.parent) : nullptr;
    if (!request.navigation && request.path != directory && !parent)
        return;
    // Notify the Workspace path frame before tree signals are blocked below.
    files->clearUploadPreview();
    files->invalidateAccessibleRows();
    files->resetTreeTransientState();
    DiagnosticScope render(DiagEvent::RenderBegin,DiagEvent::RenderEnd,session?session->diagnosticTag:0,id,request.path,r["entries"].toList().size());
    auto removeRow=[&](QTreeWidgetItem *item){
        const auto path=item->data(0,Qt::UserRole).toMap()["path"].toString();
        DiagnosticScope removal(DiagEvent::RemoveBegin,DiagEvent::RemoveEnd,session?session->diagnosticTag:0,id,path,item->childCount());
        delete item;
    };
    if (request.navigation) {
        if (directory != r["path"].toString()) {
            cancelEntryDraft();
            files->clear();
        }
        directory = r["path"].toString();
        if (homeDirectory.isEmpty() && profile.directory == ".")
            homeDirectory = directory;
        if (!request.historical && (historyIndex < 0 || history.value(historyIndex) != directory)) {
            history = history.mid(0, historyIndex + 1);
            history << directory;
            historyIndex = history.size() - 1;
        }
        updateBreadcrumbs();
    }
    const bool sorted = files->isSortingEnabled(), updates = files->updatesEnabled();
    const int horizontal = files->horizontalScrollBar()->value(), vertical = files->verticalScrollBar()->value();
    QPersistentModelIndex current = files->currentIndex();
    QSignalBlocker blockedTree(files), blockedSelection(files->selectionModel());
    files->setUpdatesEnabled(false);
    files->setSortingEnabled(false);
    auto container = parent ? parent : files->invisibleRootItem();
    if (!parent) syncParentNavigation();
    QHash<QString, QTreeWidgetItem *> existing;
    for (int n = container->childCount() - 1; n >= 0; --n) {
        auto child = container->child(n);
        if (child->data(0, FileDraftRole).toBool() || child->data(0, ParentNavigationRole).toBool())
            continue;
        const auto path = child->data(0, Qt::UserRole).toMap()["path"].toString();
        if (path.isEmpty())
            removeRow(child);
        else
            existing.insert(path, child);
    }
    for (auto v : r["entries"].toList()) {
        auto data = v.toMap();
        auto i = existing.take(data["path"].toString());
        if (i && (i->data(0, Qt::UserRole).toMap()["directory"].toBool() != data["directory"].toBool() ||
                  i->data(0, Qt::UserRole).toMap()["link"].toBool() != data["link"].toBool())) {
            const auto changedPath = data["path"].toString();
            if (entryDraft.parentPath == changedPath || entryDraft.parentPath.startsWith(changedPath + '/'))
                cancelEntryDraft();
            removeRow(i);
            i = nullptr;
        }
        const bool added = !i;
        if (added)
            i = new FileItem(container);
        itemText(i, 0, data["name"].toString());
        itemText(i, 1, data["directory"].toBool() ? QString() : data["size"].isValid()?humanSize(data["size"].toULongLong()):QString("—"));
        i->setToolTip(1,data["directory"].toBool()?QString():!data["size"].isValid()?QString("服务器未返回大小"):QString("%1 bytes · %2").arg(data["size"].toULongLong()).arg(humanSize(data["size"].toULongLong())));
        i->setData(0, Qt::UserRole, data);
        itemText(i, 2,
                 QDateTime::fromSecsSinceEpoch(data["modified"].toLongLong()).toString("yyyy-MM-dd HH:mm"));
        i->setIcon(0, data["directory"].toBool() ? FileIconProvider::directory() : FileIconProvider::file(data["path"].toString()));
        if (added && data["directory"].toBool() && !data["link"].toBool()) {
            auto loading = new QTreeWidgetItem(i, QStringList{"载入…"});
            loading->setFlags(Qt::NoItemFlags);
        }
    }
    for (auto missing : existing) {
        if (!entryDraft.parentPath.isEmpty()) {
            const auto path = missing->data(0, Qt::UserRole).toMap()["path"].toString();
            if (entryDraft.parentPath == path || entryDraft.parentPath.startsWith(path + '/'))
                cancelEntryDraft();
        }
        removeRow(missing);
    }
    if (parent)
        parent->setData(0, DirectoryLoadedRole, true);
    diagnose(DiagEvent::RenderSort,session?session->diagnosticTag:0,id,request.path);
    files->setSortingEnabled(sorted);
    diagnose(DiagEvent::RenderFilter,session?session->diagnosticTag:0,id,request.path);
    filterFiles();
    files->selectionModel()->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
    diagnose(DiagEvent::RenderLayout,session?session->diagnosticTag:0,id,request.path);
    files->doItemsLayout();
    files->horizontalScrollBar()->setValue(horizontal);
    files->verticalScrollBar()->setValue(vertical);
    files->setUpdatesEnabled(updates);
    const auto refresh=refreshDirectories;
    for(const auto &path:refresh) if(remoteJoin(path,"..")==request.path) {
        refreshDirectories.remove(path);
        if(auto item=fileItem(path);item && item->isExpanded())requestDirectory(path);
    }
    if (entryDraft.waiting && entryDraft.parentPath == request.path) {
        entryDraft.waiting = false;
        showEntryDraft();
    }
}
void Workspace::syncFileMutation(const QString &path, bool removed, int job) {
    if (removed && (entryDraft.parentPath == path || entryDraft.parentPath.startsWith(path + '/')))
        cancelEntryDraft();
    if (removed) {
        if (auto item = fileItem(path)) {
            DiagnosticScope removal(DiagEvent::RemoveBegin,DiagEvent::RemoveEnd,session?session->diagnosticTag:0,job,path,item->childCount());
            delete item;
        }
    } else {
        // A recursive delete can fail after removing some descendants. Refresh only
        // already loaded directories under that target, plus its immediate parent.
        QStringList loaded;
        for (QTreeWidgetItemIterator it(files); *it; ++it) {
            const auto info = (*it)->data(0, Qt::UserRole).toMap();
            const QString candidate = info["path"].toString();
            if ((candidate == path || candidate.startsWith(path + '/')) &&
                (*it)->data(0, DirectoryLoadedRole).toBool())
                loaded.append(candidate);
        }
        for (const auto &candidate : loaded)
            requestDirectory(candidate);
    }
    const auto parentPath = remoteJoin(path, "..");
    requestDirectory(parentPath);
}
void Workspace::filterFiles() {
    // setHidden() has no model-structure signal. Invalidate before filtering,
    // including renders which deliberately block QTreeWidget's own signals.
    files->invalidateAccessibleRows();
    files->clearUploadPreview();
    files->resetTreeTransientState();
    QTreeWidgetItemIterator it(files);
    for (; *it; ++it) {
        if ((*it)->data(0, FileDraftRole).toBool() || (*it)->data(0, ParentNavigationRole).toBool())
            continue;
        const auto name = (*it)->data(0, Qt::UserRole).toMap()["name"].toString();
        const bool hidden = (!showHiddenFiles && name.startsWith('.')) ||
                            (!name.isEmpty() && !name.contains(fileFilter->text(), Qt::CaseInsensitive));
        (*it)->setHidden(hidden);
        if (hidden)
            (*it)->setSelected(false);
    }
}
void Workspace::updateBreadcrumbs() {
    files->setUploadRoot(directory);
    while (auto i = crumbLayout->takeAt(0)) {
        delete i->widget();
        delete i;
    }
    breadcrumbs->setToolTip(directory);
    const auto parts = directory.split('/', Qt::SkipEmptyParts);
    auto add = [this](const QString &label, const QString &path, bool current) {
        if (crumbLayout->count())
            crumbLayout->addWidget(new BreadcrumbSeparator);
        auto b = new BreadcrumbButton(label, path, current);
        crumbLayout->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, path] { navigate(path); });
    };
    add("/", "/", parts.isEmpty());
    QString path;
    int start = qMax(0, parts.size() - 3);
    if (start > 0) {
        crumbLayout->addWidget(new BreadcrumbSeparator);
        auto omitted = new QLabel("…");
        omitted->setProperty("muted", true);
        omitted->setToolTip("省略的父目录：/" + parts.mid(0, start).join('/'));
        omitted->setFocusPolicy(Qt::NoFocus);
        omitted->setAttribute(Qt::WA_TransparentForMouseEvents);
        crumbLayout->addWidget(omitted);
    }
    for (int i = 0; i < parts.size(); ++i) {
        path += "/" + parts[i];
        if (i >= start)
            add(parts[i], path, i == parts.size() - 1);
    }
    crumbLayout->addStretch();
    pathEdit->setText(directory);
    addressStack->setCurrentWidget(breadcrumbs);
}
void Workspace::editDirectoryPath() {
    if(OperationGate::locked())return;
    pathEdit->setText(directory);
    addressStack->setCurrentWidget(pathEdit);
    pathEdit->setFocus();
    pathEdit->selectAll();
}
bool Workspace::eventFilter(QObject *o, QEvent *e) {
    if(connectionLoading && o==connectionLoading->parentWidget() && (e->type()==QEvent::Resize || e->type()==QEvent::LayoutRequest))connectionLoading->setGeometry(connectionLoading->parentWidget()->rect());
    if (o == files && e->type() == QEvent::KeyPress && files->hasFocus() && !entryDraft.editor) {
        const auto key = static_cast<QKeyEvent *>(e);
        if (key->modifiers() == Qt::ControlModifier && !key->isAutoRepeat()) {
            if (key->key()==Qt::Key_X) {copyFileSelection(true);return true;}
            if (key->key()==Qt::Key_C) {copyFileSelection(false);return true;}
            if (key->key()==Qt::Key_V) {pasteFiles();return true;}
            if (key->key()==Qt::Key_Z) {undoLastFileOperation();return true;}
        }
        if (key->key() == Qt::Key_Delete && key->modifiers() == Qt::NoModifier) {
            if (!key->isAutoRepeat()) deleteSelectedFiles();
            return true;
        }
    }
    if (o == breadcrumbs && e->type() == QEvent::MouseButtonPress &&
        static_cast<QMouseEvent *>(e)->button() == Qt::LeftButton) {
        editDirectoryPath();
        return true;
    }
    if (terminalManager && o == terminalManager->viewport() && (e->type() == QEvent::Paint || e->type() == QEvent::Resize))
        updateTerminalRowActions();
    if (o == entryDraft.editor) {
        if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(e)->key() == Qt::Key_Escape) {
            cancelEntryDraft();
            files->setFocus();
            return true;
        }
        if (e->type() == QEvent::FocusOut) {
            // Defer teardown-sensitive focus events; navigation explicitly submits
            // a named draft to its captured target before clearing the row.
            QPointer<QLineEdit> editor = entryDraft.editor;
            QTimer::singleShot(0, this, [this, editor] {
                if (editor && editor == entryDraft.editor)
                    submitEntryDraft();
            });
        }
    }
    if (o == terminalPanel && e->type() == QEvent::Hide && terminalPanel->isHidden() && terminalMaximized)
        setTerminalMaximized(false);
    if (o == pathEdit &&
        (e->type() == QEvent::FocusOut ||
         (e->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(e)->key() == Qt::Key_Escape))) {
        addressStack->setCurrentWidget(breadcrumbs);
        pathEdit->setText(directory);
        if (e->type() == QEvent::KeyPress) return true;
    }
    if (e->type() == QEvent::Close) {
        auto window = qobject_cast<QDialog *>(o);
        if (window) {
            auto page = floating.key(window, nullptr);
            if (page && !closingWindows.contains(page)) {
                e->ignore();
                QTimer::singleShot(0, this, [this, page] { closePage(page); });
                return true;
            }
        }
    }
    return QWidget::eventFilter(o, e);
}
void Workspace::fileMenu(QPoint p) {
    if(OperationGate::locked())return;
    auto item = files->rowAt(p);
    auto validRow = [](QTreeWidgetItem *row) {
        const auto data = row->data(0, Qt::UserRole).toMap();
        return !row->data(0, ParentNavigationRole).toBool() && !row->data(0, FileDraftRole).toBool() && data.contains("directory") &&
               data["path"].toString().startsWith('/');
    };
    if (item && !validRow(item)) return;
    // Blank space belongs to the committed current directory, even if a prior
    // row selection remains visible. Never reuse that selection's operations.
    QList<QTreeWidgetItem *> selected;
    if (item) {
        if (!item->isSelected()) { files->clearSelection(); item->setSelected(true); }
        selected = files->selectedItems();
        for (auto row : selected) if (!validRow(row)) return;
    }
    QStringList paths;
    for (auto row : selected) paths << row->data(0, Qt::UserRole).toMap()["path"].toString();
    const bool single = selected.size() == 1;
    const auto selectedInfo = single ? selected.first()->data(0, Qt::UserRole).toMap() : QVariantMap{};
    const auto target = files->uploadTarget(p);
    const QString createTarget = target.path;
    QList<QVariantMap> clipboardRows;
    for (auto row:selected) clipboardRows<<row->data(0,Qt::UserRole).toMap();
    FileContextMenu m(this);m.setObjectName("resourceFileMenu");
    auto addBookmark = [this, &m](const QString &path) {
        m.addAction(resourceMenuIcon(Icon::BookmarkPlus), "添加到目录收藏", this, [this, path] {
            if (bookmarkIdentityValid() && bookmarkStore->add(profile.id, path)) bookmarkStatus->hide();
            else if (!bookmarkIdentityValid()) { bookmarkStatus->setText("连接已删除或身份已更改，请重新打开工作区。"); bookmarkStatus->show(); }
        })->setEnabled(!DirectoryBookmarkStore::normalizePath(path).isEmpty());
    };
    if (single) {
        if (selectedInfo["directory"].toBool()) {
            m.addAction(resourceMenuIcon(Icon::Terminal), "在此处打开终端", this, [this, path=paths.first()] { addTerminal(0, path); });
            addBookmark(paths.first());
        }
    }
    if (!m.isEmpty()) m.addSeparator();
    auto addUpload = [&] {
        auto uploadAction = m.addAction(resourceMenuIcon(Icon::Upload), "上传文件…", this, [this, createTarget] {
            uploadFilesTo(createTarget);
        });
        uploadAction->setEnabled(target.valid());
        uploadAction->setToolTip(target.valid()?"上传到 "+target.path:target.error);
    };
    if (item) {
        m.addAction(resourceMenuIcon(single && selectedInfo["directory"].toBool()?Icon::DownloadFolder:Icon::DownloadFile), paths.size()>1 ? "下载所选项" : selectedInfo["directory"].toBool() ? "下载目录文件" : "下载",
                    this, [this, paths] { queueTransfers(paths, false); });
        addUpload();
        m.addSeparator();
    }
    m.addAction(resourceMenuIcon(Icon::NewFile), "新建文件", this, [this, createTarget] { createEntry(false, createTarget); })->setEnabled(target.valid());
    m.addAction(resourceMenuIcon(Icon::NewFolder), "新建文件夹", this, [this, createTarget] { createEntry(true, createTarget); })->setEnabled(target.valid());
    if (!item) addUpload();
    m.addSeparator();
    if (item) {
        m.addAction(resourceMenuIcon(Icon::Cut),"剪切\tCtrl+X",this,[this,clipboardRows]{copyFileSelection(true,clipboardRows);})->setEnabled(!clipboardRows.isEmpty() && online);
        m.addAction(resourceMenuIcon(Icon::Copy),"复制\tCtrl+C",this,[this,clipboardRows]{copyFileSelection(false,clipboardRows);})->setEnabled(!clipboardRows.isEmpty() && online);
    }
    auto paste=m.addAction(resourceMenuIcon(Icon::Paste),"粘贴\tCtrl+V",this,[this,createTarget]{pasteFiles(createTarget);});
    auto updatePaste=[this,paste,createTarget,valid=target.valid()] {
        const auto error=filePasteError(createTarget,QApplication::clipboard()->mimeData(),FileClipboard::instance().current());
        paste->setEnabled(valid && error.isEmpty());paste->setVisible(valid && error.isEmpty());paste->setToolTip(error);
    };
    updatePaste();
    connect(QApplication::clipboard(),&QClipboard::dataChanged,&m,updatePaste);
    connect(this,&Workspace::stateChanged,&m,updatePaste);
    if (single) {
        // Tab is Qt's display-only shortcut column; the existing F2 QShortcut
        // remains the sole registration on the file view.
        m.addAction(resourceMenuIcon(Icon::Rename), "重命名\tF2", this, [this, selectedInfo] { renameFile(selectedInfo); });
    }
    if (!item) {
        m.addAction(resourceMenuIcon(Icon::Undo),"撤销上次文件操作\tCtrl+Z",this,&Workspace::undoLastFileOperation)->setEnabled(online && session);
        if (directory.startsWith('/')) { m.addSeparator(); addBookmark(directory); }
    }
    m.addSeparator();
    m.addAction(resourceMenuIcon(Icon::Refresh), "刷新", this, [this] { refreshFiles(); });
    if (!paths.isEmpty()) {
        m.addSeparator();
        m.addAction(resourceMenuIcon(Icon::Trash), "删除…", this, [this, paths] { deleteEntries(paths); });
    }
    if (single) {
        m.addSeparator();
        m.addAction(resourceMenuIcon(Icon::Info), "属性 / 权限", this, [this, path=paths.first()] {
            if (!session || !online) return;
            Job j; j.type="stat"; j.path=path; session->submit(j);
        });
    }
    m.exec(files->viewport()->mapToGlobal(p));
}
void Workspace::deleteSelectedFiles() {
    if(OperationGate::locked())return;
    if (files->currentItem() && files->currentItem()->data(0, ParentNavigationRole).toBool()) return;
    QStringList paths;
    for (auto item : files->selectedItems()) {
        if (item->data(0, ParentNavigationRole).toBool() || item->data(0, FileDraftRole).toBool()) continue;
        const auto path = item->data(0, Qt::UserRole).toMap().value("path").toString();
        if (!path.isEmpty()) paths << path;
    }
    deleteEntries(paths);
}
void Workspace::deleteEntries(QStringList paths) {
    if(OperationGate::locked())return;
    if (!online || !session || deleteConfirming) return;
    for (const auto &path:paths) if (clipboardMutationBlocked(path)) {
        taskSummary->setText("相关文件正在移动，请稍后删除。");return;
    }
    paths.removeAll(QString());
    std::sort(paths.begin(), paths.end());
    QStringList targets;
    for (const auto &path : paths) {
        bool covered = false;
        for (const auto &root : targets)
            if (path == root || path.startsWith(root + '/')) { covered = true; break; }
        if (!covered) targets << path;
    }
    if (targets.isEmpty()) return;
    const auto lockEpoch=OperationGate::token();QPointer<Session> origin = session;
    diagnose(DiagEvent::DeleteConfirm,origin->diagnosticTag,0,targets.first(),targets.size());
    QString message = "将永久删除以下远端项目，此操作无法撤销：\n\n" + targets.join('\n');
    for (auto editor : editors) if (editor && editor->dirty())
        for (const auto &path : targets)
            if (editor->path == path || editor->path.startsWith(path + '/'))
                message += "\n含未保存草稿：" + editor->path;
    QScopedValueRollback<bool> confirming(deleteConfirming, true);
    AppMessageBox confirm(QMessageBox::Warning,"确认删除",message,QMessageBox::Yes|QMessageBox::Cancel,this);
    confirm.button(QMessageBox::Yes)->setText("删除");confirm.setDefaultButton(QMessageBox::Cancel);
    if (confirm.exec() != QMessageBox::Yes) {
        diagnose(DiagEvent::DeleteCancelled,origin?origin->diagnosticTag:0,0,targets.first(),targets.size());return;
    }
    diagnose(DiagEvent::DeleteAccepted,origin?origin->diagnosticTag:0,0,targets.first(),targets.size());
    if (!OperationGate::accepts(lockEpoch)||!online || !origin || session != origin || shuttingDown) return;
    const auto operationBatch=QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (const auto &path : targets) {
        Job job; job.type = "delete"; job.path = path; job.recursive = true; job.id = nextJob++;
        job.options["operationBatch"]=operationBatch;
        job.options["operationBatchOpen"]=true;
        fileMutations[job.id] = path;
        origin->submit(job);
    }
    Job seal;seal.type="journalSeal";seal.id=nextJob++;seal.options["operationBatch"]=operationBatch;origin->submit(seal);
}
void Workspace::createEntry(bool folder, const QString &target) {
    if(OperationGate::locked())return;
    if (!online || !session)
        return;
    if (!finishEntryDraft())
        return;
    const QString path = target.isEmpty() ? directory : target;
    auto parent = path == directory ? nullptr : fileItem(path);
    if (path != directory && (!parent || !parent->data(0, Qt::UserRole).toMap()["directory"].toBool() ||
                             parent->data(0, Qt::UserRole).toMap()["link"].toBool()))
        return;
    entryDraft.parentPath = path;
    entryDraft.parent = parent ? QPersistentModelIndex(files->indexFromItem(parent)) : QPersistentModelIndex();
    entryDraft.folder = folder;
    if (parent) {
        files->resetTreeTransientState();
        QSignalBlocker blocked(files);
        parent->setExpanded(true);
        if (!parent->data(0, DirectoryLoadedRole).toBool()) {
            entryDraft.waiting = true;
            requestDirectory(path);
            return;
        }
    }
    showEntryDraft();
}
void Workspace::showEntryDraft() {
    if (entryDraft.parentPath.isEmpty() || entryDraft.editor)
        return;
    auto parent = entryDraft.parent.isValid() ? files->itemFromIndex(entryDraft.parent) : nullptr;
    if (entryDraft.parentPath != directory && !parent) {
        cancelEntryDraft();
        return;
    }
    auto row = new FileItem(parent ? parent : files->invisibleRootItem());
    row->setData(0, FileDraftRole, true);
    row->setFlags(Qt::ItemIsEnabled);
    row->setIcon(0, icon(entryDraft.folder ? Icon::NewFolder : Icon::NewFile));
    auto editor = new QLineEdit(files);
    editor->setObjectName("newEntryName");
    editor->setAccessibleName(entryDraft.folder ? "新文件夹名称" : "新文件名称");
    editor->setPlaceholderText(entryDraft.folder ? "输入文件夹名称" : "输入文件名称");
    editor->ensurePolished();
    entryDraft.row = files->indexFromItem(row);
    entryDraft.editor = editor;
    files->setUniformRowHeights(false);
    row->setFirstColumnSpanned(true);
    // The item delegate reserves the tree's vertical padding around its editor.
    row->setSizeHint(0, QSize(0, qMax(uiPx(40), editor->sizeHint().height() + uiPx(12))));
    files->setItemWidget(row, 0, editor);
    editor->installEventFilter(this);
    connect(editor, &QLineEdit::returnPressed, this, &Workspace::submitEntryDraft);
    files->scrollToItem(row);
    editor->setFocus();
}
void Workspace::cancelEntryDraft() {
    const auto previous = entryDraft;
    entryDraft = {};
    if (previous.editor)
        previous.editor->removeEventFilter(this);
    if (previous.row.isValid())
        delete files->itemFromIndex(previous.row);
    files->setUniformRowHeights(true);
}
bool Workspace::finishEntryDraft() {
    submitEntryDraft();
    // Invalid/conflicting input remains correctable; navigation or another new
    // entry must not silently discard it. Escape explicitly cancels the draft.
    if (entryDraft.editor && !entryDraft.job)
        return false;
    cancelEntryDraft();
    return true;
}
void Workspace::entryDraftError(const QString &reason) {
    if (!entryDraft.editor || !entryDraft.row.isValid())
        return;
    entryDraft.job = 0;
    entryDraft.editor->setEnabled(true);
    entryDraft.editor->setToolTip(reason);
    entryDraft.editor->setAccessibleDescription(reason);
    setUiStyleSheet(entryDraft.editor,"QLineEdit {border:1px solid " + colors().red.name() + ";}");
    auto row = files->itemFromIndex(entryDraft.row);
    itemText(row, 1, "创建失败");
    row->setToolTip(1, reason);
}
void Workspace::submitEntryDraft() {
    if(OperationGate::locked())return;
    if (!entryDraft.editor || entryDraft.job || entryDraft.waiting)
        return;
    if (!online || !session || !entryDraft.row.isValid() ||
        (entryDraft.parentPath != directory && !entryDraft.parent.isValid())) {
        cancelEntryDraft();
        return;
    }
    const auto name = entryDraft.editor->text();
    if (name.trimmed().isEmpty()) {
        cancelEntryDraft();
        return;
    }
    if (name == "." || name == ".." || name.contains('/') || name.contains('\\') ||
        std::any_of(name.begin(), name.end(), [](QChar c) { return c.unicode() < 32; })) {
        entryDraftError("请输入单个有效名称，不能包含路径分隔符或控制字符。");
        return;
    }
    const auto target = remoteJoin(entryDraft.parentPath, name);
    if (clipboardMutationBlocked(target)) {entryDraftError("目标正在移动，请稍后重试。");return;}
    if (fileItem(target)) {
        entryDraftError("该名称已存在，请修改名称；不会覆盖已有文件。");
        return;
    }
    Job j;
    j.type = entryDraft.folder ? "mkdir" : "touch";
    j.path = target;
    j.id = nextJob++;
    entryDraft.job = j.id;
    fileMutations[j.id] = target;
    entryDraft.editor->setEnabled(false);
    itemText(files->itemFromIndex(entryDraft.row), 1, "创建中…");
    session->submit(j);
}
void Workspace::renameEntry() {
    if(OperationGate::locked())return;
    if (entryDraft.editor && entryDraft.editor->hasFocus())
        return;
    if (files->currentItem() && files->currentItem()->data(0, ParentNavigationRole).toBool()) return;
    auto selected = files->selectedItems();
    if (selected.size() != 1 || !online || !session)
        return;
    renameFile(selected[0]->data(0, Qt::UserRole).toMap());
}
void Workspace::renameFile(const QVariantMap &d) {
    if(OperationGate::locked())return;
    if (!online || !session || d["path"].toString().isEmpty()) return;
    if (clipboardMutationBlocked(d["path"].toString())) {taskSummary->setText("相关文件正在移动，请稍后重命名。");return;}
    QPointer<Session> origin=session; const auto generation=credentialGeneration;
    const auto lockEpoch=OperationGate::token();bool ok;
    auto name = QInputDialog::getText(this, "重命名", "新名称", QLineEdit::Normal, d["name"].toString(), &ok);
    if (!OperationGate::accepts(lockEpoch)||!ok || name.isEmpty() || name == d["name"].toString())
        return;
    if (name.contains('/') || name.contains('\\') || name == "." || name == "..")
        return;
    if (clipboardMutationBlocked(remoteJoin(remoteJoin(d["path"].toString(),".."),name))) {taskSummary->setText("目标正在移动，请稍后重命名。");return;}
    if (!origin || origin!=session || !online || generation!=credentialGeneration) return;
    Job j;
    j.type = "rename";
    j.path = d["path"].toString();
    j.destination = remoteJoin(j.path.section('/', 0, -2), name);
    j.id = nextJob++;
    session->submit(j);
}
void Workspace::properties(const QVariantMap &info) {
    if(OperationGate::locked())return;const auto lockEpoch=OperationGate::token();
    QDialog d(this);
    d.setWindowTitle("属性 · " + basename(info["path"].toString()));
    auto l = new QVBoxLayout(&d);
    auto label = new QLabel(
        QString("%1\n大小：%2\n所有者 UID：%3 · 组 GID：%4\n修改：%5")
            .arg(info["path"].toString(), humanSize(info["size"].toULongLong()), info["uid"].toString(),
                 info["gid"].toString(),
                 QDateTime::fromSecsSinceEpoch(info["modified"].toLongLong()).toString(Qt::ISODate)));
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(label);
    auto grid = new QGridLayout;
    QVector<QCheckBox *> checks;
    QStringList labels = {"所有者", "用户组", "其他"};
    int mask = 0400;
    for (int r = 0; r < 3; ++r) {
        grid->addWidget(new QLabel(labels[r]), r, 0);
        for (int c = 0; c < 3; ++c) {
            auto box = new QCheckBox(QStringList{"读取 r", "写入 w", "执行 x"}[c]);
            box->setChecked(info["permissions"].toUInt() & mask);
            checks << box;
            grid->addWidget(box, r, c + 1);
            mask >>= 1;
        }
    }
    l->addLayout(grid);
    auto octal = new QLineEdit(QString::number(info["permissions"].toUInt() & 07777, 8));
    octal->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-7]{3,4}"), octal));
    l->addWidget(octal);
    for (auto c : checks)
        connect(c, &QCheckBox::clicked, &d, [checks, octal] {
            quint32 bits = 0;
            for (int i = 0; i < checks.size(); ++i)
                if (checks[i]->isChecked())
                    bits |= (0400 >> i);
            auto special = octal->text().toUInt(nullptr, 8) & 07000;
            octal->setText(QString::number(bits | special, 8));
        });
    connect(octal, &QLineEdit::textEdited, &d, [checks](const QString &v) {
        int bits = v.toInt(nullptr, 8);
        for (int i = 0; i < checks.size(); ++i)
            checks[i]->setChecked(bits & (0400 >> i));
    });
    auto note = new QLabel("修改需要远端所有者权限；不会自动提权。当前仅应用于此项目。");
    note->setWordWrap(true);
    l->addWidget(note);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    l->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() == QDialog::Accepted && online && OperationGate::accepts(lockEpoch)) {
        Job j;
        j.type = "chmod";
        j.path = info["path"].toString();
        j.permissions = octal->text().toUInt(nullptr, 8);
        j.id = nextJob++;
        session->submit(j);
    }
}
void Workspace::openFile(const QString &path) {
    if(OperationGate::locked())return;
    if (auto e = editors.value(path)) {
        if (auto g = groupFor(e))
            editArea->activatePage(e);
        else if (auto w = floating.value(e)) {
            pagePresented(e);
            w->show();
            w->raise();
        }
        return;
    }
    if (!session || !online)
        return;
    if (clipboardPathLocked(path)) {taskSummary->setText("文件移动中，请稍后打开。");return;}
    if (pendingReads.values().contains(path))
        return;
    int id = nextJob++;
    pendingReads[id] = path;
    Job j;
    j.type = "read";
    j.path = path;
    j.id = id;
    session->submit(j);
}
void Workspace::saveEditor(Editor *e, bool overwrite) {
    if(OperationGate::locked())return;
    if (!e || e->saving || e->code->isReadOnly())
        return;
    if (e->property("remoteRemovedByUndo").toBool()) {taskSummary->setText("远端文件已删除，草稿保留；请先重新打开或另存内容。");return;}
    if (clipboardPathLocked(e->path)) {taskSummary->setText("文件移动中，草稿保留，请稍后保存。");return;}
    if (demo) {
        e->saved(e->bytes(), QCryptographicHash::hash(e->bytes(), QCryptographicHash::Sha256));
        return;
    }
    if (!online) {
        AppMessageBox::warning(this, "保存失败", "SSH 已断开，草稿保留。请重新连接后保存。");
        closing = false;
        return;
    }
    Job j;
    j.type = "save";
    j.path = e->path;
    j.data = e->bytes();
    j.expectedHash = e->remoteHash;
    j.overwrite = overwrite;
    j.id = nextJob++;
    pendingSaves[j.id] = e;
    e->saving = true;
    session->submit(j);
}
EditorTabs *Workspace::groupFor(QWidget *p) const {
    return editArea->groupFor(p);
}
void Workspace::detachPage(QWidget *p) {
    editArea->removePage(p);
    if (auto w = floating.take(p)) {
        if (auto editor = qobject_cast<Editor *>(p)) editor->setReturnAction(nullptr);
        if (auto action = w->findChild<QAction *>("returnToWorkspace")) {
            action->setEnabled(false);
            for (const auto &term : terminals)
                if (term.frame == p) term.view->removeAction(action);
        }
        closingWindows.insert(p);
        w->removeEventFilter(this);
        p->hide();
        p->setParent(this);
        w->deleteLater();
        closingWindows.remove(p);
    }
}
void Workspace::closePage(QWidget *p) {
    if(OperationGate::locked())return;
    if (auto comparison = qobject_cast<EditorCompare *>(p)) {
        detachPage(comparison);
        comparison->deleteLater();
        return;
    }
    if(p&&p->property("documentSearchResults").toBool()){emit searchPageCloseRequested();return;}
    if (p == processPage || p == listeningPage) {
        detachPage(p);
        p->setParent(this);
        p->hide();
        if (activeSystemPage == (p == processPage ? "process" : "listening")) activeSystemPage.clear();
        layoutReset = false;
        return;
    }
    for (const auto &t : terminals)
        if (t.frame == p) {
            detachPage(p);
            placeTerminal(t.id, t.group);
            return;
        }
    auto e = qobject_cast<Editor *>(p);
    if (!e)
        return;
    if (e->saving) {
        closeAfterSave.insert(e);
        return;
    }
    if (e->dirty()) {
        const auto lockEpoch=OperationGate::token();
        auto choice = AppMessageBox::warning(this, "未保存修改", e->path,
                                           QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                           QMessageBox::Save);
        if (!OperationGate::accepts(lockEpoch)||(choice!=QMessageBox::Save&&choice!=QMessageBox::Discard))
            return;
        if (choice == QMessageBox::Save) {
            closeAfterSave.insert(e);
            saveEditor(e);
            return;
        }
    }
    e->discardDraft();
    editors.remove(e->path);
    detachPage(e);
    e->deleteLater();
    emit stateChanged();
}
void Workspace::movePage(QWidget *p, EditorTabs *target) {
    if(OperationGate::locked())return;
    if (!p) return;
    for (const auto &term : terminals) if (term.frame == p) return;
    auto from = groupFor(p);
    if (!target && from) {
        auto groups = editArea->groups();
        if (groups.size() > 1) target = groups[(groups.indexOf(from)+1)%groups.size()];
        else {
            editArea->movePage(p, from, DropZone::Right);
            return;
        }
    }
    if (!target) target = editArea->activeGroup();
    if (from) { editArea->movePage(p, target); return; }
    QString label = p == processPage ? QString("进程信息") : p==listeningPage ? QString("监听端口") : p->windowTitle();
    if (auto e = qobject_cast<Editor *>(p)) label = basename(e->path) + (e->dirty() ? " ●" : "");
    detachPage(p);
    editArea->addPage(p, label, target);
}
void Workspace::floatPage(QWidget *p) {
    if(OperationGate::locked())return;
    if(p&&p->property("documentSearchResults").toBool()){emit searchPageFloatRequested();return;}
    if (!p || qobject_cast<EditorCompare *>(p) || floating.contains(p))
        return;
    QString label = p == processPage ? QString("进程信息") : p==listeningPage ? QString("监听端口") : QString("文件");
    if (auto e = qobject_cast<Editor *>(p))
        label = e->path;
    else
        for (const auto &t : terminals)
            if (t.frame == p)
                label = t.name;
    detachPage(p);
    auto d = new QDialog(this, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                   Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    d->setAttribute(Qt::WA_DeleteOnClose, false);
    d->setWindowTitle(profile.identity() + " — " + label);
    auto l = new QVBoxLayout(d);
    uiMargins(l,0,0,0,0);
    uiSpacing(l,0);
    auto back = new QAction(icon(Icon::Return), "收回工作区", d);
    back->setObjectName("returnToWorkspace");
    back->setToolTip("返回 " + profile.name + " 并收回此页签");
    d->addAction(back);
    d->setContextMenuPolicy(Qt::ActionsContextMenu);
    if (auto editor = qobject_cast<Editor *>(p)) editor->setReturnAction(back);
    for (const auto &term : terminals)
        if (term.frame == p) term.view->addAction(back);
    l->addWidget(p, 1);
    connect(back, &QAction::triggered, this, [this, p] {
        for (const auto &term : terminals) if (term.frame == p) {
            const int id = term.id, group = term.group;
            detachPage(p);
            placeTerminal(id, group);
            return;
        }
        movePage(p, editArea->activeGroup());
    });
    floating[p] = d;
    if (auto editor = qobject_cast<Editor *>(p)) updateEditorTitle(editor);
    d->installEventFilter(this);
    d->resize(760, 540);
    d->setMinimumSize(420, 300);
    d->show();
    p->show();
    for (const auto &term : terminals) if (term.frame == p) activateTerminal(term.id, true);
}
void Workspace::updateEditorTitle(Editor *e) {
    if (auto g = groupFor(e)) {
        g->setTabText(g->indexOf(e), basename(e->path) + (e->dirty() ? " ●" : ""));
        g->setTabToolTip(g->indexOf(e), e->path);
        g->setTabIcon(g->indexOf(e), FileIconProvider::file(e->path));
    }
    if (auto w = floating.value(e)) {
        w->setWindowTitle(profile.identity() + " — " + e->path + (e->dirty() ? " *" : ""));
        w->setWindowIcon(FileIconProvider::file(e->path));
    }
}
void Workspace::restoreTerminalSessions(){
    if(!session||!online)return;
    QList<int> ids=terminals.keys();std::sort(ids.begin(),ids.end());
    for(int id:ids){auto &term=terminals[id];if(!term.restorePending)continue;
        term.restorePending=false;term.running=true;
        term.view->feed("\r\n[正在重新打开 shell；保留历史，旧进程不会恢复。尝试原启动目录。]\r\n");
        Job job;job.type="open-terminal";job.id=id;job.path=term.startupDirectory;job.columns=term.view->columns();job.rows=term.view->rowCount();session->submit(job);
    }
    rebuildTerminals();activateTerminal(focusedTerm,false);
}
void Workspace::addTerminal(int group, const QString &startDirectory) {
    if(OperationGate::locked())return;
    if (!online && !demo)
        return;
    int id = nextTerm++;
    if (!group)
        group = nextGroup++;
    auto frame = new QWidget;
    frame->setProperty("terminalPage", true);
    frame->setWindowTitle("shell");
    auto l = new QVBoxLayout(frame);
    uiMargins(l,0,0,0,0);
    uiSpacing(l,0);
    auto term = new Terminal;
    l->addWidget(term, 1);
    terminals[id] = {id, group, term, frame, "shell", true, startDirectory.isEmpty()?(bookmarkGroups.contains(group)?bookmarkGroups[group].path:directory):startDirectory};
    focusedTerm = id;
    placeTerminal(id, group);
    terminalPanel->show();
    connect(term, &Terminal::focused, this, [this, id] { if(terminals.contains(id))terminals[id].lastFocus=++terminalFocusSerial;activateTerminal(id); });
    connect(term, &Terminal::input, this, [this, id](const QByteArray &bytes) {
        if (!session || !online || !terminals.value(id).running)
            return;
        Job j;
        j.type = "input";
        j.id = id;
        j.data = bytes;
        session->submit(j);
    });
    connect(term, &Terminal::sizeChanged, this, [this, id](int col, int row) {
        if (!session || !online || !terminals.contains(id) || !terminals[id].running)
            return;
        Job j;
        j.type = "resize";
        j.id = id;
        j.columns = col;
        j.rows = row;
        session->submit(j);
    });
    if (demo)
        term->feed(
            "\x1b[32mdemo@rabbitshell\x1b[0m:\x1b[36m~/project\x1b[0m$ \r\n[原生终端预览，未连接服务器]\r\n");
    else {
        Job j;
        j.type = "open-terminal";
        j.id = id;
        j.path = terminals[id].startupDirectory;
        j.columns = term->columns();
        j.rows = term->rowCount();
        session->submit(j);
    }
    rebuildTerminals();
}
void Workspace::placeTerminal(int id, int group) {
    if(OperationGate::locked())return;
    if (!terminals.contains(id))
        return;
    auto &t = terminals[id];
    t.group = group;
    if (!terminalGroups.contains(group)) {
        auto s = new PanelSplitter(Qt::Horizontal);
        terminalGroups[group] = s;
        terminalStack->addWidget(s);
    }
    auto s = terminalGroups[group];
    s->addWidget(t.frame);
    t.frame->show();
    terminalStack->setCurrentWidget(s);
    QList<int> sizes;
    for (int i = 0; i < s->count(); ++i)
        sizes << 1;
    s->setSizes(sizes);
    terminalPanel->show();
    rebuildTerminals();
    activateTerminal(id, true);
}
void Workspace::closeTerminal(int id) {
    if(OperationGate::locked())return;
    if (!terminals.contains(id)) return;
    const auto removed = terminals[id];
    int next = focusedTerm;
    const bool wasActive = id == focusedTerm;
    if (wasActive) {
        QList<int> peers, others;
        for (const auto &term : terminals) if (term.id != id) {
            others << term.id;
            if (term.group == removed.group) peers << term.id;
        }
        auto candidates = peers.isEmpty() ? others : peers;
        std::sort(candidates.begin(), candidates.end());
        auto following = std::upper_bound(candidates.cbegin(), candidates.cend(), id);
        next = candidates.isEmpty() ? 0 : following != candidates.cend() ? *following : candidates.last();
    }
    if (session && online && removed.running) {
        Job job; job.type = "close-terminal"; job.id = id; session->submit(job);
    }
    terminals.remove(id);
    detachPage(removed.frame);
    removed.frame->hide();
    removed.frame->setParent(nullptr);
    removed.frame->deleteLater();
    focusedTerm = next;
    rebuildTerminals();
    activateTerminal(next, wasActive);
}
void Workspace::activateTerminal(int id, bool focus) {
    if (terminals.contains(id)) {
        focusedTerm = id;
        auto &term = terminals[id];
        if(focus || !term.lastFocus)term.lastFocus=++terminalFocusSerial;
        if (!floating.contains(term.frame)) terminalStack->setCurrentWidget(terminalGroups.value(term.group));
        QSignalBlocker blocked(terminalManager);
        for (QTreeWidgetItemIterator it(terminalManager); *it; ++it)
            if ((*it)->data(0, Qt::UserRole).toInt() == id) {
                terminalManager->setCurrentItem(*it, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                break;
            }
        const QString name = terminalLabel(term,true);
        currentTerminal->setText(currentTerminal->fontMetrics().elidedText(name, Qt::ElideRight, uiPx(200)).replace("&","&&"));
        currentTerminal->setToolTip(safeTerminalTooltip("当前终端：" + name+"\n【启动目录】"+term.startupDirectory));
        currentTerminal->setAccessibleName("当前终端：" + name);
        if (focus) {
            if (auto window = floating.value(term.frame)) { window->show(); window->raise(); window->activateWindow(); }
            else terminalPanel->show();
            term.view->setFocus();
        }
    } else {
        focusedTerm = 0;
        currentTerminal->setText({});
        currentTerminal->setToolTip("暂无终端");
        currentTerminal->setAccessibleName("暂无终端");
    }
    for (auto button : {currentTerminal, splitTerminal, closeCurrentTerminal}) button->setEnabled(focusedTerm != 0);
    updateTerminalRowActions();
}
void Workspace::renameTerminal(int id, const QString &name) {
    if(OperationGate::locked())return;
    if (!terminals.contains(id) || name.trimmed().isEmpty()) return;
    auto &term = terminals[id];
    term.name = name.trimmed();term.manualName=true;
    term.frame->setWindowTitle(term.name);
    if (auto window = floating.value(term.frame)) window->setWindowTitle(profile.identity() + " — " + term.name);
    rebuildTerminals();
}
void Workspace::updateTerminalRowActions() {
    auto item = terminalManager->currentItem();
    const int id = item ? item->data(0, Qt::UserRole).toInt() : 0;
    terminalRowId = terminals.contains(id) ? id : 0;
    const auto row = item ? terminalManager->visualItemRect(item) : QRect();
    terminalRowActions->setVisible(terminalRowId && terminalManager->isVisible() && !row.isEmpty() &&
                                  row.intersects(terminalManager->viewport()->rect()));
    if (!terminalRowActions->isVisible()) return;
    terminalRowActions->setGeometry(terminalManager->viewport()->width() - uiPx(60), row.top(), uiPx(56), row.height());
    terminalRowActions->raise();
}
void Workspace::pagePresented(QWidget *page) {
    if (restoringWorkspaceLayout || !page) return;
    if (page == processPage) activeSystemPage = "process";
    else if (page == listeningPage) activeSystemPage = "listening";
    layoutReset = false;
    for (auto group : editArea->groups()) {
        if (group->count()) {
            hadEditorPages = true;
            if (terminalMaximized) setTerminalMaximized(false);
            break;
        }
    }
}
void Workspace::scheduleEditorEmptyCheck() {
    if (restoringWorkspaceLayout || shuttingDown) return;
    for (auto group : editArea->groups()) {
        if (group->count()) { hadEditorPages = true; return; }
    }
    if (!hadEditorPages || editorEmptyCheckQueued) return;
    editorEmptyCheckQueued = true;
    // Tab moves can briefly empty a group before inserting into its target.
    // Check all groups after the synchronous migration completes.
    QTimer::singleShot(0, this, [this] {
        editorEmptyCheckQueued = false;
        applyEditorEmptyCheck();
    });
}
void Workspace::applyEditorEmptyCheck() {
    if (restoringWorkspaceLayout || shuttingDown || !hadEditorPages) return;
    for (auto group : editArea->groups()) if (group->count()) return;
    hadEditorPages = false;
    setTerminalMaximized(true);
}
void Workspace::restoreWorkspaceLayout() {
    if (!layoutPersistenceEnabled) return;
    QSettings settings;
    const auto key = QString::fromLatin1(profile.id.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    settings.beginGroup("workspace-layout/" + key);
    const auto pages = settings.value("pages").toStringList();
    const bool hasProcess = pages.contains("process"), hasListening = pages.contains("listening");
    if (hasProcess || hasListening) {
        for (const auto &page : pages) {
            if (page == "process" && !groupFor(processPage)) showProcessPage();
            else if (page == "listening" && !groupFor(listeningPage)) showListeningPage();
        }
        const auto selected = settings.value("active").toString();
        QWidget *page = selected == "process" ? processPage : selected == "listening" ? listeningPage : nullptr;
        if (page && groupFor(page)) editArea->activatePage(page);
        activeSystemPage = page && groupFor(page) ? selected : (hasProcess ? "process" : "listening");
        const qreal ratio = qBound(qreal(0.05), settings.value("terminalRatio", 1.0 / 3.0).toDouble(), qreal(0.95));
        verticalSplit->setSizes({qRound((1.0-ratio)*1000), qRound(ratio*1000)});
        if (settings.value("maximized", false).toBool()) setTerminalMaximized(true);
        else if (!settings.value("visible", true).toBool()) terminalPanel->hide();
        savedTerminalRatio = ratio;
    } else {
        setTerminalMaximized(true);
    }
}
void Workspace::saveWorkspaceLayout() {
    if (!layoutPersistenceEnabled || layoutSaved || layoutReset || profile.id.isEmpty()) return;
    applyEditorEmptyCheck();
    layoutSaved = true;
    QStringList pages;
    for (auto group : editArea->groups()) {
        for (int i = 0; i < group->count(); ++i) {
            auto page = group->widget(i);
            if (page == processPage && processPage) pages << "process";
            else if (page == listeningPage && listeningPage) pages << "listening";
        }
    }
    if (processPage && floating.contains(processPage) && !pages.contains("process")) pages << "process";
    if (listeningPage && floating.contains(listeningPage) && !pages.contains("listening")) pages << "listening";
    if (!terminalMaximized && !terminalPanel->isHidden()) {
        const auto sizes = verticalSplit->sizes();
        if (sizes.size() == 2 && sizes[0] > 0 && sizes[1] > 0)
            savedTerminalRatio = qreal(sizes[1]) / (sizes[0] + sizes[1]);
    }
    if (!pages.contains(activeSystemPage)) activeSystemPage = pages.value(0);
    const auto key = QString::fromLatin1(profile.id.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    QSettings settings;
    settings.beginGroup("workspace-layout/" + key);
    settings.setValue("pages", pages);
    settings.setValue("active", activeSystemPage);
    settings.setValue("maximized", terminalMaximized);
    settings.setValue("visible", !terminalPanel->isHidden());
    settings.setValue("terminalRatio", savedTerminalRatio);
}
void Workspace::setTerminalMaximized(bool maximized) {
    if (terminalMaximized == maximized)
        return;
    if (!restoringWorkspaceLayout) layoutReset = false;
    terminalMaximized = maximized;
    if (maximized) {
        terminalPanel->show();
        const auto sizes = verticalSplit->sizes();
        if (sizes.size() == 2 && sizes[0] > 0 && sizes[1] > 0) {
            savedTerminalHeight = sizes[1];
            savedTerminalRatio = qreal(sizes[1]) / (sizes[0] + sizes[1]);
        }
        editorPanel->hide();
    } else {
        editorPanel->show();
        const int available = qMax(1, verticalSplit->height() - verticalSplit->handleWidth());
        savedTerminalHeight = qMax(1, qRound(available * savedTerminalRatio));
        verticalSplit->setSizes({qMax(1, available - savedTerminalHeight), savedTerminalHeight});
    }
    maximizeTerminal->setChecked(maximized);
    const QString label = maximized ? "还原面板" : "最大化面板";
    maximizeTerminal->setToolTip(label);
    maximizeTerminal->setAccessibleName(label);
}
void Workspace::hideTerminalPanel() {
    setTerminalMaximized(false);
    terminalPanel->hide();
}
void Workspace::sizeTerminalManager(){
    if(!terminalManager || !terminalManager->isVisible())return;
    auto split=qobject_cast<QSplitter *>(terminalManager->parentWidget());if(!split)return;
    const int width=uiPx(terminalManagerBaseWidth);
    split->setSizes({qMax(1,split->width()-split->handleWidth()-width),width});
}
void Workspace::rebuildTerminals() {
    for (auto it = terminalGroups.begin(); it != terminalGroups.end();) {
        bool used = false;
        for (const auto &term : terminals)
            if (term.group == it.key())
                used = true;
        if (!used) {
            bookmarkGroups.remove(it.key());
            terminalStack->removeWidget(it.value());
            it.value()->deleteLater();
            it = terminalGroups.erase(it);
        } else
            ++it;
    }
    QSignalBlocker blocked(terminalManager);
    // Keep every terminal as a peer, reusing rows and live session views.
    const int scroll=terminalManager->verticalScrollBar()->value();
    terminalManager->resetTreeTransientState();
    QHash<int,QTreeWidgetItem *> items;
    while(terminalManager->topLevelItemCount()) {
        auto item=terminalManager->takeTopLevelItem(0);
        items.insert(item->data(0,Qt::UserRole).toInt(),item);
    }
    auto ids = terminals.keys(); std::sort(ids.begin(), ids.end());
    QList<int> groupOrder;
    QHash<int,QList<int>> groupIds;
    for (int id : ids) {
        const int group=terminals[id].group;
        if(!groupIds.contains(group))groupOrder.append(group);
        groupIds[group].append(id);
    }
    for(int group:groupOrder) {
        const auto peers=groupIds.value(group);
        for(int n=0;n<peers.size();++n) {
            const int id=peers[n]; const auto &term=terminals[id];
            auto item=items.take(id); if(!item)item=new QTreeWidgetItem;
            item->setData(0,Qt::UserRole,id);
            item->setData(0,Qt::UserRole+1,group);
            item->setData(0,TerminalPeerPositionRole,
                          peers.size()==1 ? 0 : n==0 ? 1 : n==peers.size()-1 ? 3 : 2);
            item->setText(0,terminalLabel(term));
            item->setToolTip(0,safeTerminalTooltip(terminalLabel(term,true)+"\n【启动目录】"+term.startupDirectory));
            item->setIcon(0,icon(Icon::Terminal));
            item->setSizeHint(0,QSize(0,uiPx(28)));
            terminalManager->addTopLevelItem(item);
        }
    }
    qDeleteAll(items);
    terminalManager->setVisible(terminals.size() > 1);
    if(terminals.size()>1 && !terminalManager->property("initialWidthApplied").toBool()){
        terminalManager->setProperty("initialWidthApplied",true);
        auto split=qobject_cast<QSplitter *>(terminalManager->parentWidget());
        QTimer::singleShot(0,split,[this]{sizeTerminalManager();});
    }
    activateTerminal(focusedTerm);
    terminalManager->verticalScrollBar()->setValue(scroll);
    terminalManager->viewport()->update();
    emit stateChanged();
}
void Workspace::terminalMenu(QPoint p) {
    if(OperationGate::locked())return;
    auto item = terminalManager->itemAt(p);
    if (!item)
        return;
    int id = item->data(0, Qt::UserRole).toInt();
    if (!terminals.contains(id)) {
        const int group=item->data(0,Qt::UserRole+1).toInt();
        if(!terminalGroups.contains(group))return;
        QMenu menu;menu.addAction(icon(Icon::Add),"在组内新建终端",this,[this,group]{addTerminal(group);});
        menu.exec(terminalManager->viewport()->mapToGlobal(p));return;
    }
    QMenu m;
    m.addAction("拆分终端", this, [this, id] { if (terminals.contains(id)) addTerminal(terminals[id].group); });
    m.addAction("取消拆分，成为独立组", this, [this, id] { placeTerminal(id, nextGroup++); });
    m.addAction("移到独立窗口", this, [this, id] { if (terminals.contains(id)) floatPage(terminals[id].frame); });
    m.addSeparator();
    m.addAction("重命名", this, [this, id] {
        if (!terminals.contains(id)) return;
        bool ok;
        auto name =
            QInputDialog::getText(this, "终端名称", "名称", QLineEdit::Normal, terminals[id].name, &ok);
        if (ok) renameTerminal(id, name);
    });
    m.addSeparator();
    m.addAction(icon(Icon::Trash), "关闭终端", this, [this, id] { closeTerminal(id); });
    m.exec(terminalManager->viewport()->mapToGlobal(p));
}
std::shared_ptr<FileDragBatch> Workspace::prepareFileDrag(const QVariantList &rows,QString *error){
    auto fail=[&](QString why)->std::shared_ptr<FileDragBatch>{if(error)*error=why;return {};};
    if(demo||!online||shuttingDown)return fail("请在已连接的服务器中拖出普通文件。");
    if(rows.isEmpty()||rows.size()>256||pendingTransfers()+rows.size()>256)return fail("最多保留256项未完成任务，请分批拖出。");
    QList<DragFile> files;
    for(auto value:rows){const auto d=value.toMap();const auto mode=d["permissions"].toUInt();
        if(d["directory"].toBool()||d["link"].toBool()||(mode&0170000)!=0100000)return fail("仅支持普通文件；目录、链接或来源不确定的项不能拖出。");
        files<<DragFile{d["path"].toString(),d["name"].toString(),d["size"].toULongLong(),d["modified"].toLongLong()};
    }
    auto batch=FileDragBatch::create(files,error);if(!batch)return {};
    const QPointer<Workspace> owner=this;const QPointer<Session> origin=session;
    const auto generation=credentialGeneration;const auto token=repository?repository->revision(profile.id):0;
    const std::weak_ptr<FileDragBatch> weak=batch;
    batch->fetch=[owner,origin,generation,token,weak](int index,const QString &destination,FileDragBatch::Done done){
        const auto batch=weak.lock();
        if(!owner||!batch||batch->cancelled||!owner->online||owner->shuttingDown||owner->session!=origin||owner->credentialGeneration!=generation||
            (owner->repository&&owner->repository->revision(owner->profile.id)!=token)){done("拖出来源或连接身份已失效，请重新拖出。");return;}
        if(owner->pendingTransfers()>=256){done("传输队列已满，请稍后重新拖出。");return;}
        const auto file=batch->files[index];Job job;job.type="download";job.path=file.remotePath;job.destination=destination;job.collision="overwrite";job.id=owner->nextJob++;
        job.options={{"nativeDrop",true},{"dragSize",QVariant::fromValue(file.size)},{"dragModified",file.modified}};
        owner->transfers[job.id]=job;owner->transferQueue.enqueue(job.id);owner->transferOrder.append(job.id);
        auto group=new QTreeWidgetItem(owner->downloadList,{file.name+" · 系统拖出","等待"});group->setExpanded(true);
        auto item=new QTreeWidgetItem(group,{file.name,"拖出 · 等待远端下载"});item->setData(0,Qt::UserRole,job.id);item->setToolTip(0,file.remotePath+"\n系统选择最终保存目录；本任务仅暂存远端内容。");owner->transferItems[job.id]=item;
        owner->dragCompletions[job.id]=std::move(done);
        owner->dragValid[job.id]=[owner,origin,generation,token,weak]{auto b=weak.lock();return owner&&b&&!b->cancelled&&owner->online&&!owner->shuttingDown&&owner->session==origin&&owner->credentialGeneration==generation&&(!owner->repository||owner->repository->revision(owner->profile.id)==token);};
        owner->transferPanel->show();owner->transferPanel->raise();owner->trimTransferHistory();owner->ensureTransferSession();emit owner->stateChanged();
    };
    batch->cancelJobs=[owner,weak]{
        if(!owner)return;const auto batch=weak.lock();if(!batch)return;
        for(auto id:owner->dragCompletions.keys())if(owner->transfers[id].destination.startsWith(QFileInfo(batch->stagePath(0)).absolutePath()+"/")){
            if(id==owner->activeTransfer){owner->cancelledTransfers.insert(id);if(owner->transferSession)owner->transferSession->cancelTransfer();}
            else{owner->transferQueue.removeAll(id);auto done=owner->dragCompletions.take(id);owner->dragValid.remove(id);if(owner->transferItems.contains(id))owner->transferItems[id]->setText(1,"拖出已取消 · 请重新拖出");if(done)done("系统已取消拖出");}
        }
    };
    batch->systemFinished=[owner,weak](bool accepted){
        if(!owner)return;const auto batch=weak.lock();if(!batch)return;
        for(auto it=owner->transfers.cbegin();it!=owner->transfers.cend();++it)if(it.value().options.value("nativeDrop").toBool()&&it.value().destination.startsWith(QFileInfo(batch->stagePath(0)).absolutePath()+"/")&&owner->transferItems.contains(it.key())){
            auto item=owner->transferItems[it.key()];item->setToolTip(1,item->toolTip(1)+(accepted?"\n系统已接收投放；最终落盘由目标负责。":"\n系统取消或拒绝投放；不表示文件已保存。"));
            if(!accepted)item->setText(1,"系统取消/拒绝投放 · 请重新拖出");
        }
    };
    fileDrags.erase(std::remove_if(fileDrags.begin(),fileDrags.end(),[](auto &w){return w.expired();}),fileDrags.end());fileDrags<<weak;
    return batch;
}
void Workspace::dragFiles(const QVariantList &rows){
    if(OperationGate::locked())return;
    const auto token=registerInternalDrag(rows);
    if(token.isEmpty()){taskSummary->setText("拖动来源无效，请刷新并重新选择。");return;}
    const auto cleanup=qScopeGuard([token]{Workspace::unregisterInternalDrag(token);});
    bool directories=false;for(const auto &row:rows)directories|=row.toMap().value("directory").toBool();
    const auto first=rows.first().toMap();const auto name=first.value("name").toString();
    const auto badge=fileDragBadge(first.value("directory").toBool()?FileIconProvider::directory():FileIconProvider::file(name),name,int(rows.size()),devicePixelRatioF());
#ifdef Q_OS_WIN
    if(!directories) {
        QString error;auto batch=prepareFileDrag(rows,&error);if(!batch){taskSummary->setText(error);return;}
        batch->internalMoveToken=token;
        const auto execute=fileDragExecutor;execute(files,batch,badge);return;
    }
#endif
    QPointer<QDrag> drag=new QDrag(files);auto mime=new QMimeData;mime->setData(internalFileMoveMimeType(),token);drag->setMimeData(mime);drag->setPixmap(badge);
    drag->exec(Qt::MoveAction,Qt::MoveAction);
    if(drag)drag->deleteLater();
}
void Workspace::cancelFileDrags(){
    emit clipboardSourceInvalidated();
    cancelClipboardJobs();
    for(auto weak:fileDrags)if(auto batch=weak.lock())batch->cancel();
    for(auto id:dragCompletions.keys()){
        if(id==activeTransfer){cancelledTransfers.insert(id);if(transferSession)transferSession->cancelTransfer();}
        else{transferQueue.removeAll(id);auto done=dragCompletions.take(id);dragValid.remove(id);if(transferItems.contains(id))transferItems[id]->setText(1,"拖出已取消 · 请重新拖出");if(done)done("拖出来源已关闭或断开");}
    }
}
void Workspace::showUploadConflict(Session *worker,int id,quint64 nonce,const QString &path,bool canOverwrite) {
    if(!worker->hasUploadConflict(id,nonce))return;
    if(OperationGate::locked()){deferredConflict=DeferredConflict{worker,id,nonce,path,canOverwrite};return;}
    const auto lockEpoch=OperationGate::token();
    if(worker!=transferSession || id!=activeTransfer || !online || shuttingDown || cancelledTransfers.contains(id)) {
        worker->answerUploadConflict(id,nonce,"cancel");return;
    }
    const bool moving=transfers.value(id).type=="move";
    const bool copying=transfers.value(id).options.value("clipboardPaste").toBool();
    if(moving)canOverwrite=false;
    if(uploadConflictDialog)uploadConflictDialog->reject();
    auto dialog=new UploadConflictDialog((moving?"移动文件 · ":copying?"复制文件 · ":"上传文件 · ")+profile.name,path,moving,canOverwrite,this);
    dialog->setProperty("job",id);dialog->setProperty("nonce",QVariant::fromValue(nonce));
    auto overwrite=dialog->buttons().at(0),keep=dialog->buttons().at(1);
    auto all=dialog->checkBox();
    uploadConflictDialog=dialog;
    if(transferItems.contains(id))transferItems[id]->setText(1,"等待处理同名目标 · 0 B/s");
    taskSummary->setText(moving?"移动等待处理同名目标":copying?"复制等待处理同名目标":"上传等待处理同名目标");
    QPointer<Session> source=worker;
    connect(dialog,&QDialog::finished,this,[this,source,dialog,id,nonce,overwrite,keep,all,lockEpoch,path,canOverwrite](int){
        if(!OperationGate::accepts(lockEpoch)){if(source&&source->hasUploadConflict(id,nonce))deferredConflict=DeferredConflict{source,id,nonce,path,canOverwrite};if(uploadConflictDialog==dialog)uploadConflictDialog=nullptr;return;}
        if(!source || uploadConflictDialog!=dialog)return;
        const auto button=dialog->clickedButton();
        QString choice=button==overwrite?"overwrite":button==keep?"keep":"cancel";
        if(source!=transferSession || id!=activeTransfer || !online || shuttingDown)choice="cancel";
        const bool accepted=source->answerUploadConflict(id,nonce,choice,all->isChecked());
        if(accepted && choice=="cancel")cancelledTransfers.insert(id);
        if(accepted && all->isChecked() && choice!="cancel") {
            const auto batch=transfers.value(id).options.value("uploadBatch").toString();
            if(!batch.isEmpty())for(auto it=transfers.begin();it!=transfers.end();++it)
                if(it->options.value("uploadBatch").toString()==batch)it->collision=choice;
        }
        if(uploadConflictDialog==dialog)uploadConflictDialog=nullptr;
    });
    dialog->open(); // asynchronous; the GUI never waits for the worker
}
bool Workspace::queueTransfers(const QStringList &input, bool upload, const QString &remote, bool clipboardPaste, std::shared_ptr<FileClipboardBatch> hold) {
    if(OperationGate::locked())return false;const auto lockEpoch=OperationGate::token();
    if (demo || !online)
        return false;
    if(!upload)diagnose(DiagEvent::DownloadRequested,session?session->diagnosticTag:0,0,{},input.size());
    QString local;
    if (!upload) {
        local = QFileDialog::getExistingDirectory(
            this, "选择下载位置",
            QSettings()
                .value("downloads/path", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
                .toString());
        if (local.isEmpty())
            return false;
        QSettings().setValue("downloads/path", local);
    }
    QString collision="ask";
    if(upload && (!remote.startsWith('/') || remote.contains(QChar::Null))) {
        AppMessageBox::warning(this,"无法上传","没有有效的远端目标目录，请重新选择。"); return false;
    }
    if(!upload) {
        bool ok=false;
        const auto choice=QInputDialog::getItem(this,"同名文件处理","对本批次所有重名文件",{"保留两份","覆盖","跳过"},0,false,&ok);
        if(!ok)return false;
        collision=choice=="覆盖"?"overwrite":choice=="跳过"?"skip":"keep";
    }
    if(!OperationGate::accepts(lockEpoch))return false;
    QStringList paths = input;
    if (upload) for(const auto &path:paths) if(clipboardMutationBlocked(remoteJoin(remote,QFileInfo(path).fileName()))) {
        taskSummary->setText("上传目标正在移动，请稍后重试。");return false;
    }
    std::sort(paths.begin(), paths.end());
    QStringList roots;
    for (auto p : paths) {
        bool covered = false;
        for (auto r : roots)
            if (p == r || p.startsWith(r + "/")) {
                covered = true;
                break;
            }
        if (!covered)
            roots << p;
    }
    if (roots.size() + pendingTransfers() > 256) {
        AppMessageBox::warning(this, "传输队列已满", "最多保留 256 个未完成根任务，请分批传输。");
        return false;
    }
    if (roots.isEmpty())
        return false;
    auto batch =
        new QTreeWidgetItem(upload ? uploadList : downloadList,
                            {QString("%1 · %2 项").arg(upload ? "上传" : "下载").arg(roots.size()), "等待"});
    batch->setExpanded(true);
    const auto batchId=QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (const auto &path : roots) {
        Job j;
        j.type = upload ? "upload" : "download";
        j.id = nextJob++;
        j.path =
            upload ? remoteJoin(remote, QFileInfo(path).fileName()) : path;
        j.destination = upload ? path : QDir(local).filePath(basename(path));
        j.collision = collision;
        if(!upload)j.options["downloadOrigin"]=1;
        if(upload) j.options["uploadBatch"]=batchId;
        if(upload) {j.options["operationBatch"]=batchId;j.options["operationBatchOpen"]=true;uploadJournalPending[batchId].insert(j.id);}
        if(clipboardPaste) j.options["clipboardPaste"]=true;
        if(hold) clipboardUploadHolds[j.id]=hold;
        transfers[j.id] = j;
        transferQueue.enqueue(j.id);
        auto i = new QTreeWidgetItem(batch, {basename(j.path), "等待"});
        i->setData(0, Qt::UserRole, j.id);
        i->setToolTip(0, j.path + "\n" + j.destination);
        transferItems[j.id] = i;
        transferOrder.append(j.id);
        if(!upload)diagnose(DiagEvent::DownloadQueued,session?session->diagnosticTag:0,j.id,{},roots.size());
    }
    transferPanel->show();
    transferPanel->raise();
    trimTransferHistory();
    ensureTransferSession();
    emit stateChanged();
    return true;
}
void Workspace::finishUploadJournalRoot(int id,bool uncertain) {
    const auto batch=transfers.value(id).options.value("operationBatch").toString();
    auto it=uploadJournalPending.find(batch);
    if(batch.isEmpty() || it==uploadJournalPending.end() || !it->remove(id))return;
    if(uncertain)uploadJournalUncertain.insert(batch);
    if(!it->isEmpty())return;
    uploadJournalPending.erase(it);
    if(uploadJournalUncertain.remove(batch)) {taskSummary->setText("上传批次结果待核查，未自动重试。");return;}
    if(!transferSession || !transferReady) {taskSummary->setText("上传结果未知，请刷新核实远端；关闭该服务器的相关连接后重连可清空撤销历史。");return;}
    Job seal;seal.id=nextJob++;seal.type="journalSeal";seal.options["operationBatch"]=batch;
    uploadJournalSealJobs.insert(seal.id);transferSession->submit(seal);
}
void Workspace::ensureTransferSession() {
    if (!online || shuttingDown)
        return;
    bool runnable = false;
    for (int id : transferQueue)
        runnable |= !pausedTransfers.contains(id);
    if (!runnable)
        return;
    transferIdleTimer.stop();
    if (transferSession && transferStopping) {
        transferRetiring = true;
        return;
    }
    if (!transferSession) {
        if (keyNeedsReview || (!credential && profile.authentication != "agent"))
            promptCredential(true);
        else
            startSession(true);
    } else if (transferReady)
        startTransfer();
}
void Workspace::startTransfer() {
    if (!transferReady || !online || shuttingDown || activeTransfer)
        return;
    int runnable = -1;
    for (int i = 0; i < transferQueue.size(); ++i) {
        if (!pausedTransfers.contains(transferQueue[i])) {
            runnable = i;
            break;
        }
    }
    if (runnable < 0) {
        taskSummary->setText(transferQueue.isEmpty() ? "任务处理完成" : "任务已暂停");
        transferIdleTimer.start();
        return;
    }
    const int candidate=transferQueue[runnable];
    if(clipboardValid.contains(candidate) && !clipboardValid[candidate]()) {
        transferQueue.removeAt(runnable);
        if(transferItems.contains(candidate))transferItems[candidate]->setText(1,"剪贴板任务已失效");
        completeClipboardJob(candidate,{},"剪贴板来源或连接身份已失效");startTransfer();return;
    }
    if(dragValid.contains(candidate)&&!dragValid[candidate]()){
        transferQueue.removeAt(runnable);auto done=dragCompletions.take(candidate);dragValid.remove(candidate);
        if(transferItems.contains(candidate))transferItems[candidate]->setText(1,"拖出已失效 · 请重新拖出");
        if(done)done("拖出身份或来源已失效");startTransfer();return;
    }
    activeTransfer = transferQueue.takeAt(runnable);
    transferItems[activeTransfer]->setText(1, "传输中");
    transferItems[activeTransfer]->setData(1, Qt::UserRole, QVariant());
    taskSummary->setText(transfers[activeTransfer].type == "move" ? "正在移动" : transfers[activeTransfer].type == "upload" ? "↑ 上传 0 B/s" : "↓ 下载 0 B/s");
    transferSession->pauseTransfer(false);
    if(transfers[activeTransfer].type=="download") {
        const auto &options=transfers[activeTransfer].options;
        const int origin=options.value("nativeDrop").toBool()?3:options.value("clipboardStage").toBool()?2:options.value("downloadOrigin").toInt()==1?1:0;
        diagnose(DiagEvent::TransferStart,transferSession->diagnosticTag,activeTransfer,{},origin);
    }
    transferSession->submit(transfers[activeTransfer]);
    emit stateChanged();
}
void Workspace::trimTransferHistory() {
    int finished = transferOrder.size() - pendingTransfers();
    for (auto it = transferOrder.begin(); it != transferOrder.end() && finished > 100;) {
        int id = *it;
        if (id == activeTransfer || transferQueue.contains(id)) {
            ++it;
            continue;
        }
        auto item = transferItems.take(id);
        auto batch = item ? item->parent() : nullptr;
        delete item;
        if (batch && batch->childCount() == 0)
            delete batch;
        transfers.remove(id);
        clipboardUploadHolds.remove(id);
        pausedTransfers.remove(id);
        cancelledTransfers.remove(id);
        it = transferOrder.erase(it);
        --finished;
    }
}
void Workspace::updateTransferBatches() {
    for (auto list : {uploadList, downloadList}) {
        for (int n = 0; n < list->topLevelItemCount(); ++n) {
            auto batch = list->topLevelItem(n);
            int completed = 0, pending = 0, failed = 0;
            for (int i = 0; i < batch->childCount(); ++i) {
                auto child = batch->child(i);
                int id = child->data(0, Qt::UserRole).toInt();
                if (id == activeTransfer || transferQueue.contains(id))
                    ++pending;
                else if (child->text(1).contains("失败"))
                    ++failed;
                else
                    ++completed;
            }
            batch->setText(
                1, QString("%1 结束 · %2 等待/传输 · %3 失败").arg(completed).arg(pending).arg(failed));
        }
    }
}
void Workspace::transferMenu(QTreeWidget *list, QPoint p) {
    auto i = list->itemAt(p);
    if (!i)
        return;
    while (i->parent() && !i->data(0, Qt::UserRole).toInt())
        i = i->parent();
    QList<int> ids;
    if (transfers.contains(i->data(0, Qt::UserRole).toInt()))
        ids << i->data(0, Qt::UserRole).toInt();
    else
        for (int child = 0; child < i->childCount(); ++child)
            ids << i->child(child)->data(0, Qt::UserRole).toInt();
    QMenu m;
    bool pending = false, retry = false;
    for (int id : ids) {
        pending |= id == activeTransfer || transferQueue.contains(id);
        retry |= transfers.value(id).type!="move" && !transfers.value(id).options.value("clipboardStage").toBool() && !transfers.value(id).options.value("nativeDrop").toBool() && transferItems.contains(id) && transferItems[id]->text(1).contains("失败");
    }
    if (pending) {
        m.addAction(ids.size() > 1 ? "暂停批次" : "暂停", this, [this, ids] {
            for (int id : ids) {
                if (id != activeTransfer && !transferQueue.contains(id))
                    continue;
                pausedTransfers.insert(id);
                transferItems[id]->setText(1, "已暂停\n0 B/s");
                if (id == activeTransfer) taskSummary->setText(transfers[id].type == "upload" ? "↑ 上传已暂停 · 0 B/s" : "↓ 下载已暂停 · 0 B/s");
                if (id == activeTransfer && transferSession)
                    transferSession->pauseTransfer(true);
            }
        });
        m.addAction("继续 / 重新连接传输", this, [this, ids] {
            for (int id : ids) {
                pausedTransfers.remove(id);
                if (id == activeTransfer && transferSession) {
                    transferSession->pauseTransfer(false);
                    transferItems[id]->setText(1, "传输中\n0 B/s");
                    taskSummary->setText(transfers[id].type == "upload" ? "↑ 上传 0 B/s" : "↓ 下载 0 B/s");
                }
                if (transferQueue.contains(id))
                    transferItems[id]->setText(1, online ? "等待" : "等待连接");
            }
            ensureTransferSession();
        });
    }
    if (retry)
        m.addAction("失败项从头重试", this, [this, ids] {
            for (int id : ids) {
                auto item = transferItems.value(id);
                if (!item || transfers.value(id).type=="move" || transfers.value(id).options.value("clipboardStage").toBool() || transfers.value(id).options.value("nativeDrop").toBool() || id == activeTransfer || transferQueue.contains(id) ||
                    !item->text(1).contains("失败"))
                    continue;
                if (pendingTransfers() >= 256)
                    break;
                qDeleteAll(item->takeChildren());
                item->setData(1, Qt::UserRole, QVariant());
                item->setToolTip(1, {});
                item->setText(1, online ? "等待" : "等待连接");
                if(transfers[id].type=="upload") {transfers[id].options.remove("operationBatch");transfers[id].options.remove("operationBatchOpen");}
                transferQueue.enqueue(id);
            }
            updateTransferBatches();
            ensureTransferSession();
        });
    if (pending || retry) m.addSeparator();
    m.addAction("任务详情", this, [this, ids] {
        QStringList details;
        for (int id : ids) {
            if (!transferItems.contains(id))
                continue;
            const auto j = transfers[id];
            auto item = transferItems[id];
            details << j.path + "\n↔ " + j.destination + "\n" + item->text(1) + "\n" + item->toolTip(1);
        }
        AppMessageBox::information(this, "任务详情", details.join("\n\n"));
    });
    if (pending) {
        m.addSeparator();
        m.addAction(ids.size() > 1 ? "取消批次" : "取消", this, [this, ids] {
            for (int id : ids) {
                if (id == activeTransfer && transferSession) {
                    cancelledTransfers.insert(id);
                    transferSession->cancelTransfer();
                } else if (transferQueue.removeAll(id)) {
                    if(transferItems.contains(id))transferItems[id]->setText(1, "已取消");
                    if(transfers.value(id).type=="upload")finishUploadJournalRoot(id);
                }
                if(transfers.value(id).options.value("nativeDrop").toBool()&&id!=activeTransfer){auto done=dragCompletions.take(id);dragValid.remove(id);if(done)done("用户取消了拖出下载");}
                if(id!=activeTransfer && clipboardCompletions.contains(id))completeClipboardJob(id,{},"用户取消了剪贴板任务");
                pausedTransfers.remove(id);
            }
            updateTransferBatches();
            trimTransferHistory();
            emit stateChanged();
        });
    }
    m.exec(list->viewport()->mapToGlobal(p));
}
int Workspace::pendingTransfers() const {
    return transferQueue.size() + (activeTransfer ? 1 : 0);
}
bool Workspace::dirty() const {
    for (auto e : editors)
        if (e && e->dirty())
            return true;
    return false;
}
int Workspace::dirtyFileCount() const {int count=0;for(auto e:editors)if(e&&e->dirty())++count;return count;}
QByteArray Workspace::closeSignature() const {
    QByteArrayList parts;for(auto e:editors)if(e&&e->dirty())parts<<QByteArray::number(qulonglong(quintptr(e.data())))+":"+QByteArray::number(e->code->document()->revision());
    parts<<transferSignature();
    std::sort(parts.begin(),parts.end());return parts.join('|');
}
QByteArray Workspace::transferSignature() const {QByteArrayList parts;for(int id:transferQueue)parts<<"t"+QByteArray::number(id);if(activeTransfer)parts<<"a"+QByteArray::number(activeTransfer);std::sort(parts.begin(),parts.end());return parts.join(',');}
bool Workspace::canApproveExit(const QByteArray &signature) const {return !closing&&pendingSaves.isEmpty()&&closeSignature()==signature;}
void Workspace::discardExitDrafts(){for(auto e:editors)if(e)e->discardDraft();}
bool Workspace::saveForExit(){
    if(closing||!pendingSaves.isEmpty()||(!online&&!demo))return false;
    closing=true;for(auto e:editors)if(e&&e->dirty())saveEditor(e);
    if(pendingSaves.isEmpty()) {closing=false;QTimer::singleShot(0,this,[this]{if(dirty())emit closeCancelled();else emit readyToClose();});}
    return true;
}
bool Workspace::requestClose() {
    const auto lockEpoch=OperationGate::token();if(!OperationGate::accepts(lockEpoch))return false;
    const int count=dirtyFileCount();const auto signature=closeSignature();
    QString summary="是否确认关闭：<br><b style='color:"+colors().accent.name()+"'>"+
        (profile.name+" · "+profile.user+"@"+profile.host+(profile.port!=22?QString(":%1").arg(profile.port):QString())).toHtmlEscaped()+"</b>";
    if(count)summary+=QString("<br>%1 个未保存文件").arg(count);
    if(pendingTransfers())summary+=QString("<br>%1 个未完成的传输").arg(pendingTransfers());
    if(tunnelCount())summary+=QString("<br>%1 条端口转发").arg(tunnelCount());
    auto choices = count ? (QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel)
                         : (QMessageBox::Close | QMessageBox::Cancel);
    AppMessageBox confirm(QMessageBox::Warning,"关闭服务器工作区",summary,choices,this);confirm.setTextFormat(Qt::RichText);confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.exec();auto result=confirm.standardButton(confirm.clickedButton());
    if (!OperationGate::accepts(lockEpoch)||(result!=QMessageBox::Save&&result!=QMessageBox::Discard&&result!=QMessageBox::Close))
        return false;
    if(!canApproveExit(signature)){AppMessageBox::information(this,"状态已变化","文件或传输状态已变化，请重新确认关闭。");return false;}
    if (result == QMessageBox::Save) {
        if (!online && !demo) {
            AppMessageBox::warning(this, "无法保存", "连接已断开，请重连后保存，或保留窗口中的草稿。");
            return false;
        }
        closing = true;
        for (auto e : editors)
            if (e && e->dirty())
                saveEditor(e);
        if (demo) {
            closing = false;
            return !dirty();
        }
        return false;
    }
    for (auto e : editors)
        if (e)
            e->discardDraft();
    return true;
}
void Workspace::shutdown() {
    cancelBookmarkIntent();
    if (shuttingDown)
        return;
    saveWorkspaceLayout();
    emit aboutToShutdown();
    cancelEntryDraft();
    shuttingDown = true;
    cancelFileDrags();
    ++mainAttempt;mainPending=false;syncConnectionNotice();
    stopTunnels();
    stopServices();stopListening("连接已断开或身份已变化；上次端口结果已过期");
    monitorTimer.stop();
    setEnabled(false);
    connectionStatus->setText("正在关闭会话、传输及端口转发…");
    if (session)
        session->stop();
    if (transferSession)
        transferSession->stop();
    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this, timer] {
        if ((!session || !session->isRunning()) && (!transferSession || !transferSession->isRunning()) && !tunnelService && !hasServiceConnection()) {
            timer->stop();
            timer->deleteLater();
            emit shutdownComplete();
        }
    });
    timer->start(50);
}
void Workspace::setActive(bool value) {
    active = value;
    if(serverClock)serverClock->setActive(value&&isVisible());
    if(value)ensureServerClock();
    syncConnectionNotice();
    servicesPanel->setActivity(value);
    monitorTimer.setInterval(value ? 2000 : 10000);
    for (auto d : floating)
        d->setVisible(value);
}
void Workspace::refreshTheme() {
    connectionLoading->refreshTheme();connectionBar->refreshTheme();
    miniListeningState->setStyleSheet("color:"+colors().accent.name()+";font-weight:500;");
    if(auto legend=findChild<QLabel *>("networkLegend"))legend->setText(QString("<span style='color:%1'>— 接收 RX</span>&nbsp;&nbsp;&nbsp;<span style='color:%2'>┄ 发送 TX</span>").arg(networkReceiveColor().name(),networkSendColor().name()));
    resourceSummary->update();networkRate->update();networkPlot->update();
    servicesPanel->refreshTheme();
    portsPanel->refreshTheme();
    auto latency = connectionStatus->property("latency").toLongLong();
    connectionStatus->setStyleSheet("color:" + (demo             ? colors().muted
                                                : mainPending||mainResponseWaiting ? colors().orange
                                                : !online        ? colors().red
                                                : latency >= 500 ? colors().red
                                                : latency >= 150 ? colors().orange
                                                                 : colors().green)
                                                   .name());
    for (auto &t : terminals)
        t.view->refreshTheme();
    for (auto e : editors)
        if (e) {
            e->code->refreshTheme();
            e->refreshFileIcon();
            updateEditorTitle(e);
        }
    for (auto comparison : findChildren<EditorCompare *>()) comparison->refreshTheme();
    QTreeWidgetItemIterator it(files);
    for (; *it; ++it) {
        if ((*it)->data(0, ParentNavigationRole).toBool()) {
            (*it)->setIcon(0, icon(Icon::Up));
            continue;
        }
        const auto info = (*it)->data(0, Qt::UserRole).toMap();
        if (!info["path"].toString().isEmpty())
            (*it)->setIcon(0, info["directory"].toBool() ? FileIconProvider::directory() : FileIconProvider::file(info["path"].toString()));
    }
}
void Workspace::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    int w = qMax(1,qMin(uiPx(460), width() - uiPx(20))), h = qMax(1,qMin(uiPx(310), height() - uiPx(50)));
    transferPanel->setGeometry(width() - w - uiPx(12), height() - h - uiPx(42), w, h);
    if(connectionLoading)connectionLoading->setGeometry(connectionLoading->parentWidget()->rect());
}
void Workspace::fail(const QString &type, int id, const QString &reason) {
    if(type=="open-terminal"&&terminals.contains(id)){terminals[id].running=false;terminals[id].view->feed("\r\n[终端启动失败，历史保留]\r\n");rebuildTerminals();}
    if(type=="list"&&bookmarkIntent&&bookmarkIntent->job==id){
        cancelBookmarkIntent();bookmarkStatus->setText("无法打开收藏目录："+reason);bookmarkStatus->show();return;
    }
    if (type == "touch" || type == "mkdir" || type == "delete") {
        const auto path = fileMutations.take(id);
        if (!path.isEmpty()) {
            if(type=="delete") {
                DiagnosticScope mutation(DiagEvent::DeleteUiFailedBegin,DiagEvent::DeleteUiEnd,session?session->diagnosticTag:0,id,path);
                syncFileMutation(path,false,id);
            } else syncFileMutation(path,false,id);
        }
        if (entryDraft.job == id) {
            entryDraftError(reason);
            return;
        }
    }
    if (type == "monitor") {
        monitorPending = false;
        serverPlatform->setText("监控暂不可用");serverArchitecture->setText("—");resourceSummary->clear();networkRate->setRates({});
        return;
    }
    if (type == "read") {
        pendingReads.remove(id);
        pendingConflicts.remove(id);
    }
    if (type == "list") {
        if (!directoryRequests.contains(id))
            return;
        const auto request = directoryRequests.take(id);
        if(id==restoringDirectoryRequest){
            restoringDirectoryRequest=0;
            const QString fallback=profile.directory.isEmpty()||profile.directory==request.path?QString("."):profile.directory;
            AppMessageBox::warning(this,"无法恢复资源目录","重连后无法打开原目录："+request.path+"\n"+reason+"\n将尝试连接的默认目录；终端仍按各自的启动目录打开。");
            navigate(fallback);return;
        }
        if (request.generation != directoryGeneration || newestDirectoryRequest.value(request.path) != id)
            return;
        newestDirectoryRequest.remove(request.path);
        if (entryDraft.waiting && entryDraft.parentPath == request.path) {
            entryDraft.waiting = false;
            showEntryDraft();
            entryDraftError("目录载入失败：" + reason + "；可修正后重新提交。");
            return;
        }
        // A vanished expansion target must not surface a stale modal error.
        if (!request.navigation && request.path != directory && !request.parent.isValid())
            return;
    }
    if (type == "save") {
        auto editor = pendingSaves.take(id);
        closing = false;
        emit closeCancelled();
        if (editor) {
            editor->saving = false;
            closeAfterSave.remove(editor);
            if (reason.startsWith("CONFLICT:")) {
                Job read;
                read.type = "read";
                read.path = editor->path;
                read.id = nextJob++;
                pendingConflicts[read.id] = editor;
                session->submit(read);
                return;
            }
        }
    }
    AppMessageBox::warning(this, "操作失败 · " + profile.name, reason);
}
void Workspace::resolveConflict(Editor *editor, const QVariantMap &remote) {
    if(OperationGate::locked())return;
    QPointer<Editor> guarded(editor);
    QDialog dialog(this);
    dialog.setWindowTitle("文件冲突 · " + profile.identity());
    dialog.resize(1000, 650);
    auto layout = new QVBoxLayout(&dialog);
    auto label =
        new QLabel(editor->path + "\n远端文件已变化，请比较后选择处理方式。关闭此窗口会保留本地草稿。");
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto split = new PanelSplitter;
    auto raw = remote["data"].toByteArray();
    for (int side = 0; side < 2; ++side) {
        auto pane = new QWidget;
        auto column = new QVBoxLayout(pane);
        uiMargins(column,0,0,0,0);
        column->addWidget(new QLabel(side ? "远端当前内容" : "本地未保存内容"));
        auto view = new CodeEdit;
        view->setPlainText(side ? QString::fromUtf8(raw.startsWith("\xef\xbb\xbf") ? raw.mid(3) : raw)
                                : editor->code->toPlainText());
        view->setReadOnly(true);
        column->addWidget(view);
        split->addWidget(pane);
    }
    layout->addWidget(split, 1);
    auto buttons = new QDialogButtonBox;
    auto keep = buttons->addButton("保留本地草稿", QDialogButtonBox::RejectRole);
    auto reload = buttons->addButton("重新载入远端", QDialogButtonBox::ActionRole);
    auto overwrite = buttons->addButton("用本地内容覆盖远端", QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(keep, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(reload, &QPushButton::clicked, &dialog, [&] {
        if (guarded && AppMessageBox::question(&dialog, "重新载入", "放弃此文件的本地修改，载入右侧远端内容？",
                                             QMessageBox::Yes | QMessageBox::Cancel,
                                             QMessageBox::Cancel) == QMessageBox::Yes) {
            guarded->reload(raw, remote["hash"].toByteArray());
            if (remote["link"].toBool() || !(remote["permissions"].toUInt() & 0222))
                guarded->code->setReadOnly(true);
            dialog.accept();
        }
    });
    connect(overwrite, &QPushButton::clicked, &dialog, [&] {
        if (!guarded)
            return;
        // Compare against the version shown here; another change must open a new conflict.
        guarded->remoteHash = remote["hash"].toByteArray();
        saveEditor(guarded);
        dialog.accept();
    });
    dialog.exec();
}
void Workspace::monitor(const QByteArray &bytes, qint64 latency) {
    DiagnosticScope monitorScope(DiagEvent::MonitorUiBegin,DiagEvent::MonitorUiEnd,session?session->diagnosticTag:0,0,{},bytes.size());
    QString text = QString::fromUtf8(bytes);
    auto section = [&](QString name) {
        auto p = text.indexOf(name);
        if (p < 0)
            return QString();
        p += name.size();
        static const QRegularExpression marker("^__(?:OS|ARCH|CPU|MEM|UP|NET|DISK|PROC)__\\r?$",QRegularExpression::MultilineOption);
        auto next=marker.match(text,p);auto end=next.hasMatch()?next.capturedStart():-1;
        return text.mid(p, end < 0 ? -1 : end - p).trimmed();
    };
    auto cpu = section("__CPU__").split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    quint64 total = 0, idle = 0;
    for (int i = 1; i < qMin(9, cpu.size()); ++i)
        total += cpu[i].toULongLong();
    if (cpu.size() > 4)
        idle = cpu[4].toULongLong() + (cpu.size() > 5 ? cpu[5].toULongLong() : 0);
    std::optional<double> percent;
    if(cpu.size()>4 && previousTotal && total>previousTotal && idle>=previousIdle && idle-previousIdle<=total-previousTotal)
        percent=qBound(0.,100.*(1.-double(idle-previousIdle)/double(total-previousTotal)),100.);
    previousTotal = cpu.size()>4?total:0;
    previousIdle = idle;
    QHash<QString, quint64> memory;
    for (auto line : section("__MEM__").split('\n')) {
        auto parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() > 1)
            memory[parts[0]] = parts[1].toULongLong() * 1024;
    }
    memoryTotal = memory["MemTotal:"];
    const bool memoryReady=memoryTotal && memory.contains("MemAvailable:");
    const quint64 used=memoryReady&&memoryTotal>memory["MemAvailable:"]?memoryTotal-memory["MemAvailable:"]:0;
    const auto up=section("__UP__").split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);bool upOk=false;
    const double seconds=up.value(0).toDouble(&upOk);
    const auto disk=section("__DISK__").split('\n').value(1).split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
    bool diskOk=false;const quint64 diskTotal=disk.value(1).toULongLong(&diskOk)*1024;
    bool usedOk=false;const quint64 diskUsed=disk.value(2).toULongLong(&usedOk)*1024;
    resourceSummary->setMetrics(percent,used,memoryReady?memoryTotal:0,diskUsed,diskOk&&usedOk?diskTotal:0,upOk&&seconds>=0?std::optional(seconds):std::nullopt);
    QString os=cpu.size()>4?"Linux":"系统信息 —";
    for(const auto &line:section("__OS__").split('\n'))if(line.startsWith("PRETTY_NAME=")){os=line.mid(12);if(os.startsWith('"')&&os.endsWith('"'))os=os.mid(1,os.size()-2);break;}
    serverSummary->setText(profile.name);serverPlatform->setText(os);serverPlatform->setProperty("lastPlatform",os);resourceSummary->setToolTip({});serverArchitecture->setText(section("__ARCH__").isEmpty()?QString("—"):section("__ARCH__"));
    if(!mainResponseWaiting){connectionStatus->setText(QString("● SSH: %1   %2 ms   已连接").arg(profile.name).arg(latency));
    connectionStatus->setToolTip("系统采样响应耗时，包含远端执行及 SSH 往返；颜色仅提示，不触发断线。并非纯网络 RTT 或 ICMP Ping。");
    connectionStatus->setProperty("latency", latency);
    connectionStatus->setStyleSheet("color:" + (latency < 150   ? colors().green
                                                : latency < 500 ? colors().orange
                                                                : colors().red)
                                                   .name());}
    double elapsed = networkClock.isValid() ? networkClock.restart() / 1000.0 : 0;
    if (!networkClock.isValid())
        networkClock.start();
    QString selected = nic->currentText();
    QStringList names;QHash<QString,QPair<quint64,quint64>> nextNetwork;QHash<QString,QPair<double,double>> nextRates;
    for (const auto &line : section("__NET__").split('\n')) {
        int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        QString name = line.left(colon).trimmed();
        auto v = line.mid(colon + 1).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (v.size() < 9)
            continue;
        names << name;
        bool rxOk=false,txOk=false;quint64 rx = v[0].toULongLong(&rxOk), tx = v[8].toULongLong(&txOk);if(!rxOk||!txOk)continue;
        auto previous = lastNetwork.value(name);
        if (elapsed > 0 && lastNetwork.contains(name) && rx >= previous.first && tx >= previous.second)
            nextRates[name] = {double(rx - previous.first) / elapsed,
                                  double(tx - previous.second) / elapsed};
        nextNetwork[name] = {rx, tx};
    }
    lastNetwork=nextNetwork;networkRates=nextRates;
    {
        QSignalBlocker block(nic);
        QStringList previousNames;
        for (int n = 0; n < nic->count(); ++n)
            previousNames.append(nic->itemText(n));
        if (previousNames != names) {
            nic->hidePopup();
            nic->clear();
            nic->addItems(names);
        }
        int i = nic->findText(preferredNic);
        if (i < 0)
            i = nic->findText(selected);
        if (i >= 0)
            nic->setCurrentIndex(i);
    }
    if(demo){connectionStatus->setText("预览模式 · 模拟数据 · 未连接服务器");connectionStatus->setStyleSheet("color:"+colors().muted.name());}
    networkPlot->add(networkRates);networkPlot->select(nic->currentText());
    networkRate->setRates(networkRates.contains(nic->currentText())?std::optional(networkRates.value(nic->currentText())):std::nullopt);
    if(!text.contains("__PROC__\n"))return;
    auto lines = section("__PROC__").split('\n', Qt::SkipEmptyParts);
    QVector<ProcessInfo> snapshot;
    for (int n = 1; n < lines.size(); ++n) {
        auto p = lines[n].trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (p.size() < 6)
            continue;
        snapshot.append({p[0].toLongLong(), p.mid(5).join(' '), p[1], p[4], p[2].toDouble(),
                         p[3].toULongLong() * 1024});
    }
    std::stable_sort(snapshot.begin(),snapshot.end(),[](const auto &a,const auto &b){return a.cpu>b.cpu;});
    miniProcesses->applySnapshot(snapshot.mid(0, 5), memoryTotal);
    if (processPage)
        processes->applySnapshot(snapshot, memoryTotal);
}
void Workspace::demoData() {
    online = false;
    directory = "/home/demo/project";
    homeDirectory = "/home/demo";
    updateBreadcrumbs();
    if (files->topLevelItemCount() == 0) {
        for (const auto &name : QStringList{"logs", "src", "config.json", "README.md"}) {
            bool folder = !name.contains('.');
            auto item = new FileItem(files, {name, folder ? QString() : QString("128 B")});
            item->setData(
                0, Qt::UserRole,
                QVariantMap{{"name", name}, {"path", directory + "/" + name}, {"directory", folder}});
            item->setIcon(0, folder ? FileIconProvider::directory() : FileIconProvider::file(directory + "/" + name));
        }
    }
    syncParentNavigation();
    if (historyIndex < 0) { history << directory; historyIndex = 0; }
    connectionStatus->setText("预览模式 · 未连接服务器 · 不执行远端操作");
    if (terminals.isEmpty()) {
        addTerminal();
        addTerminal(focusedTerm ? terminals[focusedTerm].group : 0);
    }
    if (editors.isEmpty()) {
        const QVector<QPair<QString,QByteArray>> samples = {
            {"config.json", "{\n  \"host\": \"127.0.0.1\",\n  \"port\": 3000,\n  \"environment\": \"staging\"\n}\n"},
            {"deploy.sh", QString("#!/bin/sh\n# 合成部署脚本\necho 'offline demo'\n").toUtf8()},
            {"README.md", QString("# FrogShell\n\nQt 原生桌面工作区\n\n这是独立的界面预览，不会连接服务器。\n").toUtf8()},
            {"service.yaml", "service:\n  name: rabbitshell\n  replicas: 3\n"}
        };
        for (const auto &[name, content] : samples) {
            const QString path=directory+"/"+name;
            auto e = new Editor(profile.id, path, content,
                                QCryptographicHash::hash(content, QCryptographicHash::Sha256));
            editors[path] = e;
            editArea->addPage(e, name);
            connect(e, &Editor::saveRequested, this, &Workspace::saveEditor);
            connect(e, &Editor::changed, this, [this, e] { updateEditorTitle(e); });
        }
        auto config=editors.value(directory+"/config.json");
        auto deploy=editors.value(directory+"/deploy.sh");
        auto readme=editors.value(directory+"/README.md");
        auto service=editors.value(directory+"/service.yaml");
        auto first=editArea->groupFor(config);
        editArea->movePage(readme,first,DropZone::Right);
        auto last=editArea->groupFor(readme);
        editArea->movePage(deploy,first,DropZone::Right);
        editArea->movePage(service,last);
        // The initial synthetic example starts with three equal columns; later
        // user splits divide only their target and never rearrange direction.
        if (auto split=qobject_cast<QSplitter *>(first->parentWidget())) split->setSizes({1,1,1});
        editArea->activatePage(readme);
    }
    monitor("__CPU__\ncpu 15 0 10 200 0 0 0 0\n__MEM__\nMemTotal: 8388608 kB\nMemAvailable: 4613734 "
            "kB\n__UP__\n2476800 0\n0.32 0.28 0.21\n__NET__\neth0: 10000 0 0 0 0 0 0 0 5000 0\neth1: 8000 0 "
            "0 0 0 0 0 0 3000 0\n__DISK__\nFilesystem 1024-blocks Used Available Capacity Mounted\n/dev/sda1 "
            "167772160 88080384 79691776 53% /\n__PROC__\nPID USER %CPU RSS STAT COMMAND\n18917 service 12.4 "
            "1363149 S java\n21348 deploy 4.8 301977 R node\n18923 redis 1.2 679526 S redis-server\n21483 "
            "www-data 0.6 67072 S nginx\n",
            32);
    connectionStatus->setText("预览模式 · 模拟数据 · 未连接服务器");
    connectionStatus->setStyleSheet("color:" + colors().muted.name());
}
} // namespace rs
