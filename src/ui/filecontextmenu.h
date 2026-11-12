#pragma once
#include <QMenu>
#include <QPropertyAnimation>

namespace rs {
// Keep QAction state, layout, input and accessibility in QMenu. Only this
// resource menu moves the visual check to the right of its semantic icon.
class FileContextMenu final : public QMenu {
    friend class MenuTests;
  public:
    explicit FileContextMenu(QWidget *parent=nullptr);
  protected:
    void initStyleOption(QStyleOptionMenuItem *,const QAction *) const override;
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
  private:
    void syncMetrics();
    QRect itemViewport() const;
    QRect iconRect(QAction *) const;
    QRect checkRect(QAction *) const;
    QPropertyAnimation *fade=nullptr;
};
}
