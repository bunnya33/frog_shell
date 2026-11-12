#include "listeningtable.h"
#include "theme.h"
#include "layoutmetrics.h"
#include "processtable.h"
#include "uipreferences.h"
#include <algorithm>
namespace rs {
namespace {
class EndpointItem final:public QTableWidgetItem {
  public:bool operator<(const QTableWidgetItem &other) const override {
    if(data(Qt::UserRole).isValid()&&other.data(Qt::UserRole).isValid())return data(Qt::UserRole).toLongLong()<other.data(Qt::UserRole).toLongLong();
    return QTableWidgetItem::operator<(other);
  }
};
void paintSemanticText(QPainter *painter,QStyleOptionViewItem option,const QColor &ink) {
    const auto widget=option.widget;auto style=widget?widget->style():QApplication::style();
    const auto text=option.text;const auto textRect=style->subElementRect(QStyle::SE_ItemViewItemText,&option,widget);
    option.text.clear();style->drawControl(QStyle::CE_ItemViewItem,&option,painter,widget);
    // QSS item selection-color may replace an option palette. Paint semantic
    // text after the native item background so its contrast survives selection.
    painter->save();painter->setClipRect(option.rect);painter->setFont(option.font);painter->setPen(ink);
    painter->drawText(textRect,option.displayAlignment,option.fontMetrics.elidedText(text,option.textElideMode,textRect.width()));painter->restore();
}
class ProtocolDelegate final:public QStyledItemDelegate {
  public:using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        auto item=option;initStyleOption(&item,index);
        paintSemanticText(painter,item,index.data().toString()=="UDP"?colors().orange:colors().link);
    }
  protected:void initStyleOption(QStyleOptionViewItem *o,const QModelIndex &index)const override{
    QStyledItemDelegate::initStyleOption(o,index);o->font.setBold(true);
    const auto ink=index.data().toString()=="UDP"?colors().orange:colors().link;
    o->palette.setColor(QPalette::Text,ink);o->palette.setColor(QPalette::HighlightedText,ink);
  }
};
class PortNumberDelegate final:public QStyledItemDelegate {
  public:using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        auto item=option;initStyleOption(&item,index);
        const bool selected=option.state.testFlag(QStyle::State_Selected);
        paintSemanticText(painter,item,selected&&lightTheme()?QColor("#074a9e"):colors().accent);
    }
  protected:void initStyleOption(QStyleOptionViewItem *option,const QModelIndex &index)const override{
    QStyledItemDelegate::initStyleOption(option,index);option->font.setWeight(QFont::Medium);
    const auto ink=colors().accent;
    const auto selected=lightTheme()?QColor("#074a9e"):ink;
    option->palette.setColor(QPalette::Text,ink);option->palette.setColor(QPalette::HighlightedText,selected);
  }
};
}
ListeningTable::ListeningTable(bool small,QWidget *parent):QTableWidget(0,small?3:5,parent),compact(small) {
    setProperty("processTable",true);setObjectName(small?"miniListeningPorts":"listeningPorts");
    setAccessibleName(small?"监听端口概览":"监听端口");bindUiFont(this,small?12:13);
    setHorizontalHeader(processHeader(small,this));
    setItemDelegateForColumn(0,new ProtocolDelegate(this));
    setItemDelegateForColumn(2,new PortNumberDelegate(this));
    setHorizontalHeaderLabels(small?QStringList{"协议","监听地址","端口"}:QStringList{"协议","监听地址","端口","进程","PID"});
    setEditTriggers(NoEditTriggers);setSelectionBehavior(SelectRows);setSelectionMode(ExtendedSelection);
    setShowGrid(false);setAlternatingRowColors(true);setFrameShape(QFrame::NoFrame);verticalHeader()->hide();
    setHorizontalScrollMode(ScrollPerPixel);setVerticalScrollMode(ScrollPerPixel);setTextElideMode(Qt::ElideRight);
    horizontalHeader()->setMinimumSectionSize(30);horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    horizontalHeader()->setProperty("listeningHeader",true);bindUiFont(horizontalHeader(),small?12:13);
    horizontalHeader()->style()->unpolish(horizontalHeader());horizontalHeader()->style()->polish(horizontalHeader());
    const QList<int> widths=compact?QList<int>{52,140,72}:QList<int>{90,300,100,240,120};
    const auto saved=QSettings().value("listening-table/full/widths-v1").toList();
    const bool custom=!compact&&saved.size()==columnCount()&&std::all_of(saved.cbegin(),saved.cend(),[](const QVariant &v){bool ok=false;const int n=v.toInt(&ok);return ok&&n>=60&&n<=4096;});
    setProperty("customListeningWidths",custom);
    for(int c=0;c<columnCount();++c)baseWidths.append(custom?saved[c].toDouble():widths[c]);
    UiZoom::instance().bind(this,"metrics",[this]{
        verticalHeader()->setDefaultSectionSize(qMax(uiPx(compact?27:30),fontMetrics().height()+uiPx(8)));
        horizontalHeader()->setFixedHeight(qMax(uiPx(compact?26:32),horizontalHeader()->fontMetrics().height()+uiPx(8)));
        QSignalBlocker block(horizontalHeader());for(int c=0;c<columnCount();++c)setColumnWidth(c,uiPx(baseWidths[c]));
    });
    if(!compact){widthSaveTimer.setSingleShot(true);widthSaveTimer.setInterval(250);
        connect(&widthSaveTimer,&QTimer::timeout,this,&ListeningTable::saveWidths);
        connect(horizontalHeader(),&QHeaderView::sectionResized,this,[this](int column,int,int width){if(!UiZoom::instance().changing()&&!UiPreferences::instance().resetting()){if(!property("customListeningWidths").toBool())for(int c=0;c<columnCount();++c)baseWidths[c]=uiBase(columnWidth(c));baseWidths[column]=uiBase(width);setProperty("customListeningWidths",true);widthSaveTimer.start();}});
        connect(&UiPreferences::instance(),&UiPreferences::aboutToReset,this,[this]{widthSaveTimer.stop();});
        connect(&UiPreferences::instance(),&UiPreferences::defaultsRestored,this,[this]{widthSaveTimer.stop();setProperty("customListeningWidths",false);baseWidths={90,300,100,240,120};{QSignalBlocker block(horizontalHeader());for(int c=0;c<columnCount();++c)setColumnWidth(c,uiPx(baseWidths[c]));}QResizeEvent event(size(),size());resizeEvent(&event);});
    }
    connect(&UiZoom::instance(),&UiZoom::changed,this,[this]{QResizeEvent event(size(),size());resizeEvent(&event);});
    setSortingEnabled(true);sortItems(2,Qt::AscendingOrder);setStatus(status);
}
ListeningTable::~ListeningTable(){if(widthSaveTimer.isActive())saveWidths();}
void ListeningTable::saveWidths(){QVariantList widths;for(auto width:baseWidths)widths<<qRound(width);QSettings().setValue("listening-table/full/widths-v1",widths);}
void ListeningTable::setStatus(const QString &value) {status=value;setAccessibleDescription(value);setToolTip(value);viewport()->update();}
void ListeningTable::applySnapshot(const listening::Snapshot &snapshot) {
    if(!snapshot.complete||!snapshot.available){setStatus(snapshot.error.isEmpty()?QString("等待采集"):snapshot.error);return;}
    QSet<QString> selected;for(const auto &i:selectionModel()->selectedRows())selected.insert(item(i.row(),0)->data(Qt::UserRole+1).toString());
    const QString current=currentItem()?currentItem()->data(Qt::UserRole+1).toString():QString();const int currentCol=currentColumn();
    const int x=horizontalScrollBar()->value(),y=verticalScrollBar()->value();
    const int sort=horizontalHeader()->sortIndicatorSection();const auto order=horizontalHeader()->sortIndicatorOrder();
    QSignalBlocker tableSignals(this),selectionSignals(selectionModel());setUpdatesEnabled(false);setSortingEnabled(false);
    const auto entries=compact?snapshot.endpoints.mid(0,5):snapshot.endpoints;QSet<QString> incoming;
    for(const auto &e:entries)incoming.insert(e.key());
    for(int r=rowCount()-1;r>=0;--r)if(!incoming.contains(item(r,0)->data(Qt::UserRole+1).toString()))removeRow(r);
    QHash<QString,int> rows;for(int r=0;r<rowCount();++r)rows[item(r,0)->data(Qt::UserRole+1).toString()]=r;
    for(const auto &e:entries){int r=rows.value(e.key(),-1);if(r<0){r=rowCount();insertRow(r);for(int c=0;c<columnCount();++c)setItem(r,c,new EndpointItem);}
        const QStringList values{e.protocol,e.address,QString::number(e.port),e.process.isEmpty()?QString("—"):e.process,e.pid?QString::number(e.pid):QString("—")};
        for(int c=0;c<columnCount();++c){auto it=item(r,c);it->setText(values[c]);it->setData(Qt::UserRole+1,e.key());it->setData(Qt::UserRole,c==2?QVariant(e.port):c==4?QVariant(e.pid):QVariant());it->setTextAlignment((c==2||c==4?Qt::AlignRight:Qt::AlignLeft)|Qt::AlignVCenter);it->setToolTip(e.protocol+" "+e.address+":"+QString::number(e.port)+(e.protocol=="UDP"?" · 已绑定（未连接）":" · LISTEN")+"\n"+values[3]+" · "+values[4]+"\nPID / 进程仅为可见归属之一；权限可能限制查询，同一端点可能由多个进程共享。");}
    }
    setSortingEnabled(true);sortItems(sort,order);clearSelection();
    for(int r=0;r<rowCount();++r){const auto key=item(r,0)->data(Qt::UserRole+1).toString();if(selected.contains(key))selectionModel()->select(model()->index(r,0),QItemSelectionModel::Select|QItemSelectionModel::Rows);if(key==current&&currentCol>=0)selectionModel()->setCurrentIndex(model()->index(r,currentCol),QItemSelectionModel::NoUpdate);}
    horizontalScrollBar()->setValue(x);verticalScrollBar()->setValue(y);setUpdatesEnabled(true);
    setStatus(snapshot.error.isEmpty()?QString("%1 个端点 · %2；TCP LISTEN / UDP 已绑定（未连接）").arg(snapshot.endpoints.size()).arg(snapshot.source):snapshot.error);
    applyFilter();
}
void ListeningTable::setFilterText(const QString &text){filter=text.trimmed();applyFilter();}
void ListeningTable::applyFilter(){
    int shown=0;
    for(int r=0;r<rowCount();++r){bool match=filter.isEmpty();for(int c=0;c<columnCount()&&!match;++c)match=item(r,c)&&item(r,c)->text().contains(filter,Qt::CaseInsensitive);setRowHidden(r,!match);if(match)++shown;}
    if(countsChanged)countsChanged(shown,rowCount());
}
void ListeningTable::resizeEvent(QResizeEvent *e) {
    QTableWidget::resizeEvent(e);
    if(compact){QSignalBlocker block(horizontalHeader());setColumnWidth(1,qMax(uiPx(100),viewport()->width()-columnWidth(0)-columnWidth(2)));}
    else if(!property("customListeningWidths").toBool()) {int remaining=viewport()->width();for(int c=0;c<columnCount();++c)if(c!=1)remaining-=columnWidth(c);QSignalBlocker block(horizontalHeader());setColumnWidth(1,qMax(uiPx(200),remaining));}
}
void ListeningTable::paintEvent(QPaintEvent *e) {
    QTableWidget::paintEvent(e);
    if(!rowCount()){QPainter p(viewport());p.setPen(colors().muted);p.drawText(viewport()->rect().adjusted(uiPx(10),uiPx(4),-uiPx(10),-uiPx(4)),Qt::AlignCenter|Qt::TextWordWrap,status.startsWith("0 个")?QString("暂无监听端口"):status);}
}
}
