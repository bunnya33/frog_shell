#pragma once
#include <QtWidgets>
#include "contentfont.h"
#ifdef small
#undef small
#endif
#include <deque>
#include <optional>
#include <vterm.h>
namespace rs {
enum class DiagEvent;
class Terminal final : public QAbstractScrollArea {
    Q_OBJECT
  public:
    explicit Terminal(QWidget *parent = nullptr);
    ~Terminal() override;
    void feed(const QByteArray &bytes);
    void refreshTheme();
    QString text() const;
    qsizetype historyBytes() const {
        return historySize;
    }
    int columns() const {
        return cols;
    }
    int rowCount() const {
        return rows;
    }
    void paste();
    void copy();
    bool hasSelection() const {return selectionActive;}
    void clearOutput();
  signals:
    void input(const QByteArray &bytes);
    void sizeChanged(int columns, int rows);
    void focused();

  protected:
    bool event(QEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void inputMethodEvent(QInputMethodEvent *) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;

  private:
    friend class TerminalTests;
    friend class AppearanceTests;
    friend class WorkspaceTests;
    struct InkSpan { int first, end; QColor color; };
    VTerm *term;
    VTermScreen *screen;
    VTermState *state;
    VTermScreenCallbacks callbacks{};
    struct HistoryRow : QVector<VTermScreenCell> {
        using QVector<VTermScreenCell>::QVector;
        VTermLineInfo info{};
        QBitArray protectedCells;
    };
    std::deque<HistoryRow> history;
    qsizetype historySize = 0;
    qint64 discardedGlyphs = 0;
    int cols = 80, rows = 24, cellW = 8, cellH = 18, ascent = 14;
    bool cursorVisible = true, cursorBlink = true, cursorOn = true;
    bool selecting = false, selectionActive = false, alternate = false;
    bool feeding = false;
    bool screenTransition = false;
    const quint64 diagnosticTag = QRandomGenerator::global()->generate64();
    int cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
    QPoint selectionStart{-1, -1}, selectionEnd{-1, -1};
    QPoint selectionAnchor;
    VTermPos cursor{};
    QTimer repaintTimer;
    QTimer resizeTimer;
    QTimer cursorTimer;
    QPointer<QWidget> cursorWindow;
    QPointer<QWindow> cursorHandle;
    QByteArray preparedPaste;
    bool cell(int absoluteRow, int col, VTermScreenCell *out) const;
    void pushHistory(int n, const VTermScreenCell *cells, const VTermLineInfo *info = nullptr, const QBitArray *protectedCells = nullptr);
    void appendHistory(HistoryRow line);
    static qsizetype rowBytes(const HistoryRow &line) { return sizeof(HistoryRow)+line.capacity()*qsizetype(sizeof(VTermScreenCell))+(line.protectedCells.size()+7)/8; }
    void resizePrimary(int columns, int rows);
    bool continuesPrevious(int absoluteRow) const;
    int usedColumns(int absoluteRow) const;
    std::optional<qint64> glyphOrdinal(QPoint position) const;
    QPoint glyphPosition(qint64 ordinal) const;
    QPoint point(QPoint p) const;
    void sendText(const QString &text, Qt::KeyboardModifiers modifiers = {});
    QString selection() const;
    void clearSelection();
    void logState(DiagEvent);
    bool findSelection(const QString &text);
    QRect cursorRect() const;
    bool cursorInView() const;
    void updateCursorBlink(bool restart = false);
    void updateCursorRect(const QRect &rect);
    void watchCursorWindow();
    void applySize();
    QVector<InkSpan> rowHighlights(const QVector<VTermScreenCell> &line) const;
    QColor displayColor(VTermColor color, const QColor &defaultFg, const QColor &defaultBg) const;
    QFont cellFont(const VTermScreenCellAttrs &attributes) const;
};
} // namespace rs
