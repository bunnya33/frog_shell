#pragma once
#include "layoutmetrics.h"

namespace rs {
inline constexpr int ConnectionStateRole = Qt::UserRole + 80;
enum class ConnectionState { Disconnected, Connecting, Connected };

// Paint semantic labels in the view; no per-row QWidget or timer is needed.
class ConnectionDelegate final : public DensityDelegate {
  public:
    explicit ConnectionDelegate(QObject *parent) : DensityDelegate(30, parent) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};
} // namespace rs
