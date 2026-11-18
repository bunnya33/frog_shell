#pragma once
#include "editor.h"
#include "../core/textdiff.h"

namespace rs {
class CompareOverview;
class CompareSurface;
class EditorLexer;
class ElidedLabel;

class EditorCompare final : public QWidget {
    Q_OBJECT
  public:
    explicit EditorCompare(Editor *source, QWidget *parent = nullptr);
    Editor *sourceEditor() const { return source; }
    void refreshTheme();

  protected:
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

  private:
    QPointer<Editor> source;
    CompareSurface *left, *right;
    EditorLexer *leftLexer, *rightLexer;
    QWidget *leftGap, *rightGap;
    CompareOverview *overview;
    ElidedLabel *sourceLabel, *status;
    QCheckBox *ignoreSpace;
    QAction *previous, *next;
    QTimer refresh;
    QString sourceName, sourceSnapshot, displayedSource;
    TextDiff difference;
    quint64 generation = 0;
    int currentHunk = -1;
    int firstRow = 0, leftLeading = 0, rightLeading = 0;
    qreal wheelRemainder = 0;
    bool running = false, pending = false, applying = false, sourceChanged = false;

    void scheduleCompare();
    void startCompare();
    void showResult(TextDiff result);
    int decorate(EditorSurface *view, bool leftSide);
    void navigate(int offset);
    int rowCount() const;
    int pageRows() const;
    void scrollTo(int row);
    void syncScroll(EditorSurface *from);
};
}
