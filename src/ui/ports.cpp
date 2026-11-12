#include "messagebox.h"
#include "ports.h"
#include "icons.h"
#include "contentfont.h"
#include "processtable.h"
#include "filecontextmenu.h"
#include "../core/operationgate.h"
#include <QHostAddress>
namespace rs {
QString tunnelEndpoint(const QString &address, quint16 port) {
    return (address.contains(':') ? "[" + address + "]" : address) + ":" + QString::number(port);
}
namespace {
QString speed(quint64 bytes) {
    if(bytes<1024)return QString::number(bytes)+" B/s";
    if(bytes<1024*1024)return QString::number(bytes/1024.0,'f',1)+" KiB/s";
    return QString::number(bytes/(1024.0*1024),'f',1)+" MiB/s";
}
QString shortState(const QString &state) {
    if (state == "connecting") return "连接中…";
    if (state == "starting") return "启动中…";
    if (state == "stopping") return "停止中…";
    if (state == "running") return "已建立";
    if (state == "failed") return "启动失败";
    return "已停止";
}
bool preserveDraft(QWidget *widget) {
    for (auto w = widget; w; w = w->parentWidget())
        if (w->property("preservePortDraft").toBool() || qobject_cast<QTabBar *>(w)) return true;
    return false;
}
}
class PortsModel : public QAbstractTableModel {
  public:
    explicit PortsModel(PortsPanel *owner) : QAbstractTableModel(owner), panel(owner) {}
    QVector<TunnelRule> values;
    PortsPanel *panel;
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(values.size()) + (panel->draft && panel->draft->id.isEmpty() ? 1 : 0) + 1;
    }
    int columnCount(const QModelIndex & = {}) const override { return 5; }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
            return QStringList{"本地端口", "远端端口", "上传速率", "下载速率", "状态"}.value(section);
        return {};
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid()) return {};
        if (index.row() >= values.size()) {
            if (role == Qt::SizeHintRole) return uiSize(80, index.row() == rowCount()-1 ? 40 : 112);
            return {};
        }
        const auto &rule = values[index.row()]; const auto status = panel->runtime.value(rule.id);
        if (role == Qt::UserRole) return rule.id;
        if (role == Qt::CheckStateRole && index.column() == 4)
            return status.state == "running" ? Qt::Checked : Qt::Unchecked;
        if (role == Qt::DisplayRole) {
            if (index.column() == 0) return rule.localPort;
            if (index.column() == 1) return rule.remotePort;
            if (index.column() == 2) return speed(status.upload);
            if (index.column() == 3) return speed(status.download);
            return shortState(status.state)+(status.intentError.isEmpty()?QString():QString(" · 未保存"));
        }
        if (role == Qt::AccessibleTextRole || role == Qt::AccessibleDescriptionRole)
            return QString("%1 → %2，%3，%4").arg(tunnelEndpoint(rule.localAddress,rule.localPort),
                   tunnelEndpoint(rule.remoteAddress,rule.remotePort),shortState(status.state),status.reason+"\n"+status.intentError);
        if (role == Qt::ToolTipRole) {
            if(index.column()==2||index.column()==3)return "仅统计通过本工具此转发规则的数据，并非服务器端口总流量。";
            if (index.column() < 2) return index.column() == 0 ? tunnelEndpoint(rule.localAddress,rule.localPort)
                                                                             : tunnelEndpoint(rule.remoteAddress,rule.remotePort);
            const auto intent=rule.autoStart?QString("下次连接将自动恢复；可在菜单中取消自动恢复。"):QString("未设置自动恢复；成功启用后会记住。");
            return (status.state == "running" ? QString("停止转发并结束其连接\n已建立 SSH 和本地监听；不代表目标业务健康。\n") : QString())+
                   intent+"\n"+status.reason+"\n"+status.intentError;
        }
        if (role == Qt::SizeHintRole) {
            const int line = qMax(uiPx(22), QFontMetrics(panel->table->font()).height());
            return QSize(uiPx(80), panel->draft && panel->draft->id == rule.id ? qMax(uiPx(118), 3*line+uiPx(14))
                         : rule.localAddress!="127.0.0.1"||rule.remoteAddress!="127.0.0.1" ? 2*line+uiPx(8) : qMax(uiPx(32),line+uiPx(8)));
        }
        return {};
    }
    Qt::ItemFlags flags(const QModelIndex &index) const override {
        auto result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (index.column() == 4 && index.row() < values.size() && !panel->busy(values[index.row()].id))
            result |= Qt::ItemIsUserCheckable;
        return result;
    }
    void reset() { beginResetModel(); endResetModel(); }
};
class PortsDelegate : public QStyledItemDelegate {
  public:
    explicit PortsDelegate(PortsPanel *owner) : QStyledItemDelegate(owner), panel(owner) {}
    PortsPanel *panel;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        if (index.row() >= panel->model->values.size()) return;
        const auto &rule = panel->model->values[index.row()];
        if (panel->draft && panel->draft->id == rule.id) return;
        const auto c = colors(); const auto status = panel->runtime.value(rule.id);
        p->save();
        p->setFont(option.font);
        p->setClipRect(option.rect);
        p->fillRect(option.rect, option.state & QStyle::State_Selected ? c.processSelection : index.row()%2?c.processAlternate:c.processBody);
        auto rect = option.rect.adjusted(uiPx(10),uiPx(3),-uiPx(10),-uiPx(3)); p->setPen(c.text);
        const int line = qMax(uiPx(22), QFontMetrics(option.font).height());
        if (index.column() < 2) {
            const auto address = index.column() == 0 ? rule.localAddress : rule.remoteAddress;
            const auto port = index.column() == 0 ? rule.localPort : rule.remotePort;
            p->drawText(QRect(rect.left(),rect.top(),rect.width(),line), Qt::AlignLeft|Qt::AlignVCenter,QString::number(port));
            if (address != "127.0.0.1") {
                p->setPen(c.muted);
                p->drawText(QRect(rect.left(),rect.top()+line+uiPx(1),rect.width(),line),Qt::AlignLeft|Qt::AlignVCenter,
                            p->fontMetrics().elidedText(tunnelEndpoint(address,port),Qt::ElideMiddle,rect.width()));
            }
        } else if(index.column()==4) {
            paintToggle(*p,QRectF(rect.left(),rect.center().y()-uiPx(10),uiPx(36),uiPx(20)),status.state == "running" ? 1 : 0,
                        !panel->busy(rule.id),option.state & QStyle::State_HasFocus);
            p->setPen(!status.intentError.isEmpty()?c.orange:status.state=="failed"?c.red:status.state=="running"?c.green:c.muted);
            const auto statusRect = QRect(rect.left()+uiPx(44),rect.top(),rect.width()-uiPx(44),rect.height());
            p->drawText(statusRect,Qt::AlignVCenter,p->fontMetrics().elidedText(index.data().toString(),Qt::ElideRight,statusRect.width()));
        } else {p->setPen(index.column()==2?networkSendColor():networkReceiveColor());p->drawText(rect,Qt::AlignLeft|Qt::AlignVCenter,index.data(Qt::DisplayRole).toString());}
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override {
        return index.data(Qt::SizeHintRole).toSize();
    }
};
PortsPanel::PortsPanel(const QString &id, bool offline, QWidget *parent)
    : QWidget(parent), profileId(id), offline(offline) {
    setProperty("rabbitUi",true);
    setObjectName("portsPanel"); setAccessibleName("本地端口转发");
    auto layout = new QVBoxLayout(this); uiMargins(layout,0,0,0,0); uiSpacing(layout,0);
    storageError = new QLabel; storageError->setWordWrap(true); storageError->setTextFormat(Qt::PlainText);
    storageError->hide(); layout->addWidget(storageError);
    table = new QTableView; table->setObjectName("portsTable"); table->setAccessibleName("本地端口、远端端口、上传速率、下载速率、状态");table->setProperty("processTable",true);
    table->setHorizontalHeader(processHeader(false,table));table->horizontalHeader()->setSortIndicatorShown(false);table->setAlternatingRowColors(true);
    model = new PortsModel(this); QString error; model->values = loadTunnelRules(id,&error); loaded = error.isEmpty();
    if (!loaded) { storageError->setText(error); storageError->show(); }
    table->setModel(model); table->setItemDelegate(new PortsDelegate(this));
    table->setShowGrid(false); table->verticalHeader()->hide(); table->horizontalHeader()->setHighlightSections(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
    // Editing uses the custom draft row, not QAbstractItemView's delegate editor.
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table->setContextMenuPolicy(Qt::CustomContextMenu); uiMinimumHeight(table,100);table->setFrameShape(QFrame::NoFrame);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);tableCard=processCard(table);tableCard->setObjectName("portsTableCard");layout->addWidget(tableCard,1);
    empty = new QLabel("暂无端口转发"); empty->setProperty("muted",true); empty->setAlignment(Qt::AlignCenter);
    layout->addWidget(empty,1);
    emptyAdd = button("添加端口"); emptyAdd->setIcon(icon(Icon::Add)); emptyAdd->setObjectName("addPort");
    layout->addWidget(emptyAdd,0,Qt::AlignHCenter); connect(emptyAdd,&QPushButton::clicked,this,[this]{beginEdit();});
    connect(table,&QTableView::clicked,this,[this](const QModelIndex &index){
        const auto id = index.data(Qt::UserRole).toString(); if (id.isEmpty() || busy(id)) return;
        if(index.column()==4) {
            const auto rect = table->visualRect(index); const auto point = table->property("portsClickPosition").toPoint();
            if(QRect(rect.left()+uiPx(4),rect.center().y()-uiPx(14),uiPx(48),uiPx(28)).contains(point))toggleRule(id);
        }
    });
    connect(table,&QTableView::doubleClicked,this,[this](const QModelIndex &index){
        const auto id=index.data(Qt::UserRole).toString();
        if(index.column()<2&&!id.isEmpty()&&!busy(id))beginEdit(id,index.column());
    });
    connect(table,&QWidget::customContextMenuRequested,this,[this](QPoint point){
        auto id=table->indexAt(point).data(Qt::UserRole).toString();
        if (!id.isEmpty() && (!draft || commitDraft())) showMenu(id,table->viewport()->mapToGlobal(point));
    });
    connect(table->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{positionDraft();});
    connect(table->horizontalHeader(),&QHeaderView::sectionResized,this,[this]{positionDraft();});
    qApp->installEventFilter(this); table->installEventFilter(this);
    connect(table->horizontalScrollBar(),&QScrollBar::valueChanged,this,[this]{positionDraft();});
    bindContentFont(table,ContentFontRole::List);
    refreshRows();
}
PortsPanel::~PortsPanel() { qApp->removeEventFilter(this); }
QVector<TunnelRule> PortsPanel::rules() const { return model->values; }
std::optional<TunnelRule> PortsPanel::rule(const QString &id) const {
    for (const auto &r : model->values) if (r.id == id) return r; return std::nullopt;
}
QString PortsPanel::state(const QString &id) const { return runtime.value(id).state; }
bool PortsPanel::running(const QString &id) const { return state(id) == "running"; }
bool PortsPanel::busy(const QString &id) const {
    const auto s = state(id); return s == "connecting" || s == "starting" || s == "stopping";
}
int PortsPanel::activeCount() const {
    int count=0; for (const auto &r : model->values) if (running(r.id)||busy(r.id)) ++count; return count;
}
int PortsPanel::rowFor(const QString &id) const {
    for (int n=0;n<model->values.size();++n) if (model->values[n].id == id) return n;
    return draft && draft->id.isEmpty() ? int(model->values.size()) : -1;
}
bool PortsPanel::rememberEnabled(const QString &id, bool enabled) {
    const auto value=rule(id);if(!value)return false;
    bool ok=true;
    if(value->autoStart!=enabled) {
        auto candidate=model->values;
        for(auto &r:candidate)if(r.id==id)r.autoStart=enabled;
        ok=save(candidate);
    }
    auto &status=runtime[id];
    status.intentError=ok?QString():enabled?QString("启用状态未保存；下次连接不会自动恢复。"):QString("关闭状态未保存；下次连接仍会尝试自动恢复。");
    if(!ok)storageError->setText(status.intentError+"\n"+storageError->text());
    const int row=rowFor(id);if(row>=0)emit model->dataChanged(model->index(row,0),model->index(row,4));
    return ok;
}
void PortsPanel::setRuleState(const QString &id, const QString &state, const QString &reason) {
    if (!rule(id)) return;
    auto &value=runtime[id];value.state=state;value.reason=reason.left(1024);
    if(state!="running")value.upload=value.download=0;
    if(draft && draft->id==id)syncDraftState();
    if (pendingDelete.contains(id) && (state == "stopped" || state == "failed")) {
        pendingDelete.remove(id); auto candidate=model->values;
        candidate.removeIf([&](const auto &r){return r.id == id;});
        if (save(candidate)) {
            runtime.remove(id);
            if (draft && draft->id == id) cancelDraft(); else refreshRows();
        } else {
            const int row=rowFor(id);emit model->dataChanged(model->index(row,0),model->index(row,4));
            table->resizeRowToContents(row);positionDraft();
        }
        return;
    }
    const int row=rowFor(id); emit model->dataChanged(model->index(row,0),model->index(row,4));
    table->resizeRowToContents(row); positionDraft();
}
void PortsPanel::setRuleTraffic(const QString &id, quint64 upload, quint64 download) {
    if(!rule(id)||!running(id))return;
    auto &value=runtime[id];value.upload=upload;value.download=download;
    const int row=rowFor(id);if(row>=0)emit model->dataChanged(model->index(row,2),model->index(row,3),{Qt::DisplayRole});
}
bool PortsPanel::ensureMapping(const QString &address, quint16 port) {
    if(!loaded||!port||draft||OperationGate::locked())return false;
    QString target=address;
    if(target=="0.0.0.0"||target=="*")target="127.0.0.1";
    else if(target=="::"||target=="[::]")target="::1";
    QString id;
    for(const auto &r:model->values)if(r.remoteAddress==target&&r.remotePort==port){id=r.id;break;}
    if(id.isEmpty()) {
        QSet<quint16> used;for(const auto &r:model->values)if(r.localAddress=="127.0.0.1")used.insert(r.localPort);
        quint32 local=port;while(local<=65535&&used.contains(quint16(local)))++local;
        if(local>65535){local=1;while(local<port&&used.contains(quint16(local)))++local;}
        if(local>65535||used.contains(quint16(local)))return false;
        TunnelRule rule;rule.profileId=profileId;rule.localPort=quint16(local);rule.remotePort=port;rule.remoteAddress=target;
        auto candidate=model->values;candidate.append(rule);if(!save(candidate))return false;id=rule.id;refreshRows();
    }
    const int row=rowFor(id);if(row<0)return false;
    table->selectRow(row);table->scrollTo(model->index(row,0));return true;
}
bool PortsPanel::save(const QVector<TunnelRule> &candidate) {
    QString error;
    if (!loaded || !saveTunnelRules(profileId,candidate,&error)) {
        if (error.isEmpty()) error="规则文件未能读取，未覆盖原配置。";
        storageError->setText(error); storageError->show(); return false;
    }
    model->values=candidate; storageError->hide(); emit configurationChanged(); return true;
}
bool PortsPanel::validPort(const QString &value) {
    if (value.isEmpty()) return false;
    for (auto c : value) if (c < '0' || c > '9') return false;
    bool ok=false; const auto number=value.toUInt(&ok); return ok && number>=1 && number<=65535;
}
void PortsPanel::beginEdit(const QString &id, int column) {
    if (!loaded || (!id.isEmpty() && busy(id))) return;
    if (draft && draft->id != id) {
        if (running(draft->id)) { reportDraft("请先应用或取消当前修改"); return; }
        if (!commitDraft()) return;
    }
    if (draft) { (column ? remoteInput : localInput)->setFocus(); return; }
    const auto current=rule(id);
    draft=Draft{id,current?QString::number(current->localPort):QString(),current?QString::number(current->remotePort):QString(),bool(current)};
    draftRow=new QWidget(table->viewport()); draftRow->setObjectName("portDraftRow"); draftRow->setAutoFillBackground(true);
    auto row=new QHBoxLayout(draftRow); uiMargins(row,0,0,0,0); uiSpacing(row,0);
    auto field=[&](const QString &name, const QString &value, QLineEdit **input, QLabel **error) {
        auto cell=new QWidget; auto layout=new QVBoxLayout(cell); uiMargins(layout,8,7,8,7); uiSpacing(layout,2);
        *input=new QLineEdit(value); (*input)->setAccessibleName(name); (*input)->setObjectName(name=="本地端口"?"localPort":"remotePort");
        (*input)->setMaxLength(64); (*input)->setPlaceholderText(name=="本地端口"?"同远端":"1–65535");
        (*input)->setAttribute(Qt::WA_InputMethodEnabled,false); layout->addWidget(*input);
        bindContentFont(*input,ContentFontRole::List);
        *error=new QLabel; (*error)->setWordWrap(true); (*error)->setTextFormat(Qt::PlainText); layout->addWidget(*error); layout->addStretch();
        row->addWidget(cell); return cell;
    };
    field("本地端口",draft->local,&localInput,&localError); field("远端端口",draft->remote,&remoteInput,&remoteError);
    auto gap=new QWidget;gap->setObjectName("portDraftRateGap");row->addWidget(gap);
    auto cell=new QWidget; auto layout=new QVBoxLayout(cell); uiMargins(layout,8,7,8,7); uiSpacing(layout,2);
    draftToggle=new Toggle; draftToggle->setObjectName("draftPortToggle"); draftToggle->setAccessibleName(offline?"连接并启动":"保存并启用端口转发");
    draftToggle->syncChecked(current && running(id)); draftToggle->setEnabled(!current || !running(id)); layout->addWidget(draftToggle);
    auto actions=new QBoxLayout(QBoxLayout::LeftToRight);draftActions=actions;uiSpacing(actions,2);
    auto apply=button(current && running(id)?"应用并重启":"保存"),cancel=button("取消");
    bindUiFont(apply); bindUiFont(cancel);
    apply->setObjectName("applyPortDraft"); cancel->setObjectName("cancelPortDraft");
    actions->addWidget(apply); actions->addWidget(cancel); actions->addStretch(); layout->addLayout(actions);
    draftMessage=new QLabel; draftMessage->setTextFormat(Qt::PlainText); draftMessage->setWordWrap(true);draftMessage->hide();layout->addWidget(draftMessage); row->addWidget(cell);
    connect(localInput,&QLineEdit::textEdited,this,[this](const QString &s){if(draft){draft->local=s;draft->manualLocal=true;localError->clear();}});
    connect(remoteInput,&QLineEdit::textEdited,this,[this](const QString &s){if(draft){draft->remote=s;remoteError->clear();if(!draft->manualLocal){draft->local=s;localInput->setText(s);}}});
    connect(apply,&QPushButton::clicked,this,[this]{
        if (!draft) return;
        if (running(draft->id)) {
            if(startGuard){const auto error=startGuard();if(!error.isEmpty()){reportDraft(error);return;}}
            if (!validateDraft()) return; const auto id=draft->id; auto candidate=model->values;
            auto it=std::find_if(candidate.begin(),candidate.end(),[&](const auto &r){return r.id==id;});
            if(it==candidate.end())return;it->localPort=quint16(draft->local.toUInt());it->remotePort=quint16(draft->remote.toUInt());
            const auto changed=*it;
            if(save(candidate)){cancelDraft();emit restartRequested(changed);}
        } else commitDraft();
    });
    connect(cancel,&QPushButton::clicked,this,&PortsPanel::cancelDraft);
    connect(draftToggle,&QAbstractButton::clicked,this,[this]{if(!commitDraft(true) && draftToggle)draftToggle->syncChecked(false);});
    refreshRows(); (column ? remoteInput : localInput)->setFocus();
}
void PortsPanel::reportDraft(const QString &text) {if(draftMessage){draftMessage->setText(text);draftMessage->setVisible(!text.isEmpty());positionDraft();}}
bool PortsPanel::validateDraft() {
    if (!draft) return false;
    localError->setText(validPort(draft->local)?QString():draft->local.isEmpty()?"请输入端口":"请输入 1–65535 的整数");
    remoteError->setText(validPort(draft->remote)?QString():draft->remote.isEmpty()?"请输入端口":"请输入 1–65535 的整数");
    localError->setVisible(!localError->text().isEmpty());remoteError->setVisible(!remoteError->text().isEmpty());
    refreshTheme();positionDraft();return localError->text().isEmpty() && remoteError->text().isEmpty();
}
bool PortsPanel::commitDraft(bool start) {
    if (!draft || committing) return true;
    if (running(draft->id)) return true;
    QScopedValueRollback<bool> guard(committing,true);
    if (draft->id.isEmpty() && draft->local.isEmpty() && draft->remote.isEmpty() && !start) { cancelDraft(); return true; }
    if (!validateDraft()) return false;
    if(start && startGuard){const auto error=startGuard();if(!error.isEmpty()){reportDraft(error);return false;}}
    auto value=rule(draft->id).value_or(TunnelRule{});
    if (value.id.isEmpty()) value.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    value.profileId=profileId; value.localPort=quint16(draft->local.toUInt()); value.remotePort=quint16(draft->remote.toUInt());
    auto candidate=model->values; const int row=rowFor(draft->id);
    if (draft->id.isEmpty()) candidate.append(value);
    else if(row>=0 && row<candidate.size()) candidate[row]=value;
    else {reportDraft("规则已删除，请取消此草稿。");return false;}
    if (!save(candidate)) return false;
    cancelDraft(); if (start) emit startRequested(value); return true;
}
void PortsPanel::cancelDraft() {
    draft.reset(); if(draftRow){draftRow->hide();draftRow->deleteLater();draftRow=nullptr;}
    localInput=remoteInput=nullptr; localError=remoteError=draftMessage=nullptr; draftToggle=nullptr;draftActions=nullptr;refreshRows();
}
void PortsPanel::refreshRows() {
    model->reset(); const bool isEmpty=model->values.isEmpty()&&!draft;
    tableCard->setVisible(!isEmpty);table->setVisible(!isEmpty); empty->setVisible(isEmpty); emptyAdd->setVisible(isEmpty); emptyAdd->setEnabled(loaded);
    table->clearSpans(); table->resizeRowsToContents();
    if (!isEmpty) {
        const int last=model->rowCount()-1; table->setSpan(last,0,1,5);
        auto add=button("添加端口"); add->setIcon(icon(Icon::Add)); add->setObjectName("addPort");
        bindUiFont(add);
        add->setStyleSheet("text-align:left;"); add->setEnabled(loaded); table->setIndexWidget(model->index(last,0),add);
        connect(add,&QPushButton::clicked,this,[this]{beginEdit();});
        if (draft) table->scrollTo(model->index(rowFor(draft->id),0));
    }
    sizeColumns(); refreshTheme();
}
void PortsPanel::positionDraft() {
    if (!draftRow || !draft) return;
    const int row=rowFor(draft->id);if(row<0)return;
    const auto rect=table->visualRect(model->index(row,0));
    int total=0;for(int col=0;col<5;++col)total+=table->columnWidth(col);
    const int width=qMax(table->viewport()->width(),total);
    if(draftActions)draftActions->setDirection(table->columnWidth(4)<uiPx(170)?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);
    draftRow->setGeometry(table->columnViewportPosition(0),rect.top(),width,table->rowHeight(row));
    const auto children=draftRow->layout();
    for (int n=0;n<2;++n) children->itemAt(n)->widget()->setFixedWidth(table->columnWidth(n));
    children->itemAt(2)->widget()->setFixedWidth(table->columnWidth(2)+table->columnWidth(3));
    children->activate();
    const int height=qMax(uiPx(112),qMax(draftRow->minimumSizeHint().height(),children->heightForWidth(width)));
    if(table->rowHeight(row)!=height)table->setRowHeight(row,height);
    draftRow->resize(width,height);
    draftRow->show();draftRow->raise();
}
void PortsPanel::syncDraftState() {
    if(!draft || !draftToggle)return;
    draftToggle->syncChecked(running(draft->id));draftToggle->setEnabled(!busy(draft->id)&&!running(draft->id));
    if(auto apply=draftRow->findChild<QPushButton *>("applyPortDraft"))apply->setText(running(draft->id)?"应用并重启":"保存");
}
void PortsPanel::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
}
void PortsPanel::sizeColumns() {
    const int width=table->viewport()->width();if(width<1)return;
    const QFontMetrics fm(table->font());
    const int port=qMax(qMax(uiPx(80),fm.horizontalAdvance("65535")+uiPx(24)),fm.horizontalAdvance("本地端口")+uiPx(20));
    const int state=qMax(uiPx(134),fm.horizontalAdvance("启动失败")+uiPx(64));
    const int rate=qMax(uiPx(100),fm.horizontalAdvance("上传速率")+uiPx(20));
    const int surplus=qMax(0,width-port*2-rate*2-state);
    {QSignalBlocker blocked(table->horizontalHeader());
     table->setColumnWidth(0,port+surplus/4);table->setColumnWidth(1,port+surplus/4);table->setColumnWidth(2,rate+surplus/4);table->setColumnWidth(3,rate+surplus-surplus/4*3);table->setColumnWidth(4,state);}
    positionDraft();
}
void PortsPanel::refreshTheme() {
    table->viewport()->update();
    for(auto label:{storageError,localError,remoteError,draftMessage}) if(label) label->setStyleSheet("color:"+colors().red.name());
}
void PortsPanel::scheduleFontLayout() {
    if(fontLayoutPending)return;fontLayoutPending=true;
    QTimer::singleShot(0,this,[this]{fontLayoutPending=false;table->horizontalHeader()->setFont(table->font());table->resizeRowsToContents();sizeColumns();table->viewport()->update();});
}
void PortsPanel::toggleRule(const QString &id) {
    if (busy(id)) return;
    if (running(id)) emit stopRequested(id);
    else if (const auto value=rule(id)) emit startRequested(*value);
}
void PortsPanel::showMenu(const QString &id, QPoint global) {
    if (busy(id) || !rule(id)) return;
    QScopedValueRollback<bool> guard(dialogOpen,true);
    FileContextMenu menu(this); menu.setObjectName("portMenu");
    if(offline) {
        menu.addAction("连接并启动",this,[this,id]{if(auto r=rule(id))emit startRequested(*r);});
        menu.addSeparator();
    }
    menu.addAction("编辑端口",this,[this,id]{beginEdit(id);});
    menu.addAction("高级设置",this,[this,id]{advanced(id);});
    if(rule(id)->autoStart && !running(id))
        menu.addAction("取消自动恢复",this,[this,id]{if(!OperationGate::locked())emit stopRequested(id);});
    menu.addSeparator();
    menu.addAction("复制本地地址",this,[this,id]{if(auto r=rule(id))QApplication::clipboard()->setText(tunnelEndpoint(r->localAddress,r->localPort));});
    menu.addSeparator();
    menu.addAction("删除",this,[this,id]{remove(id);}); menu.exec(global);
}
void PortsPanel::advanced(const QString &id) {
    const auto value=rule(id); if(!value || busy(id))return;
    QScopedValueRollback<bool> guard(dialogOpen,true);
    QDialog dialog(this);dialog.setObjectName("portAdvanced");dialog.setWindowTitle("高级设置 · "+QString::number(value->localPort));
    auto layout=new QVBoxLayout(&dialog);auto form=new QFormLayout;
    auto local=new QLineEdit(value->localAddress),remote=new QLineEdit(value->remoteAddress),name=new QLineEdit(value->name);
    local->setObjectName("localAddress");remote->setObjectName("remoteAddress");
    for(auto field:{local,remote,name})field->setMaxLength(253);
    form->addRow("本地监听地址",local);form->addRow("远端目标地址",remote);form->addRow("备注",name);layout->addLayout(form);
    auto restore=new QLabel("成功启用后会记住，并随工作区连接恢复；手动关闭后不再自动恢复。");
    restore->setWordWrap(true);layout->addWidget(restore);
    auto scope=new QLabel;scope->setWordWrap(true);layout->addWidget(scope);
    auto updateScope=[=]{QHostAddress address(local->text().trimmed());scope->setText(address.isLoopback()?"监听仅限本机。":"非回环监听：其他设备可能访问此本地端口。");};
    connect(local,&QLineEdit::textChanged,&dialog,updateScope);updateScope();
    if(running(id)){auto message=new QLabel("应用后将停止本条转发并结束其连接，再用新配置启动。");message->setWordWrap(true);layout->addWidget(message);}
    auto error=new QLabel;error->setObjectName("portDialogError");error->setWordWrap(true);error->setStyleSheet("color:"+colors().red.name());layout->addWidget(error);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Save)->setText(running(id)?"应用并重启":"保存");layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        if(!rule(id) || busy(id)){error->setText("规则状态已变化，请取消后重新打开设置。");return;}
        if(running(id) && startGuard){const auto message=startGuard();if(!message.isEmpty()){error->setText(message);return;}}
        QHostAddress address;const auto target=remote->text().trimmed();
        if(!address.setAddress(local->text().trimmed())){error->setText("本地监听地址须为有效 IPv4 或 IPv6 地址");return;}
        QHostAddress targetAddress; const auto ace=QString::fromLatin1(QUrl::toAce(target));
        static const QRegularExpression hostname("^(?=.{1,253}$)[a-zA-Z0-9](?:[a-zA-Z0-9.-]*[a-zA-Z0-9])?$");
        if(!targetAddress.setAddress(target) && (!hostname.match(ace).hasMatch() || target.contains("..") || QRegularExpression("^[0-9.]+$").match(target).hasMatch())) {
            error->setText("远端目标须为有效 IP 地址或域名");return;
        }
        auto candidate=model->values;auto it=std::find_if(candidate.begin(),candidate.end(),[&](const auto &r){return r.id==id;});if(it==candidate.end())return;
        it->localAddress=address.toString();it->remoteAddress=targetAddress.isNull()?ace:targetAddress.toString();it->name=name->text().trimmed();
        const auto changed=*it;const bool restart=running(id);
        if(save(candidate)){dialog.accept();refreshRows();if(restart)emit restartRequested(changed);}
    });
    dialog.resize(420,300);dialog.exec();
}
void PortsPanel::remove(const QString &id) {
    if (busy(id)) return;
    if (running(id)) {
        if(AppMessageBox::warning(this,"停止并删除端口",QString("停止端口 %1 的转发并结束其连接，然后删除规则？").arg(rule(id)->localPort),
                               QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
        if (!rule(id) || busy(id)) return;
        pendingDelete.insert(id);emit stopRequested(id);return;
    }
    auto candidate=model->values;candidate.removeIf([&](const auto &r){return r.id==id;});
    if(save(candidate)){runtime.remove(id);if(draft && draft->id==id)cancelDraft();else refreshRows();}
}
bool PortsPanel::eventFilter(QObject *object, QEvent *event) {
    if(object==table && (event->type()==QEvent::FontChange || event->type()==QEvent::StyleChange || event->type()==QEvent::DevicePixelRatioChange))scheduleFontLayout();
    if (object==table->viewport() && event->type()==QEvent::Resize)sizeColumns();
    if (object==table->viewport() && event->type()==QEvent::MouseButtonPress)
        table->setProperty("portsClickPosition",static_cast<QMouseEvent *>(event)->position().toPoint());
    if (event->type()==QEvent::KeyPress) {
        auto key=static_cast<QKeyEvent *>(event);auto widget=qobject_cast<QWidget *>(object);
        if (draftRow && widget && (widget==draftRow || draftRow->isAncestorOf(widget))) {
            if(key->key()==Qt::Key_Escape){cancelDraft();return true;}
            if((key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter) && qobject_cast<QLineEdit *>(widget)) {
                if (draft && running(draft->id)) {validateDraft();reportDraft("请选择“应用并重启”或取消");}
                else commitDraft();return true;
            }
        } else if(object==table) {
            const auto index=table->currentIndex();const auto id=index.data(Qt::UserRole).toString();
            if(key->key()==Qt::Key_Space && index.column()==4 && !id.isEmpty()){toggleRule(id);return true;}
            if(key->key()==Qt::Key_F2 && !id.isEmpty()){beginEdit(id);return true;}
            if(key->key()==Qt::Key_Menu || (key->key()==Qt::Key_F10 && key->modifiers()==Qt::ShiftModifier)) {
                if(!id.isEmpty())showMenu(id,table->viewport()->mapToGlobal(table->visualRect(index).bottomRight()));return true;
            }
        }
    }
    if (draft && draftRow && isVisible() && !dialogOpen) {
        auto widget=qobject_cast<QWidget *>(object);
        if(event->type()==QEvent::MouseButtonPress && widget && widget!=draftRow && !draftRow->isAncestorOf(widget) && !preserveDraft(widget)) commitDraft();
        if(event->type()==QEvent::FocusOut && widget && draftRow->isAncestorOf(widget))
            QTimer::singleShot(0,this,[this]{auto focus=QApplication::focusWidget();if(draft && isVisible() && !dialogOpen && focus && !draftRow->isAncestorOf(focus) && !preserveDraft(focus))commitDraft();});
    }
    return QWidget::eventFilter(object,event);
}
} // namespace rs
