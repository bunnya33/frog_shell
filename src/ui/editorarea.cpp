#include "../core/operationgate.h"
#include "editorarea.h"
#include "editorcompare.h"
#include "theme.h"
#include "icons.h"
#include "fileicons.h"
#include "layoutmetrics.h"
#include <QDrag>
#include <QMimeData>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif
namespace rs {
namespace {
constexpr auto tabMimeType = "application/x-rabbitshell-editor-tab";
// The guard exists only for QDrag's nested event loop. IgnoreAction alone cannot
// distinguish an explicit release outside the window from Escape/native cancel.
class DragReleaseGuard : public QObject {
  public:
    bool released = false, cancelled = false;
    QPoint releasePosition;
    std::function<void(QPoint)> feedback;
    explicit DragReleaseGuard(std::function<void(QPoint)> update) : feedback(std::move(update)) {
        qApp->installEventFilter(this);
#ifdef Q_OS_WIN
        current = this;
        hook = SetWindowsHookExW(WH_GETMESSAGE, observeMessage, nullptr, GetCurrentThreadId());
#endif
    }
    ~DragReleaseGuard() override {
#ifdef Q_OS_WIN
        if (hook) UnhookWindowsHookEx(hook);
        current = nullptr;
#endif
        qApp->removeEventFilter(this);
    }
    bool eventFilter(QObject *, QEvent *event) override {
        if(event->type()==QEvent::MouseMove&&feedback)feedback(static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
            cancelled = true;
        if (event->type() == QEvent::MouseButtonRelease &&
            static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            released = true;
            releasePosition = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
        }
        return false;
    }
#ifdef Q_OS_WIN
    HHOOK hook = nullptr;
    static inline thread_local DragReleaseGuard *current = nullptr;
    static LRESULT CALLBACK observeMessage(int code, WPARAM removed, LPARAM value) {
        if (code >= 0 && removed == PM_REMOVE && current) {
            const auto message = reinterpret_cast<MSG *>(value);
            if(message->message==WM_MOUSEMOVE&&current->feedback)current->feedback(QCursor::pos());
            if (message->message == WM_KEYDOWN && message->wParam == VK_ESCAPE)
                current->cancelled = true;
            if (message->message == WM_LBUTTONUP) {
                current->released = true;
                current->releasePosition = QCursor::pos();
            }
        }
        return CallNextHookEx(nullptr, code, removed, value);
    }
#endif
};
}
class EditorDragMime final : public QMimeData {
  public:
    QPointer<EditorArea> owner;
    QPointer<EditorTabs> source;
    QPointer<QWidget> page;
    bool committed = false;
    EditorDragMime(EditorArea *owner, EditorTabs *source, QWidget *page)
        : owner(owner), source(source), page(page) {
        setData(tabMimeType, "internal-tab");
    }
};
class DropOverlay : public QWidget {
  public:
    explicit DropOverlay(QWidget *parent) : QWidget(parent) {
        setObjectName("editorDropPreview");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        hide();
    }
    bool insertion = false;
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        const auto c = colors();
        if (insertion) {
            p.fillRect(rect(), c.dropBorder);
        } else {
            p.fillRect(rect(), c.dropFill);
            p.setPen(QPen(c.dropBorder, uiReal(1)));
            p.drawRect(rect().adjusted(0,0,-1,-1));
        }
    }
};
EditorArea::EditorArea(QWidget *parent) : QWidget(parent) {
    setObjectName("editorArea");setProperty("rabbitUi",true);
    uiMinimumWidth(this,140);uiMinimumHeight(this,100);
    rootLayout = new QVBoxLayout(this);
    uiMargins(rootLayout,0,0,0,0);
    uiSpacing(rootLayout,0);
    root = makeGroup();
    rootLayout->addWidget(root);
    overlay = new DropOverlay(this);
    setActiveGroup(qobject_cast<EditorTabs *>(root));
    qApp->installEventFilter(this);
}
EditorArea::~EditorArea() {
    qApp->removeEventFilter(this);
    if (dragging) QDrag::cancel();
}
QList<EditorTabs *> EditorArea::groups() const {
    QList<EditorTabs *> result;
    std::function<void(QWidget *)> visit = [&](QWidget *node) {
        if (auto tabs = qobject_cast<EditorTabs *>(node)) result << tabs;
        else if (auto split = qobject_cast<QSplitter *>(node))
            for (int i=0;i<split->count();++i) visit(split->widget(i));
    };
    visit(root);
    return result;
}
EditorTabs *EditorArea::makeGroup() {
    auto group = new EditorTabs(this);
    uiMinimumWidth(group,120);uiMinimumHeight(group,80);
    auto actions = new PanelTabActions(group);
    actions->setObjectName("editorGroupActions");
    auto actionLayout = new QHBoxLayout(actions);
    uiMargins(actionLayout,3,3,3,3);
    uiSpacing(actionLayout,2);
    auto split = iconButton(Icon::SplitHorizontal, "向右拆分此组；菜单可选择向下拆分", actions);
    uiFixedWidth(split,40);
    auto merge = iconButton(Icon::Merge, "将所有编辑组合并到此组", actions);
    split->setObjectName("splitEditorGroup");
    merge->setObjectName("mergeEditorGroups");
    actionLayout->addWidget(split);
    actionLayout->addWidget(merge);
    group->setCornerWidget(actions, Qt::TopRightCorner);
    auto menu = new QMenu(split);
    split->setMenu(menu);
    split->setPopupMode(QToolButton::MenuButtonPopup);
    split->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(split, &QWidget::customContextMenuRequested, menu,
            [split, menu](QPoint point) { menu->popup(split->mapToGlobal(point)); });
    const QPointer<EditorTabs> guarded(group);
    auto splitGroup = [this, guarded](Qt::Orientation direction) {
        if (!guarded || !groups().contains(guarded) || guarded->count() < 2)
            return;
        setActiveGroup(guarded);
        splitActive(direction);
    };
    auto right = menu->addAction(icon(Icon::SplitHorizontal), "向右拆分", group,
                                 [splitGroup] { splitGroup(Qt::Horizontal); });
    auto down = menu->addAction(icon(Icon::SplitVertical), "向下拆分", group,
                                [splitGroup] { splitGroup(Qt::Vertical); });
    connect(split, &QToolButton::clicked, group, [splitGroup] { splitGroup(Qt::Horizontal); });
    connect(merge, &QToolButton::clicked, group, [this, guarded] {
        if (!guarded || !groups().contains(guarded))
            return;
        setActiveGroup(guarded);
        mergeAll();
    });
    connect(menu, &QMenu::aboutToShow, group, [this, guarded] {
        if (guarded) {
            guarded->actionMenuOpen = true;
            setActiveGroup(guarded);
        }
    });
    connect(menu, &QMenu::aboutToHide, group, [this, guarded] {
        if (guarded)
            guarded->actionMenuOpen = false;
        scheduleCleanup();
    });
    split->setEnabled(false);
    merge->setEnabled(false);
    connect(this, &EditorArea::layoutChanged, group, [this, group, split, merge, right, down] {
        const bool enabled = group->count() > 1;
        split->setEnabled(enabled);
        right->setEnabled(enabled);
        down->setEnabled(enabled);
        merge->setEnabled(groups().size() > 1);
    });
    connect(group, &EditorTabs::closeEditor, this, &EditorArea::closePageRequested);
    connect(group, &EditorTabs::floatEditor, this, &EditorArea::floatPageRequested);
    connect(group, &EditorTabs::moveEditor, this, &EditorArea::movePageRequested);
    connect(group, &EditorTabs::compareEditor, this, &EditorArea::openComparison);
    connect(group, &EditorTabs::dragRequested, this, [this,group](QWidget *page) { startDrag(group,page); });
    connect(group, &EditorTabs::interactionFinished, this, &EditorArea::scheduleCleanup);
    connect(group, &QTabWidget::currentChanged, this, [this,group] {
        if (!mutating && group->count()) { setActiveGroup(group); emit pagePresented(group->currentWidget()); }
        emit layoutChanged();
    });
    return group;
}
EditorTabs *EditorArea::activeGroup() const {
    const auto all = groups();
    return active && all.contains(active) ? active.data() : all.value(0);
}
void EditorArea::setActiveGroup(EditorTabs *group) {
    if (!group || !groups().contains(group)) return;
    if (active != group) {
        if (active) { active->setProperty("activeGroup", false); active->update(); active->tabBar()->update(); }
        active = group;
        group->setProperty("activeGroup", true);
        group->update();
        group->tabBar()->update();
    }
    emit layoutChanged();
}
EditorTabs *EditorArea::groupFor(QWidget *page) const {
    for (auto group : groups()) if (group->indexOf(page)>=0) return group;
    return nullptr;
}
void EditorArea::activatePage(QWidget *page) {
    if (auto group = groupFor(page)) {
        setActiveGroup(group);
        group->setCurrentWidget(page);
        emit pagePresented(page);
    }
}
void EditorArea::addPage(QWidget *page, const QString &label, EditorTabs *target) {
    if (page && page->property("terminalPage").toBool()) return;
    if (groupFor(page)) { activatePage(page); return; }
    if (!groups().contains(target)) target = activeGroup();
    const auto editor = qobject_cast<Editor *>(page);
    target->addTab(page,editor ? FileIconProvider::file(editor->path) : QIcon(),label);
    target->setCurrentWidget(page);
    page->show();
    setActiveGroup(target);
    emit layoutChanged();
    emit pagePresented(page);
}
void EditorArea::removePage(QWidget *page) {
    if (auto source = groupFor(page)) {
        const QScopedValueRollback<bool> migration(mutating,true);
        source->removeTab(source->indexOf(page));
        page->hide();
        page->setParent(this);
        scheduleCleanup();
        emit layoutChanged();
    }
}
void EditorArea::openComparison(Editor *source) {
    auto group = groupFor(source);
    if (OperationGate::locked() || !group) return;
    for (auto tabs : groups()) {
        for (int i = 0; i < tabs->count(); ++i) {
            auto comparison = qobject_cast<EditorCompare *>(tabs->widget(i));
            if (comparison && comparison->sourceEditor() == source) {
                activatePage(comparison);
                comparison->setFocus();
                return;
            }
        }
    }
    auto comparison = new EditorCompare(source);
    addPage(comparison,comparison->windowTitle(),group);
    comparison->setFocus();
}
bool EditorArea::movePage(QWidget *page, EditorTabs *target, DropZone zone, int index) {
    if (page && page->property("terminalPage").toBool()) return false;
    auto source = groupFor(page);
    if (!page || !source || !groups().contains(target)) return false;
    const bool edge = zone != DropZone::Center && zone != DropZone::TabBar;
    if (edge && source == target && source->count()<2) return false;
    if (edge && !target->count()) zone = DropZone::Center;
    if (source == target && (zone == DropZone::Center || zone == DropZone::TabBar)) {
        if (zone == DropZone::TabBar && index>=0) {
            int old = source->indexOf(page);
            if (index>old) --index;
            source->tabBar()->moveTab(old,qBound(0,index,source->count()-1));
        }
        activatePage(page);
        return true;
    }
    const QScopedValueRollback<bool> migration(mutating,true);
    const int old = source->indexOf(page);
    const QString label = source->tabText(old), tooltip = source->tabToolTip(old);
    const QIcon tabIcon = source->tabIcon(old);
    if (zone != DropZone::Center && zone != DropZone::TabBar) {
        const auto orientation = zone == DropZone::Left || zone == DropZone::Right ? Qt::Horizontal : Qt::Vertical;
        const bool before = zone == DropZone::Left || zone == DropZone::Top;
        auto fresh = makeGroup();
        auto parentSplit = qobject_cast<QSplitter *>(target->parentWidget());
        if (parentSplit && parentSplit->orientation() == orientation) {
            auto sizes = parentSplit->sizes();
            const int position = parentSplit->indexOf(target);
            const int available = qMax(0,sizes[position]-parentSplit->handleWidth());
            sizes[position] = available/2;
            sizes.insert(position+(before?0:1),available-available/2);
            // Same direction adds a sibling column/row. Only divide the target's
            // allocation, matching the half-panel preview and preserving peers.
            parentSplit->insertWidget(position+(before?0:1),fresh);
            fresh->show();
            parentSplit->setSizes(sizes);
        } else {
            auto branch = new PanelSplitter(orientation);
            if (parentSplit) {
                auto sizes = parentSplit->sizes();
                parentSplit->replaceWidget(parentSplit->indexOf(target),branch);
                parentSplit->setSizes(sizes);
            } else {
                rootLayout->removeWidget(target);
                target->setParent(nullptr);
                root = branch;
                rootLayout->addWidget(root);
            }
            branch->addWidget(before ? fresh : target);
            branch->addWidget(before ? target : fresh);
            target->show(); fresh->show(); branch->show();
            branch->setSizes({1,1});
        }
        target = fresh;
    }
    source->removeTab(old);
    int added = target->insertTab(index<0 ? target->count() : index,page,tabIcon,label);
    target->setTabToolTip(added,tooltip);
    target->setCurrentWidget(page);
    page->show();
    setActiveGroup(target);
    scheduleCleanup();
    emit layoutChanged();
    return true;
}
bool EditorArea::canSplit() const { return activeGroup() && activeGroup()->count()>1; }
void EditorArea::splitActive(Qt::Orientation orientation) {
    auto group = activeGroup();
    if (canSplit()) movePage(group->currentWidget(),group,orientation == Qt::Horizontal ? DropZone::Right : DropZone::Bottom);
}
void EditorArea::mergeAll() {
    auto target = activeGroup();
    auto selected = target->currentWidget();
    for (auto group : groups()) {
        if (group == target) continue;
        while (group->count()) movePage(group->widget(0),target);
    }
    if (selected) activatePage(selected);
    scheduleCleanup();
}
void EditorArea::scheduleCleanup() {
    if (cleanupQueued) return;
    cleanupQueued = true;
    QTimer::singleShot(0,this,[this] {
        cleanupQueued = false;
        // Reparenting a QDrag source while exec() is running can invalidate the
        // native drag. Finish the entire gesture before pruning its source.
        if (!dragging) cleanupEmptyGroups();
    });
}
void EditorArea::cleanupEmptyGroups() {
    const auto all = groups();
    EditorTabs *keep = nullptr;
    for (auto group : all) if (group->count()) { keep = group; break; }
    if (!keep) keep = activeGroup();
    if (!active || !active->count()) setActiveGroup(keep);
    for (auto group : all) {
        if (group == keep || group->count() || group->interactionActive()) continue;
        group->hide();
        group->setParent(nullptr);
        group->deleteLater();
    }
    collapseSplitters();
    emit layoutChanged();
}
void EditorArea::collapseSplitters() {
    std::function<void(QWidget *)> collapse = [&](QWidget *node) {
        auto split = qobject_cast<QSplitter *>(node);
        if (!split) return;
        // Child replacement changes indices, so take a stable pointer snapshot.
        QList<QPointer<QWidget>> children;
        for (int i=0;i<split->count();++i) children << split->widget(i);
        for (const auto &child : children) if (child) collapse(child);
        if (split->count()>1) return;
        QWidget *child = split->count() ? split->widget(0) : nullptr;
        auto parent = qobject_cast<QSplitter *>(split->parentWidget());
        if (child) child->setParent(nullptr);
        if (parent) {
            const auto sizes = parent->sizes();
            if (child) parent->replaceWidget(parent->indexOf(split),child);
            else split->setParent(nullptr);
            parent->setSizes(sizes);
        } else if (root == split) {
            rootLayout->removeWidget(split);
            root = child ? child : makeGroup();
            rootLayout->addWidget(root);
        }
        split->hide();
        split->deleteLater();
        if (child) child->show();
    };
    collapse(root);
}
EditorTabs *EditorArea::tabsAt(QWidget *widget) const {
    while (widget && widget != this) {
        if (auto group = qobject_cast<EditorTabs *>(widget))
            return groups().contains(group) ? group : nullptr;
        widget = widget->parentWidget();
    }
    return nullptr;
}
QMimeData *EditorArea::beginDrag(EditorTabs *source, QWidget *page) {
    if (liveMime || !source || groupFor(page)!=source) return nullptr;
    auto mime = new EditorDragMime(this,source,page);
    liveMime = mime;
    return mime;
}
bool EditorArea::validDrag(const QMimeData *mime) const {
    const auto internal = dynamic_cast<const EditorDragMime *>(mime);
    return internal && internal == liveMime && internal->owner == this && internal->source && internal->page &&
           !internal->committed && groupFor(internal->page) == internal->source;
}
bool EditorArea::outsideApplication(QPoint point){
    for(auto window:QApplication::topLevelWidgets()){
        if(window->windowType()==Qt::ToolTip)continue;
        if(window->isVisible()&&window->frameGeometry().contains(point))return false;
    }return true;
}
void EditorArea::dragFeedback(QDrag *drag,QPoint global){
    if(!drag)return;
    const bool outside=drag->property("canFloatPage").toBool() && outsideApplication(global);
    if(drag->property("floatCursorOutside").isValid()&&drag->property("floatCursorOutside").toBool()==outside)return;
    drag->setProperty("floatCursorOutside",outside);
    // Qt/Windows GiveFeedback tracks dragCursor(action).cacheKey and recreates
    // the native cursor. IgnoreAction stays a rejection for OLE targets; only
    // the guarded physical release outside our windows commits a float.
    const auto key=outside?"floatCursor":"forbiddenCursor";
    drag->setDragCursor(drag->property(key).value<QPixmap>(),Qt::IgnoreAction);
}
void EditorArea::startDrag(EditorTabs *source, QWidget *page) {
    if(OperationGate::locked())return;const auto lockEpoch=OperationGate::token();
    auto mime = static_cast<EditorDragMime *>(beginDrag(source,page));
    if (!mime) return;
    dragging = true;
    auto drag = new QDrag(source);
    drag->setMimeData(mime);
    const bool canFloat = !qobject_cast<EditorCompare *>(page);
    drag->setProperty("canFloatPage",canFloat);
    QPointer<EditorArea> self(this);
    QPointer<QWidget> guardedPage(page);
    QPointer<EditorDragMime> guardedMime(mime);
    QPointer<QDrag> guardedDrag(drag);
    connect(source,&QObject::destroyed,drag,[] { QDrag::cancel(); });
    const auto c = colors();
    QString text = source->tabText(source->indexOf(page));
    QFontMetrics metrics(source->font());
    text = metrics.elidedText(text,Qt::ElideRight,uiPx(210));
    QSize badgeSize(qBound(uiPx(90),metrics.horizontalAdvance(text)+uiPx(42),uiPx(250)),uiPx(34));
    const qreal dpr = source->devicePixelRatioF();
    QPixmap badge(badgeSize*dpr);
    badge.setDevicePixelRatio(dpr);
    badge.fill(Qt::transparent);
    {
        QPainter p(&badge);
        p.setRenderHint(QPainter::Antialiasing);
        QColor fill = c.tabs; fill.setAlpha(238);
        p.setBrush(fill); p.setPen(QPen(c.panelBorder,uiReal(1)));
        p.drawRoundedRect(QRectF(QPointF(.5,.5),QSizeF(badgeSize)-QSizeF(1,1)),uiReal(4),uiReal(4));
        p.setPen(c.accent); p.drawLine(uiPx(1),uiPx(2),uiPx(1),badgeSize.height()-uiPx(3));
        p.setFont(source->font()); p.setPen(c.text);
        auto fileIcon=source->tabIcon(source->indexOf(page));
        if(fileIcon.isNull()){auto editor=qobject_cast<Editor *>(page);fileIcon=editor?FileIconProvider::file(editor->path):page->windowIcon();}
        if(fileIcon.isNull())fileIcon=icon(Icon::NewFile);
        fileIcon.paint(&p,QRect(uiPx(8),uiPx(8),uiPx(18),uiPx(18)));
        p.drawText(QRect(uiPx(34),0,badgeSize.width()-uiPx(40),badgeSize.height()),Qt::AlignVCenter,text);
    }
    drag->setPixmap(badge);
    drag->setHotSpot(QPoint(uiPx(20),uiPx(17)));
    QPixmap floatCursor(uiSize(32,32)*dpr),forbidden(uiSize(32,32)*dpr);floatCursor.setDevicePixelRatio(dpr);forbidden.setDevicePixelRatio(dpr);floatCursor.fill(Qt::transparent);forbidden.fill(Qt::transparent);
    {QPainter p(&floatCursor);p.scale(uiReal(1),uiReal(1));p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(c.text,2));p.setBrush(c.tabs);p.drawRoundedRect(QRectF(9,9,20,17),2,2);p.drawLine(9,14,29,14);p.setPen(QPen(c.accent,2));p.drawLine(14,16,27,3);p.drawLine(19,3,27,3);p.drawLine(27,3,27,11);}
    {QPainter p(&forbidden);p.scale(uiReal(1),uiReal(1));p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(c.red,3));p.drawEllipse(QRectF(5,5,22,22));p.drawLine(8,8,24,24);}
    drag->setProperty("floatCursor",floatCursor);drag->setProperty("forbiddenCursor",forbidden);
    dragFeedback(drag,QCursor::pos());
    DragReleaseGuard release([guardedDrag](QPoint point){if(guardedDrag)EditorArea::dragFeedback(guardedDrag,point);});
    const auto result = executeDrag(drag);
    // exec() runs a nested event loop. A normal close may have destroyed every
    // object involved, including this area and QDrag's source/parent.
    if (!self) return;
    clearPreview();
    bool outside = false;
    if (canFloat && guardedMime && guardedDrag && guardedPage && result == Qt::IgnoreAction &&
        !guardedMime->committed && !release.cancelled && release.released && validDrag(guardedMime)) {
        outside = outsideApplication(release.releasePosition);
    }
    liveMime.clear();
    dragging = false;
    if (guardedDrag) guardedDrag->deleteLater();
    if (outside && guardedPage && OperationGate::accepts(lockEpoch)) emit floatPageRequested(guardedPage);
    if (self) scheduleCleanup();
}
Qt::DropAction EditorArea::executeDrag(QDrag *drag) {
    return drag->exec(Qt::MoveAction,Qt::MoveAction);
}
DropZone EditorArea::dropZone(EditorTabs *group, const QPoint &position, int *index) const {
    auto bar = group->tabBar();
    const auto barPoint = bar->mapFrom(group,position);
    if (bar->isVisible() && bar->rect().contains(barPoint)) {
        int tab = bar->tabAt(barPoint);
        *index = tab<0 ? group->count() : tab + (barPoint.x() > bar->tabRect(tab).center().x() ? 1 : 0);
        return DropZone::TabBar;
    }
    *index = -1;
    QRect content = group->rect().adjusted(0,bar->isVisible()?bar->height():0,0,0);
    if (content.width()<1 || content.height()<1) return DropZone::Center;
    qreal x = qreal(position.x()-content.left())/content.width();
    qreal y = qreal(position.y()-content.top())/content.height();
    const qreal closest = qMin(qMin(x,1-x),qMin(y,1-y));
    if (closest>=.23) return DropZone::Center;
    if (closest==x) return DropZone::Left;
    if (closest==1-x) return DropZone::Right;
    return closest==y ? DropZone::Top : DropZone::Bottom;
}
void EditorArea::showPreview(EditorTabs *group, DropZone zone, int index) {
    QRect rect = group->rect();
    auto bar = group->tabBar();
    overlay->insertion = zone == DropZone::TabBar;
    if (zone == DropZone::TabBar) {
        int x = index>=group->count() ? (group->count()?bar->tabRect(group->count()-1).right()+1:0) : bar->tabRect(index).left();
        rect = QRect(bar->mapTo(group,QPoint(qBound(0,x,bar->width()-uiPx(3)),0)),QSize(uiPx(3),bar->height()));
    } else {
        rect.setTop(bar->isVisible()?bar->height():0);
        if (zone==DropZone::Left) rect.setWidth(rect.width()/2);
        if (zone==DropZone::Right) rect.setLeft(rect.center().x());
        if (zone==DropZone::Top) rect.setHeight(rect.height()/2);
        if (zone==DropZone::Bottom) rect.setTop(rect.center().y());
        rect.adjust(1,1,-1,-1);
    }
    rect.moveTopLeft(group->mapTo(this,rect.topLeft()));
    overlay->setGeometry(rect);
    overlay->show(); overlay->raise(); overlay->update();
}
void EditorArea::clearPreview() { overlay->hide(); }
bool EditorArea::previewVisible() const { return overlay->isVisible(); }
bool EditorArea::handleDropEvent(QWidget *receiver, QEvent *event) {
    if (event->type() == QEvent::DragLeave) { clearPreview(); return false; }
    const auto drop = static_cast<QDropEvent *>(event);
    if (!drop->mimeData()->hasFormat(tabMimeType)) return false;
    auto group = tabsAt(receiver);
    if (!group) {
        if (drop->mimeData() == liveMime) clearPreview();
        return false;
    }
    if (!group || !validDrag(drop->mimeData()) || !(drop->possibleActions() & Qt::MoveAction)) {
        clearPreview(); drop->ignore(); return true;
    }
    const auto position = receiver->mapTo(group,drop->position().toPoint());
    const auto corner = group->cornerWidget(Qt::TopRightCorner);
    if (corner && corner->isVisible() && corner->geometry().contains(position)) {
        clearPreview(); drop->ignore(); return true;
    }
    int index = -1;
    const auto zone = dropZone(group,position,&index);
    if (group == liveMime->source && group->count()==1 && zone!=DropZone::Center && zone!=DropZone::TabBar) {
        clearPreview(); drop->ignore(); return true;
    }
    if (event->type() == QEvent::Drop) {
        auto mime = liveMime.data();
        if (movePage(mime->page,group,zone,index)) {
            mime->committed = true;
            drop->setDropAction(Qt::MoveAction); drop->accept();
        } else drop->ignore();
        clearPreview();
    } else {
        showPreview(group,zone,index);
        drop->setDropAction(Qt::MoveAction); drop->accept();
    }
    return true;
}
bool EditorArea::eventFilter(QObject *object, QEvent *event) {
    auto widget = qobject_cast<QWidget *>(object);
    if (!widget) return false;
    if (!mutating && (event->type()==QEvent::FocusIn || event->type()==QEvent::MouseButtonPress)) {
        if (auto group = tabsAt(widget)) {
            const bool changed = group != activeGroup();
            setActiveGroup(group);
            if (changed && group->currentWidget()) emit pagePresented(group->currentWidget());
        }
    }
    if (event->type()==QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key()==Qt::Key_Escape)
        clearPreview();
    if (event->type()==QEvent::DragEnter || event->type()==QEvent::DragMove ||
        event->type()==QEvent::Drop || (event->type()==QEvent::DragLeave && tabsAt(widget)))
        return handleDropEvent(widget,event);
    if (event->type()==QEvent::Resize || event->type()==QEvent::Hide) {
        if (widget==this || tabsAt(widget)) clearPreview();
    }
    return false;
}
} // namespace rs
