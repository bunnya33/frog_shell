#include "messagebox.h"
#include "services.h"
#include "icons.h"
#include "styledcombobox.h"
#include "contentfont.h"
#include "uipreferences.h"
#include "windowstyle.h"
namespace rs {
namespace {
class ServiceCard : public QWidget {
  public:
    ServiceCard(QWidget *child) {setFrameRole(this,"card");auto l=new QVBoxLayout(this);uiMargins(l,1,1,1,1);l->addWidget(child);}
};
bool live(const QVariantMap &d){return QStringList{"active","activating","deactivating","reloading"}.contains(d["ActiveState"].toString());}
QString describe(const QVariantMap &d){QStringList lines;for(auto i=d.cbegin();i!=d.cend();++i)if(!i.value().toString().isEmpty())lines<<i.key()+": "+i.value().toString();return lines.join('\n');}
QPushButton *plainButton(const QString &s){auto b=new QPushButton(s);b->setAutoDefault(false);return b;}
}
RemoteServicePicker::RemoteServicePicker(Factory factory,const QString &start,bool dirs,QWidget *parent):QDialog(parent),directories(dirs) {
    setWindowTitle(dirs?"选择服务器目录":"选择服务器文件");setObjectName("remoteServicePicker");resize(680,460);
    auto l=new QVBoxLayout(this);auto nav=new QHBoxLayout; l->addLayout(nav);
    auto back=iconButton(Icon::Back,"后退"),forward=iconButton(Icon::Forward,"前进"),up=iconButton(Icon::Up,"上一级"),home=iconButton(Icon::Home,"服务器主目录"),refresh=iconButton(Icon::Refresh,"刷新目录");
    for(auto b:{back,forward,up,home,refresh})nav->addWidget(b);
    address=new QStackedWidget;nav->addWidget(address,1);
    path=new QLineEdit;path->setObjectName("remotePickerPath");path->setPlaceholderText("输入服务器绝对路径后按 Enter");path->installEventFilter(this);
    crumbs=new QWidget;crumbs->installEventFilter(this);crumbLayout=new QHBoxLayout(crumbs);uiMargins(crumbLayout,0,0,0,0);uiSpacing(crumbLayout,2);address->addWidget(crumbs);address->addWidget(path);
    files=new QTreeWidget;files->setObjectName("remotePickerFiles");files->setHeaderLabels({"服务器文件 / 目录"});files->setRootIsDecorated(false);files->setUniformRowHeights(true);files->setSelectionMode(QAbstractItemView::SingleSelection);l->addWidget(files,1);
    message=new QLabel("正在连接服务器 SFTP…");message->setTextFormat(Qt::PlainText);message->setWordWrap(true);l->addWidget(message);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);choose=buttons->button(QDialogButtonBox::Ok);choose->setEnabled(false);choose->setText(dirs?"选择目录":"选择文件");l->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,this,&RemoteServicePicker::acceptSelection);
    connect(path,&QLineEdit::returnPressed,this,[this]{navigate(path->text());});
    connect(back,&QToolButton::clicked,this,[this]{if(position>0)navigate(history[--position],true);});
    connect(forward,&QToolButton::clicked,this,[this]{if(position+1<history.size())navigate(history[++position],true);});
    connect(up,&QToolButton::clicked,this,[this]{navigate(current=="/"?"/":current.left(current.lastIndexOf('/')).isEmpty()?"/":current.left(current.lastIndexOf('/')));});
    connect(home,&QToolButton::clicked,this,[this]{navigate(".");});connect(refresh,&QToolButton::clicked,this,[this]{navigate(current,true);});
    connect(files,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem *i){if(i->data(0,Qt::UserRole+1).toBool())navigate(i->data(0,Qt::UserRole).toString());else if(!directories)acceptSelection();});
    worker=factory?factory(this,Session::Mode::Files):nullptr;
    if(!worker){message->setText("SFTP 不可用。取消选择后仍可手填服务器路径。");return;}
    connect(worker,&Session::connected,this,[this,start]{navigate(start.isEmpty()?".":start);});
    connect(worker,&Session::completed,this,[this](const QString &type,int id,const QVariantMap &r){
        if(type!="list" || id!=next)return;
        current=r["path"].toString();path->setText(current);address->setCurrentWidget(crumbs);files->clear();choose->setEnabled(true);
        if(history.isEmpty() || position<0 || history[position]!=current){history=history.mid(0,position+1);history<<current;if(history.size()>100)history.removeFirst();position=int(history.size())-1;}
        for(auto v:r["entries"].toList()){auto d=v.toMap();auto i=new QTreeWidgetItem(files,{d["name"].toString()+(d["directory"].toBool()?"/":"")});i->setData(0,Qt::UserRole,d["path"]);i->setData(0,Qt::UserRole+1,d["directory"]);}
        while(auto item=crumbLayout->takeAt(0)){delete item->widget();delete item;}
        QString p;auto parts=current.split('/',Qt::SkipEmptyParts);parts.prepend("/");
        for(auto part:parts){if(!p.isEmpty()){auto chevron=new QLabel;UiZoom::instance().bind(chevron,"icon",[chevron]{chevron->setPixmap(icon(Icon::Forward).pixmap(uiSize(12,12)));});crumbLayout->addWidget(chevron);}p=part=="/"?"/":p=="/"?p+part:p+"/"+part;auto b=plainButton(part);b->setToolTip(p);b->setProperty("breadcrumb",true);const auto target=p;connect(b,&QPushButton::clicked,this,[this,target]{navigate(target);});crumbLayout->addWidget(b);}
        crumbLayout->addStretch();message->setText("仅返回服务器路径；不执行、不下载，也不改变主资源树目录。");
    });
    connect(worker,&Session::failed,this,[this](const QString &,int id,const QString &why){if(id==next)message->setText(why+"；可重新输入服务器路径。");});
    connect(worker,&Session::disconnected,this,[this](const QString &why){message->setText("服务器文件浏览不可用："+why+"\n取消后仍可手填路径。");});worker->start();
}
RemoteServicePicker::~RemoteServicePicker(){if(worker){worker->stop();worker->wait();}}
void RemoteServicePicker::navigate(const QString &p,bool historical){Q_UNUSED(historical);if(!worker || p.isEmpty())return;choose->setEnabled(false);files->clear();selected.clear();Job j;j.type="list";j.id=++next;j.path=p;worker->submit(j);address->setCurrentWidget(crumbs);message->setText("正在读取服务器目录…");}
bool RemoteServicePicker::eventFilter(QObject *o,QEvent *e){if(o==crumbs&&e->type()==QEvent::MouseButtonPress&&static_cast<QMouseEvent *>(e)->button()==Qt::LeftButton){address->setCurrentWidget(path);path->setText(current);path->setFocus();path->selectAll();return true;}if(o==path&&(e->type()==QEvent::FocusOut||(e->type()==QEvent::KeyPress&&static_cast<QKeyEvent *>(e)->key()==Qt::Key_Escape))){path->setText(current);address->setCurrentWidget(crumbs);return e->type()==QEvent::KeyPress;}return QDialog::eventFilter(o,e);}
void RemoteServicePicker::acceptSelection(){if(!choose->isEnabled())return;auto i=files->currentItem();if(directories){selected=i&&i->data(0,Qt::UserRole+1).toBool()?i->data(0,Qt::UserRole).toString():current;}else if(i&&!i->data(0,Qt::UserRole+1).toBool())selected=i->data(0,Qt::UserRole).toString();if(!selected.startsWith('/')){message->setText(directories?"请等待目录加载或选择目录。":"请选择单个服务器文件。");return;}accept();}

ServicesPanel::ServicesPanel(QString identity,bool offline,QWidget *parent):QWidget(parent),server(std::move(identity)),demo(offline) {
    setProperty("rabbitUi",true);
    setObjectName("servicesPanel");auto l=new QVBoxLayout(this);uiMargins(l,8,6,8,6);uiSpacing(l,5);
    toolbar=new QWidget;auto actions=new QHBoxLayout(toolbar);uiMargins(actions,0,0,0,0);uiSpacing(actions,0);
    auto add=iconButton(Icon::Add,"新增服务"),reload=iconButton(Icon::Refresh,"刷新服务");add->setObjectName("addService");reload->setObjectName("refreshServices");actions->addWidget(add);actions->addWidget(reload);
    connect(add,&QToolButton::clicked,this,&ServicesPanel::newService);connect(reload,&QToolButton::clicked,this,&ServicesPanel::refresh);
    auto tools=new QHBoxLayout;search=new QLineEdit;search->setObjectName("serviceSearch");search->setPlaceholderText("搜索服务名或描述");search->setClearButtonEnabled(true);tools->addWidget(search,1);
    filter=new StyledComboBox;filter->setObjectName("serviceFilter");filter->addItems({"全部","运行中","已停止","失败"});tools->addWidget(filter);l->addLayout(tools);
    table=new QTreeWidget;table->setObjectName("serviceTable");table->setHeaderLabels({"服务名","运行状态","自启","描述"});table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setRootIsDecorated(false);table->setUniformRowHeights(true);table->setAlternatingRowColors(true);table->setSelectionMode(QAbstractItemView::SingleSelection);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSortingEnabled(true);table->sortItems(0,Qt::AscendingOrder);table->setTextElideMode(Qt::ElideRight);table->setContextMenuPolicy(Qt::CustomContextMenu);
    table->header()->setStretchLastSection(false);
    auto widths=QSettings().value("services/columns-v1").toList();if(widths.size()!=4)widths={230,130,110,420};
    auto baseWidths=std::make_shared<QVariantList>(widths);
    UiZoom::instance().bind(table,"columns",[this,baseWidths]{QSignalBlocker block(table->header());table->header()->setMinimumSectionSize(uiPx(80));for(int i=0;i<4;++i)table->setColumnWidth(i,uiPx(qBound(80.,(*baseWidths)[i].toDouble(),1200.)));});
    connect(table->header(),&QHeaderView::sectionResized,this,[this,baseWidths](int section,int,int width){if(!UiZoom::instance().changing()&&!UiPreferences::instance().resetting()){(*baseWidths)[section]=uiBase(width);QSettings().setValue("services/columns-v1",*baseWidths);}positionActions();});
    l->addWidget(new ServiceCard(table),1);
    connect(&UiPreferences::instance(),&UiPreferences::defaultsRestored,this,[this,baseWidths]{
        *baseWidths={230,130,110,420}; QSignalBlocker block(table->header());
        for(int i=0;i<4;++i)table->setColumnWidth(i,uiPx((*baseWidths)[i].toInt()));
        refreshTheme();positionActions();
    });
    status=new ElidedLabel;status->setObjectName("serviceStatus");status->setTextFormat(Qt::PlainText);status->hide();l->addWidget(status);

    rowActions=new QWidget(table->viewport());auto row=new QHBoxLayout(rowActions);uiMargins(row,0,0,0,0);auto more=iconButton(Icon::More,"服务操作");row->addWidget(more);rowActions->hide();
    connect(more,&QToolButton::pressed,this,[this]{pressedUnit=table->currentItem()?table->currentItem()->data(0,Qt::UserRole).toMap()["Id"].toString():QString();});
    connect(more,&QToolButton::clicked,this,[this,more]{if(!pressedUnit.isEmpty())menu(pressedUnit,more->mapToGlobal(QPoint(0,more->height())));});
    connect(table,&QTreeWidget::currentItemChanged,this,[this]{positionActions();});connect(table->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{positionActions();});table->viewport()->installEventFilter(this);
    table->installEventFilter(this);bindContentFont(table,ContentFontRole::List);
    connect(table,&QTreeWidget::customContextMenuRequested,this,[this](QPoint p){if(auto i=table->itemAt(p)){table->setCurrentItem(i);menu(i->data(0,Qt::UserRole).toMap()["Id"].toString(),table->viewport()->mapToGlobal(p));}});
    connect(table,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem *i){action("detail",i->data(0,Qt::UserRole).toMap()["Id"].toString());});
    connect(search,&QLineEdit::textChanged,this,&ServicesPanel::filterRows);connect(filter,&QComboBox::currentIndexChanged,this,&ServicesPanel::filterRows);
    timer.setInterval(5000);connect(&timer,&QTimer::timeout,this,&ServicesPanel::refresh);refreshTheme();
    if(demo){QVariantList rows;const QStringList names{"nginx.service","docker.service","backup.service","example-worker.service","systemd-journald.service"};int n=0;for(auto name:names){bool exit=n==2;rows<<QVariantMap{{"Id",name},{"Description",QStringList{"Web 服务器","容器服务","定时备份已完成","自定义任务队列 · 等待启动","系统日志"}[n]},{"ActiveState",n==3?"inactive":"active"},{"SubState",exit?"exited":n==3?"dead":"running"},{"UnitFileState",n==2?"static":n==3?"disabled":"enabled"},{"CanReload",n==0?"yes":"no"}};++n;}snapshot(rows);}
}
ServicesPanel::~ServicesPanel(){stopLocal();delete toolbar;}
void ServicesPanel::showEvent(QShowEvent *e){QWidget::showEvent(e);syncTimer();if(active)refresh();}
void ServicesPanel::hideEvent(QHideEvent *e){QWidget::hideEvent(e);timer.stop();}
void ServicesPanel::setActivity(bool a){active=a;syncTimer();if(a && isVisible())refresh();}
void ServicesPanel::syncTimer(){if(active && isVisible() && ready && !demo)timer.start();else timer.stop();}
void ServicesPanel::stopLocal(){timer.stop();connecting=false;ready=false;if(worker)worker->stop();}
void ServicesPanel::feedback(const QString &error){
    status->setText(error.simplified());status->setToolTip(error);
    status->setVisible(!error.isEmpty());
}
void ServicesPanel::connectionError(const QString &why){connecting=false;feedback(why);}
void ServicesPanel::setSession(Session *s) {
    worker=s;connecting=true;
    connect(s,&Session::connected,this,[this,s]{if(worker!=s)return;connecting=false;ready=true;syncTimer();refresh();});
    connect(s,&Session::completed,this,[this,s](const QString &type,int id,const QVariantMap &r){if(worker!=s || type!="service" || id!=requestId)return;busy=false;auto a=pendingAction,u=pendingUnit;deliver(a,u,r);bool again=refreshAfter;refreshAfter=false;if((a!="list"||again)&&active&&isVisible())refresh();});
    connect(s,&Session::failed,this,[this,s](const QString &,int id,const QString &why){if(worker==s&&id==requestId){busy=false;status->setProperty("operationResult",pendingAction!="list");feedback(why+"；变更不会自动重试。");if(saveForm)saveForm->setEnabled(true);}});
    connect(s,&Session::disconnected,this,[this,s](const QString &why){if(worker!=s)return;ready=false;connecting=false;timer.stop();if(busy){busy=false;auto message="结果未知，请刷新确认；不会自动重放变更。\n"+why;feedback(message);if(formReport)formReport->setPlainText(message);}else feedback("服务连接已断开："+why+"；手动刷新可重新查询。");if(saveForm)saveForm->setEnabled(false);});
    connect(s,&QThread::finished,this,[this,s]{if(worker==s)worker=nullptr;});
}
void ServicesPanel::refresh(){if(demo)return;if(interacting){refreshAfter=true;return;}if(busy){refreshAfter=true;return;}if(!ready){if(!worker&&!connecting){connecting=true;emit connectionRequested();}return;}submit("list");}
bool ServicesPanel::submit(const QString &a,QVariantMap req){if(services::isTemplate(req["unit"].toString())&&a!="detail"){feedback(services::templateRestriction());return false;}if(demo||busy||!ready||!worker){feedback("当前任务尚未完成或连接不可用，请稍后重新发起。");return false;}if(mutationGuard){auto why=mutationGuard();if(!why.isEmpty()){feedback(why);return false;}}busy=true;requestId=++next;pendingAction=a;pendingUnit=req["unit"].toString();Job j;j.type="service";j.id=requestId;j.path=a;j.options=req;worker->submit(j);if(a!="list"){status->setProperty("operationResult",false);feedback();}return true;}
void ServicesPanel::snapshot(const QVariantList &rows){
    const auto y=table->verticalScrollBar()->value(),x=table->horizontalScrollBar()->value();QString selected;if(auto i=table->currentItem())selected=i->data(0,Qt::UserRole).toMap()["Id"].toString();
    table->setUpdatesEnabled(false);table->setSortingEnabled(false);QSet<QString> seen;
    for(auto v:rows){auto d=v.toMap();auto id=d["Id"].toString();if(!services::validUnit(id)||seen.contains(id))continue;seen.insert(id);auto i=items.value(id);if(!i){i=new QTreeWidgetItem(table);items[id]=i;}i->setData(0,Qt::UserRole,d);QStringList fields{id,services::stateText(d),services::bootText(d["UnitFileState"].toString()),d["Description"].toString()};for(int c=0;c<4;++c){i->setText(c,fields[c]);i->setToolTip(c,fields[c]);}if(!d["QueryError"].toString().isEmpty())i->setToolTip(1,fields[1]+"\n"+d["QueryError"].toString());i->setForeground(1,d["ActiveState"]=="failed"?colors().red:fields[1]=="运行中"?colors().green:colors().muted);}
    for(auto id:items.keys())if(!seen.contains(id))delete items.take(id);
    table->setSortingEnabled(true);filterRows();table->clearSelection();auto i=items.value(selected);table->selectionModel()->setCurrentIndex(i?table->indexFromItem(i):QModelIndex(),QItemSelectionModel::NoUpdate);if(i)i->setSelected(true);
    table->verticalScrollBar()->setValue(y);table->horizontalScrollBar()->setValue(x);table->setUpdatesEnabled(true);positionActions();
}
void ServicesPanel::filterRows(){for(auto i:items){auto d=i->data(0,Qt::UserRole).toMap();bool match=(d["Id"].toString()+" "+d["Description"].toString()).contains(search->text(),Qt::CaseInsensitive);auto n=filter->currentIndex();if(n==1)match&=d["ActiveState"]=="active"&&d["SubState"]=="running";if(n==2)match&=d["ActiveState"]=="inactive";if(n==3)match&=d["ActiveState"]=="failed";i->setHidden(!match);}positionActions();}
void ServicesPanel::positionActions(){auto i=table->currentItem();if(!i||i->isHidden()){rowActions->hide();return;}auto r=table->visualItemRect(i);if(!r.intersects(table->viewport()->rect())){rowActions->hide();return;}rowActions->setGeometry(table->viewport()->width()-uiPx(32),r.top(),uiPx(30),r.height());rowActions->show();rowActions->raise();}
bool ServicesPanel::eventFilter(QObject *o,QEvent *e){if(o==table&&(e->type()==QEvent::FontChange||e->type()==QEvent::StyleChange||e->type()==QEvent::DevicePixelRatioChange))scheduleFontLayout();if(o==table->viewport() && e->type()==QEvent::Resize)positionActions();return QWidget::eventFilter(o,e);}
void ServicesPanel::refreshTheme(){const auto c=colors();const auto actualFont=table->font();setUiStyleSheet(table,QString("QTreeWidget {border:0;background:%1;alternate-background-color:%2;} QTreeWidget::item {min-height:30px;padding:4px 8px;} QTreeWidget::item:selected {background:%3;color:%4;} QHeaderView::section {background:%5;color:%4;border:0;border-right:1px solid %6;padding:7px 8px;} ").arg(c.processBody.name(),c.processAlternate.name(),c.processSelection.name(),c.text.name(),c.processHeader.name(),c.line.name()));table->setFont(actualFont);for(auto i:items){auto d=i->data(0,Qt::UserRole).toMap();i->setForeground(1,d["ActiveState"]=="failed"?c.red:services::stateText(d)=="运行中"?c.green:c.muted);}for(auto b:toolbar->findChildren<QToolButton *>())b->setIcon(icon(b->objectName()=="addService"?Icon::Add:Icon::Refresh));for(auto b:rowActions->findChildren<QToolButton *>())b->setIcon(icon(Icon::More));}
void ServicesPanel::scheduleFontLayout(){
    if(fontLayoutPending)return;fontLayoutPending=true;
    QTimer::singleShot(0,this,[this]{fontLayoutPending=false;table->header()->setFont(table->font());
        {QSignalBlocker blocked(table->header());table->header()->setMinimumSectionSize(qMax(uiPx(80),table->fontMetrics().horizontalAdvance("运行状态")+uiPx(24)));}
        table->doItemsLayout();positionActions();table->viewport()->update();});
}
void ServicesPanel::textDialog(const QString &title,const QString &text){auto d=new QDialog(this);d->setAttribute(Qt::WA_DeleteOnClose);d->setWindowTitle(title);auto l=new QVBoxLayout(d);auto t=new QPlainTextEdit;bindContentFont(t,ContentFontRole::List);t->setReadOnly(true);t->setPlainText(text.left(256*1024));l->addWidget(t);auto b=new QDialogButtonBox(QDialogButtonBox::Close);l->addWidget(b);connect(b,&QDialogButtonBox::rejected,d,&QDialog::reject);d->resize(680,440);d->open();}
void ServicesPanel::menu(const QString &u,QPoint global){
    if(!items.contains(u))return;auto d=items[u]->data(0,Qt::UserRole).toMap();QScopedValueRollback<bool> interaction(interacting,true);QMenu m(this);
    const QVector<QPair<QString,QString>> entries{{"start","启动"},{"stop","停止…"},{"restart","重启…"},{"reload","重新加载配置"},{"enable","启用开机自启"},{"disable","禁用开机自启"},{"detail","详情"},{"logs","最近日志"},{"copy","复制名称"},{"inspect-delete","删除服务…"}};
    m.setToolTipsVisible(true);
    for(auto entry:entries){if(entry.first=="enable"||entry.first=="detail"||entry.first=="inspect-delete")m.addSeparator();auto a=m.addAction(entry.second);a->setData(entry.first);bool mutation=QStringList{"start","stop","restart","reload","enable","disable","inspect-delete"}.contains(entry.first);a->setEnabled(!busy&&(!mutation||!demo));if(entry.first=="reload"&&d["CanReload"]!="yes")a->setEnabled(false);if(entry.first=="enable"||entry.first=="disable")if(!QStringList{"enabled","enabled-runtime","disabled"}.contains(d["UnitFileState"].toString())){a->setEnabled(false);a->setToolTip("此自启状态不支持直接切换");}if(services::isTemplate(u)&&entry.first!="detail"&&entry.first!="copy"){a->setEnabled(false);a->setToolTip(services::templateRestriction());}connect(a,&QAction::triggered,this,[this,u,key=entry.first]{action(key,u);});}m.exec(global);
}
void ServicesPanel::action(const QString &a,const QString &u){
    if(a=="copy"){QApplication::clipboard()->setText(u);return;}
    if(services::isTemplate(u)){
        if(a=="detail"&&items.contains(u))textDialog("服务模板 · "+u,describe(items[u]->data(0,Qt::UserRole).toMap())+"\n\n"+services::templateRestriction());
        else feedback(services::templateRestriction());
        return;
    }
    if(demo){if(a=="detail"&&items.contains(u))textDialog("服务详情 · 离线示例",describe(items[u]->data(0,Qt::UserRole).toMap()));return;}
    if(busy)return;
    if(a=="stop"||a=="restart"){
        QScopedValueRollback<bool> interaction(interacting,true);
        AppMessageBox box(QMessageBox::Warning,a=="stop"?"停止服务":"重启服务",server+"\n"+u+(a=="restart"?"\n重启会短暂中断服务。":"\n将停止此服务。"),QMessageBox::Yes|QMessageBox::Cancel,this);box.setTextFormat(Qt::PlainText);box.setDefaultButton(QMessageBox::Cancel);if(box.exec()!=QMessageBox::Yes)return;
    }
    submit(a,{{"unit",u}});
}
void ServicesPanel::deliver(const QString &a,const QString &u,const QVariantMap &r){
    auto steps=r["steps"].toStringList();auto text=steps.join('\n');if(!r["ok"].toBool()){status->setProperty("operationResult",a!="list");if(!text.isEmpty())text+="\n";text+=r["error"].toString();feedback(text);status->setToolTip(text);if(formReport&&(a=="create"||a=="check-create"))formReport->setPlainText(text);if(saveForm)saveForm->setEnabled(a!="create"&&ready);return;}
    if(a=="list"){snapshot(r["rows"].toList());if(!status->property("operationResult").toBool())feedback();return;}
    if(a=="detail"){textDialog("服务详情 · "+u,describe(r["detail"].toMap()));return;}
    if(a=="logs"){textDialog("最近 200 条日志 · "+u,r["text"].toString());return;}
    if(a=="check-create"){if(formReport)formReport->setPlainText(r["text"].toString());if(saveForm)saveForm->setEnabled(true);return;}
    if(a=="inspect-delete"){
        auto d=r["detail"].toMap();auto why=d["deleteReason"].toString();if(!why.isEmpty()){textDialog("不能直接删除 · "+u,why+"\n\n"+describe(d));return;}
        QScopedValueRollback<bool> interaction(interacting,true);
        auto label=live(d)?"停止并删除":"删除服务";
        AppMessageBox box(QMessageBox::Warning,"删除服务",server+"\n"+u+"\n"+d["FragmentPath"].toString()+"\n运行状态："+services::stateText(d)+"\n自启："+services::bootText(d["UnitFileState"].toString())+"\n关联："+d["RequiredBy"].toString()+" "+d["WantedBy"].toString()+"\n将停止活动服务、取消自启并移除核对的配置文件。保留可执行程序、工作目录和业务数据。不会删除 timer/socket。",QMessageBox::NoButton,this);box.setTextFormat(Qt::PlainText);auto yes=box.addButton(label,QMessageBox::DestructiveRole);auto cancel=box.addButton(QMessageBox::Cancel);box.setDefaultButton(cancel);box.exec();if(box.clickedButton()==yes)submit("delete",{{"unit",u},{"expected",d}});return;
    }
    if(!r["detail"].toMap().isEmpty())text+="\n复查："+services::stateText(r["detail"].toMap())+" · 自启 "+services::bootText(r["detail"].toMap()["UnitFileState"].toString());
    status->setProperty("operationResult",false);feedback();if(a=="create"){if(formReport)formReport->setPlainText(text);if(saveForm)saveForm->setEnabled(false);}
}
void ServicesPanel::newService(){
    if(form){form->raise();form->activateWindow();return;}
    auto dialog=new QDialog(this);form=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName("newServiceDialog");dialog->setWindowTitle("新增服务 · "+server);dialog->resize(740,700);
    auto l=new QVBoxLayout(dialog);auto scroll=new QScrollArea;scroll->setWidgetResizable(true);auto body=new QWidget;auto fields=new QFormLayout(body);scroll->setWidget(body);l->addWidget(scroll,1);
    QMap<QString,QLineEdit *> edits;
    const QVector<QPair<QString,QString>> keys{{"name","名称（最终 .service）"},{"description","描述"},{"program","程序绝对路径 / 脚本"},{"interpreter","已有解释器（可选）"},{"arguments","参数"},{"directory","工作目录（可选）"},{"user","已有运行用户"}};
    for(auto pair:keys){auto edit=new QLineEdit;edit->setObjectName("serviceForm_"+pair.first);edits[pair.first]=edit;auto label=new QLabel(pair.second);label->setBuddy(edit);
        if(pair.first=="program"||pair.first=="interpreter"||pair.first=="directory") {auto row=new QWidget;auto h=new QHBoxLayout(row);uiMargins(h,0,0,0,0);h->addWidget(edit,1);auto browse=iconButton(Icon::Explorer,"浏览服务器"+pair.second);h->addWidget(browse);fields->addRow(label,row);
            connect(browse,&QToolButton::clicked,dialog,[this,dialog,edit,key=pair.first,edits]{if(!sessionFactory){return;}RemoteServicePicker picker(sessionFactory,directory?directory():".",key=="directory",dialog);if(picker.exec()==QDialog::Accepted){edit->setText(picker.selectedPath());if(key=="program"&&!edits["directory"]->isModified()&&edits["directory"]->text().isEmpty())edits["directory"]->setText(picker.selectedPath().left(picker.selectedPath().lastIndexOf('/')));}});
        }else fields->addRow(label,edit);
    }
    edits["arguments"]->setPlaceholderText("空格分隔；单/双引号分组，反斜杠转义下一字符；不执行 shell");
    auto restart=new QCheckBox("异常退出后重启（间隔 3 秒）");restart->setChecked(true);auto enable=new QCheckBox("开机自启"),start=new QCheckBox("保存后立即启动");restart->setObjectName("serviceRestart");enable->setObjectName("serviceEnable");start->setObjectName("serviceStart");fields->addRow(restart);fields->addRow(enable);fields->addRow(start);
    auto preview=new QPlainTextEdit;preview->setObjectName("serviceConfigPreview");preview->setReadOnly(true);uiMinimumHeight(preview,170);fields->addRow("只读配置预览",preview);
    auto report=new QPlainTextEdit;formReport=report;report->setObjectName("serviceCreateReport");report->setReadOnly(true);uiMaximumHeight(report,115);report->setPlainText(demo?"离线预览：允许填写与预览，禁止服务器变更。":"只使用现有账号和权限，不隐式 sudo。检查不执行程序；ACL / 安全策略与实际启动结果仍需服务器判定。");l->addWidget(report);
    auto buttons=new QHBoxLayout;auto inspect=plainButton("检查服务器配置"),save=plainButton("创建服务"),cancel=plainButton("关闭");saveForm=save;buttons->addWidget(inspect);buttons->addStretch();buttons->addWidget(save);buttons->addWidget(cancel);l->addLayout(buttons);
    auto values=[edits,restart,enable,start]{QVariantMap f;for(auto i=edits.cbegin();i!=edits.cend();++i)f[i.key()]=i.value()->text();f["restart"]=restart->isChecked();f["enable"]=enable->isChecked();f["start"]=start->isChecked();return f;};
    auto update=[values,preview,save,inspect,this]{try{preview->setPlainText(QString::fromUtf8(services::configuration(values())));save->setEnabled(!demo&&ready&&!busy);inspect->setEnabled(!demo&&ready&&!busy);}catch(const QString &why){preview->setPlainText(why);save->setEnabled(false);inspect->setEnabled(false);}};
    for(auto e:edits)connect(e,&QLineEdit::textChanged,dialog,update);for(auto c:{restart,enable,start})connect(c,&QCheckBox::toggled,dialog,update);
    connect(inspect,&QPushButton::clicked,dialog,[this,values]{submit("check-create",values());});
    connect(save,&QPushButton::clicked,dialog,[this,values,save,report,dialog]{auto f=values();QScopedValueRollback<bool> interaction(interacting,true);AppMessageBox confirm(QMessageBox::Question,"确认创建服务",server+"\n"+f["name"].toString()+"\n运行用户："+f["user"].toString()+"\n程序："+f["program"].toString()+"\n解释器："+f["interpreter"].toString()+"\n工作目录："+f["directory"].toString()+"\n自启："+(f["enable"].toBool()?"是":"否")+"\n立即启动："+(f["start"].toBool()?"是":"否"),QMessageBox::Ok|QMessageBox::Cancel,dialog);confirm.setTextFormat(Qt::PlainText);confirm.setDefaultButton(QMessageBox::Cancel);if(confirm.exec()!=QMessageBox::Ok)return;save->setEnabled(false);report->setPlainText("正在创建；每一步将分别报告结果。请勿重复提交。");if(!submit("create",f))report->setPlainText(status->text());});
    connect(cancel,&QPushButton::clicked,dialog,&QDialog::reject);update();dialog->open();
}
}
