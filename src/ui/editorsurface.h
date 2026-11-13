#pragma once
#include <ScintillaEditBase.h>
#include <Scintilla.h>
#include <QtWidgets>
#include <memory>

namespace rs {
class EditorSurface;
class EditorDocument;
class EditorBlock;
struct EditorCursorState { int anchor = 0, position = 0; bool valid = true; };

// Positions exposed to search/diagnostics are UTF-16. The engine and tracked
// anchors use UTF-8 bytes; Scintilla maintains the incremental per-line index.
class EditorCursor {
  public:
    EditorCursor() = default;
    explicit EditorCursor(EditorDocument *document);
    explicit EditorCursor(const EditorBlock &block);
    bool isNull() const;
    EditorDocument *document() const;
    int position() const;
    int anchor() const;
    int selectionStart() const;
    int selectionEnd() const;
    bool hasSelection() const;
    QString selectedText() const;
    int blockNumber() const;
    EditorBlock block() const;
    void setPosition(int position, QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);
    bool movePosition(QTextCursor::MoveOperation operation, QTextCursor::MoveMode mode = QTextCursor::MoveAnchor, int count = 1);
    void clearSelection();
    void select(QTextCursor::SelectionType selection);
    void insertText(const QString &text);
    void beginEditBlock();
    void endEditBlock();
    bool operator==(const EditorCursor &other) const;
    int bytePosition() const;
    int byteAnchor() const;
  private:
    friend class EditorSurface;
    void detach();
    QPointer<EditorDocument> owner;
    std::shared_ptr<EditorCursorState> state;
};

class EditorBlock {
  public:
    EditorBlock() = default;
    EditorBlock(EditorDocument *document, int line);
    bool isValid() const;
    bool isVisible() const { return isValid(); }
    EditorDocument *document() const;
    int blockNumber() const { return line; }
    int position() const;
    int length() const;
    QString text() const;
    EditorBlock next() const;
    EditorBlock previous() const;
    bool operator==(const EditorBlock &other) const { return owner == other.owner && line == other.line; }
  private:
    QPointer<EditorDocument> owner;
    int line = -1;
};

// Metadata/anchors only. Owns no text and no hidden QTextDocument.
class EditorDocument final : public QObject {
    Q_OBJECT
  public:
    explicit EditorDocument(EditorSurface *surface);
    int revision() const { return serial; }
    int characterCount() const { return utf16Length + 1; }
    bool isModified() const;
    void setModified(bool value);
    bool isUndoAvailable() const;
    bool isRedoAvailable() const;
    int availableUndoSteps() const;
    EditorBlock firstBlock() const;
    EditorBlock lastBlock() const;
    EditorBlock findBlock(int position) const;
    EditorBlock findBlockByNumber(int line) const;
    EditorSurface *surface() const { return view; }
    std::shared_ptr<EditorCursorState> track(int anchor = 0, int position = 0);
    void changed(int position, int removed, int added, const QByteArray &text);
    void reset();
  signals:
    void contentsChanged();
    void bytesChanged(int position, int removed, int added);
    void modificationChanged(bool modified);
  private:
    EditorSurface *view;
    int serial = 0, utf16Length = 0;
    bool forcedModified = false;
    QVector<std::weak_ptr<EditorCursorState>> cursors;
};

struct EditorExtraSelection { EditorCursor cursor; QTextCharFormat format; };
class EditorSurface : public ScintillaEditBase {
    Q_OBJECT
  public:
    enum LineWrapMode { NoWrap, WidgetWidth };
    explicit EditorSurface(QWidget *parent = nullptr);
    EditorDocument *document() const { return model; }
    QString toPlainText() const;
    void setPlainText(const QString &text);
    void insertPlainText(const QString &text);
    EditorCursor textCursor() const;
    void setTextCursor(const EditorCursor &cursor);
    void moveCursor(QTextCursor::MoveOperation operation, QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);
    EditorCursor cursorForPosition(QPoint point) const;
    QRect cursorRect() const;
    QRect cursorRect(const EditorCursor &cursor) const;
    int blockCount() const;
    EditorBlock firstVisibleBlock() const;
    QRectF blockBoundingRect(const EditorBlock &block) const;
    QRectF blockBoundingGeometry(const EditorBlock &block) const;
    QPointF contentOffset() const { return {}; }
    void centerCursor();
    void ensureCursorVisible();
    void setReadOnly(bool readOnly);
    bool isReadOnly() const;
    void setLineWrapMode(LineWrapMode mode);
    LineWrapMode lineWrapMode() const;
    bool canPaste() const;
    void setTabStopDistance(qreal distance);
    qreal tabStopDistance() const;
    int fromUtf16(int position) const;
    int toUtf16(int bytePosition) const;
    QByteArray byteRange(int start, int end) const;
    int byteLength() const;
    int lineHeight() const;
    void setExtraSelections(const QList<EditorExtraSelection> &selections);
    QList<EditorExtraSelection> extraSelections() const { return decorations; }
    void refreshSurfaceTheme();
    void setSearchSelectionStyle(bool enabled);
    void setCursorFrameVisible(bool visible);
    QColor selectionBackground() const;
  public slots:
    void undo(); void redo(); void cut(); void copy(); void paste(); void selectAll();
  signals:
    void textChanged();
    void cursorPositionChanged();
    void selectionChanged();
    void blockCountChanged(int blocks);
  protected:
    bool event(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void refreshFont();
  private:
    EditorDocument *model;
    QList<EditorExtraSelection> decorations;
    int lastCaret = -1, lastAnchor = -1;
    bool loading = false, applyingFont = false, searchStyle = false, draggingSelection = false;
    QColor selectionColor;
    bool cursorFrameVisible = false;
    QRect lastFrame;
    QRect lineFrameRect() const;
    void syncSelection();
    void refreshMargin();
    void notifySelectionAccessibility();
};
}
