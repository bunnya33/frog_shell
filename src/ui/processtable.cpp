#include "processtable.h"
#include "theme.h"
#include "roundedsurface.h"
#include "uipreferences.h"
#include "../core/profile.h"
#include "../core/diagnostics.h"
#include "layoutmetrics.h"
namespace rs {
namespace {
class ProcessItem final : public QTableWidgetItem {
  public:
    bool operator<(const QTableWidgetItem &other) const override {
        const auto a = data(Qt::UserRole), b = other.data(Qt::UserRole);
        if (a.isValid() && b.isValid())
            return a.toDouble() < b.toDouble();
        return QTableWidgetItem::operator<(other);
    }
};
class ProcessHeader final : public QHeaderView {
  public:
    explicit ProcessHeader(bool compact,QWidget *parent) : QHeaderView(Qt::Horizontal, parent) {
        setSectionsClickable(true);
        setSortIndicatorShown(true);
        UiZoom::instance().bind(this,"minimumSection",[this,compact]{setMinimumSectionSize(uiPx(compact?40:60));});
        bandHeight(this,compact?26:32);bindUiFont(this,compact?12:13);
    }

  protected:
    void paintSection(QPainter *p, const QRect &rect, int section) const override {
        if (!rect.isValid())
            return;
        p->save();
        const auto c = colors();
        p->fillRect(rect, c.processHeader);
        if (visualIndex(section) < count() - 1) {
            p->setPen(c.divider);
            p->drawLine(rect.right(), rect.top() + uiPx(7), rect.right(), rect.bottom() - uiPx(7));
        }
        QRect textRect = rect.adjusted(uiPx(10), 0, -uiPx(10), 0);
        const bool sorted = isSortIndicatorShown() && section == sortIndicatorSection();
        if (sorted)
            textRect.adjust(0, 0, -uiPx(14), 0);
        p->setPen(c.text);
        const auto label = model()->headerData(section, orientation()).toString();
        const int alignment = model()->headerData(section, orientation(), Qt::TextAlignmentRole).toInt();
        p->drawText(textRect, (alignment ? alignment : Qt::AlignLeft) | Qt::AlignVCenter,
                    fontMetrics().elidedText(label, Qt::ElideRight, textRect.width()));
        if (sorted) {
            const int x = rect.right() - uiPx(14), y = rect.center().y();
            const int direction = sortIndicatorOrder() == Qt::AscendingOrder ? 1 : -1;
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(Qt::NoPen);
            p->setBrush(c.activeGroup);
            p->drawPolygon(QPolygon{{x - uiPx(4), y + direction * uiPx(2)}, {x + uiPx(4), y + direction * uiPx(2)},
                                    {x, y - direction * uiPx(3)}});
        }
        p->restore();
    }
};
class ProcessDelegate final : public QStyledItemDelegate {
  public:
    ProcessDelegate(bool compact,QObject *parent):QStyledItemDelegate(parent),compact(compact){}
    void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        QStyleOptionViewItem opt(option);initStyleOption(&opt,index);
        const QString text=opt.text;opt.text.clear();
        const QWidget *widget=opt.widget;auto style=widget?widget->style():QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem,&opt,p,widget);
        const bool selected=opt.state.testFlag(QStyle::State_Selected);auto c=colors();QColor ink=c.text;
        if(index.column()==(compact?1:3))ink=cpuColor(selected);
        else if(index.column()==(compact?2:4))ink=memoryColor();
        else if(!compact&&(index.column()==0||index.column()==2))ink=selected&&!lightTheme()?QColor("#c0c6d9"):c.muted;
        else if(!compact&&index.column()==5)ink=index.data(Qt::UserRole+10).toString().startsWith('R')?c.green:(selected&&!lightTheme()?QColor("#c0c6d9"):c.muted);
        p->save();p->setClipRect(opt.rect);p->setPen(ink);auto font=opt.font;
        if(index.column()==(compact?1:3)||index.column()==(compact?2:4))font.setWeight(QFont::Medium);
        p->setFont(font);const auto rect=opt.rect.adjusted(uiPx(10),0,-uiPx(10),0);
        p->drawText(rect,opt.displayAlignment|Qt::AlignVCenter,QFontMetrics(font).elidedText(text,Qt::ElideRight,rect.width()));p->restore();
    }
  private:bool compact;
};
class ProcessCard final : public QWidget {
  public:
    explicit ProcessCard(QAbstractItemView *table,QWidget *header) {
        setProperty("processCard", true);
        installRoundedSurface(this,RoundedRole::Process);
        auto layout = new QVBoxLayout(this);
        uiMargins(layout,1,1,1,1);
        uiSpacing(layout,0);
        if(header)layout->addWidget(header);layout->addWidget(table);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        paintRoundedSurface(p,rect(),uiReal(8),colors().processBody,Qt::transparent,0);
    }
};
qint64 pidAt(const QTableWidget &table, int row) {
    const auto item = table.item(row, 0);
    return item ? item->data(ProcessIdRole).toLongLong() : 0;
}
} // namespace
QHeaderView *processHeader(bool compact,QWidget *parent) { return new ProcessHeader(compact,parent); }
ProcessTable::ProcessTable(bool compactView, QWidget *parent)
    : QTableWidget(0, compactView ? 3 : 6, parent), compact(compactView) {
    setProperty("processTable", true);
    setAccessibleName(compact ? "进程概览" : "全部进程");
    setHorizontalHeader(new ProcessHeader(compact,this));
    bindUiFont(this,compact?12:13);setItemDelegate(new ProcessDelegate(compact,this));
    setHorizontalHeaderLabels(compact ? QStringList{"进程", "CPU", "内存 RSS"}
                                      : QStringList{"PID", "名称", "用户", "CPU %", "内存 RSS", "状态"});
    horizontalHeaderItem(compact ? 1 : 3)->setToolTip("CPU 为 ps 报告的占比；点击列标题排序。");
    horizontalHeaderItem(compact ? 2 : 4)->setToolTip("RSS 为常驻内存；按原始字节数排序。");
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setAlternatingRowColors(true);
    setShowGrid(false);
    setFrameShape(QFrame::NoFrame);
    setTextElideMode(Qt::ElideRight);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    verticalHeader()->hide();
    UiZoom::instance().bind(this,"rowHeight",[this]{verticalHeader()->setDefaultSectionSize(qMax(uiPx(compact?27:30),fontMetrics().height()+uiPx(8)));});
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    const QList<int> widths = compact ? QList<int>{102,60,110} : QList<int>{94,240,150,155,195,120};
    for (int col = 0; col < columnCount(); ++col) {
        setColumnWidth(col, uiPx(widths[col]));
        if ((compact && col > 0) || (!compact && (col == 0 || col == 3 || col == 4)))
            horizontalHeaderItem(col)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    const auto saved = QSettings().value(widthSettingsKey()).toList();
    if (saved.size() == columnCount() &&
        std::all_of(saved.begin(), saved.end(), [](const QVariant &v) {
            bool ok = false;
            const int n = v.toInt(&ok);
            return ok && n >= 60 && n <= 4096;
        })) {
        customWidths = true;
        for (int col = 0; col < columnCount(); ++col)
            setColumnWidth(col, uiPx(saved[col].toInt()));
    }
    for(int col=0;col<columnCount();++col)baseWidths.append(saved.size()==columnCount()&&customWidths?saved[col].toDouble():widths[col]);
    UiZoom::instance().bind(this,"columns",[this]{QSignalBlocker block(horizontalHeader());for(int col=0;col<columnCount();++col)setColumnWidth(col,uiPx(baseWidths[col]));});
    connect(&UiPreferences::instance(), &UiPreferences::aboutToReset, this, [this]{widthSaveTimer.stop();});
    connect(&UiPreferences::instance(), &UiPreferences::defaultsRestored, this, [this] {
        widthSaveTimer.stop(); customWidths=false;
        baseWidths=compact ? QList<qreal>{102,60,110} : QList<qreal>{94,240,150,155,195,120};
        QSignalBlocker blocked(horizontalHeader());
        for(int col=0;col<columnCount();++col)setColumnWidth(col,uiPx(baseWidths[col]));
        viewport()->update();
    });
    widthSaveTimer.setSingleShot(true);
    widthSaveTimer.setInterval(250);
    connect(&widthSaveTimer, &QTimer::timeout, this, &ProcessTable::saveWidths);
    connect(horizontalHeader(), &QHeaderView::sectionResized, this, [this](int column,int,int width) {
        if (adjustingWidths || UiZoom::instance().changing() || UiPreferences::instance().resetting())
            return;
        baseWidths[column]=uiBase(width);
        customWidths = true;
        widthSaveTimer.start();
    });
    horizontalHeader()->setSortIndicator(compact ? 1 : 3, Qt::DescendingOrder);
    setSortingEnabled(true);
    connect(&UiZoom::instance(),&UiZoom::changed,this,[this]{QResizeEvent e(size(),size());resizeEvent(&e);viewport()->update();});
}
ProcessTable::~ProcessTable() {
    if (widthSaveTimer.isActive())
        saveWidths();
}
QString ProcessTable::widthSettingsKey() const {
    return compact ? "process-table/compact/widths-v1" : "process-table/full/widths-v1";
}
void ProcessTable::saveWidths() {
    QVariantList widths;
    for (int col = 0; col < columnCount(); ++col)
        widths.append(baseWidths[col]);
    QSettings().setValue(widthSettingsKey(), widths);
}
void ProcessTable::resizeEvent(QResizeEvent *event) {
    QTableWidget::resizeEvent(event);
    if (!customWidths) {
        const int nameColumn = compact ? 0 : 1;
        int remaining = viewport()->width();
        for (int col = 0; col < columnCount(); ++col)
            if (col != nameColumn)
                remaining -= columnWidth(col);
        adjustingWidths = true;
        setColumnWidth(nameColumn, qMax(uiPx(compact ? 102 : 200), remaining));
        adjustingWidths = false;
    }
    // Clip all child surfaces, including the header/scrollbars, inside the card's outer border.
    // The outer ProcessCard owns the complete contour, including an optional
    // title above the table. Its border layer clips only the outside corners.
    clearMask();
}
void ProcessTable::applySnapshot(const QVector<ProcessInfo> &snapshot, quint64 memoryTotal) {
    DiagnosticScope snapshotScope(DiagEvent::ProcessSnapshotBegin,DiagEvent::ProcessSnapshotEnd,0,0,{},snapshot.size());
    QSet<qint64> selected;
    for (const auto &index : selectionModel()->selectedRows())
        selected.insert(pidAt(*this, index.row()));
    const qint64 current = pidAt(*this, currentRow());
    const int currentCol = currentColumn();
    const int scrollX = horizontalScrollBar()->value(), scrollY = verticalScrollBar()->value();
    const int sortColumn = horizontalHeader()->sortIndicatorSection();
    const auto sortOrder = horizontalHeader()->sortIndicatorOrder();
    const bool sorted = isSortingEnabled(), updates = updatesEnabled();
    QSignalBlocker tableSignals(this), selectionSignals(selectionModel());
    setUpdatesEnabled(false);
    setSortingEnabled(false);

    QSet<qint64> incoming;
    for (const auto &process : snapshot)
        if (process.pid > 0)
            incoming.insert(process.pid);
    for (int row = rowCount() - 1; row >= 0; --row)
        if (!incoming.contains(pidAt(*this, row)))
            removeRow(row);
    QHash<qint64, int> rowFor;
    for (int row = 0; row < rowCount(); ++row)
        rowFor.insert(pidAt(*this, row), row);
    for (const auto &process : snapshot) {
        if (process.pid <= 0)
            continue;
        int row = rowFor.value(process.pid, -1);
        if (row < 0) {
            row = rowCount();
            insertRow(row);
            rowFor.insert(process.pid, row);
            for (int col = 0; col < columnCount(); ++col)
                setItem(row, col, new ProcessItem);
        }
        const QString memory = humanSize(process.rss) +
            (memoryTotal ? QString(" (%1%)").arg(100.0 * double(process.rss) / double(memoryTotal), 0, 'f', 1)
                         : QString());
        const QStringList values = compact
            ? QStringList{process.name, QString::number(process.cpu, 'f', 1) + "%", humanSize(process.rss)}
            : QStringList{QString::number(process.pid), process.name, process.user,
                          QString::number(process.cpu, 'f', 1), humanSize(process.rss),
                          process.state.startsWith('R')?QString("运行中"):process.state.startsWith('S')||process.state.startsWith('I')?QString("睡眠"):process.state};
        for (int col = 0; col < columnCount(); ++col) {
            auto entry = item(row, col);
            entry->setText(values[col]);
            entry->setData(ProcessIdRole, process.pid);
            entry->setData(Qt::UserRole+10,process.state);
            QVariant numeric;
            if (!compact && col == 0)
                numeric = process.pid;
            if (col == (compact ? 1 : 3))
                numeric = process.cpu;
            if (col == (compact ? 2 : 4))
                numeric = QVariant::fromValue(process.rss);
            entry->setData(Qt::UserRole, numeric);
            entry->setTextAlignment((numeric.isValid() ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
            entry->setToolTip(QString("%1\nPID %2 · %3\n内存 %4")
                                  .arg(process.name, QString::number(process.pid), process.user, memory));
        }
    }
    horizontalHeader()->setSortIndicator(sortColumn, sortOrder);
    setSortingEnabled(sorted);
    QItemSelection restored;
    QModelIndex currentIndex;
    for (int row = 0; row < rowCount(); ++row) {
        const auto pid = pidAt(*this, row);
        if (selected.contains(pid))
            restored.select(model()->index(row, 0), model()->index(row, columnCount() - 1));
        if (pid == current && currentCol >= 0)
            currentIndex = model()->index(row, currentCol);
    }
    selectionModel()->select(restored, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    // Setting the selection model's current index avoids QTableWidget's scroll-to-current behavior.
    selectionModel()->setCurrentIndex(currentIndex, QItemSelectionModel::NoUpdate);
    updateGeometries();
    horizontalScrollBar()->setValue(scrollX);
    verticalScrollBar()->setValue(scrollY);
    setUpdatesEnabled(updates);
    applyFilter();
}
void ProcessTable::setFilterText(const QString &text) {filter=text.trimmed();applyFilter();}
int ProcessTable::visibleProcessCount() const {int n=0;for(int r=0;r<rowCount();++r)n+=!isRowHidden(r);return n;}
void ProcessTable::applyFilter() {
    for(int r=0;r<rowCount();++r) {
        const bool match=filter.isEmpty()||item(r,compact?0:1)->text().contains(filter,Qt::CaseInsensitive)||
            (!compact&&(item(r,0)->text().contains(filter,Qt::CaseInsensitive)||item(r,2)->text().contains(filter,Qt::CaseInsensitive)));
        setRowHidden(r,!match);
    }
    if(countsChanged)countsChanged(visibleProcessCount(),rowCount());viewport()->update();
}
void ProcessTable::paintEvent(QPaintEvent *event) {
    QTableWidget::paintEvent(event);
    if(!compact&&!visibleProcessCount()) {QPainter p(viewport());p.setPen(colors().muted);p.drawText(viewport()->rect().adjusted(uiPx(12),0,-uiPx(12),0),Qt::AlignCenter,filter.isEmpty()?"暂无已采集进程":"没有匹配的已采集进程");}
}
QWidget *processCard(QAbstractItemView *table,QWidget *header) {
    return new ProcessCard(table,header);
}
} // namespace rs
