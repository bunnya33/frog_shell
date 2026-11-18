#include "editorcompare.h"
#include "contentfont.h"
#include "editorlexer.h"
#include "icons.h"
#include "layoutmetrics.h"
#include "windowstyle.h"
#include "../core/operationgate.h"
#include "applock.h"
#include <QScopedValueRollback>

namespace rs {
namespace {
constexpr int addedMark = 16, removedMark = 17, detailIndicator = 16, gapStyle = 40;

int rgb(QColor color) {
    return color.red() | (color.green() << 8) | (color.blue() << 16);
}

QColor blend(QColor base, QColor tint, qreal opacity) {
    return QColor::fromRgbF(base.redF()*(1-opacity)+tint.redF()*opacity,
                            base.greenF()*(1-opacity)+tint.greenF()*opacity,
                            base.blueF()*(1-opacity)+tint.blueF()*opacity);
}

void drawGap(QPainter &painter, const QRect &area) {
    const auto theme = colors();
    QColor hatch = blend(theme.background, theme.muted, .27);
    painter.fillRect(area, QBrush(hatch, Qt::BDiagPattern));
}

class LeadingGap final : public QWidget {
  public:
    explicit LeadingGap(EditorSurface *editor) : editor(editor) {}
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), colors().background);
        drawGap(painter, rect().adjusted(int(editor->send(SCI_GETMARGINWIDTHN,0)),0,0,0));
    }
  private:
    EditorSurface *editor;
};

class ReadOnlyMessage final : public QLabel {
  public:
    explicit ReadOnlyMessage(QWidget *parent)
        : QLabel(QStringLiteral("无法在只读编辑器中编辑"), parent) {
        setObjectName("compareReadOnlyMessage");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setContentsMargins(uiPx(7), 0, uiPx(7), uiPx(5));
        setMinimumHeight(uiPx(26));
        hide();
    }

    void placeAt(QPoint caret, int rowHeight) {
        adjustSize();
        const QRect area = parentWidget()->rect();
        const int x = qBound(0, caret.x()-uiPx(8), qMax(0, area.width()-width()));
        below = caret.y() < height();
        const int y = below ? caret.y()+rowHeight : caret.y()-height();
        arrow = qBound(uiPx(5), caret.x()-x, width()-uiPx(5));
        setContentsMargins(uiPx(7), below ? uiPx(5) : 0, uiPx(7), below ? 0 : uiPx(5));
        move(x, qBound(0, y, qMax(0, area.height()-height())));
        raise();
        show();
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        const auto theme = colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int tail = uiPx(5);
        const QRectF box = QRectF(rect()).adjusted(.5, below ? tail+.5 : .5, -.5, below ? -.5 : -tail-.5);
        const qreal edge = below ? box.top() : box.bottom();
        painter.setPen(theme.accent);
        painter.setBrush(theme.tabs);
        painter.drawRoundedRect(box, uiReal(3), uiReal(3));
        QPolygonF point{{qreal(arrow-tail), edge}, {qreal(arrow), edge+(below ? -tail : tail)},
                        {qreal(arrow+tail), edge}};
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(point);
        painter.setPen(theme.accent);
        painter.drawPolyline(point);
        painter.setPen(theme.text);
        painter.drawText(box, Qt::AlignCenter, text());
    }
  private:
    int arrow = 0;
    bool below = false;
};
}

class CompareSurface final : public EditorSurface {
  public:
    explicit CompareSurface(bool readOnly) {
        setReadOnly(readOnly);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        send(SCI_SETENDATLASTLINE, false);
        send(SCI_ANNOTATIONSETVISIBLE, ANNOTATION_STANDARD);
        bindContentFont(this, ContentFontRole::Editor);
        hideHint.setSingleShot(true);
        hideHint.setInterval(1800);
        connect(&hideHint, &QTimer::timeout, this, &CompareSurface::dismissHint);
        if (readOnly) {
            connect(this, &ScintillaEditBase::modifyAttemptReadOnly, this, &CompareSurface::showHint);
            connect(this, &EditorSurface::cursorPositionChanged, this, &CompareSurface::dismissHint);
        }
    }

    void dismissHint() {
        if (hint) hint->hide();
        hideHint.stop();
    }

  protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (isReadOnly() && (event->matches(QKeySequence::Paste) || event->matches(QKeySequence::Cut) ||
                            event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete)) {
            showHint();
            event->accept();
            return;
        }
        EditorSurface::keyPressEvent(event);
    }

    void inputMethodEvent(QInputMethodEvent *event) override {
        if (isReadOnly() && (!event->commitString().isEmpty() ||
                            !event->preeditString().isEmpty() || event->replacementLength())) {
            showHint();
            event->accept();
            return;
        }
        EditorSurface::inputMethodEvent(event);
    }

    void focusOutEvent(QFocusEvent *event) override {
        dismissHint();
        EditorSurface::focusOutEvent(event);
    }

    void hideEvent(QHideEvent *event) override {
        dismissHint();
        EditorSurface::hideEvent(event);
    }

    void paintEvent(QPaintEvent *event) override {
        EditorSurface::paintEvent(event);
        const int first = int(send(SCI_GETFIRSTVISIBLELINE));
        const int height = qMax(1, lineHeight());
        const int last = first+viewport()->height()/height+1;
        QPainter painter(viewport());
        painter.setClipRegion(event->region());
        const int gutter = int(send(SCI_GETMARGINWIDTHN, 0));
        const int width = viewport()->width()-gutter;
        for (int line = int(send(SCI_DOCLINEFROMVISIBLE, first)); line < blockCount(); ++line) {
            const int row = int(send(SCI_VISIBLEFROMDOCLINE, line));
            if (row >= last) break;
            const int count = int(send(SCI_ANNOTATIONGETLINES, line));
            const int start = qMax(first, row+1);
            const int end = qMin(last, row+1+count);
            if (end > start) drawGap(painter, QRect(gutter, (start-first)*height, width, (end-start)*height));
        }
    }

  private:
    ReadOnlyMessage *hint = nullptr;
    QTimer hideHint;

    void showHint() {
        if (!isVisible() || OperationGate::locked()) return;
        if (!hint) hint = new ReadOnlyMessage(viewport());
        auto caret = cursorRect().topLeft();
        caret.setX(qBound(0, caret.x(), viewport()->width()));
        hint->placeAt(caret, lineHeight());
        hideHint.start();
    }
};

class CompareOverview final : public QWidget {
  public:
    CompareOverview(QWidget *parent, std::function<void(int)> jump)
        : QWidget(parent), jump(std::move(jump)) {
        setObjectName("compareOverview");
        setAccessibleName("差异概览");
        setToolTip("差异概览");
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setFixedWidth(uiPx(22));
        connect(&UiZoom::instance(), &UiZoom::changed, this, [this] { setFixedWidth(uiPx(22)); });
    }

    void setDiff(const QList<DiffRow> &rows) {
        flags.clear();
        flags.reserve(rows.size());
        for (const auto &row : rows)
            flags.append(!row.changed ? 0 : (row.left>=0 ? 1 : 0) | (row.right>=0 ? 2 : 0));
        invalidate();
    }

    void invalidate() { bands = {}; update(); }

    void setRange(int firstRow, int visibleRows, int totalRows) {
        first = firstRow;
        visible = visibleRows;
        total = totalRows;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        if (bands.isNull()) {
            bands = QPixmap(size()*devicePixelRatioF());
            bands.setDevicePixelRatio(devicePixelRatioF());
            const auto theme = colors();
            bands.fill(theme.background);
            QPainter painter(&bands);
            painter.setPen(theme.divider);
            painter.drawLine(0, 0, 0, height());
            const int lane = (width()-uiPx(5))/2;
            for (int row = 0; row < flags.size(); ++row) {
                if (!flags[row]) continue;
                const auto top = int(qint64(row)*height()/flags.size());
                const auto bottom = int(qint64(row+1)*height()/flags.size());
                const int h = qMax(uiPx(2), bottom-top);
                if (flags[row] & 1) painter.fillRect(uiPx(3),top,lane,h,theme.red);
                if (flags[row] & 2) painter.fillRect(uiPx(3)+lane,top,lane,h,theme.green);
            }
        }
        QPainter painter(this);
        painter.drawPixmap(0, 0, bands);
        const int top = int(qint64(first)*height()/total);
        const int bottom = int(qint64(qMin(first+visible,total))*height()/total);
        QRect viewport(1, top, width()-1, qMax(1,bottom-top));
        auto tint = colors().accent;
        tint.setAlpha(36);
        painter.fillRect(viewport,tint);
        tint.setAlpha(130);
        painter.setPen(tint);
        painter.drawRect(viewport.adjusted(0,0,-1,-1));
        if (hasFocus()) painter.drawRect(rect().adjusted(0,0,-1,-1));
    }

    void resizeEvent(QResizeEvent *) override { invalidate(); }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) jumpTo(event->position().y());
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (event->buttons() & Qt::LeftButton) jumpTo(event->position().y());
    }
    void keyPressEvent(QKeyEvent *event) override {
        switch (event->key()) {
        case Qt::Key_Up: jump(first-1); break;
        case Qt::Key_Down: jump(first+1); break;
        case Qt::Key_PageUp: jump(first-visible); break;
        case Qt::Key_PageDown: jump(first+visible); break;
        case Qt::Key_Home: jump(0); break;
        case Qt::Key_End: jump(total-1); break;
        default: QWidget::keyPressEvent(event);
        }
    }

  private:
    void jumpTo(qreal y) { jump(int(y*total/qMax(1,height()))-visible/2); }
    std::function<void(int)> jump;
    QPixmap bands;
    QList<quint8> flags;
    int first = 0, visible = 1, total = 1;
};

namespace {
QWidget *pane(ElidedLabel *caption, CompareSurface *surface, QWidget *gap, QWidget *overview = nullptr) {
    auto box = new QWidget;
    auto layout = new QVBoxLayout(box);
    uiMargins(layout,0,0,0,0);
    uiSpacing(layout,0);
    auto band = new PanelBand(28);
    auto header = new QHBoxLayout(band);
    uiMargins(header,10,0,10,0);
    caption->setTextFormat(Qt::PlainText);
    caption->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    header->addWidget(caption);
    layout->addWidget(band);

    auto body = new QHBoxLayout;
    uiMargins(body,0,0,0,0);
    uiSpacing(body,0);
    auto content = new QVBoxLayout;
    uiMargins(content,0,0,0,0);
    uiSpacing(content,0);
    gap->setFixedHeight(0);
    content->addWidget(gap);
    content->addWidget(surface,1);
    body->addLayout(content,1);
    if (overview) body->addWidget(overview);
    layout->addLayout(body,1);
    return box;
}
}

EditorCompare::EditorCompare(Editor *editor, QWidget *parent) : QWidget(parent), source(editor) {
    setObjectName("editorCompare");
    setWindowTitle("文本对比 · " + QFileInfo(editor->path).fileName());
    auto layout = new QVBoxLayout(this);
    uiMargins(layout,0,0,0,0);
    uiSpacing(layout,0);

    left = new CompareSurface(true);
    right = new CompareSurface(false);
    left->setObjectName("compareLeft");
    right->setObjectName("compareRight");
    left->setAccessibleName("原文（只读）");
    right->setAccessibleName("对比文本");
    leftLexer = new EditorLexer(left);
    rightLexer = new EditorLexer(right);
    setFocusProxy(right);

    auto toolbar = new PanelBand(32);
    auto controls = new QHBoxLayout(toolbar);
    uiMargins(controls,8,0,8,0);
    uiSpacing(controls,6);
    status = new ElidedLabel;
    status->setObjectName("compareStatus");
    status->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    status->setProperty("muted",true);
    controls->addWidget(status,1);
    ignoreSpace = new QCheckBox("忽略空白");
    controls->addWidget(ignoreSpace);
    auto navigation = [this,controls](Icon glyph, const QString &name, int delta) {
        auto action = new QAction(icon(glyph),name,this);
        action->setEnabled(false);
        auto button = iconButton(glyph,name);
        button->setDefaultAction(action);
        controls->addWidget(button);
        connect(action,&QAction::triggered,this,[this,delta]{ navigate(delta); });
        return action;
    };
    previous = navigation(Icon::Up,"上一处差异",-1);
    next = navigation(Icon::Down,"下一处差异",1);
    layout->addWidget(toolbar);

    auto split = new PanelSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    leftGap = new LeadingGap(left);
    rightGap = new LeadingGap(right);
    sourceLabel = new ElidedLabel;
    auto rightLabel = new ElidedLabel;
    rightLabel->setText("对比文本（可编辑）");
    rightLabel->setToolTip("在右侧输入或粘贴文本");
    overview = new CompareOverview(this, [this](int row) { scrollTo(row); });
    split->addWidget(pane(sourceLabel,left,leftGap));
    split->addWidget(pane(rightLabel,right,rightGap,overview));
    split->setSizes({500,500});
    layout->addWidget(split,1);

    sourceName = source->path;
    sourceLabel->setText(sourceName+"（只读）");
    sourceLabel->setToolTip(sourceName);
    sourceSnapshot = source->code->toPlainText();
    displayedSource = sourceSnapshot;
    left->setPlainText(displayedSource);
    status->setText("在右侧输入或粘贴文本");

    refresh.setSingleShot(true);
    refresh.setInterval(160);
    connect(&refresh,&QTimer::timeout,this,&EditorCompare::startCompare);
    connect(ignoreSpace,&QCheckBox::toggled,this,&EditorCompare::scheduleCompare);
    connect(right,&EditorSurface::textChanged,this,&EditorCompare::scheduleCompare);
    connect(source->code,&EditorSurface::textChanged,this,[this] {
        sourceChanged = true;
        if (right->byteLength() || !difference.rows.isEmpty()) scheduleCompare();
        else {
            sourceSnapshot = source->code->toPlainText();
            displayedSource = sourceSnapshot;
            left->setPlainText(displayedSource);
            sourceChanged = false;
            scrollTo(firstRow);
        }
    });
    connect(source,&Editor::changed,this,[this] {
        if (source && sourceName != source->path) {
            sourceName = source->path;
            sourceLabel->setText(sourceName+"（只读）");
            sourceLabel->setToolTip(sourceName);
        }
    });
    connect(source,&QObject::destroyed,this,[this] {
        sourceLabel->setText(sourceName+"（已关闭）");
    });
    for (auto view : {left,right}) {
        view->viewport()->installEventFilter(this);
        connect(view,&ScintillaEditBase::verticalScrolled,this,[this,view]{ syncScroll(view); });
        connect(view->horizontalScrollBar(),&QScrollBar::valueChanged,view,&CompareSurface::dismissHint);
    }
    for (auto target : {leftGap,rightGap,static_cast<QWidget *>(overview)}) target->installEventFilter(this);
    connect(&UiZoom::instance(),&UiZoom::changed,this,[this] {
        QTimer::singleShot(0,this,[this]{ scrollTo(firstRow); });
    });
    if (auto lock = AppLock::instance()) {
        connect(lock,&AppLock::lockedChanged,this,[this](bool locked) {
            if (locked) left->dismissHint();
            else if (pending && isVisible()) refresh.start();
        });
    }
    refreshTheme();
}

void EditorCompare::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    QTimer::singleShot(0,this,[this]{ scrollTo(firstRow); });
    if (pending) refresh.start();
}

void EditorCompare::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    QTimer::singleShot(0,this,[this]{ scrollTo(firstRow); });
}

bool EditorCompare::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::Wheel && (object == left->viewport() || object == right->viewport() ||
        object == leftGap || object == rightGap || object == overview)) {
        auto wheel = static_cast<QWheelEvent *>(event);
        const int angle = wheel->angleDelta().y();
        const int pixels = wheel->pixelDelta().y();
        if (!(wheel->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) && (angle || pixels)) {
            wheelRemainder -= pixels ? qreal(pixels)/qMax(1,left->lineHeight()) :
                                      qreal(angle)*QApplication::wheelScrollLines()/120;
            const int delta = int(wheelRemainder);
            wheelRemainder -= delta;
            scrollTo(firstRow+delta);
            wheel->accept();
            return true;
        }
    }
    return QWidget::eventFilter(object,event);
}

void EditorCompare::scheduleCompare() {
    ++generation;
    pending = true;
    previous->setEnabled(false);
    next->setEnabled(false);
    if (isVisible()) refresh.start();
}

void EditorCompare::startCompare() {
    if (running || !pending || !isVisible() || OperationGate::locked()) return;
    pending = false;
    if (source && sourceChanged) sourceSnapshot = source->code->toPlainText();
    sourceChanged = false;
    const auto rightText = right->toPlainText();
    running = true;
    const quint64 revision = generation;
    const bool ignore = ignoreSpace->isChecked();
    auto result = std::make_shared<TextDiff>();
    auto worker = QThread::create([snapshot=sourceSnapshot,rightText,ignore,result] {
        *result = compareText(snapshot,rightText,ignore);
    });
    connect(worker,&QThread::finished,this,[this,result,revision] {
        running = false;
        if (revision == generation) {
            if (isVisible() && !OperationGate::locked()) showResult(std::move(*result));
            else pending = true;
        }
        if (pending && isVisible()) refresh.start();
    });
    connect(worker,&QThread::finished,worker,&QObject::deleteLater);
    worker->start();
}

void EditorCompare::showResult(TextDiff result) {
    const QScopedValueRollback<bool> change(applying,true);
    const int oldTop = int(right->send(SCI_GETFIRSTVISIBLELINE));
    const int docLine = int(right->send(SCI_DOCLINEFROMVISIBLE,oldTop));
    const int subLine = oldTop-int(right->send(SCI_VISIBLEFROMDOCLINE,docLine));
    const bool scrolled = firstRow > 0;
    const bool inLeadingGap = firstRow < rightLeading;
    if (displayedSource != sourceSnapshot) {
        displayedSource = sourceSnapshot;
        left->setPlainText(displayedSource);
    }
    difference = std::move(result);
    currentHunk = -1;
    leftLeading = decorate(left,true);
    rightLeading = decorate(right,false);
    overview->setDiff(difference.rows);
    const int count = int(difference.hunks.size());
    status->setText(difference.tooLarge ? QString("单侧最多支持 8 Mi 字符和 40000 行") :
                    count ? QString("%1 处差异%2").arg(count).arg(difference.coarse ? " · 大段差异已按行显示" : "") :
                            QString("内容一致"));
    previous->setEnabled(count>0);
    next->setEnabled(count>0);
    const int row = int(right->send(SCI_VISIBLEFROMDOCLINE,docLine));
    const int extra = int(right->send(SCI_ANNOTATIONGETLINES,docLine));
    const int alignedRow = inLeadingGap && rightLeading > firstRow ? firstRow :
        rightLeading+row+qMin(subLine,extra);
    scrollTo(scrolled ? alignedRow : 0);
}

int EditorCompare::decorate(EditorSurface *view, bool leftSide) {
    const auto theme = colors();
    const int mark = leftSide ? removedMark : addedMark;
    const auto tint = leftSide ? theme.red : theme.green;
    view->send(SCI_MARKERDELETEALL,mark);
    view->send(SCI_MARKERDEFINE,mark,SC_MARK_BACKGROUND);
    view->send(SCI_MARKERSETBACK,mark,rgb(blend(theme.background,tint,.23)));
    view->send(SCI_ANNOTATIONCLEARALL);
    view->send(SCI_STYLESETBACK,gapStyle,rgb(theme.background));
    view->send(SCI_INDICSETSTYLE,detailIndicator,INDIC_FULLBOX);
    view->send(SCI_INDICSETFORE,detailIndicator,rgb(tint));
    view->send(SCI_INDICSETALPHA,detailIndicator,lightTheme() ? 86 : 112);
    view->send(SCI_INDICSETOUTLINEALPHA,detailIndicator,0);
    view->send(SCI_INDICSETUNDER,detailIndicator,true);
    view->send(SCI_SETINDICATORCURRENT,detailIndicator);
    view->send(SCI_SETINDICATORVALUE,1);
    view->send(SCI_INDICATORCLEARRANGE,0,view->byteLength());
    int previousLine = -1, gaps = 0, leading = 0;
    auto flush = [&] {
        if (!gaps) return;
        if (previousLine < 0) leading = gaps;
        else {
            QByteArray blank(gaps*2-1,' ');
            for (int i=1; i<blank.size(); i+=2) blank[i] = '\n';
            view->send(SCI_ANNOTATIONSETSTYLE,previousLine,gapStyle);
            view->sends(SCI_ANNOTATIONSETTEXT,previousLine,blank.constData());
        }
        gaps = 0;
    };
    for (const auto &row : difference.rows) {
        const int line = leftSide ? row.left : row.right;
        if (line < 0) { ++gaps; continue; }
        flush();
        previousLine = line;
        if (!row.changed) continue;
        view->send(SCI_MARKERADD,line,mark);
        const auto &spans = leftSide ? row.leftSpans : row.rightSpans;
        if (spans.isEmpty()) continue;
        const int start = int(view->send(SCI_INDEXPOSITIONFROMLINE,line,SC_LINECHARACTERINDEX_UTF16));
        for (const auto &span : spans) {
            const int begin = view->fromUtf16(start+span.start);
            const int end = view->fromUtf16(start+span.start+span.length);
            view->send(SCI_INDICATORFILLRANGE,begin,end-begin);
        }
    }
    flush();
    view->viewport()->update();
    return leading;
}

void EditorCompare::refreshTheme() {
    for (auto view : {left,right}) {
        view->refreshSurfaceTheme();
        view->send(SCI_STYLESETBACK,gapStyle,rgb(colors().background));
        const bool original = view == left;
        view->send(SCI_MARKERSETBACK,original ? removedMark : addedMark,
                   rgb(blend(colors().background,original ? colors().red : colors().green,.23)));
        view->send(SCI_INDICSETFORE,detailIndicator,rgb(original ? colors().red : colors().green));
        view->send(SCI_INDICSETALPHA,detailIndicator,lightTheme() ? 86 : 112);
        view->viewport()->update();
    }
    leftLexer->rehighlight();
    rightLexer->rehighlight();
    leftGap->update();
    rightGap->update();
    overview->invalidate();
    previous->setIcon(icon(Icon::Up));
    next->setIcon(icon(Icon::Down));
}

void EditorCompare::navigate(int offset) {
    const int count = int(difference.hunks.size());
    if (!count || pending || running || OperationGate::locked()) return;
    currentHunk = currentHunk < 0 ? (offset>0 ? 0 : count-1) : (currentHunk+offset+count)%count;
    scrollTo(difference.hunks[currentHunk]-pageRows()/3);
    status->setText(QString("第 %1 处，共 %2 处差异").arg(currentHunk+1).arg(count));
}

int EditorCompare::rowCount() const {
    const auto displayRows = [](EditorSurface *view, int leading) {
        const int last = view->blockCount()-1;
        return leading+int(view->send(SCI_VISIBLEFROMDOCLINE,last)+view->send(SCI_ANNOTATIONGETLINES,last))+1;
    };
    return qMax(displayRows(left,leftLeading),displayRows(right,rightLeading));
}

int EditorCompare::pageRows() const {
    const int height = right->viewport()->height()+rightGap->height();
    return qMax(1,height/qMax(1,right->lineHeight()));
}

void EditorCompare::scrollTo(int row) {
    if (OperationGate::locked()) return;
    const QScopedValueRollback<bool> change(applying,true);
    const int rows = rowCount();
    const int visible = pageRows();
    row = qBound(0,row,qMax(0,rows-visible));
    if (row != firstRow) left->dismissHint();
    firstRow = row;
    const auto scroll = [row,visible](EditorSurface *view, QWidget *gap, int leading) {
        // Internal alignment rows are native annotations. A gap above line 1
        // uses viewport space because Scintilla annotations follow a real line.
        const int gapRows = qBound(0,leading-row,visible);
        gap->setFixedHeight(gapRows*view->lineHeight());
        view->send(SCI_SETFIRSTVISIBLELINE,qMax(0,row-leading));
    };
    scroll(left,leftGap,leftLeading);
    scroll(right,rightGap,rightLeading);
    overview->setRange(firstRow,visible,rows);
}

void EditorCompare::syncScroll(EditorSurface *from) {
    if (applying) return;
    const int leading = from == left ? leftLeading : rightLeading;
    const int row = int(from->send(SCI_GETFIRSTVISIBLELINE));
    if (!row && firstRow < leading) return;
    scrollTo(row+leading);
}
}
