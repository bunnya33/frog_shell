#include "wheeleditguard.h"
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QWheelEvent>

namespace rs {
namespace {
QWidget *editableControl(QWidget *widget) {
    for (auto w = widget; w; w = w->parentWidget()) {
        if (qobject_cast<QComboBox *>(w) || qobject_cast<QAbstractSpinBox *>(w)) return w;
        if (w->isWindow()) break;
    }
    return nullptr;
}
bool ownedBy(QObject *object, const QObject *owner) {
    for (auto parent = object; parent; parent = parent->parent())
        if (parent == owner) return true;
    return false;
}
class WheelEditGuard final : public QObject {
public:
    explicit WheelEditGuard(QObject *parent) : QObject(parent) { qApp->installEventFilter(this); }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::ApplicationStateChange &&
            QApplication::applicationState() != Qt::ApplicationActive) armed.clear();
        auto widget = qobject_cast<QWidget *>(object);
        if (!widget) return false;
        auto popup = QApplication::activePopupWidget();
        const bool inPopup = popup && (widget == popup || popup->isAncestorOf(widget));
        if (event->type() == QEvent::MouseButtonPress) {
            if (inPopup) {
                const auto point = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
                if (!popup->rect().contains(popup->mapFromGlobal(point))) armed.clear();
                return false;
            }
            auto clicked = editableControl(widget);
            // A spin button may propagate the same press to its parent.
            const auto point = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
            if (!clicked && armed && widget->isAncestorOf(armed) &&
                armed->rect().contains(armed->mapFromGlobal(point))) return false;
            armed = clicked;
        } else if (event->type() == QEvent::FocusOut && armed &&
                   (widget == armed || armed->isAncestorOf(widget))) {
            const auto reason = static_cast<QFocusEvent *>(event)->reason();
            // Native combo popups queue window activation after hiding. A real
            // application deactivation is handled above; another focus target
            // is handled below, without racing this popup activation sequence.
            if (reason != Qt::PopupFocusReason && reason != Qt::ActiveWindowFocusReason && !popup)
                armed.clear();
        } else if (event->type() == QEvent::FocusIn && armed) {
            // QWidget::isAncestorOf stops at window boundaries; the combo's
            // own popup list is a separate window with an QObject owner chain.
            if (inPopup && ownedBy(popup, armed)) return false;
            const auto reason = static_cast<QFocusEvent *>(event)->reason();
            if (widget != armed && !armed->isAncestorOf(widget)) armed.clear();
            else if (!inPopup && reason != Qt::MouseFocusReason && reason != Qt::PopupFocusReason &&
                     reason != Qt::ActiveWindowFocusReason) armed.clear();
        } else if (event->type() == QEvent::Wheel && !forwarding && !inPopup) {
            auto control = editableControl(widget);
            auto focus = QApplication::focusWidget();
            if (control && (armed != control || !focus ||
                            (focus != control && !control->isAncestorOf(focus)))) {
                auto wheel = static_cast<QWheelEvent *>(event);
                for (auto parent = control->parentWidget(); parent; parent = parent->parentWidget()) {
                    if (auto scroll = qobject_cast<QAbstractScrollArea *>(parent)) {
                        auto viewport = scroll->viewport();
                        QWheelEvent forwarded(viewport->mapFromGlobal(wheel->globalPosition()), wheel->globalPosition(),
                                              wheel->pixelDelta(), wheel->angleDelta(), wheel->buttons(), wheel->modifiers(),
                                              wheel->phase(), wheel->inverted(), wheel->source(), wheel->pointingDevice());
                        forwarded.setTimestamp(wheel->timestamp());
                        forwarding = true;
                        QCoreApplication::sendEvent(viewport, &forwarded);
                        forwarding = false;
                        break;
                    }
                }
                event->accept();
                return true;
            }
            if (control && widget != control) {
                // QLineEdit can ignore a synthetic wheel without bubbling it to
                // the spin box. Deliver it once to the owning control.
                auto wheel = static_cast<QWheelEvent *>(event);
                QWheelEvent forwarded(control->mapFromGlobal(wheel->globalPosition()), wheel->globalPosition(),
                                      wheel->pixelDelta(), wheel->angleDelta(), wheel->buttons(), wheel->modifiers(),
                                      wheel->phase(), wheel->inverted(), wheel->source(), wheel->pointingDevice());
                forwarded.setTimestamp(wheel->timestamp());
                forwarding = true;
                QCoreApplication::sendEvent(control, &forwarded);
                forwarding = false;
                event->accept();
                return true;
            }
        }
        return false;
    }
private:
    QPointer<QWidget> armed;
    bool forwarding = false;
};
}
void installWheelEditGuard() {
    static QPointer<WheelEditGuard> guard;
    if (!guard && qApp) guard = new WheelEditGuard(qApp);
}
}
