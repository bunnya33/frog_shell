#pragma once
#include <QtWidgets>
namespace rs {
// Qt 6.8's visible-row accessible cache must retire before persistent indices move.
// Only accessible interfaces are reset; the business model and selection survive.
class SafeTreeWidget : public QTreeWidget {
  public:
    explicit SafeTreeWidget(QWidget *parent=nullptr):QTreeWidget(parent) {
        auto reset=[this]{invalidateAccessibleRows();};
        connect(model(),&QAbstractItemModel::rowsAboutToBeRemoved,this,reset);
        connect(model(),&QAbstractItemModel::rowsAboutToBeInserted,this,reset);
        connect(model(),&QAbstractItemModel::rowsAboutToBeMoved,this,reset);
        connect(model(),&QAbstractItemModel::layoutAboutToBeChanged,this,reset);
        connect(model(),&QAbstractItemModel::modelAboutToBeReset,this,reset);
        connect(model(),&QAbstractItemModel::rowsRemoved,this,reset);
        connect(model(),&QAbstractItemModel::rowsInserted,this,reset);
        connect(model(),&QAbstractItemModel::rowsMoved,this,reset);
        connect(model(),&QAbstractItemModel::layoutChanged,this,[this]{
            invalidateAccessibleRows();
            // The view may still be using its old visible-row layout when the
            // model's sort notification arrives. Complete it outside the model
            // callback, then retire any cells requested in that interval.
            if(accessibleLayoutPending)return;
            accessibleLayoutPending=true;
            QTimer::singleShot(0,this,[this]{doItemsLayout();invalidateAccessibleRows();accessibleLayoutPending=false;});
        });
        connect(model(),&QAbstractItemModel::modelReset,this,reset);
        connect(this,&QTreeWidget::itemExpanded,this,reset);
        connect(this,&QTreeWidget::itemCollapsed,this,reset);
    }
    ~SafeTreeWidget() override {disconnect(model(),nullptr,this,nullptr);disconnect(this,nullptr,this,nullptr);}
    void invalidateAccessibleRows() {
        if(auto accessible=QAccessible::queryAccessibleInterface(this))if(auto table=accessible->tableInterface()) {
            QAccessibleTableModelChangeEvent reset(this,QAccessibleTableModelChangeEvent::ModelReset);
            table->modelChange(&reset);
        }
    }
  private:
    bool accessibleLayoutPending=false;
};
}
