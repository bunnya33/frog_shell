#include "editorsurface.h"
#include "contentfont.h"
#include "theme.h"
#include "layoutmetrics.h"
#include <QAccessibleWidget>
#include <algorithm>

namespace rs {
namespace {
int rgb(const QColor &color) { return color.red() | (color.green() << 8) | (color.blue() << 16); }
int argb(const QColor &color) { return rgb(color) | int(0xff000000u); }
class AccessibleEditor final : public QAccessibleWidget, public QAccessibleTextInterface, public QAccessibleEditableTextInterface {
    EditorSurface *edit() const { return static_cast<EditorSurface *>(object()); }
  public:
    explicit AccessibleEditor(EditorSurface *view) : QAccessibleWidget(view, QAccessible::EditableText) {}
    void *interface_cast(QAccessible::InterfaceType type) override {
        if (type == QAccessible::TextInterface) return static_cast<QAccessibleTextInterface *>(this);
        if (type == QAccessible::EditableTextInterface) return static_cast<QAccessibleEditableTextInterface *>(this);
        return QAccessibleWidget::interface_cast(type);
    }
    QAccessible::State state() const override {
        auto result = QAccessibleWidget::state(); result.multiLine = true; result.selectableText = true;
        result.readOnly = edit()->isReadOnly(); result.editable = !result.readOnly; return result;
    }
    QString text(QAccessible::Text type) const override { return type == QAccessible::Value ? edit()->toPlainText() : QAccessibleWidget::text(type); }
    void selection(int index, int *start, int *end) const override {
        const auto cursor = edit()->textCursor();
        *start = index == 0 && cursor.hasSelection() ? cursor.selectionStart() : -1;
        *end = *start >= 0 ? cursor.selectionEnd() : -1;
    }
    int selectionCount() const override { return edit()->send(SCI_GETSELECTIONEMPTY) ? 0 : 1; }
    void addSelection(int start, int end) override { setSelection(0, start, end); }
    void removeSelection(int) override { auto cursor = edit()->textCursor(); cursor.clearSelection(); edit()->setTextCursor(cursor); }
    void setSelection(int index, int start, int end) override {
        if (index) return; EditorCursor cursor(edit()->document()); cursor.setPosition(start); cursor.setPosition(end, QTextCursor::KeepAnchor); edit()->setTextCursor(cursor);
    }
    int cursorPosition() const override { return edit()->toUtf16(int(edit()->send(SCI_GETCURRENTPOS))); }
    void setCursorPosition(int position) override { EditorCursor cursor(edit()->document()); cursor.setPosition(position); edit()->setTextCursor(cursor); }
    QString text(int start, int end) const override {
        if (end < 0) end = characterCount();
        return QString::fromUtf8(edit()->byteRange(edit()->fromUtf16(start), edit()->fromUtf16(end)));
    }
    int characterCount() const override { return edit()->document()->characterCount() - 1; }
    QRect characterRect(int offset) const override {
        EditorCursor cursor(edit()->document()); cursor.setPosition(offset); auto rect = edit()->cursorRect(cursor);
        rect.moveTopLeft(edit()->viewport()->mapToGlobal(rect.topLeft())); return rect;
    }
    int offsetAtPoint(const QPoint &point) const override { return edit()->cursorForPosition(edit()->viewport()->mapFromGlobal(point)).position(); }
    void scrollToSubstring(int start, int end) override { setSelection(0, start, end); edit()->ensureCursorVisible(); }
    QString attributes(int offset, int *start, int *end) const override {
        *start = qBound(0, offset, characterCount()); *end = qMin(characterCount(), *start + 1);
        return QString("font-family:%1;font-size:%2;font-weight:%3;").arg(edit()->font().family()).arg(edit()->font().pixelSize()).arg(int(edit()->font().weight()));
    }
    void deleteText(int start, int end) override { replaceText(start, end, {}); }
    void insertText(int offset, const QString &text) override { replaceText(offset, offset, text); }
    void replaceText(int start, int end, const QString &text) override {
        if (edit()->isReadOnly()) return;
        EditorCursor cursor(edit()->document()); cursor.setPosition(start); cursor.setPosition(end, QTextCursor::KeepAnchor); cursor.insertText(text);
    }
};
}

EditorDocument::EditorDocument(EditorSurface *surface) : QObject(surface), view(surface) {}
std::shared_ptr<EditorCursorState> EditorDocument::track(int anchor, int position) {
    auto value = std::make_shared<EditorCursorState>(); value->anchor = anchor; value->position = position;
    // Expired cursor handles contain no text. Bound even repeated read-only
    // cursor queries without waiting for a document edit to reclaim them.
    if (cursors.size() > 256 && cursors.size() % 128 == 0)
        cursors.erase(std::remove_if(cursors.begin(), cursors.end(), [](const auto &cursor) { return cursor.expired(); }), cursors.end());
    cursors.append(value); return value;
}
void EditorDocument::reset() {
    for (auto &weak : cursors) if (auto cursor = weak.lock()) cursor->valid = false;
    cursors.clear(); ++serial; utf16Length = 0; forcedModified = false;
}
void EditorDocument::changed(int position, int removed, int added, const QByteArray &text) {
    ++serial;
    const int units = QString::fromUtf8(text).size(); utf16Length += added ? units : -units;
    auto adjust = [=](int value) {
        if (removed) return value >= position + removed ? value - removed : value > position ? position : value;
        return value >= position ? value + added : value;
    };
    auto out = cursors.begin();
    for (auto it = cursors.begin(); it != cursors.end(); ++it)
        if (auto cursor = it->lock()) { cursor->anchor = adjust(cursor->anchor); cursor->position = adjust(cursor->position); *out++ = *it; }
    cursors.erase(out, cursors.end());
    emit bytesChanged(position, removed, added); emit contentsChanged();
}
bool EditorDocument::isModified() const { return forcedModified || view->send(SCI_GETMODIFY); }
void EditorDocument::setModified(bool value) {
    forcedModified = value; if (!value) view->send(SCI_SETSAVEPOINT); emit modificationChanged(isModified());
}
bool EditorDocument::isUndoAvailable() const { return view->send(SCI_CANUNDO); }
bool EditorDocument::isRedoAvailable() const { return view->send(SCI_CANREDO); }
int EditorDocument::availableUndoSteps() const { return int(view->send(SCI_GETUNDOACTIONS)); }
EditorBlock EditorDocument::firstBlock() const { return {const_cast<EditorDocument *>(this), 0}; }
EditorBlock EditorDocument::lastBlock() const { return {const_cast<EditorDocument *>(this), view->blockCount() - 1}; }
EditorBlock EditorDocument::findBlock(int position) const { return findBlockByNumber(int(view->send(SCI_LINEFROMPOSITION, view->fromUtf16(position)))); }
EditorBlock EditorDocument::findBlockByNumber(int line) const { return {const_cast<EditorDocument *>(this), line}; }

EditorBlock::EditorBlock(EditorDocument *document, int number) : owner(document), line(number) {}
bool EditorBlock::isValid() const { return owner && line >= 0 && line < owner->surface()->blockCount(); }
EditorDocument *EditorBlock::document() const { return owner; }
int EditorBlock::position() const { return isValid() ? int(owner->surface()->send(SCI_INDEXPOSITIONFROMLINE, line, SC_LINECHARACTERINDEX_UTF16)) : -1; }
int EditorBlock::length() const { return isValid() ? text().size() + 1 : 0; }
QString EditorBlock::text() const {
    if (!isValid()) return {};
    auto view = owner->surface(); const int start = int(view->send(SCI_POSITIONFROMLINE, line));
    return QString::fromUtf8(view->byteRange(start, int(view->send(SCI_GETLINEENDPOSITION, line))));
}
EditorBlock EditorBlock::next() const { return {owner, line + 1}; }
EditorBlock EditorBlock::previous() const { return {owner, line - 1}; }

EditorCursor::EditorCursor(EditorDocument *document) : owner(document) { if (owner) state = owner->track(); }
EditorCursor::EditorCursor(const EditorBlock &block) : EditorCursor(block.document()) { if (block.isValid()) setPosition(block.position()); }
bool EditorCursor::isNull() const { return !owner || !state || !state->valid; }
EditorDocument *EditorCursor::document() const { return owner; }
void EditorCursor::detach() { if (!isNull() && state.use_count() > 1) state = owner->track(state->anchor, state->position); }
int EditorCursor::bytePosition() const { return isNull() ? 0 : state->position; }
int EditorCursor::byteAnchor() const { return isNull() ? 0 : state->anchor; }
int EditorCursor::position() const { return isNull() ? 0 : owner->surface()->toUtf16(state->position); }
int EditorCursor::anchor() const { return isNull() ? 0 : owner->surface()->toUtf16(state->anchor); }
int EditorCursor::selectionStart() const { return qMin(position(), anchor()); }
int EditorCursor::selectionEnd() const { return qMax(position(), anchor()); }
bool EditorCursor::hasSelection() const { return !isNull() && state->anchor != state->position; }
QString EditorCursor::selectedText() const {
    if (isNull()) return {};
    auto text = QString::fromUtf8(owner->surface()->byteRange(qMin(state->anchor, state->position), qMax(state->anchor, state->position)));
    return text.replace('\n', QChar::ParagraphSeparator);
}
int EditorCursor::blockNumber() const { return isNull() ? -1 : int(owner->surface()->send(SCI_LINEFROMPOSITION, state->position)); }
EditorBlock EditorCursor::block() const { return {owner, blockNumber()}; }
void EditorCursor::setPosition(int value, QTextCursor::MoveMode mode) {
    if (isNull()) return; detach(); state->position = owner->surface()->fromUtf16(value);
    if (mode == QTextCursor::MoveAnchor) state->anchor = state->position;
}
void EditorCursor::clearSelection() { if (!isNull()) { detach(); state->anchor = state->position; } }
void EditorCursor::select(QTextCursor::SelectionType selection) {
    if (isNull()) return;
    if (selection == QTextCursor::Document) { movePosition(QTextCursor::Start); movePosition(QTextCursor::End, QTextCursor::KeepAnchor); }
    else if (selection == QTextCursor::BlockUnderCursor || selection == QTextCursor::LineUnderCursor) {
        movePosition(QTextCursor::StartOfBlock); movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    } else if (selection == QTextCursor::WordUnderCursor) {
        detach(); auto view = owner->surface();
        state->anchor = int(view->send(SCI_WORDSTARTPOSITION, state->position, true));
        state->position = int(view->send(SCI_WORDENDPOSITION, state->position, true));
    }
}
bool EditorCursor::movePosition(QTextCursor::MoveOperation operation, QTextCursor::MoveMode mode, int count) {
    if (isNull()) return false; detach(); auto view = owner->surface(); const int before = state->position;
    for (int i = 0; i < count; ++i) {
        const int pos = state->position, line = int(view->send(SCI_LINEFROMPOSITION, pos));
        switch (operation) {
        case QTextCursor::Start: state->position = 0; break;
        case QTextCursor::End: state->position = view->byteLength(); break;
        case QTextCursor::StartOfBlock: case QTextCursor::StartOfLine: state->position = int(view->send(SCI_POSITIONFROMLINE, line)); break;
        case QTextCursor::EndOfBlock: case QTextCursor::EndOfLine: state->position = int(view->send(SCI_GETLINEENDPOSITION, line)); break;
        case QTextCursor::NextBlock: state->position = line + 1 < view->blockCount() ? int(view->send(SCI_POSITIONFROMLINE, line + 1)) : view->byteLength(); break;
        case QTextCursor::PreviousBlock: state->position = int(view->send(SCI_POSITIONFROMLINE, qMax(0, line - 1))); break;
        case QTextCursor::Left: case QTextCursor::PreviousCharacter: state->position = int(view->send(SCI_POSITIONBEFORE, pos)); break;
        case QTextCursor::Right: case QTextCursor::NextCharacter: state->position = int(view->send(SCI_POSITIONAFTER, pos)); break;
        case QTextCursor::Up: case QTextCursor::Down: {
            const int next = qBound(0, line + (operation == QTextCursor::Up ? -1 : 1), view->blockCount() - 1);
            state->position = int(view->send(SCI_FINDCOLUMN, next, view->send(SCI_GETCOLUMN, pos))); break;
        }
        default: return false;
        }
    }
    if (mode == QTextCursor::MoveAnchor) state->anchor = state->position;
    return before != state->position;
}
void EditorCursor::insertText(const QString &value) {
    if (isNull() || owner->surface()->isReadOnly()) return;
    detach(); auto view = owner->surface(); const int start = qMin(state->anchor, state->position);
    auto text = value; text.replace("\r\n", "\n").replace('\r', '\n'); const auto bytes = text.toUtf8();
    view->send(SCI_SETTARGETSTART, start); view->send(SCI_SETTARGETEND, qMax(state->anchor, state->position));
    view->sends(SCI_REPLACETARGET, bytes.size(), bytes.constData()); state->anchor = state->position = start + bytes.size();
}
void EditorCursor::beginEditBlock() { if (!isNull()) owner->surface()->send(SCI_BEGINUNDOACTION); }
void EditorCursor::endEditBlock() { if (!isNull()) owner->surface()->send(SCI_ENDUNDOACTION); }
bool EditorCursor::operator==(const EditorCursor &other) const {
    return owner == other.owner && isNull() == other.isNull() && byteAnchor() == other.byteAnchor() && bytePosition() == other.bytePosition();
}

EditorSurface::EditorSurface(QWidget *parent) : ScintillaEditBase(parent), model(nullptr) {
    model = new EditorDocument(this);
    static const bool factoryInstalled = [] {
        QAccessible::installFactory([](const QString &, QObject *object) -> QAccessibleInterface * {
            if (auto edit = qobject_cast<EditorSurface *>(object)) return new AccessibleEditor(edit); return nullptr;
        }); return true;
    }(); Q_UNUSED(factoryInstalled);
    setAccessibleName("文件编辑器"); setFrameShape(QFrame::NoFrame);
    send(SCI_SETCODEPAGE, SC_CP_UTF8); send(SCI_ALLOCATELINECHARACTERINDEX, SC_LINECHARACTERINDEX_UTF16);
    send(SCI_SETEOLMODE, SC_EOL_LF); send(SCI_SETWRAPMODE, SC_WRAP_NONE); send(SCI_SETUSETABS, true); send(SCI_SETTABWIDTH, 4);
    send(SCI_SETLAYOUTCACHE, SC_CACHE_PAGE); send(SCI_SETSCROLLWIDTHTRACKING, true); send(SCI_SETSCROLLWIDTH, 1);
    // Keep the width-cache byte budget near upstream's 1024 x 29-byte
    // entries, but include its ~100-byte layout segments for mixed CJK text.
    send(SCI_SETPOSITIONCACHE, 256);
    send(SCI_SETMARGINWIDTHN, 1, 0); send(SCI_SETMARGINWIDTHN, 2, 0); send(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    send(SCI_SETCARETLINEVISIBLE, false);
    send(SCI_SETCARETPERIOD, QApplication::cursorFlashTime() / 2);
    connect(this, &ScintillaEditBase::modified, this, [this](Scintilla::ModificationFlags flags, Scintilla::Position pos, Scintilla::Position length, auto, const QByteArray &text, auto, auto, auto) {
        const auto type = int(flags); if (!(type & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) return;
        model->changed(int(pos), type & SC_MOD_DELETETEXT ? int(length) : 0, type & SC_MOD_INSERTTEXT ? int(length) : 0, text);
        if (!loading) {
            emit textChanged(); emit blockCountChanged(blockCount());
            if (QAccessible::isActive()) {
                const auto changed = QString::fromUtf8(text);
                QAccessibleTextUpdateEvent event(this, toUtf16(int(pos)), type & SC_MOD_DELETETEXT ? changed : QString(), type & SC_MOD_INSERTTEXT ? changed : QString());
                QAccessible::updateAccessibility(&event);
            }
        }
    });
    connect(this, &ScintillaEditBase::savePointChanged, model, [this](bool) { emit model->modificationChanged(model->isModified()); });
    connect(this, &ScintillaEditBase::updateUi, this, [this](Scintilla::Update flags) {
        if (!(int(flags) & SC_UPDATE_SELECTION)) return;
        syncSelection();
    });
    connect(this, &EditorSurface::blockCountChanged, this, &EditorSurface::refreshMargin);
    refreshSurfaceTheme(); refreshFont();
}
void EditorSurface::syncSelection() {
    const int caret = int(send(SCI_GETCURRENTPOS)), anchor = int(send(SCI_GETANCHOR));
    const bool moved = caret != lastCaret, changed = moved || anchor != lastAnchor;
    lastCaret = caret; lastAnchor = anchor;
    if (moved) emit cursorPositionChanged(); if (changed) emit selectionChanged();
    // Live text/selection queries stay available during drag. Publish the final
    // gesture once, rather than entering external UIA code for every mouse move.
    if (changed && !draggingSelection) notifySelectionAccessibility();
}
QString EditorSurface::toPlainText() const { return QString::fromUtf8(byteRange(0, byteLength())); }
void EditorSurface::setPlainText(const QString &value) {
    auto text = value; text.replace("\r\n", "\n").replace('\r', '\n'); const auto bytes = text.toUtf8();
    const bool readOnly = isReadOnly(); loading = true; send(SCI_SETREADONLY, false);
    // Delete notifications still update the old anchors; invalidate/reset only
    // after deletion so the UTF-16 length cannot become negative.
    send(SCI_CLEARALL); model->reset(); send(SCI_CLEARALL);
    sends(SCI_ADDTEXT, bytes.size(), bytes.constData()); send(SCI_EMPTYUNDOBUFFER); model->setModified(false);
    send(SCI_SETSEL, 0, 0); send(SCI_SETREADONLY, readOnly); loading = false;
    syncSelection(); emit textChanged(); emit blockCountChanged(blockCount());
}
void EditorSurface::insertPlainText(const QString &text) { auto cursor = textCursor(); cursor.insertText(text); setTextCursor(cursor); }
EditorCursor EditorSurface::textCursor() const {
    EditorCursor cursor; cursor.owner = model; cursor.state = model->track(int(send(SCI_GETANCHOR)), int(send(SCI_GETCURRENTPOS))); return cursor;
}
void EditorSurface::setTextCursor(const EditorCursor &cursor) {
    if (!cursor.isNull() && cursor.document() == model) { send(SCI_SETSEL, cursor.byteAnchor(), cursor.bytePosition()); syncSelection(); }
}
void EditorSurface::moveCursor(QTextCursor::MoveOperation operation, QTextCursor::MoveMode mode) { auto cursor = textCursor(); cursor.movePosition(operation, mode); setTextCursor(cursor); ensureCursorVisible(); }
EditorCursor EditorSurface::cursorForPosition(QPoint point) const {
    EditorCursor cursor(model); const int pos = int(send(SCI_CHARPOSITIONFROMPOINT, point.x(), point.y()));
    cursor.state->anchor = cursor.state->position = qMax(0, pos); return cursor;
}
QRect EditorSurface::cursorRect() const { return cursorRect(textCursor()); }
QRect EditorSurface::cursorRect(const EditorCursor &cursor) const {
    const int pos = cursor.bytePosition(); return {int(send(SCI_POINTXFROMPOSITION, 0, pos)), int(send(SCI_POINTYFROMPOSITION, 0, pos)), 1, lineHeight()};
}
int EditorSurface::blockCount() const { return int(send(SCI_GETLINECOUNT)); }
EditorBlock EditorSurface::firstVisibleBlock() const { return {model, int(send(SCI_DOCLINEFROMVISIBLE, send(SCI_GETFIRSTVISIBLELINE)))}; }
QRectF EditorSurface::blockBoundingRect(const EditorBlock &block) const { return {0, 0, qreal(viewport()->width()), qreal(lineHeight() * send(SCI_WRAPCOUNT, block.blockNumber()))}; }
QRectF EditorSurface::blockBoundingGeometry(const EditorBlock &block) const {
    if (!block.isValid()) return {}; auto rect = blockBoundingRect(block); rect.moveTop(int(send(SCI_POINTYFROMPOSITION, 0, send(SCI_POSITIONFROMLINE, block.blockNumber())))); return rect;
}
void EditorSurface::centerCursor() {
    send(SCI_SCROLLCARET);
    const auto display = send(SCI_GETFIRSTVISIBLELINE) + send(SCI_POINTYFROMPOSITION, 0, send(SCI_GETCURRENTPOS)) / qMax(1, lineHeight());
    send(SCI_SETFIRSTVISIBLELINE, qMax(0, int(display) - qMax(1, int(send(SCI_LINESONSCREEN))) / 2));
}
void EditorSurface::ensureCursorVisible() { send(SCI_SCROLLCARET); }
void EditorSurface::setReadOnly(bool readOnly) { send(SCI_SETREADONLY, readOnly); }
bool EditorSurface::isReadOnly() const { return send(SCI_GETREADONLY); }
void EditorSurface::setLineWrapMode(LineWrapMode mode) { send(SCI_SETWRAPMODE, mode == NoWrap ? SC_WRAP_NONE : SC_WRAP_WORD); }
EditorSurface::LineWrapMode EditorSurface::lineWrapMode() const { return send(SCI_GETWRAPMODE) == SC_WRAP_NONE ? NoWrap : WidgetWidth; }
bool EditorSurface::canPaste() const { return send(SCI_CANPASTE); }
void EditorSurface::setTabStopDistance(qreal) { send(SCI_SETTABWIDTH, 4); }
qreal EditorSurface::tabStopDistance() const { return QFontMetricsF(font(), viewport()).horizontalAdvance(' ') * send(SCI_GETTABWIDTH); }
int EditorSurface::byteLength() const { return int(send(SCI_GETTEXTLENGTH)); }
int EditorSurface::lineHeight() const { return int(send(SCI_TEXTHEIGHT, 0)); }
int EditorSurface::fromUtf16(int position) const {
    position = qBound(0, position, model->characterCount() - 1);
    const auto line = send(SCI_LINEFROMINDEXPOSITION, position, SC_LINECHARACTERINDEX_UTF16);
    return int(send(SCI_POSITIONRELATIVECODEUNITS, send(SCI_POSITIONFROMLINE, line), position - send(SCI_INDEXPOSITIONFROMLINE, line, SC_LINECHARACTERINDEX_UTF16)));
}
int EditorSurface::toUtf16(int position) const {
    position = qBound(0, position, byteLength()); const auto line = send(SCI_LINEFROMPOSITION, position);
    return int(send(SCI_INDEXPOSITIONFROMLINE, line, SC_LINECHARACTERINDEX_UTF16) + send(SCI_COUNTCODEUNITS, send(SCI_POSITIONFROMLINE, line), position));
}
QByteArray EditorSurface::byteRange(int start, int end) const {
    start = qBound(0, start, byteLength()); end = qBound(start, end, byteLength()); QByteArray bytes(end - start + 1, '\0');
    Sci_TextRangeFull range{{start, end}, bytes.data()}; send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<sptr_t>(&range)); bytes.chop(1); return bytes;
}
void EditorSurface::undo() { if (!isReadOnly()) send(SCI_UNDO); }
void EditorSurface::redo() { if (!isReadOnly()) send(SCI_REDO); }
void EditorSurface::cut() { if (!isReadOnly()) send(SCI_CUT); }
void EditorSurface::copy() { send(SCI_COPY); }
void EditorSurface::paste() { if (!isReadOnly()) send(SCI_PASTE); }
void EditorSurface::selectAll() { send(SCI_SELECTALL); }

void EditorSurface::refreshFont() {
    if (applyingFont) return; applyingFont = true;
    const auto family = font().family().toUtf8(); const qreal pixels = font().pixelSize() > 0 ? font().pixelSize() : QFontInfo(font()).pixelSize();
    const int size = qRound(pixels * 72.0 / logicalDpiY() * 100);
    for (int style = 0; style <= STYLE_MAX; ++style) {
        sends(SCI_STYLESETFONT, style, family.constData()); send(SCI_STYLESETSIZEFRACTIONAL, style, size); send(SCI_STYLESETWEIGHT, style, int(font().weight()));
    }
    send(SCI_SETFONTQUALITY, SC_EFF_QUALITY_LCD_OPTIMIZED);
    send(SCI_SETEXTRAASCENT, 0); send(SCI_SETEXTRADESCENT, 0);
    const int extra = qMax(0, qRound(pixels * 19.0 / 14.0) - lineHeight());
    send(SCI_SETEXTRAASCENT, extra / 2); send(SCI_SETEXTRADESCENT, extra - extra / 2);
    refreshMargin(); applyingFont = false;
}
void EditorSurface::refreshMargin() {
    const int margin = uiPx(16) + QFontMetrics(font()).horizontalAdvance('9') * QString::number(qMax(1, blockCount())).size();
    if (send(SCI_GETMARGINWIDTHN, 0) != margin) send(SCI_SETMARGINWIDTHN, 0, margin);
    send(SCI_SETMARGINLEFT, 0, uiPx(4)); send(SCI_SETMARGINRIGHT, 0, 0);
}
void EditorSurface::refreshSurfaceTheme() {
    const auto c = colors();
    for (int style = 0; style <= STYLE_MAX; ++style) { send(SCI_STYLESETFORE, style, rgb(c.text)); send(SCI_STYLESETBACK, style, rgb(c.background)); }
    send(SCI_STYLESETFORE, STYLE_LINENUMBER, rgb(c.muted)); send(SCI_STYLESETBACK, STYLE_LINENUMBER, rgb(c.background));
    send(SCI_SETCARETFORE, rgb(c.text));
    send(SCI_SETWHITESPACEFORE, true, rgb(c.muted)); send(SCI_SETMARGINBACKN, 0, rgb(c.background));
    setSearchSelectionStyle(searchStyle);
}
void EditorSurface::setSearchSelectionStyle(bool enabled) {
    searchStyle = enabled; selectionColor = enabled ? QColor("#eaa13b") : colors().selection;
    const auto ink = enabled ? QColor("#211806") : colors().text;
    for (int element : {SC_ELEMENT_SELECTION_BACK, SC_ELEMENT_SELECTION_ADDITIONAL_BACK, SC_ELEMENT_SELECTION_INACTIVE_BACK, SC_ELEMENT_SELECTION_INACTIVE_ADDITIONAL_BACK}) send(SCI_SETELEMENTCOLOUR, element, argb(selectionColor));
    for (int element : {SC_ELEMENT_SELECTION_TEXT, SC_ELEMENT_SELECTION_ADDITIONAL_TEXT, SC_ELEMENT_SELECTION_INACTIVE_TEXT, SC_ELEMENT_SELECTION_INACTIVE_ADDITIONAL_TEXT}) send(SCI_SETELEMENTCOLOUR, element, argb(ink));
}
QColor EditorSurface::selectionBackground() const { return selectionColor; }
void EditorSurface::setCursorFrameVisible(bool visible) {
    const QRect next = visible ? lineFrameRect() : QRect();
    if (cursorFrameVisible == visible && lastFrame == next) return;
    const QRect old = lastFrame; cursorFrameVisible = visible; lastFrame = next;
    if (!old.isEmpty()) viewport()->update(old.adjusted(-1,-1,1,1));
    if (!next.isEmpty()) viewport()->update(next.adjusted(-1,-1,1,1));
}
QRect EditorSurface::lineFrameRect() const {
    const auto line = send(SCI_LINEFROMPOSITION, send(SCI_GETCURRENTPOS));
    const int top = int(send(SCI_POINTYFROMPOSITION, 0, send(SCI_POSITIONFROMLINE,line)));
    const int left = int(send(SCI_GETMARGINWIDTHN,0));
    return {left,top,qMax(0,viewport()->width()-left),lineHeight()*int(send(SCI_WRAPCOUNT,line))};
}
void EditorSurface::paintEvent(QPaintEvent *event) {
    ScintillaEditBase::paintEvent(event);
    // Scintilla's caret-line visibility setters invalidate every cached line
    // layout. Keep this decoration independent of shaping/style invalidation.
    // Geometry comes from the engine; no QTextDocument or text layout is used.
    if (cursorFrameVisible) {
        lastFrame = lineFrameRect();
        QPainter painter(viewport()); painter.setClipRegion(event->region());
        painter.setPen(colors().line); painter.setBrush(Qt::NoBrush);
        painter.drawRect(lastFrame.adjusted(0,0,-1,-1));
    }
}
void EditorSurface::setExtraSelections(const QList<EditorExtraSelection> &selections) {
    decorations = selections;
    for (int indicator = 8; indicator <= 11; ++indicator) { send(SCI_SETINDICATORCURRENT, indicator); send(SCI_INDICATORCLEARRANGE, 0, byteLength()); }
    send(SCI_MARKERDELETEALL, 8); send(SCI_MARKERDEFINE, 8, SC_MARK_BACKGROUND);
    for (const auto &selection : selections) {
        if (selection.cursor.isNull()) continue;
        const int start = qMin(selection.cursor.byteAnchor(), selection.cursor.bytePosition()), end = qMax(selection.cursor.byteAnchor(), selection.cursor.bytePosition());
        if (selection.format.boolProperty(QTextFormat::FullWidthSelection)) {
            send(SCI_MARKERSETBACK, 8, rgb(selection.format.background().color())); send(SCI_MARKERADD, send(SCI_LINEFROMPOSITION, start), 8); continue;
        }
        const bool error = selection.format.underlineStyle() == QTextCharFormat::WaveUnderline;
        const auto color = error ? selection.format.underlineColor() : selection.format.background().color();
        const int indicator = error ? 10 : color == QColor("#eaa13b") ? 9 : 8;
        send(SCI_INDICSETSTYLE, indicator, error ? INDIC_SQUIGGLEPIXMAP : INDIC_FULLBOX); send(SCI_INDICSETFORE, indicator, rgb(color));
        send(SCI_INDICSETALPHA, indicator, SC_ALPHA_OPAQUE); send(SCI_INDICSETOUTLINEALPHA, indicator, SC_ALPHA_OPAQUE); send(SCI_INDICSETUNDER, indicator, !error);
        if (!error) { send(SCI_INDICSETFLAGS, indicator, SC_INDICFLAG_VALUEFORE); }
        send(SCI_SETINDICATORCURRENT, indicator); send(SCI_SETINDICATORVALUE, error ? 1 : (SC_INDICVALUEBIT | rgb(color)));
        if (end > start) send(SCI_INDICATORFILLRANGE, start, end - start);
        if (!error && end > start && selection.format.hasProperty(QTextFormat::ForegroundBrush)) {
            send(SCI_INDICSETSTYLE, 11, INDIC_TEXTFORE); send(SCI_INDICSETFORE, 11, rgb(selection.format.foreground().color()));
            send(SCI_SETINDICATORCURRENT, 11); send(SCI_SETINDICATORVALUE, 1); send(SCI_INDICATORFILLRANGE, start, end - start);
        }
    }
}
bool EditorSurface::event(QEvent *event) {
    const bool handled = ScintillaEditBase::event(event);
    if (model && (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::DevicePixelRatioChange)) refreshFont();
    return handled;
}
void EditorSurface::wheelEvent(QWheelEvent *event) {
    // The upstream wrapper checks QApplication::keyboardModifiers(), which can
    // retain Ctrl from the preceding key gesture. Use the event's own modifiers
    // and route scrolling directly; global zoom belongs to UiZoom.
    QAbstractScrollArea::wheelEvent(event);
}
void EditorSurface::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    auto undoAction = menu.addAction("撤销", this, &EditorSurface::undo, QKeySequence::Undo); undoAction->setEnabled(!isReadOnly() && model->isUndoAvailable());
    auto redoAction = menu.addAction("重做", this, &EditorSurface::redo, QKeySequence::Redo); redoAction->setEnabled(!isReadOnly() && model->isRedoAvailable()); menu.addSeparator();
    auto cutAction = menu.addAction("剪切", this, &EditorSurface::cut, QKeySequence::Cut); cutAction->setEnabled(!isReadOnly() && !send(SCI_GETSELECTIONEMPTY));
    menu.addAction("复制", this, &EditorSurface::copy, QKeySequence::Copy)->setEnabled(!send(SCI_GETSELECTIONEMPTY));
    menu.addAction("粘贴", this, &EditorSurface::paste, QKeySequence::Paste)->setEnabled(!isReadOnly() && canPaste()); menu.addSeparator();
    menu.addAction("全选", this, &EditorSurface::selectAll, QKeySequence::SelectAll); menu.exec(event->globalPos());
}
void EditorSurface::mousePressEvent(QMouseEvent *event) { if (event->button() == Qt::LeftButton) draggingSelection = true; ScintillaEditBase::mousePressEvent(event); }
void EditorSurface::mouseReleaseEvent(QMouseEvent *event) {
    ScintillaEditBase::mouseReleaseEvent(event); if (event->button() == Qt::LeftButton) { draggingSelection = false; notifySelectionAccessibility(); }
}
void EditorSurface::focusOutEvent(QFocusEvent *event) { draggingSelection = false; ScintillaEditBase::focusOutEvent(event); }
void EditorSurface::notifySelectionAccessibility() {
    if (!QAccessible::isActive() || !hasFocus()) return;
    const int anchor = toUtf16(int(send(SCI_GETANCHOR))), caret = toUtf16(int(send(SCI_GETCURRENTPOS)));
    QAccessibleTextSelectionEvent event(this, qMin(anchor, caret), qMax(anchor, caret)); event.setCursorPosition(caret); QAccessible::updateAccessibility(&event);
}
}
