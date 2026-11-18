#include "editorminimap.h"
#include "editor.h"
#include "editorlexer.h"

namespace rs {

EditorMinimap::EditorMinimap(CodeEdit *code, QWidget *parent) : QWidget(parent), editor(code) {
    setObjectName("editorMinimap");
    setAccessibleName("文档缩略图");
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    parent->installEventFilter(this);
    refresh.setSingleShot(true);
    refresh.setInterval(120);
    connect(&refresh, &QTimer::timeout, this, &EditorMinimap::renderPreview);
    connect(editor, &EditorSurface::textChanged, this, &EditorMinimap::invalidate);
    connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { update(); });
    connect(editor->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { update(); });
    connect(&UiZoom::instance(), &UiZoom::changed, this, [this] { fitWidth(); invalidate(); });
    fitWidth();
}

void EditorMinimap::invalidate() {
    if (isVisible()) refresh.start();
    else preview = {};
    update();
}

bool EditorMinimap::eventFilter(QObject *watched, QEvent *event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        fitWidth();
        invalidate();
    }
    if (watched == parentWidget() && event->type() == QEvent::PaletteChange) invalidate();
    return QWidget::eventFilter(watched,event);
}

void EditorMinimap::fitWidth() {
    const int available = parentWidget()->width();
    const int desired = qMin(uiPx(108), available / 5);
    setFixedWidth(available >= uiPx(330) ? desired : 0);
}

void EditorMinimap::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    renderPreview();
}
void EditorMinimap::hideEvent(QHideEvent *event) {
    refresh.stop();
    preview = {};
    QWidget::hideEvent(event);
}

void EditorMinimap::renderPreview() {
    if (!isVisible() || width() < 8 || height() < 1) return;
    const int lineCount = editor->blockCount();
    pitch = qMax(1,uiPx(2));
    const int samples = qMin(lineCount, qBound(1,height()/pitch,1100));
    preview = QImage(width(), samples * pitch, QImage::Format_RGB32);
    const auto palette = colors();
    preview.fill(palette.background);
    QPainter painter(&preview);
    const int advance = qMax(1,uiPx(1));
    const int padding = uiPx(5);
    const int columns = qMax(0,(width()-padding*2)/advance);
    const auto swatches = EditorLexer::styleColors();
    for (int row = 0; row < samples; ++row) {
        const int line = int(qint64(row)*lineCount/samples);
        const int pos = int(editor->send(SCI_POSITIONFROMLINE,line));
        const int end = int(editor->send(SCI_GETLINEENDPOSITION,line));
        const auto bytes = editor->byteRange(pos,qMin(end,pos+columns*2));
        const auto styles = EditorLexer::previewStyles(bytes);
        int column = 0;
        for (int offset = 0; offset < bytes.size() && column < columns; ++offset) {
            const auto value = uchar(bytes[offset]);
            if ((value & 0xc0) == 0x80) continue;
            if (value == '\t') { column += 4 - column % 4; continue; }
            if (value != ' ') {
                const auto ink = swatches[uchar(styles[offset])];
                painter.setOpacity(uchar(styles[offset]) == 0 ? .46 : .82);
                painter.fillRect(padding+column*advance,row*pitch,advance,qMax(1,pitch-1),ink);
            }
            ++column;
        }
    }
    update();
}

QRectF EditorMinimap::visibleRegion() const {
    if (preview.isNull()) return {};
    const int start = int(editor->send(SCI_DOCLINEFROMVISIBLE,editor->send(SCI_GETFIRSTVISIBLELINE)));
    const int visible = int(editor->send(SCI_LINESONSCREEN));
    const int end = int(editor->send(SCI_DOCLINEFROMVISIBLE,editor->send(SCI_GETFIRSTVISIBLELINE)+visible));
    const qreal scale = qreal(preview.height()) / editor->blockCount();
    const qreal band = qMin(qreal(preview.height()),qMax(qreal(uiPx(8)),scale*qMax(1,end-start)));
    const qreal top = qBound(0.,start*scale,preview.height()-band);
    return {1,top,qreal(width()-1),band};
}

void EditorMinimap::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    const auto palette = colors();
    painter.fillRect(rect(),palette.background);
    painter.drawImage(QPoint(0,0),preview);
    QRectF region = visibleRegion();
    if (!region.isEmpty()) {
        auto tint = palette.accent;
        tint.setAlpha(underMouse() ? 66 : 43);
        painter.fillRect(region,tint);
        tint.setAlpha(125);
        painter.setPen(tint);
        painter.drawRect(region.adjusted(0,.5,-.5,-.5));
    }
    painter.setPen(palette.divider);
    painter.drawLine(0,0,0,height());
    if (hasFocus()) {
        painter.setPen(palette.accent);
        painter.drawRect(rect().adjusted(0,0,-1,-1));
    }
}

void EditorMinimap::scrollTo(qreal y) {
    if (preview.isNull()) return;
    const qreal freeHeight = preview.height()-visibleRegion().height();
    if (freeHeight <= 0) return;
    const auto bar = editor->verticalScrollBar();
    bar->setValue(qRound(qBound(0.,(y-dragOffset)/freeHeight,1.) * bar->maximum()));
}

void EditorMinimap::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    const auto region = visibleRegion();
    const auto y = event->position().y();
    dragOffset = region.contains(event->position()) ? y-region.top() : region.height()/2;
    scrollTo(y);
}

void EditorMinimap::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) scrollTo(event->position().y());
}

void EditorMinimap::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) editor->setFocus();
}

void EditorMinimap::wheelEvent(QWheelEvent *event) {
    QApplication::sendEvent(editor->viewport(),event);
}

void EditorMinimap::keyPressEvent(QKeyEvent *event) {
    const auto bar = editor->verticalScrollBar();
    switch (event->key()) {
    case Qt::Key_Up: bar->triggerAction(QAbstractSlider::SliderSingleStepSub); break;
    case Qt::Key_Down: bar->triggerAction(QAbstractSlider::SliderSingleStepAdd); break;
    case Qt::Key_PageUp: bar->triggerAction(QAbstractSlider::SliderPageStepSub); break;
    case Qt::Key_PageDown: bar->triggerAction(QAbstractSlider::SliderPageStepAdd); break;
    case Qt::Key_Home: bar->triggerAction(QAbstractSlider::SliderToMinimum); break;
    case Qt::Key_End: bar->triggerAction(QAbstractSlider::SliderToMaximum); break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}
}
