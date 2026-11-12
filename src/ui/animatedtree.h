#pragma once

#include "safetree.h"
#include <memory>

namespace rs {

// Values are stored on column zero. Terminal peers stay ordinary sibling rows.
inline constexpr int TerminalPeerPositionRole = Qt::UserRole + 160;

class AnimatedTreeWidget : public SafeTreeWidget {
  public:
    enum class TreeMode { Hierarchy, TerminalPeers };
    explicit AnimatedTreeWidget(QWidget *parent = nullptr);
    ~AnimatedTreeWidget() override;

    void setTreeMode(TreeMode mode);
    void setTreeMotionEnabled(bool enabled);
    void resetTreeTransientState();

  protected:
    void drawRow(QPainter *, const QStyleOptionViewItem &, const QModelIndex &) const override;
    void drawBranches(QPainter *, const QRect &, const QModelIndex &) const override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    bool viewportEvent(QEvent *) override;
    void hideEvent(QHideEvent *) override;
    void changeEvent(QEvent *) override;

  private:
    struct State;
    std::unique_ptr<State> state;
    void setHovered(const QModelIndex &);
    void animate(const QModelIndex &, bool angle, qreal target);
    qreal value(const QModelIndex &, bool angle, qreal fallback) const;
    void paintRowBackground(QPainter *, const QRect &, const QModelIndex &) const;
};
}
