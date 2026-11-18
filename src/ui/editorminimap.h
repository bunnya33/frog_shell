#pragma once
#include <QtWidgets>

namespace rs {
class CodeEdit;

class EditorMinimap final : public QWidget {
  public:
    EditorMinimap(CodeEdit *editor, QWidget *parent);
    void invalidate();

  protected:
    bool eventFilter(QObject *, QEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    CodeEdit *editor;
    QTimer refresh;
    QImage preview;
    int pitch = 0;
    qreal dragOffset = 0;
    void fitWidth();
    void renderPreview();
    void scrollTo(qreal y);
    QRectF visibleRegion() const;
};
}
