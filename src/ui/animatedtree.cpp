#include "animatedtree.h"
#include "layoutmetrics.h"
#include "theme.h"
#include <cmath>

namespace rs {
namespace {
// The default delegate retains Qt's normal icon/text rendering and editing.
// The wider rect only affects standalone terminal rows, never the action column.
class TreeDensityDelegate final : public DensityDelegate {
  public:
    explicit TreeDensityDelegate(AnimatedTreeWidget *tree) : DensityDelegate(28, tree), tree(tree) {}
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        if (!tree->property("animatedTreeTerminalPeers").toBool())
            return DensityDelegate::sizeHint(option, index);
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(uiPx(22), option.fontMetrics.height() + uiPx(2)));
        return size;
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        auto adjusted = option;
        if (index.column() == 0 && tree->property("animatedTreeTerminalPeers").toBool() &&
            index.data(TerminalPeerPositionRole).toInt() == 0)
            adjusted.rect.adjust(-uiPx(14), 0, 0, 0);
        DensityDelegate::paint(p, adjusted, index);
    }
  private:
    AnimatedTreeWidget *tree;
};
}
struct AnimatedTreeWidget::State {
    struct Entry {
        quint64 id;
        QPersistentModelIndex index;
        bool angle;
        qreal value;
        QVariantAnimation *animation = nullptr;
    };
    TreeMode mode = TreeMode::Hierarchy;
    bool motion = true;
    quint64 nextId = 0;
    QPersistentModelIndex hovered;
    QList<Entry> entries;
};

AnimatedTreeWidget::AnimatedTreeWidget(QWidget *parent)
    : SafeTreeWidget(parent), state(std::make_unique<State>()) {
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setSelectionBehavior(SelectRows);
    setUniformRowHeights(true);
    setItemDelegate(new TreeDensityDelegate(this));
    // QTreeView recomputes selected/hover state inside drawRow before invoking
    // delegates. Keep the content delegate intact while making those style
    // backgrounds transparent so our full-row rounded surface remains visible.
    setStyleSheet("QTreeView::item:selected, QTreeView::item:hover, "
                  "QTreeView::branch:selected, QTreeView::branch:hover { background: transparent; }");
    setAnimated(true);
    setRootIsDecorated(true);
    setHorizontalScrollMode(ScrollPerPixel);
    setVerticalScrollMode(ScrollPerPixel);
    UiZoom::instance().bind(this, "animatedTreeGeometry", [this] {
        resetTreeTransientState();
        setIndentation(uiPx(24));
        doItemsLayout();
    });
    auto reset = [this] { resetTreeTransientState(); };
    connect(model(), &QAbstractItemModel::rowsAboutToBeInserted, this, reset);
    connect(model(), &QAbstractItemModel::rowsAboutToBeRemoved, this, reset);
    connect(model(), &QAbstractItemModel::rowsAboutToBeMoved, this, reset);
    connect(model(), &QAbstractItemModel::layoutAboutToBeChanged, this, reset);
    connect(model(), &QAbstractItemModel::modelAboutToBeReset, this, reset);
    connect(this, &QTreeView::expanded, this, [this](const QModelIndex &i) { animate(i, true, 1); });
    connect(this, &QTreeView::collapsed, this, [this](const QModelIndex &i) { animate(i, true, 0); });
}

AnimatedTreeWidget::~AnimatedTreeWidget() {
    disconnect(model(), nullptr, this, nullptr);
    disconnect(this, nullptr, this, nullptr);
    resetTreeTransientState();
}

void AnimatedTreeWidget::setTreeMode(TreeMode mode) {
    if (state->mode == mode) return;
    resetTreeTransientState();
    state->mode = mode;
    setProperty("animatedTreeTerminalPeers", mode == TreeMode::TerminalPeers);
    viewport()->update();
}

void AnimatedTreeWidget::setTreeMotionEnabled(bool enabled) {
    state->motion = enabled;
    setAnimated(enabled);
    resetTreeTransientState();
}

void AnimatedTreeWidget::resetTreeTransientState() {
    state->hovered = {};
    for (const auto &entry : std::as_const(state->entries)) {
        if (entry.animation) { entry.animation->stop(); delete entry.animation; }
    }
    state->entries.clear();
    if (viewport()) viewport()->update();
}

qreal AnimatedTreeWidget::value(const QModelIndex &index, bool angle, qreal fallback) const {
    for (const auto &entry : std::as_const(state->entries))
        if (entry.angle == angle && entry.index == index) return entry.value;
    return fallback;
}

void AnimatedTreeWidget::animate(const QModelIndex &raw, bool angle, qreal target) {
    const QModelIndex index = raw.siblingAtColumn(0);
    if (!index.isValid()) return;
    int position = -1;
    for (int i = 0; i < state->entries.size(); ++i)
        if (state->entries[i].angle == angle && state->entries[i].index == index) { position = i; break; }
    const qreal start = position < 0 ? 1 - target : state->entries[position].value;
    if (position >= 0 && state->entries[position].animation) {
        delete state->entries[position].animation;
        state->entries[position].animation = nullptr;
    }
    if (!state->motion || !isVisible() || qFuzzyCompare(start + 1, target + 1)) {
        if (position >= 0) state->entries.removeAt(position);
        viewport()->update();
        return;
    }
    // Only short-lived visible transitions are retained, regardless of tree size.
    if (position < 0) {
        constexpr int maxTransitions = 64;
        if (state->entries.size() >= maxTransitions) {
            delete state->entries.front().animation;
            state->entries.removeFirst();
        }
        state->entries.append({++state->nextId, QPersistentModelIndex(index), angle, start, nullptr});
        position = int(state->entries.size()) - 1;
    }
    auto &entry = state->entries[position];
    const quint64 id = entry.id;
    auto *animation = new QVariantAnimation(this);
    entry.animation = animation;
    animation->setDuration(qMax(1, qRound((angle ? 170 : 120) * std::abs(target - start))));
    animation->setStartValue(start);
    animation->setEndValue(target);
    animation->setEasingCurve(angle ? QEasingCurve::InOutCubic : QEasingCurve::OutCubic);
    connect(animation, &QVariantAnimation::valueChanged, this, [this, id](const QVariant &v) {
        for (auto &entry : state->entries) if (entry.id == id) { entry.value = v.toReal(); break; }
        viewport()->update();
    });
    connect(animation, &QVariantAnimation::finished, this, [this, id, animation] {
        for (int i = 0; i < state->entries.size(); ++i) {
            if (state->entries[i].id == id) { state->entries.removeAt(i); break; }
        }
        animation->deleteLater();
        viewport()->update();
    });
    animation->start();
}

void AnimatedTreeWidget::setHovered(const QModelIndex &raw) {
    const QModelIndex index = raw.siblingAtColumn(0);
    if (state->hovered == index) return;
    const QPersistentModelIndex previous = state->hovered;
    state->hovered = index;
    if (previous.isValid()) animate(previous, false, 0);
    if (index.isValid()) animate(index, false, 1);
}

void AnimatedTreeWidget::paintRowBackground(QPainter *p, const QRect &rect, const QModelIndex &index) const {
    const bool selected = selectionModel() && selectionModel()->isRowSelected(index.row(), index.parent());
    const qreal hover = value(index.siblingAtColumn(0), false, state->hovered == index.siblingAtColumn(0) ? 1 : 0);
    if (!selected && hover <= 0) return;
    QColor color = selected ? QColor(lightTheme() ? "#dceaff" : "#45475c")
                            : QColor(lightTheme() ? "#eef1f4" : "#343746");
    if (!selected) color.setAlphaF(qBound(qreal(0), hover, qreal(1)));
    const bool terminal = state->mode == TreeMode::TerminalPeers;
    const int gutter = uiPx(terminal ? 2 : 8);
    const QRectF row(gutter, rect.top() + uiPx(1), qMax(0, viewport()->width() - 2*gutter), qMax(0, rect.height() - uiPx(2)));
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    const auto radius = uiReal(terminal ? 4 : 6);
    p->drawRoundedRect(row, radius, radius);
    p->restore();
}

void AnimatedTreeWidget::drawRow(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    paintRowBackground(p, option.rect, index);
    QStyleOptionViewItem clean(option);
    clean.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver);
    SafeTreeWidget::drawRow(p, clean, index);
}

void AnimatedTreeWidget::drawBranches(QPainter *p, const QRect &rect, const QModelIndex &index) const {
    p->save();
    p->setClipRect(rect, Qt::IntersectClip);
    // Qt paints a separate branch panel before this callback. Re-establish a
    // clean base before painting the same translucent row surface exactly once.
    p->fillRect(rect, palette().brush(QPalette::Base));
    paintRowBackground(p, rect, index);
    p->setRenderHint(QPainter::Antialiasing);
    QPen line(colors().line, uiReal(1));
    p->setPen(line);
    const qreal mid = rect.center().y();
    if (state->mode == TreeMode::TerminalPeers) {
        line.setColor(colors().muted);
        p->setPen(line);
        const int position = index.siblingAtColumn(0).data(TerminalPeerPositionRole).toInt();
        if (position >= 1 && position <= 3) {
            const qreal x = rect.right() - indentation()/2.0;
            p->drawLine(QPointF(x, position == 1 ? mid : rect.top()), QPointF(x, position == 3 ? mid : rect.bottom()+1));
            p->drawLine(QPointF(x, mid), QPointF(qMin(qreal(rect.right()), x + uiReal(8)), mid));
        }
    } else {
        qreal x = rect.right() - indentation()/2.0 - indentation();
        for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent(), x -= indentation())
            if (x >= rect.left() && x <= rect.right()) p->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()+1));
        if (model()->hasChildren(index)) {
            p->translate(rect.right() - indentation()/2.0, mid);
            p->rotate(90 * value(index, true, isExpanded(index) ? 1 : 0));
            QPen arrow(colors().muted, uiReal(1.5), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p->setPen(arrow);
            QPainterPath path;
            path.moveTo(-uiReal(2.2), -uiReal(4));
            path.lineTo(uiReal(1.8), 0);
            path.lineTo(-uiReal(2.2), uiReal(4));
            p->drawPath(path);
        }
    }
    p->restore();
}

void AnimatedTreeWidget::mouseMoveEvent(QMouseEvent *event) {
    setHovered(indexAt(event->position().toPoint()));
    SafeTreeWidget::mouseMoveEvent(event);
}
void AnimatedTreeWidget::leaveEvent(QEvent *event) {
    setHovered({});
    SafeTreeWidget::leaveEvent(event);
}
bool AnimatedTreeWidget::viewportEvent(QEvent *event) {
    if (event->type() == QEvent::Leave) setHovered({});
    return SafeTreeWidget::viewportEvent(event);
}
void AnimatedTreeWidget::hideEvent(QHideEvent *event) {
    resetTreeTransientState();
    SafeTreeWidget::hideEvent(event);
}
void AnimatedTreeWidget::changeEvent(QEvent *event) {
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange)
        resetTreeTransientState();
    SafeTreeWidget::changeEvent(event);
}
}
