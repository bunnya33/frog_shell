#include "messagebox.h"
#include "terminal.h"
#include "theme.h"
#include "contentfont.h"
#include "vterm_extensions.h"
#include "../core/diagnostics.h"
namespace rs {
Terminal::Terminal(QWidget *parent)
    : QAbstractScrollArea(parent), term(vterm_new(24, 80)), screen(vterm_obtain_screen(term)),
      state(vterm_obtain_state(term)) {
    setAccessibleName("SSH 终端");
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
    viewport()->setCursor(Qt::IBeamCursor);
    setFrameShape(QFrame::NoFrame);
    // Reset invokes the callbacks synchronously, so timers must already be configured.
    repaintTimer.setSingleShot(true);
    repaintTimer.setInterval(16);
    connect(&repaintTimer, &QTimer::timeout, viewport(), qOverload<>(&QWidget::update));
    resizeTimer.setSingleShot(true);
    resizeTimer.setInterval(0);
    connect(&resizeTimer, &QTimer::timeout, this, &Terminal::applySize);
    connect(&cursorTimer, &QTimer::timeout, this, [this] {
        if (!cursorInView()) {
            updateCursorBlink();
            return;
        }
        cursorOn = !cursorOn;
        viewport()->update(cursorRect());
    });
    connect(QGuiApplication::styleHints(), &QStyleHints::cursorFlashTimeChanged, this,
            [this] { updateCursorBlink(true); });
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this] { updateCursorBlink(true); });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        updateCursorBlink(true);
        viewport()->update();
    });
    vterm_set_utf8(term, 1);
    vterm_output_set_callback(
        term,
        [](const char *s, size_t n, void *u) {
            emit static_cast<Terminal *>(u)->input(QByteArray(s, qsizetype(n)));
        },
        this);
    callbacks.damage = [](VTermRect, void *u) {
        auto t = static_cast<Terminal *>(u);
        if (!t->repaintTimer.isActive())
            t->repaintTimer.start();
        return 1;
    };
    callbacks.movecursor = [](VTermPos p, VTermPos, int visible, void *u) {
        auto t = static_cast<Terminal *>(u);
        const QRect old = t->cursorRect();
        t->cursor = p;
        t->cursorVisible = visible;
        t->updateCursorRect(old);
        t->updateCursorBlink(true);
        QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
        return 1;
    };
    callbacks.settermprop = [](VTermProp p, VTermValue *v, void *u) {
        auto t = static_cast<Terminal *>(u);
        switch (p) {
        case VTERM_PROP_ALTSCREEN:
            t->alternate = v->boolean;
            t->screenTransition = true;
            t->viewport()->update();
            break;
        case VTERM_PROP_CURSORVISIBLE:
            t->cursorVisible = v->boolean;
            break;
        case VTERM_PROP_CURSORBLINK:
            t->cursorBlink = v->boolean;
            break;
        case VTERM_PROP_CURSORSHAPE:
            t->cursorShape = v->number;
            break;
        default:
            return 1;
        }
        // Visibility/shape can change without any cursor movement or text damage.
        t->updateCursorBlink(true);
        return 1;
    };
    callbacks.sb_pushline = [](int n, const VTermScreenCell *cells, void *u) {
        auto t = static_cast<Terminal *>(u);
        QBitArray protectedCells(n);for(int col=0;col<n;++col)if(rabbitshell_vterm_screen_pushed_protected(t->screen,col))protectedCells.setBit(col);
        t->pushHistory(n,cells,rabbitshell_vterm_screen_pushed_lineinfo(t->screen),&protectedCells);
        return 1;
    };
    callbacks.sb_popline = [](int n, VTermScreenCell *cells, void *u) {
        auto t = static_cast<Terminal *>(u);
        if (t->history.empty())
            return 0;
        auto &line = t->history.back();
        // Never consume a row that the caller's old/new grid would truncate.
        if(line.size()>qMin(n,t->cols))return 0;
        VTermScreenCell blank{};blank.width=1;
        vterm_state_get_default_colors(t->state,&blank.fg,&blank.bg);
        blank.fg.type=(blank.fg.type & ~VTERM_COLOR_DEFAULT_MASK)|VTERM_COLOR_DEFAULT_FG;
        blank.bg.type=(blank.bg.type & ~VTERM_COLOR_DEFAULT_MASK)|VTERM_COLOR_DEFAULT_BG;
        std::fill(cells, cells + n, blank);
        std::copy(line.cbegin(),line.cend(),cells);
        t->historySize -= rowBytes(line);
        t->history.pop_back();
        return 1;
    };
    callbacks.sb_clear = [](void *u) {
        auto t = static_cast<Terminal *>(u);
        t->history.clear();
        t->historySize = 0;
        return 1;
    };
    vterm_screen_set_callbacks(screen, &callbacks, this);
    vterm_screen_enable_altscreen(screen, 1);
    vterm_screen_enable_reflow(screen, 1); // patched 0.3.3: primary buffer only
    vterm_screen_set_damage_merge(screen, VTERM_DAMAGE_SCREEN);
    vterm_screen_reset(screen, 1);
    bindContentFont(this, ContentFontRole::Terminal);
    refreshTheme();
}
Terminal::~Terminal() {
    vterm_free(term);
}
namespace {
int glyphCount(const QVector<VTermScreenCell> &line) {
    int count=0,used=int(line.size());
    while(used>0 && !line[used-1].chars[0])--used;
    for(int col=0;col<used;++col)if(line[col].chars[0]!=0xffffffffU)++count;
    return count;
}
}
void Terminal::appendHistory(HistoryRow line) {
    line.squeeze();historySize+=rowBytes(line);history.push_back(std::move(line));
    while(history.size()>2000 || historySize>2*1024*1024) {
        discardedGlyphs+=glyphCount(history.front());historySize-=rowBytes(history.front());history.pop_front();
    }
}
void Terminal::pushHistory(int n,const VTermScreenCell *cells,const VTermLineInfo *info,const QBitArray *protectedCells) {
    while(n>0 && !cells[n-1].chars[0])--n;
    // Preserve a genuinely blank physical history row, with valid cell width.
    VTermScreenCell blank{};blank.width=1;
    if(!n) {vterm_state_get_default_colors(state,&blank.fg,&blank.bg);cells=&blank;n=1;}
    for(int first=0;first<n;) {
        int end=qMin(first+cols,n);
        if(end<n && cells[end].chars[0]==0xffffffffU)--end;
        // cols>=10, so a valid two-cell glyph always fits on an empty row.
        if(end<=first)return;
        HistoryRow line(cells+first,cells+end);if(info)line.info=*info;
        if(protectedCells)for(int col=first;col<end&&col<protectedCells->size();++col)if(protectedCells->testBit(col)){
            line.protectedCells.resize(col-first+1);line.protectedCells.setBit(col-first);
        }
        if(first)line.info.continuation=1;
        appendHistory(std::move(line));
        first=end;
    }
}
void Terminal::resizePrimary(int columns,int lines) {
    VTermPos oldCursor,oldCombine;int pendingWrap=0;
    rabbitshell_vterm_screen_primary_cursor(screen,&oldCursor,&pendingWrap,&oldCombine);
    // Both inputs and the output are bounded: consume the old deque as we go,
    // keep at most the new screen tail, and enforce the history cap on emission.
    auto previous=std::move(history);history.clear();historySize=0;
    std::deque<HistoryRow> visible;
    HistoryRow current;bool record=false;
    int produced=0,cursorRow=-1,cursorColumn=0,combineRow=-1,combineColumn=0;
    auto flush=[&]{
        current.squeeze();visible.push_back(std::move(current));current=HistoryRow{};++produced;
        if(int(visible.size())>lines){appendHistory(std::move(visible.front()));visible.pop_front();}
    };
    auto consume=[&](const HistoryRow &source,int screenRow){
        if(!record||!source.info.continuation){if(record)flush();current.info=source.info;record=true;}
        int used=int(source.size());while(used>0&&!source[used-1].chars[0])--used;
        for(int col=0;col<used;){
            if(source[col].chars[0]==0xffffffffU){++col;continue;}
            const int width=col+1<used&&source[col+1].chars[0]==0xffffffffU?2:1;
            if(current.size()+width>columns){const auto info=current.info;flush();current.info=info;current.info.continuation=1;}
            if(screenRow==oldCursor.row&&!pendingWrap&&oldCursor.col>=col&&oldCursor.col<col+width){cursorRow=produced;cursorColumn=int(current.size())+oldCursor.col-col;}
            if(screenRow==oldCombine.row&&oldCombine.col>=col&&oldCombine.col<col+width){combineRow=produced;combineColumn=int(current.size())+oldCombine.col-col;}
            for(int n=0;n<width;++n){auto cell=source[col+n];
                // An unwritten gap inside content remains a space if it lands
                // at a new row edge; unused wide-glyph padding is never copied.
                if(!cell.chars[0])cell.chars[0]=' ';
                const int outputColumn=int(current.size());current.append(cell);
                if(source.protectedCells.size()>col+n&&source.protectedCells.testBit(col+n)){
                    if(current.protectedCells.size()<=outputColumn)current.protectedCells.resize(outputColumn+1);
                    current.protectedCells.setBit(outputColumn);
                }
            }
            col+=width;
        }
        if(screenRow==oldCursor.row&&(pendingWrap||oldCursor.col>=used)){
            cursorRow=produced;cursorColumn=int(current.size())+(pendingWrap?0:oldCursor.col-used);
        }
    };
    while(!previous.empty()){consume(previous.front(),-1);previous.pop_front();}
    int last=oldCursor.row;
    for(int row=rows-1;row>last;--row){bool used=false;for(int col=0;col<cols;++col){VTermScreenCell value{};rabbitshell_vterm_screen_primary_cell(screen,{row,col},&value);if(value.chars[0]){used=true;break;}}if(used){last=row;break;}}
    for(int row=0;row<=last;++row){
        HistoryRow source(cols);source.info=*rabbitshell_vterm_screen_primary_lineinfo(screen,row);
        for(int col=0;col<cols;++col)rabbitshell_vterm_screen_primary_cell(screen,{row,col},&source[col]);
        for(int col=0;col<cols;++col)if(rabbitshell_vterm_screen_primary_protected(screen,{row,col})){
            if(source.protectedCells.size()<=col)source.protectedCells.resize(col+1);source.protectedCells.setBit(col);
        }
        consume(source,row);
    }
    if(record)flush();
    const int screenStart=produced-int(visible.size());
    VTermScreenCell blank{};blank.width=1;vterm_state_get_default_colors(state,&blank.fg,&blank.bg);
    blank.fg.type=(blank.fg.type&~VTERM_COLOR_DEFAULT_MASK)|VTERM_COLOR_DEFAULT_FG;
    blank.bg.type=(blank.bg.type&~VTERM_COLOR_DEFAULT_MASK)|VTERM_COLOR_DEFAULT_BG;
    struct Tail {std::deque<HistoryRow> *rows;VTermScreenCell blank;} tail{&visible,blank};
    RabbitShellVTermResize prepared{};prepared.user=&tail;
    prepared.row=[](int row,int width,VTermScreenCell *cells,VTermLineInfo *info,void *user){
        auto &tail=*static_cast<Tail *>(user);std::fill(cells,cells+width,tail.blank);
        if(row<int(tail.rows->size())){const auto &line=(*tail.rows)[size_t(row)];std::copy(line.cbegin(),line.cend(),cells);*info=line.info;}return 1;
    };
    prepared.protected_cell=[](int row,int column,void *user){auto &tail=*static_cast<Tail *>(user);if(row>=int(tail.rows->size()))return 0;const auto &bits=(*tail.rows)[size_t(row)].protectedCells;return column<bits.size()&&bits.testBit(column)?1:0;};
    prepared.cursor={qBound(0,cursorRow-screenStart,lines-1),qBound(0,cursorColumn,columns-1)};
    prepared.phantom=cursorColumn>=columns;
    prepared.combine={combineRow-screenStart,combineColumn};prepared.map_combine=combineRow>=screenStart&&combineRow<produced;
    rabbitshell_vterm_screen_prepare_resize(screen,&prepared);
    cols=columns;rows=lines;vterm_set_size(term,rows,cols);
}
bool Terminal::continuesPrevious(int row) const {
    if(!alternate&&row>=0&&row<int(history.size()))return history[size_t(row)].info.continuation;
    const int screenRow=row-(alternate?0:int(history.size()));
    const auto info=screenRow>=0&&screenRow<rows?vterm_state_get_lineinfo(state,screenRow):nullptr;
    return info&&info->continuation;
}
int Terminal::usedColumns(int row) const {
    int used=cols;
    if(!alternate && row>=0 && row<int(history.size()))used=int(history[size_t(row)].size());
    VTermScreenCell value{};
    while(used>0 && (!cell(row,used-1,&value) || !value.chars[0]))--used;
    return used;
}
std::optional<qint64> Terminal::glyphOrdinal(QPoint position) const {
    qint64 ordinal=0;
    for(int row=0;row<=position.y();++row) {
        const int used=usedColumns(row);
        for(int col=0;col<used;++col) {
            VTermScreenCell value{};if(!cell(row,col,&value))continue;
            if(value.chars[0]==0xffffffffU)continue;
            if(row==position.y() && col<=position.x() && position.x()<col+qMax(1,int(value.width)))return ordinal;
            ++ordinal;
        }
    }
    return {};
}
QPoint Terminal::glyphPosition(qint64 ordinal) const {
    if(ordinal<0)return {-1,-1};
    for(int row=0;row<(alternate?0:int(history.size()))+rows;++row) {
        const int used=usedColumns(row);
        for(int col=0;col<used;++col) {
            VTermScreenCell value{};if(!cell(row,col,&value) || value.chars[0]==0xffffffffU)continue;
            if(ordinal--==0)return {col,row};
        }
    }
    return {-1,-1};
}
void Terminal::refreshTheme() {
    auto c = colors();
    VTermColor fg, bg;
    vterm_color_rgb(&fg, c.text.red(), c.text.green(), c.text.blue());
    vterm_color_rgb(&bg, c.background.red(), c.background.green(), c.background.blue());
    // Screen's setter rewrites existing default cells from the current pen, which
    // can carry an explicit SGR color. Resolve default flags at paint time instead,
    // including scrollback cells retained before a theme change.
    vterm_state_set_default_colors(state, &fg, &bg);
    const auto palette = terminalAnsiPalette();
    for (int i = 0; i < palette.size(); ++i) {
        VTermColor v;
        vterm_color_rgb(&v, palette[i].red(), palette[i].green(), palette[i].blue());
        vterm_state_set_palette_color(state, i, &v);
    }
    viewport()->update();
}
void Terminal::feed(const QByteArray &b) {
    DiagnosticScope feedScope(DiagEvent::TerminalFeedBegin,DiagEvent::TerminalFeedEnd,0,0,{},b.size());
    QScopedValueRollback<bool> batching(feeding, true);
    bool bottom = verticalScrollBar()->value() == verticalScrollBar()->maximum();
    vterm_input_write(term, b.constData(), size_t(b.size()));
    vterm_screen_flush_damage(screen);
    verticalScrollBar()->setRange(0, alternate ? 0 : int(history.size()));
    if (bottom)
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    updateCursorBlink();
    if(screenTransition) {screenTransition=false;logState(DiagEvent::TerminalScreenState);}
}
void Terminal::logState(DiagEvent event) {
    diagnoseTerminal(event,{diagnosticTag,cols,rows,cursor.row,cursor.col,int(history.size()),
                           verticalScrollBar()->value(),verticalScrollBar()->maximum(),alternate});
}
bool Terminal::cell(int row, int col, VTermScreenCell *out) const {
    if (row < 0 || col < 0 || col >= cols)
        return false;
    if (!alternate && row < int(history.size())) {
        const auto &line = history[size_t(row)];
        if (col >= line.size())
            return false;
        *out = line[col];
        return true;
    }
    int r = row - (alternate ? 0 : int(history.size()));
    return r >= 0 && r < rows && vterm_screen_get_cell(screen, {r, col}, out);
}
QColor Terminal::displayColor(VTermColor color, const QColor &defaultFg, const QColor &defaultBg) const {
    if (VTERM_COLOR_IS_DEFAULT_FG(&color)) return defaultFg;
    if (VTERM_COLOR_IS_DEFAULT_BG(&color)) return defaultBg;
    vterm_screen_convert_color_to_rgb(screen, &color);
    return QColor(color.rgb.red, color.rgb.green, color.rgb.blue);
}
QVector<Terminal::InkSpan> Terminal::rowHighlights(const QVector<VTermScreenCell> &line) const {
    QVector<InkSpan> spans;
    if (alternate)
        return spans;
    QString text;
    QVector<int> columns;
    for (int col = 0; col < line.size(); ++col) {
        const auto &sc = line[col];
        if (sc.width == 0 || sc.chars[0] == 0xffffffffU)
            continue;
        const auto start = text.size();
        if (!sc.chars[0])
            text += ' ';
        for (const auto ch : sc.chars) {
            if (!ch) break;
            text += QString::fromUcs4(reinterpret_cast<const char32_t *>(&ch), 1);
        }
        for (auto i = start; i < text.size(); ++i)
            columns += col;
    }
    auto add = [&](qsizetype first, qsizetype length, QColor color) {
        if (length <= 0) return;
        const int begin = columns[first], last = columns[first + length - 1];
        const int end = last + qMax(1, int(line[last].width));
        // Do not partially recolor a token carrying remote attributes or colors.
        for (int col = begin; col < end; ++col) {
            const auto &sc = line[col];
            const auto &a = sc.attrs;
            if (!VTERM_COLOR_IS_DEFAULT_FG(&sc.fg) || !VTERM_COLOR_IS_DEFAULT_BG(&sc.bg) ||
                a.bold || a.underline || a.italic || a.blink || a.reverse || a.conceal || a.strike ||
                a.font || a.dwl || a.dhl || a.small || a.baseline)
                return;
        }
        spans += InkSpan{begin, end, color};
    };
    static const QRegularExpression prompt(
        R"(^([A-Za-z0-9_.-]+)@([A-Za-z0-9_.-]+):([~/][^\r\n]*?)([$#])(?=\s|$))");
    static const QRegularExpression level(R"((?<![\p{L}\p{N}_])(INFO|WARN|ERROR|FATAL)(?![\p{L}\p{N}_]))");
    const auto c = colors();
    const auto match = prompt.match(text);
    if (match.hasMatch()) {
        add(match.capturedStart(1), match.capturedLength(1), c.green);
        add(match.capturedStart(2), match.capturedLength(2), c.link);
        add(match.capturedStart(3), match.capturedLength(3), c.accent);
        add(match.capturedStart(4), 1, c.keyword);
    }
    auto matches = level.globalMatch(text);
    while (matches.hasNext()) {
        const auto m = matches.next();
        // A prompt path can contain these words; prompt segments take precedence.
        if (match.hasMatch() && m.capturedStart() < match.capturedEnd()) continue;
        add(m.capturedStart(), m.capturedLength(), m.captured() == "INFO" ? c.link :
            m.captured() == "WARN" ? c.orange : c.red);
    }
    std::sort(spans.begin(), spans.end(), [](const InkSpan &a, const InkSpan &b) { return a.first < b.first; });
    return spans;
}
void Terminal::paintEvent(QPaintEvent *event) {
    QPainter p(viewport());
    auto c = colors();
    p.fillRect(viewport()->rect(), c.background);
    int start = alternate ? 0 : verticalScrollBar()->value();
    QPoint a = selectionStart, b = selectionEnd;
    if (a.y() > b.y() || (a.y() == b.y() && a.x() > b.x()))
        std::swap(a, b);
    const QRect caret = cursorRect();
    const bool drawCursor = cursorInView() && cursorOn;
    const int firstRow = qBound(0, (event->rect().top() - uiPx(4)) / cellH, rows - 1);
    const int lastRow = qBound(0, (event->rect().bottom() - uiPx(4)) / cellH, rows - 1);
    QVector<VTermScreenCell> line(cols);
    for (int row = firstRow; row <= lastRow; ++row) {
        if (!event->region().intersects(QRect(0, uiPx(4) + row * cellH, viewport()->width(), cellH))) continue;
        // At most one bounded visible row of cells/text/spans; no history highlight cache.
        std::fill(line.begin(), line.end(), VTermScreenCell{});
        for (int col = 0; col < cols; ++col)
            cell(start + row, col, &line[col]);
        const auto spans = rowHighlights(line);
        qsizetype span = 0;
        for (int col = 0; col < cols; ++col) {
            const auto &sc = line[col];
            if (sc.width == 0 || sc.chars[0] == 0xffffffffU)
                continue;
            QColor f = displayColor(sc.fg, c.text, c.background);
            QColor back = displayColor(sc.bg, c.text, c.background);
            if (sc.attrs.reverse)
                std::swap(f, back);
            bool selected = selectionActive &&
                            (start + row > a.y() || (start + row == a.y() &&
                             col + qMax(1, int(sc.width)) - 1 >= a.x())) &&
                            (start + row < b.y() || (start + row == b.y() && col <= b.x()));
            if (selected)
                back = c.selection;
            QRect r(uiPx(6) + col * cellW, uiPx(4) + row * cellH, cellW * qMax(1, int(sc.width)), cellH);
            while (span < spans.size() && spans[span].end <= col) ++span;
            const bool cursorCell = drawCursor && r == caret;
            if (!selected && !cursorCell && span < spans.size() && spans[span].first <= col)
                f = spans[span].color;
            if (cursorCell && cursorShape == VTERM_PROP_CURSORSHAPE_BLOCK) {
                std::swap(f, back);
                if (f == back) {
                    back = c.text;
                    f = c.background;
                }
            }
            p.fillRect(r, back);
            if (sc.attrs.conceal)
                continue;
            QString s;
            for (auto ch : sc.chars) {
                if (!ch)
                    break;
                if (ch != 0xffffffffU)
                    s += QString::fromUcs4(reinterpret_cast<const char32_t *>(&ch), 1);
            }
            p.setFont(cellFont(sc.attrs));
            p.setPen(f);
            p.save();
            p.setClipRect(r, Qt::IntersectClip);
            p.drawText(r.x(), r.y() + ascent, s);
            p.restore();
        }
    }
    if (drawCursor && cursorShape == VTERM_PROP_CURSORSHAPE_UNDERLINE)
        p.fillRect(QRect(caret.left(), caret.bottom() - uiPx(1), caret.width(), uiPx(2)), c.text);
    else if (drawCursor && cursorShape == VTERM_PROP_CURSORSHAPE_BAR_LEFT)
        p.fillRect(QRect(caret.left(), caret.top(), uiPx(2), caret.height()), c.text);
    if (!preparedPaste.isEmpty()) {
        p.fillRect(QRect(uiPx(4), viewport()->height() - uiPx(28), viewport()->width() - uiPx(8), uiPx(26)), c.side);
        p.setPen(c.orange);
        p.drawText(uiPx(12), viewport()->height() - uiPx(10),
                   "多行粘贴已暂存 · Enter 发送，Esc 取消（发送后可能执行命令）");
    }
}
void Terminal::resizeEvent(QResizeEvent *e) {
    QAbstractScrollArea::resizeEvent(e);
    // Tab/splitter/floating migrations briefly pass through an unlaid-out
    // parent. Resize the PTY once the final layout is known, preserving output.
    resizeTimer.start();
}
void Terminal::showEvent(QShowEvent *e) {
    QAbstractScrollArea::showEvent(e);
    resizeTimer.start();
    watchCursorWindow();
    updateCursorBlink(true);
}
void Terminal::hideEvent(QHideEvent *e) {
    QAbstractScrollArea::hideEvent(e);
    updateCursorBlink();
}
bool Terminal::event(QEvent *e) {
    if (e->type() == QEvent::KeyPress && hasFocus()) {
        auto key = static_cast<QKeyEvent *>(e);
        if ((key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) &&
            !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            keyPressEvent(key);
            return true;
        }
    }
    const bool result = QAbstractScrollArea::event(e);
    switch (e->type()) {
    case QEvent::FontChange:
    case QEvent::ApplicationFontChange:
    case QEvent::StyleChange:
    case QEvent::ScreenChangeInternal:
    case QEvent::DevicePixelRatioChange:
        resizeTimer.start();
        break;
    case QEvent::ParentChange:
        watchCursorWindow();
        [[fallthrough]];
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate:
    case QEvent::WindowStateChange:
        updateCursorBlink(true);
        break;
    default:
        break;
    }
    return result;
}
void Terminal::wheelEvent(QWheelEvent *event) {
    QAbstractScrollArea::wheelEvent(event);
}
QFont Terminal::cellFont(const VTermScreenCellAttrs &attributes) const {
    auto value = font();
    if (attributes.bold) {
        value.setWeight(QFont::Weight(qMax(int(value.weight()), 700)));
        const auto wght = QFont::Tag::fromString("wght").value();
        if (value.isVariableAxisSet(wght)) value.setVariableAxis(wght, float(value.weight()));
    }
    value.setItalic(attributes.italic);
    value.setUnderline(attributes.underline);
    value.setStrikeOut(attributes.strike);
    return value;
}
bool Terminal::eventFilter(QObject *object, QEvent *e) {
    if (object == cursorWindow || object == cursorHandle || object == viewport()) {
        switch (e->type()) {
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::Expose:
        case QEvent::WindowActivate:
        case QEvent::WindowDeactivate:
        case QEvent::WindowStateChange:
            updateCursorBlink(true);
            break;
        default:
            break;
        }
    }
    return QAbstractScrollArea::eventFilter(object, e);
}
void Terminal::watchCursorWindow() {
    if (cursorWindow && cursorWindow != this)
        cursorWindow->removeEventFilter(this);
    if (cursorHandle)
        cursorHandle->removeEventFilter(this);
    cursorWindow = window();
    cursorHandle = cursorWindow->windowHandle();
    if (cursorWindow != this)
        cursorWindow->installEventFilter(this);
    if (cursorHandle)
        cursorHandle->installEventFilter(this);
}
QRect Terminal::cursorRect() const {
    int col = cursor.col;
    VTermScreenCell sc{};
    // A wide glyph's continuation cell belongs to its leading cell.
    while (col > 0 && vterm_screen_get_cell(screen, {cursor.row, col}, &sc) &&
           (sc.width == 0 || sc.chars[0] == 0xffffffffU))
        --col;
    vterm_screen_get_cell(screen, {cursor.row, col}, &sc);
    return QRect(uiPx(6) + col * cellW, uiPx(4) + cursor.row * cellH, cellW * qMax(1, int(sc.width)), cellH);
}
bool Terminal::cursorInView() const {
    const auto w = window();
    const auto handle = w->windowHandle();
    return cursorVisible && isVisible() && viewport()->isVisible() && hasFocus() && w->isActiveWindow() &&
           !w->isMinimized() && (!handle || handle->isExposed()) &&
           verticalScrollBar()->value() == verticalScrollBar()->maximum() &&
           viewport()->rect().intersects(cursorRect());
}
void Terminal::updateCursorBlink(bool restart) {
    const int flash = QGuiApplication::styleHints()->cursorFlashTime();
    const bool blink = cursorBlink && flash > 0 && cursorInView();
    if (restart || !blink)
        cursorOn = true;
    if (!blink)
        cursorTimer.stop();
    else if (restart || !cursorTimer.isActive() || cursorTimer.interval() != qMax(1, flash / 2))
        cursorTimer.start(qMax(1, flash / 2));
    updateCursorRect(cursorRect());
}
void Terminal::updateCursorRect(const QRect &rect) {
    // Incoming text and cursor callbacks share the existing 16 ms output batch.
    // A full batch repaint also erases the previous cursor position.
    if (feeding) {
        if (!repaintTimer.isActive())
            repaintTimer.start();
    } else {
        viewport()->update(rect);
    }
}
void Terminal::applySize() {
    if (!isVisible()) return;
    DiagnosticScope resizeScope(DiagEvent::TerminalResizeBegin,DiagEvent::TerminalResizeEnd,0,0,{},cols);
    QFontMetricsF fm(font(), viewport());
    cellW = qMax(uiPx(6), qCeil(fm.horizontalAdvance('M')));
    cellH = qCeil(fm.height()) + uiPx(2);
    ascent = qCeil(fm.ascent());
    int nextCols = qBound(10, (viewport()->width() - uiPx(12)) / cellW, 500);
    int nextRows = qBound(2, (viewport()->height() - uiPx(8)) / cellH, 200);
    viewport()->update();
    QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
    if (nextCols==cols && nextRows==rows) return;
    const auto first=selectionActive&&!alternate?glyphOrdinal(selectionStart):std::optional<qint64>{};
    const auto last=selectionActive&&!alternate?glyphOrdinal(selectionEnd):std::optional<qint64>{};
    const auto dropped=discardedGlyphs;
    const bool bottom=verticalScrollBar()->value()==verticalScrollBar()->maximum();
    const int scroll=verticalScrollBar()->value();
    const auto scrollAnchor=!bottom&&!alternate?glyphOrdinal({0,scroll}):std::optional<qint64>{};
    resizePrimary(nextCols,nextRows);
    verticalScrollBar()->setRange(0,alternate?0:int(history.size()));
    verticalScrollBar()->setPageStep(rows);
    if(bottom)verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    else if(scrollAnchor){const auto anchor=glyphPosition(*scrollAnchor-(discardedGlyphs-dropped));if(anchor.y()>=0)verticalScrollBar()->setValue(anchor.y());}
    if(selectionActive && !alternate) {
        const auto removed=discardedGlyphs-dropped;
        const QPoint a=first?glyphPosition(*first-removed):QPoint(-1,-1);
        const QPoint b=last?glyphPosition(*last-removed):QPoint(-1,-1);
        if(a.x()<0 || b.x()<0)clearSelection();
        else {selectionStart=a;selectionEnd=b;selecting=false;}
    }
    emit sizeChanged(cols, rows);
    vterm_screen_flush_damage(screen);
    updateCursorBlink(true);
    QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
    logState(DiagEvent::TerminalGrid);
}
void Terminal::sendText(const QString &s, Qt::KeyboardModifiers mods) {
    int m = 0;
    if (mods & Qt::ShiftModifier)
        m |= VTERM_MOD_SHIFT;
    if (mods & Qt::AltModifier)
        m |= VTERM_MOD_ALT;
    if (mods & physicalControlModifier)
        m |= VTERM_MOD_CTRL;
    for (auto ch : s.toUcs4())
        vterm_keyboard_unichar(term, ch, VTermModifier(m));
}
void Terminal::keyPressEvent(QKeyEvent *e) {
#ifdef Q_OS_MACOS
    // Qt maps the physical Control key to Meta on macOS; Command remains standard Copy.
    constexpr auto terminalControl = Qt::MetaModifier;
#else
    constexpr auto terminalControl = Qt::ControlModifier;
#endif
    // Physical Ctrl+C starts with a separate Control press. It must preserve selection.
    if (e->key() == Qt::Key_Control || e->key() == Qt::Key_Shift || e->key() == Qt::Key_Alt ||
        e->key() == Qt::Key_Meta || e->key() == Qt::Key_AltGr) {
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_C && e->modifiers() == terminalControl) {
        if (!e->isAutoRepeat()) {
            if (selectionActive) {
                copy();
                clearSelection();
            } else {
                verticalScrollBar()->setValue(verticalScrollBar()->maximum());
                updateCursorBlink(true);
                emit input(QByteArray(1, 3));
            }
        }
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_C && e->modifiers() == (terminalControl | Qt::ShiftModifier)) {
        copy();
        e->accept();
        return;
    }
    if (e->matches(QKeySequence::Copy)) {
        copy();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_V && (e->modifiers() & physicalControlModifier)) {
        paste();
        return;
    }
    if (e->key() == Qt::Key_F && (e->modifiers() & physicalControlModifier)) {
        bool ok;
        auto s = QInputDialog::getText(this, "查找终端输出", "查找", QLineEdit::Normal, {}, &ok);
        if (ok && !s.isEmpty()) {
            findSelection(s);
        }
        return;
    }
    if (!preparedPaste.isEmpty()) {
        if (e->key() == Qt::Key_Escape) {
            preparedPaste.clear();
            viewport()->update();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            updateCursorBlink(true);
            vterm_keyboard_start_paste(term);
            sendText(QString::fromUtf8(preparedPaste));
            vterm_keyboard_end_paste(term);
            preparedPaste.clear();
            viewport()->update();
            return;
        }
    }
    clearSelection();
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    updateCursorBlink(true);
    const QHash<int, VTermKey> keys = {
        {Qt::Key_Return, VTERM_KEY_ENTER},     {Qt::Key_Enter, VTERM_KEY_ENTER},
        {Qt::Key_Tab, VTERM_KEY_TAB},          {Qt::Key_Backtab, VTERM_KEY_TAB},
        {Qt::Key_Backspace, VTERM_KEY_BACKSPACE},
        {Qt::Key_Escape, VTERM_KEY_ESCAPE},    {Qt::Key_Up, VTERM_KEY_UP},
        {Qt::Key_Down, VTERM_KEY_DOWN},        {Qt::Key_Left, VTERM_KEY_LEFT},
        {Qt::Key_Right, VTERM_KEY_RIGHT},      {Qt::Key_Home, VTERM_KEY_HOME},
        {Qt::Key_End, VTERM_KEY_END},          {Qt::Key_Delete, VTERM_KEY_DEL},
        {Qt::Key_Insert, VTERM_KEY_INS},       {Qt::Key_PageUp, VTERM_KEY_PAGEUP},
        {Qt::Key_PageDown, VTERM_KEY_PAGEDOWN}};
    int m = 0;
    if ((e->modifiers() & Qt::ShiftModifier) || e->key() == Qt::Key_Backtab)
        m |= VTERM_MOD_SHIFT;
    if (e->modifiers() & Qt::AltModifier)
        m |= VTERM_MOD_ALT;
    if (e->modifiers() & physicalControlModifier)
        m |= VTERM_MOD_CTRL;
    if (keys.contains(e->key()))
        vterm_keyboard_key(term, keys[e->key()], VTermModifier(m));
    else if (e->key() >= Qt::Key_F1 && e->key() <= Qt::Key_F12)
        vterm_keyboard_key(term, VTermKey(VTERM_KEY_FUNCTION(e->key() - Qt::Key_F1 + 1)), VTermModifier(m));
    else if (e->modifiers() & physicalControlModifier && e->key() >= Qt::Key_A && e->key() <= Qt::Key_Z)
        sendText(QString(QChar('a' + e->key() - Qt::Key_A)), e->modifiers());
    else
        sendText(e->text(), e->modifiers());
    e->accept();
}
void Terminal::inputMethodEvent(QInputMethodEvent *e) {
    clearSelection();
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    updateCursorBlink(true);
    sendText(e->commitString());
    e->accept();
}
QVariant Terminal::inputMethodQuery(Qt::InputMethodQuery q) const {
    if (q == Qt::ImCursorRectangle)
        return cursorRect();
    if (q == Qt::ImEnabled)
        return true;
    return {};
}
QPoint Terminal::point(QPoint p) const {
    return {qBound(0, (p.x() - uiPx(6)) / cellW, cols - 1),
            qBound(0, (p.y() - uiPx(4)) / cellH, rows - 1) + (alternate ? 0 : verticalScrollBar()->value())};
}
void Terminal::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        setFocus();
        selectionStart = selectionEnd = point(e->position().toPoint());
        selectionAnchor = e->position().toPoint();
        selectionActive = false;
        selecting = true;
        viewport()->update();
    }
}
void Terminal::mouseMoveEvent(QMouseEvent *e) {
    if (selecting) {
        selectionEnd = point(e->position().toPoint());
        selectionActive = selectionActive || selectionEnd != selectionStart ||
                          (e->position().toPoint() - selectionAnchor).manhattanLength() >=
                              QApplication::startDragDistance();
        viewport()->update();
    }
}
void Terminal::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        if (selecting)
            mouseMoveEvent(e);
        selecting = false;
    }
}
QString Terminal::selection() const {
    QPoint a = selectionStart, b = selectionEnd;
    if (!selectionActive)
        return {};
    if (a.y() > b.y() || (a.y() == b.y() && a.x() > b.x()))
        std::swap(a, b);
    QString text;
    for (int row = a.y(); row <= b.y(); ++row) {
        QString line;
        int first = row == a.y() ? a.x() : 0;
        VTermScreenCell lead{};
        while (first > 0 && cell(row, first, &lead) &&
               (lead.width == 0 || lead.chars[0] == 0xffffffffU))
            --first;
        const int end=qMin(row==b.y()?b.x():cols-1,usedColumns(row)-1);
        for (int col = first; col <= end; ++col) {
            VTermScreenCell sc{};
            if (cell(row, col, &sc)) {
                if (sc.width == 0)
                    continue;
                if (!sc.chars[0])
                    line += ' ';
                for (auto ch : sc.chars) {
                    if (!ch)
                        break;
                    if (ch != 0xffffffffU)
                        line += QString::fromUcs4(reinterpret_cast<const char32_t *>(&ch), 1);
                }
            }
        }
        text += line;
        if (row != b.y()) {
            if(!continuesPrevious(row+1))text += '\n';
        }
    }
    return text;
}
void Terminal::clearSelection() {
    const bool repaint = selectionActive;
    selectionActive = selecting = false;
    selectionStart = selectionEnd = {-1, -1};
    if (repaint)
        viewport()->update();
}
bool Terminal::findSelection(const QString &needle) {
    if (needle.isEmpty())
        return false;
    for (int row = 0; row < (alternate ? 0 : int(history.size())) + rows; ++row) {
        QString line;
        QVector<int> columns;
        for (int col = 0; col < cols; ++col) {
            VTermScreenCell sc{};
            if (!cell(row, col, &sc) || sc.width == 0 || sc.chars[0] == 0xffffffffU)
                continue;
            QString glyph;
            for (auto ch : sc.chars) {
                if (!ch)
                    break;
                glyph += QString::fromUcs4(reinterpret_cast<const char32_t *>(&ch), 1);
            }
            if (glyph.isEmpty())
                glyph = ' ';
            line += glyph;
            for (qsizetype i = 0; i < glyph.size(); ++i)
                columns += col;
        }
        const qsizetype index = line.indexOf(needle, 0, Qt::CaseInsensitive);
        if (index >= 0) {
            clearSelection();
            selectionStart = {columns[index], row};
            selectionEnd = {columns[index + needle.size() - 1], row};
            selectionActive = true;
            verticalScrollBar()->setValue(qMin(row, verticalScrollBar()->maximum()));
            viewport()->update();
            return true;
        }
    }
    return false;
}
QString Terminal::text() const {
    QString out;
    for (int r = 0; r < int(history.size()) + rows; ++r) {
        QString line;
        for (int c = 0; c < cols; ++c) {
            VTermScreenCell sc{};
            if (cell(r, c, &sc)) {
                if (!sc.width)
                    continue;
                if (!sc.chars[0])
                    line += ' ';
                for (auto ch : sc.chars) {
                    if (!ch)
                        break;
                    if (ch != 0xffffffffU)
                        line += QString::fromUcs4(reinterpret_cast<const char32_t *>(&ch), 1);
                }
            }
        }
        out += line.trimmed() + '\n';
    }
    return out;
}
void Terminal::copy() {
    if (selectionActive)
        QApplication::clipboard()->setText(selection());
}
void Terminal::paste() {
    auto s = QApplication::clipboard()->text();
    s.remove(QChar(0x1b));
    s.replace("\r\n", "\n");
    s.replace('\r', '\n');
    while (s.endsWith('\n'))
        s.chop(1);
    if (s.toUtf8().size() > 128 * 1024) {
        AppMessageBox::warning(this, "粘贴", "内容超过 128 KiB，请通过文件传输处理。");
        return;
    }
    if (s.contains('\n')) {
        QDialog d(this);
        d.setWindowTitle("多行粘贴预览");
        auto l = new QVBoxLayout(&d);
        auto p = new QPlainTextEdit(s);
        p->setReadOnly(true);
        l->addWidget(p);
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        l->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        d.resize(560, 320);
        if (d.exec() == QDialog::Accepted)
            preparedPaste = s.toUtf8();
        viewport()->update();
    } else {
        updateCursorBlink(true);
        vterm_keyboard_start_paste(term);
        sendText(s);
        vterm_keyboard_end_paste(term);
    }
}
void Terminal::contextMenuEvent(QContextMenuEvent *e) {
    QMenu m(this);
    m.addAction("复制所选", this, &Terminal::copy);
    m.addAction("复制全部输出", this, [this] { QApplication::clipboard()->setText(text()); });
    m.addAction("粘贴…", this, &Terminal::paste);
    m.addSeparator();
    auto clear=m.addAction("清屏", this, &Terminal::clearOutput);
    const int availability=rabbitshell_vterm_screen_can_clear_output(screen);
    clear->setEnabled(availability==1);
    if(availability==1)clear->setToolTip("清除历史和其他输出，保留当前软换行及光标原位；上方留空白。");
    else if(availability<0)clear->setToolTip("全屏应用中不可清屏，以保留当前画面和主屏。");
    else if(!availability)clear->setToolTip("当前软换行延伸到历史，无法安全清屏；输入内容保持不变。");
    m.setToolTipsVisible(true);
    m.addSeparator();
    m.addAction("中断命令（直接发送；无选区时 Ctrl+C）", this, [this] { emit input(QByteArray(1, 3)); });
    if (!actions().isEmpty()) {
        m.addSeparator();
        m.addActions(actions());
    }
    m.exec(e->globalPos());
}
void Terminal::clearOutput() {
    // Recheck on activation: remote output may switch buffers while a menu is open.
    if(rabbitshell_vterm_screen_clear_output(screen)!=1)return;
    clearSelection();history.clear();historySize=0;discardedGlyphs=0;
    verticalScrollBar()->setRange(0,0);
    vterm_screen_flush_damage(screen);updateCursorBlink(true);viewport()->update();
    QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
    logState(DiagEvent::TerminalClear);
}
void Terminal::focusInEvent(QFocusEvent *e) {
    QAbstractScrollArea::focusInEvent(e);
    emit focused();
    updateCursorBlink(true);
}
void Terminal::focusOutEvent(QFocusEvent *e) {
    QAbstractScrollArea::focusOutEvent(e);
    updateCursorBlink();
}
} // namespace rs
