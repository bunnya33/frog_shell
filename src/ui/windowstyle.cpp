#include "windowstyle.h"
#include "theme.h"
#include <QStyleHints>
static void initFrogShellResources() { Q_INIT_RESOURCE(branding); }
#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <qt_windows.h>
#endif

namespace rs {
namespace {
bool decoratedWindow(const QWidget *widget) {
    return widget->isWindow() && (widget->windowType() == Qt::Window || widget->windowType() == Qt::Dialog) &&
           !(widget->windowFlags() & Qt::FramelessWindowHint);
}
void applyDecoration(QWidget *widget) {
    if (!decoratedWindow(widget) || !widget->internalWinId())
        return;
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() != "windows")
        return;
    auto handle = reinterpret_cast<HWND>(widget->internalWinId());
    HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW)};
    const bool highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
                              (contrast.dwFlags & HCF_HIGHCONTRASTON);
    BOOL dark = !lightTheme() && !highContrast;
    // Older Windows releases reject unsupported attributes; keep their system frame.
    HRESULT mode = DwmSetWindowAttribute(handle, 20, &dark, sizeof(dark));
    if (FAILED(mode))
        mode = DwmSetWindowAttribute(handle, 19, &dark, sizeof(dark));
    const auto palette = colors();
    auto nativeColor = [](QColor color) { return RGB(color.red(), color.green(), color.blue()); };
    const COLORREF systemColor = 0xffffffff;
    COLORREF border = highContrast ? systemColor : nativeColor(palette.line);
    COLORREF caption = highContrast
                           ? systemColor
                           : nativeColor(widget->isActiveWindow() ? palette.side : palette.background);
    COLORREF text =
        highContrast ? systemColor : nativeColor(widget->isActiveWindow() ? palette.text : palette.muted);
    HRESULT captionResult = DwmSetWindowAttribute(handle, 35, &caption, sizeof(caption));
    HRESULT textResult = DwmSetWindowAttribute(handle, 36, &text, sizeof(text));
    HRESULT borderResult = DwmSetWindowAttribute(handle, 34, &border, sizeof(border));
    // Expose capability for diagnostics without logging on every activation.
    widget->setProperty("nativeCaptionThemeSupported", SUCCEEDED(mode));
    widget->setProperty("nativeCaptionColorsSupported", SUCCEEDED(captionResult) && SUCCEEDED(textResult));
    widget->setProperty("nativeBorderColorSupported", SUCCEEDED(borderResult));
#else
    Q_UNUSED(widget);
#endif
}
class WindowStyle final : public QObject {
  public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() != QEvent::WinIdChange && event->type() != QEvent::Show &&
            event->type() != QEvent::WindowActivate && event->type() != QEvent::WindowDeactivate &&
            event->type() != QEvent::WindowStateChange && event->type() != QEvent::ScreenChangeInternal)
            return false;
        auto widget = qobject_cast<QWidget *>(object);
        if (!widget || !decoratedWindow(widget))
            return false;
        applyDecoration(widget);
        if (event->type() == QEvent::Show && !widget->isMinimized())
            keepWindowOnScreen(widget);
        if (event->type() == QEvent::ScreenChangeInternal) {
            QPointer<QWidget> guarded(widget);
            QTimer::singleShot(0, this, [guarded] {
                if (guarded)
                    keepWindowOnScreen(guarded);
            });
        }
        return false;
    }
};
} // namespace

void installWindowStyle() {
    if (qApp->property("rabbitWindowStyleInstalled").toBool())
        return;
    qApp->setProperty("rabbitWindowStyleInstalled", true);
    qApp->installEventFilter(new WindowStyle(qApp));
    QObject::connect(qApp, &QGuiApplication::screenRemoved, qApp, [](QScreen *) {
        QTimer::singleShot(0, qApp, [] {
            for (auto window : QApplication::topLevelWidgets())
                if (decoratedWindow(window))
                    keepWindowOnScreen(window);
        });
    });
}

void refreshWindowStyles() {
    for (auto window : QApplication::topLevelWidgets())
        applyDecoration(window);
}

QRect fitWindowToScreens(const QRect &requested, const QList<QRect> &screens) {
    if (screens.isEmpty())
        return requested;
    QRect available = screens.front();
    qint64 largestArea = -1;
    for (const auto &screen : screens) {
        auto overlap = requested.intersected(screen);
        const qint64 area = qint64(overlap.width()) * overlap.height();
        if (area > largestArea) {
            largestArea = area;
            available = screen;
        }
    }
    QSize size(qMin(requested.width(), available.width()), qMin(requested.height(), available.height()));
    QPoint origin = largestArea > 0 ? requested.topLeft()
                                    : available.center() - QPoint(size.width() / 2, size.height() / 2);
    origin.setX(qBound(available.left(), origin.x(), available.right() - size.width() + 1));
    origin.setY(qBound(available.top(), origin.y(), available.bottom() - size.height() + 1));
    return {origin, size};
}

void keepWindowOnScreen(QWidget *window) {
    if (window->isMaximized() || window->isFullScreen() || window->isMinimized())
        return;
    QList<QRect> available;
    if (auto screen = window->screen())
        available << screen->availableGeometry();
    for (auto screen : QGuiApplication::screens())
        if (!available.contains(screen->availableGeometry()))
            available << screen->availableGeometry();
    auto frame = window->frameGeometry();
    auto fitted = fitWindowToScreens(frame, available);
    if (frame == fitted)
        return;
    QSize frameSize = frame.size() - window->size();
    window->resize(fitted.size() - frameSize);
    window->move(fitted.topLeft());
}

void initializeBrandingResources() { static const bool initialized=[] {initFrogShellResources();return true;}();Q_UNUSED(initialized); }
QIcon applicationIcon() {
    initializeBrandingResources();
    QIcon supplied(":/branding/frogshell.ico");if(!supplied.isNull())return supplied;
    const QImage source(":/branding/frogshell.png");
    QIcon icon;
    for (int pixels : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addPixmap(QPixmap::fromImage(source.scaled(pixels,pixels,Qt::KeepAspectRatio,Qt::SmoothTransformation)));
    }
    return icon;
}

ElidedLabel::ElidedLabel(const QString &text, QWidget *parent) : QLabel(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setTextFormat(Qt::PlainText);
    setText(text);
}
void ElidedLabel::setText(const QString &value) {
    QLabel::setText(value);
    setToolTip(value);
    setAccessibleName(value);
}
QSize ElidedLabel::minimumSizeHint() const {
    return {0, QLabel::minimumSizeHint().height()};
}
QSize ElidedLabel::sizeHint() const {
    return {qMin(uiPx(480), QLabel::sizeHint().width()), QLabel::sizeHint().height()};
}
void ElidedLabel::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    auto rectangle = contentsRect().adjusted(margin(), margin(), -margin(), -margin());
    painter.setPen(
        palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText));
    painter.drawText(rectangle, alignment(),
                     fontMetrics().elidedText(text(), Qt::ElideMiddle, rectangle.width()));
}

ThemeChoice::ThemeChoice(const QString &name, bool light, QWidget *parent)
    : QRadioButton(name, parent), previewLight(light) {
    setAccessibleName(name);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    uiMinimumWidth(this,190);
    connect(this, &QRadioButton::toggled, this, qOverload<>(&QWidget::update));
}
QSize ThemeChoice::sizeHint() const {
    return {uiPx(220), fontMetrics().height() + uiPx(120)};
}
bool ThemeChoice::hitButton(const QPoint &position) const {
    return rect().contains(position);
}
void ThemeChoice::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    auto palette = colors();
    painter.setPen(QPen(isChecked() || hasFocus() ? palette.accent : palette.line, uiReal(hasFocus() ? 2 : 1)));
    painter.setBrush(palette.side);
    painter.drawRoundedRect(QRectF(rect()).adjusted(uiReal(2), uiReal(2), -uiReal(2), -uiReal(2)), uiReal(8), uiReal(8));
    painter.save();painter.scale(UiZoom::instance().factor(),UiZoom::instance().factor());
    QRect preview(14,14,qRound(uiBase(width()))-28,76);
    QColor background(previewLight ? "#ffffff" : "#282a36");
    QColor side(previewLight ? "#f6f8fa" : "#21222c");
    QColor accent(previewLight ? "#0366d6" : "#bd93f9");
    painter.fillRect(preview, background);
    painter.fillRect(QRect(preview.topLeft(), QSize(preview.width(), 12)), side);
    painter.fillRect(QRect(preview.left(), preview.top() + 12, 38, preview.height() - 12), side);
    painter.setPen(QPen(QColor(previewLight ? "#6a737d" : "#a5acc6"), 2, Qt::SolidLine, Qt::RoundCap));
    for (int row = 0; row < 3; ++row)
        painter.drawLine(QPoint(preview.left() + 10, preview.top() + 27 + row * 14),
                         QPoint(preview.left() + 27, preview.top() + 27 + row * 14));
    for (int row = 0; row < 4; ++row) {
        painter.setPen(QPen(row % 2 ? accent : QColor(previewLight ? "#22863a" : "#50fa7b"), 3, Qt::SolidLine,
                            Qt::RoundCap));
        painter.drawLine(QPoint(preview.left() + 51, preview.top() + 24 + row * 12),
                         QPoint(preview.left() + 82 + (row % 3) * 24, preview.top() + 24 + row * 12));
    }
    painter.restore();
    QStyleOptionButton option;
    initStyleOption(&option);
    const int indicator = style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth, &option, this);
    option.rect = QRect(uiPx(16), height() - uiPx(24) - fontMetrics().height() / 2, indicator, indicator);
    style()->drawPrimitive(QStyle::PE_IndicatorRadioButton, &option, &painter, this);
    painter.setPen(palette.text);
    painter.drawText(QRect(uiPx(24) + indicator, uiPx(94), width() - uiPx(38) - indicator, height() - uiPx(98)),
                     Qt::AlignLeft | Qt::AlignVCenter, text());
}
} // namespace rs
