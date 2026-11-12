#pragma once
#include "uizoom.h"
namespace rs {
// Canonical logical pixels. App zoom and Qt's device scale remain independent.
inline constexpr int panelBand = 36, toolHit = 28, toolImage = 18;
inline void bandHeight(QWidget *widget, int base = panelBand) {
    UiZoom::instance().bind(widget,"bandHeight",[widget,base] {
        widget->setFixedHeight(qMax(uiPx(base),widget->fontMetrics().height()+uiPx(6)));
    });
}
class DensityDelegate : public QStyledItemDelegate {
  public:
    DensityDelegate(int base, QObject *parent,int childBase=0) : QStyledItemDelegate(parent), base(base),childBase(childBase) {}
    QSize sizeHint(const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        auto size=QStyledItemDelegate::sizeHint(option,index);
        size.setHeight(qMax(size.height(),uiPx(childBase&&index.parent().isValid()?childBase:base)));
        return size;
    }
  private:
    int base,childBase;
};
}
