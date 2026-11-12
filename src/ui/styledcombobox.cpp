#include "styledcombobox.h"
#include "theme.h"
#include "roundedsurface.h"
namespace rs {
namespace {
class ComboStyle final : public UiZoomStyle {
  public:
    ComboStyle() = default;
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *data) const override {
        // Keep Qt's list popup rather than a platform menu aligned to the current row.
        if (hint == QStyle::SH_ComboBox_Popup)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
};
class ComboDelegate final : public QStyledItemDelegate {
  public:
    explicit ComboDelegate(QComboBox *combo) : QStyledItemDelegate(combo), combo(combo) {}
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(uiPx(30), size.height() + uiPx(8)));
        size.rwidth() += uiPx(30);
        return size;
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const auto c = colors();
        auto adjusted = option;
        initStyleOption(&adjusted, index);
        const bool highlighted = option.state & (QStyle::State_Selected | QStyle::State_MouseOver);
        p->fillRect(option.rect, highlighted ? c.selection : c.side);
        adjusted.rect.adjust(uiPx(25), 0, -uiPx(8), 0);
        adjusted.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        adjusted.backgroundBrush = Qt::NoBrush;
        adjusted.palette.setColor(QPalette::Text, c.text);
        adjusted.palette.setColor(QPalette::Disabled, QPalette::Text, c.muted);
        QStyledItemDelegate::paint(p, adjusted, index);
        if (index.row() == combo->currentIndex() && index.parent() == combo->rootModelIndex()) {
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(QPen(c.accent, uiReal(1.5), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const QPointF center(option.rect.left() + uiPx(12), option.rect.center().y());
            QPainterPath check;
            check.moveTo(center + QPointF(-uiReal(4), 0));
            check.lineTo(center + QPointF(-uiReal(1), uiReal(3)));
            check.lineTo(center + QPointF(uiReal(5), -uiReal(3)));
            p->drawPath(check);
            p->restore();
        }
    }
  private:
    QComboBox *combo;
};
} // namespace
StyledComboBox::StyledComboBox(QWidget *parent) : QComboBox(parent) {
    setProperty("styledCombo", true);
    auto comboStyle = new ComboStyle;
    comboStyle->setParent(this);
    setStyle(comboStyle);
    auto list = new QListView;
    list->setObjectName("styledComboList");
    list->setUniformItemSizes(true);
    list->setMouseTracking(true);
    list->setWordWrap(false);
    list->setTextElideMode(Qt::ElideRight);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setView(list);
    // Configure the Qt-owned popup before its first native window is created.
    if (auto container = list->window(); container->windowType()==Qt::Popup) {
        container->setObjectName("styledComboPopup");
        container->setWindowFlag(Qt::FramelessWindowHint, true);
        container->setAttribute(Qt::WA_TranslucentBackground);
        container->setAttribute(Qt::WA_OpaquePaintEvent, false);
        if (container->layout())
            UiZoom::instance().bind(container, "comboPopupMargins", [container] {
                container->layout()->setContentsMargins(uiPx(5), uiPx(5), uiPx(5), uiPx(5));
            });
        installRoundedSurface(container, RoundedRole::ComboPopup);
    }
    setItemDelegate(new ComboDelegate(this));
    setMaxVisibleItems(8);
}
void StyledComboBox::paintEvent(QPaintEvent *) {
    QStylePainter p(this);
    QStyleOptionComboBox option;
    initStyleOption(&option);
    p.drawComplexControl(QStyle::CC_ComboBox, option);
    p.drawControl(QStyle::CE_ComboBoxLabel, option);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(isEnabled() ? colors().text : colors().muted, uiReal(1.4), Qt::SolidLine, Qt::RoundCap,
                 Qt::RoundJoin));
    const qreal x = width() - uiPx(16), y = height() / 2.0;
    QPainterPath chevron;
    chevron.moveTo(x - uiReal(3.5), y - uiReal(1.5));
    chevron.lineTo(x, y + uiReal(2));
    chevron.lineTo(x + uiReal(3.5), y - uiReal(1.5));
    p.drawPath(chevron);
}
void StyledComboBox::showPopup() {
    if (!isEnabled() || !count())
        return;
    clearPopupWatch();
    auto container = view()->window();
    const QRect anchor(mapToGlobal(QPoint()), size());
    auto targetScreen = QGuiApplication::screenAt(anchor.center());
    if (!targetScreen)
        targetScreen = screen();
    const QRect available = targetScreen->availableGeometry();
    const int rowHeight = qMax(uiPx(30), view()->sizeHintForRow(0));
    const int wantedHeight = qMin(count(), maxVisibleItems()) * rowHeight + uiPx(10);
    const int below = qMax(0, available.bottom() - anchor.bottom() - uiPx(4));
    const int above = qMax(0, anchor.top() - available.top() - uiPx(4));
    const bool down = below >= wantedHeight || below >= above;
    const int height = qMax(1, qMin(wantedHeight, down ? below : above));
    const int width = qMin(available.width(), qMax(this->width(), view()->sizeHintForColumn(modelColumn()) + uiPx(24)));
    const int x = qBound(available.left(), anchor.left(), available.right() - width + 1);
    const int y = down ? anchor.bottom() + uiPx(5) : anchor.top() - uiPx(4) - height;
    const QRect targetGeometry(x, y, width, height);
    pendingPopupGeometry = targetGeometry;
    container->setMinimumSize(0, 0);
    container->setMaximumSize(available.size());
    popup = container;
    container->installEventFilter(this);
    // Qt 6.8's list-popup path starts a 150 ms qScrollEffect when this
    // application effect is enabled. Its animated proxy conflicts with our
    // final screen-edge geometry. Suppress it only during this synchronous
    // call, then restore the application's previous effective setting.
    const bool animateCombo = QApplication::isEffectEnabled(Qt::UI_AnimateCombo);
    if (animateCombo) QApplication::setEffectEnabled(Qt::UI_AnimateCombo, false);
    QComboBox::showPopup();
    if (animateCombo) QApplication::setEffectEnabled(Qt::UI_AnimateCombo, true);
    if (!container->isVisible()) {
        clearPopupWatch();
        return;
    }
    popup = container;
    container->installEventFilter(this);
    // Escape may leave the view's preview row different from the committed value.
    view()->setCurrentIndex(model()->index(currentIndex(), modelColumn(), rootModelIndex()));
    // Qt can adjust the rectangle after Show for a scrollbar; preserve our
    // screen-edge positioning if that happens.
    if (container->geometry() != targetGeometry) container->setGeometry(targetGeometry);
    pendingPopupGeometry = {};
    prepareRoundedPopup(container);
    view()->scrollTo(view()->currentIndex(), QAbstractItemView::EnsureVisible);
    for (auto parent = parentWidget(); parent; parent = parent->parentWidget()) {
        watchedParents.append(parent);
        parent->installEventFilter(this);
    }
    screenConnection = connect(targetScreen, &QScreen::availableGeometryChanged, this,
                               [this] { hidePopup(); });
    update();
}
void StyledComboBox::clearPopupWatch() {
    for (const auto &parent : watchedParents)
        if (parent)
            parent->removeEventFilter(this);
    watchedParents.clear();
    if (popup)
        popup->removeEventFilter(this);
    popup.clear();
    pendingPopupGeometry = {};
    disconnect(screenConnection);
}
void StyledComboBox::hidePopup() {
    clearPopupWatch();
    QComboBox::hidePopup();
    update();
}
bool StyledComboBox::event(QEvent *event) {
    if (popup && (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
                  event->type() == QEvent::Hide || event->type() == QEvent::ParentAboutToChange))
        hidePopup();
    return QComboBox::event(event);
}
bool StyledComboBox::eventFilter(QObject *object, QEvent *event) {
    if (object == popup && event->type() == QEvent::Show && !pendingPopupGeometry.isEmpty())
        popup->setGeometry(pendingPopupGeometry);
    if (object == popup && event->type() == QEvent::Paint) {
        // The private Qt container can bypass QFrame's stylesheet painting.
        // Paint only its frame; Qt still owns the view, popup and input handling.
        QPainter painter(popup);
        paintRoundedSurface(painter,popup->rect(),uiReal(6),colors().side,Qt::transparent,0);
        return true;
    }
    if (object == popup && event->type() == QEvent::Hide) {
        clearPopupWatch();
        update();
    } else if (popup && watchedParents.contains(qobject_cast<QWidget *>(object)) &&
               (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
                event->type() == QEvent::Hide || event->type() == QEvent::Close)) {
        hidePopup();
    }
    return QComboBox::eventFilter(object, event);
}
} // namespace rs
