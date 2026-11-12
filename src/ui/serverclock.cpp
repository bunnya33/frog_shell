#include "serverclock.h"
#include "applock.h"
#include "messagebox.h"
#include "uizoom.h"
#include "../core/session.h"
#include "../core/operationgate.h"
#include <atomic>
namespace rs {
namespace {
std::atomic<int> nextJob{1000000000};
int jobId(){auto n=nextJob.fetch_add(1);if(n>=2000000000){nextJob.store(1000000000);return nextJob.fetch_add(1);}return n;}
QString ntpText(const QString &state){return state=="yes"?"自动校时：已开启，修改可能被自动校时覆盖":state=="no"?"自动校时：未开启":"自动校时：不确定，修改可能被覆盖";}
}
ServerClock::ServerClock(QWidget *parent):QPushButton(parent){
    setObjectName("serverClock");setProperty("breadcrumb",true);setCursor(Qt::PointingHandCursor);setAttribute(Qt::WA_AlwaysShowToolTips);
    bindUiFont(this,12);timer.setInterval(1000);connect(&timer,&QTimer::timeout,this,&ServerClock::tick);timer.start();
    connect(this,&QPushButton::clicked,this,&ServerClock::showPanel);
    if(auto lock=AppLock::instance())connect(lock,&AppLock::lockedChanged,this,[this](bool locked){
        if(locked){++generation;valid=false;refreshAfterBusy=false;cancelIntent();closePanel();}
        else if(connected&&active){refresh(true);}
        updateView();
    });
    updateView();
}
ServerClock::~ServerClock(){cancelIntent();}
void ServerClock::setSession(Session *value){
    if(session==value)return;
    cancelIntent();closePanel();for(const auto &c:bindings)disconnect(c);bindings.clear();++generation;
    session=value;connected=false;valid=false;busy=false;pendingId=0;refreshAfterBusy=false;requestAge.invalidate();feedbackText.clear();operationFeedback.clear();
    if(value){
        bindings<<connect(value,&Session::connected,this,[this,value]{if(session==value)setConnected(true);});
        bindings<<connect(value,&Session::disconnected,this,[this,value](const QString &){if(session==value)setConnected(false);});
        bindings<<connect(value,&Session::completed,this,[this,value](const QString &type,int id,const QVariantMap &result){completed(value,type,id,result);});
        bindings<<connect(value,&Session::failed,this,[this,value](const QString &type,int id,const QString &reason){failed(value,type,id,reason);});
        bindings<<connect(value,&QObject::destroyed,this,[this]{session=nullptr;setConnected(false);bindings.clear();});
    }
    updateView();
}
void ServerClock::setConnected(bool value){
    if(connected==value)return;
    connected=value;++generation;valid=false;feedbackText.clear();
    if(!value){cancelIntent();busy=false;pendingId=0;refreshAfterBusy=false;closePanel();}
    else if(active&&!OperationGate::locked())refresh(true);
    updateView();
}
void ServerClock::setActive(bool value){
    if(active==value)return;
    active=value;++generation;valid=false;
    if(!value){cancelIntent();refreshAfterBusy=false;closePanel();}
    else if(connected&&!OperationGate::locked())refresh(true);
    updateView();
}
QDateTime ServerClock::displayedTime()const{
    if(!valid||!elapsed.isValid())return {};
    return QDateTime::fromSecsSinceEpoch(sample.epoch+elapsed.elapsed()/1000,QTimeZone::fromSecondsAheadOfUtc(sample.offsetSeconds));
}
void ServerClock::tick(){
    if(OperationGate::locked()){if(dialog)closePanel();if(valid){valid=false;updateView();}return;}
    if(valid&&elapsed.isValid()){
        const auto age=elapsed.elapsed(),wall=QDateTime::currentMSecsSinceEpoch()-wallAtSample;
        if(age>130000||qAbs(wall-age)>5000){valid=false;feedbackText="服务器时间需重新校准。";if(connected&&active)refresh(true);}
    }
    if(connected&&active&&(!requestAge.isValid()||requestAge.elapsed()>=60000))refresh();
    updateView();
}
void ServerClock::refresh(bool force){
    if(!connected||!active||!session||OperationGate::locked())return;
    if(busy){if(force)refreshAfterBusy=true;return;}
    submit("sample");
}
void ServerClock::submit(const QString &action,QVariantMap options){
    if(!connected||!active||!session||busy||OperationGate::locked())return;
    const auto token=options.contains("lockEpoch")?options.value("lockEpoch").toULongLong():OperationGate::token();
    if(!OperationGate::accepts(token))return;
    Job j;j.type="server-clock";j.id=jobId();j.path=action;options["lockEpoch"]=QVariant::fromValue(token);
    if(action=="set"){writePermit=std::make_shared<std::atomic_bool>(true);options["intent"]=QVariant::fromValue(writePermit);}j.options=options;
    pendingId=j.id;pendingAction=action;pendingGeneration=generation;busy=true;requestAge.start();session->submit(j);updateView();
}
void ServerClock::completed(Session *origin,const QString &type,int id,const QVariantMap &result){
    if(origin!=session||type!="server-clock"||!busy||id!=pendingId)return;
    const auto action=pendingAction;const auto jobGeneration=pendingGeneration;if(action=="set")cancelIntent();busy=false;pendingId=0;
    if(jobGeneration!=generation||!connected||!active||OperationGate::locked()){
        const bool again=refreshAfterBusy;refreshAfterBusy=false;if(again&&connected&&active&&!OperationGate::locked())refresh(true);updateView();return;
    }
    if(action=="sample"){
        if(result.value("ok").toBool()){
            sample.epoch=result.value("epoch").toLongLong();sample.offsetSeconds=result.value("offset").toInt();sample.zone=result.value("zone").toString();sample.ntp=result.value("ntp").toString();
            elapsed.start();wallAtSample=QDateTime::currentMSecsSinceEpoch();valid=true;feedbackText.clear();
            if(dialog&&!dialogInitialized){const auto time=displayedTime();dateEdit->setDate(time.date());timeEdit->setTime(time.time());dialogInitialized=true;}
        } else {valid=false;feedbackText=result.value("error").toString();}
    }else{
        operationFeedback=result.value("ok").toBool()?"修改已提交；下方服务器时间为复查结果。":result.value("error").toString();
        if(result.value("unknown").toBool())operationFeedback+="\n当前显示的时间仅为只读复查结果，不能证明修改是否成功。";
        valid=false;refreshAfterBusy=true;
    }
    const bool again=refreshAfterBusy;refreshAfterBusy=false;updateView();if(again)refresh(true);
}
void ServerClock::failed(Session *origin,const QString &type,int id,const QString &reason){
    if(origin!=session||type!="server-clock"||!busy||id!=pendingId)return;
    const bool wasSet=pendingAction=="set";const auto gen=pendingGeneration;if(wasSet)cancelIntent();busy=false;pendingId=0;
    if(gen==generation){valid=false;if(wasSet)operationFeedback="修改结果未知，不会自动重试；当前显示的时间仅为只读复查结果。\n"+reason;else feedbackText=reason;}
    const bool again=refreshAfterBusy||wasSet;refreshAfterBusy=false;updateView();if(again&&connected&&active&&!OperationGate::locked())refresh(true);
}
void ServerClock::updateView(){
    const bool available=connected&&active&&!OperationGate::locked();
    const auto current=displayedTime();setText(valid&&available?current.toString("yyyy-MM-dd HH:mm:ss"):QString("服务器时间 —"));
    setEnabled(available);const auto server=session?session->profile.name:QString();
    setToolTip(valid&&available?QString("服务器：%1\n时区：%2 %3\n上次同步：%4 秒前\n%5").arg(server,sample.zone,serverclock::offsetText(sample.offsetSeconds)).arg(elapsed.elapsed()/1000).arg(ntpText(sample.ntp)):QString("服务器：%1\n%2").arg(server,connected?"未同步或已过期":"未连接"));
    if(currentLabel)currentLabel->setText(valid?current.toString("yyyy-MM-dd HH:mm:ss"):"未同步");
    if(zoneLabel)zoneLabel->setText(valid?sample.zone+"  "+serverclock::offsetText(sample.offsetSeconds):"未同步");
    if(ntpLabel)ntpLabel->setText(valid?ntpText(sample.ntp):"自动校时：未确认");
    if(statusLabel){QStringList messages;if(!operationFeedback.isEmpty())messages<<operationFeedback;
        if(busy)messages<<(pendingAction=="set"?"正在提交修改…":"正在读取服务器时间…");else if(!feedbackText.isEmpty())messages<<feedbackText;
        statusLabel->setText(messages.join('\n'));}
    if(applyButton)applyButton->setEnabled(available&&valid&&!busy);
    if(refreshButton)refreshButton->setEnabled(available&&!busy);
    if(dateEdit)dateEdit->setEnabled(available&&valid&&dialogInitialized);
    if(timeEdit)timeEdit->setEnabled(available&&valid&&dialogInitialized);
}
void ServerClock::cancelIntent(){if(writePermit)writePermit->store(false,std::memory_order_release);writePermit.reset();}
void ServerClock::closePanel(){
    cancelIntent();QPointer<QDialog> panel=dialog;dialog=nullptr;dialogInitialized=false;
    if(!panel)return;
    QList<QPointer<QDialog>> prompts;
    for(auto child:panel->findChildren<QDialog*>())prompts.append(child);
    // End child exec() loops before hiding/deleting the parent form.
    for(auto it=prompts.crbegin();it!=prompts.crend();++it)if(*it)(*it)->reject();
    if(panel)panel->reject();
}
void ServerClock::showPanel(){
    if(!connected||!active||OperationGate::locked())return;
    if(dialog){dialog->raise();dialog->activateWindow();return;}
    auto d=new QDialog(this);dialog=d;d->setAttribute(Qt::WA_DeleteOnClose);d->setObjectName("serverClockDialog");d->setWindowTitle("服务器日期和时间");
    auto outer=new QVBoxLayout(d);uiMargins(outer,20,18,20,18);uiSpacing(outer,12);
    auto server=new QLabel(session?session->profile.name:QString());server->setObjectName("serverClockName");server->setTextFormat(Qt::PlainText);server->setProperty("heading",true);outer->addWidget(server);
    auto fields=new QFormLayout;outer->addLayout(fields);currentLabel=new QLabel;currentLabel->setObjectName("serverClockCurrent");fields->addRow("当前服务器时间",currentLabel);
    zoneLabel=new QLabel;zoneLabel->setTextFormat(Qt::PlainText);fields->addRow("服务器时区",zoneLabel);
    dateEdit=new QDateEdit;dateEdit->setObjectName("serverClockDate");dateEdit->setCalendarPopup(true);dateEdit->setDisplayFormat("yyyy-MM-dd");dateEdit->setDateRange(QDate(1970,1,1),QDate(9999,12,31));dateEdit->setDate(QDate(2000,1,1));fields->addRow("日期",dateEdit);
    timeEdit=new QTimeEdit;timeEdit->setObjectName("serverClockTime");timeEdit->setDisplayFormat("HH:mm:ss");timeEdit->setTime(QTime(0,0));fields->addRow("时间",timeEdit);
    ntpLabel=new QLabel;ntpLabel->setWordWrap(true);outer->addWidget(ntpLabel);
    auto hint=new QLabel("所选日期时间按服务器时区解释。需要 root 权限，非 root 时仅尝试已有的免密 sudo。修改可能影响服务、证书和定时任务。");hint->setWordWrap(true);hint->setProperty("muted",true);outer->addWidget(hint);
    statusLabel=new QLabel;statusLabel->setObjectName("serverClockStatus");statusLabel->setTextFormat(Qt::PlainText);statusLabel->setWordWrap(true);outer->addWidget(statusLabel);
    auto buttons=new QDialogButtonBox;refreshButton=buttons->addButton("刷新",QDialogButtonBox::ActionRole);auto cancel=buttons->addButton("取消",QDialogButtonBox::RejectRole);applyButton=buttons->addButton("应用",QDialogButtonBox::AcceptRole);applyButton->setObjectName("serverClockApply");outer->addWidget(buttons);
    connect(cancel,&QPushButton::clicked,d,&QDialog::reject);connect(refreshButton,&QPushButton::clicked,d,[this]{refresh(true);});connect(applyButton,&QPushButton::clicked,d,[this]{apply();});
    connect(d,&QDialog::finished,this,[this,d]{cancelIntent();if(dialog==d)dialog=nullptr;dialogInitialized=false;operationFeedback.clear();for(auto child:d->findChildren<QDialog*>())child->reject();});
    dialogInitialized=false;if(valid){const auto time=displayedTime();dateEdit->setDate(time.date());timeEdit->setTime(time.time());dialogInitialized=true;}
    d->resize(uiPx(440),d->sizeHint().height());updateView();d->open();if(!valid)refresh(true);
}
void ServerClock::apply(){
    if(!dialog||!session||!connected||!active||!valid||busy||!dialogInitialized||!dateEdit||!timeEdit||OperationGate::locked())return;
    const auto value=dateEdit->date().toString("yyyy-MM-dd")+" "+timeEdit->time().toString("HH:mm:ss");
    if(!serverclock::validLocalDateTime(value)){feedbackText="日期时间无效。";updateView();return;}
    const auto token=OperationGate::token(),revision=generation;
    QPointer<ServerClock> self=this;QPointer<Session> origin=session;QPointer<QDialog> form=dialog;
    const auto profile=origin->profile;const auto zone=sample.zone+" "+serverclock::offsetText(sample.offsetSeconds);
    const auto old=displayedTime().toString("yyyy-MM-dd HH:mm:ss");const auto ntp=sample.ntp;
    const auto environment=profile.environment.trimmed();
    const bool critical=environment=="生产"||environment=="个人"||environment=="正式"||environment=="生产环境"||environment=="正式环境";
    const auto address=profile.host+":"+QString::number(profile.port);
    auto message=QString("服务器：%1 (%2)\n环境：%3\n服务器时区：%4\n%5 → %6\n需要 root 权限，非 root 时仅尝试已有的免密 sudo。\n修改可能影响服务、证书和定时任务。")
        .arg(profile.name,address,profile.environment,zone,old,value);
    if(ntp=="yes")message+="\n自动校时已开启，修改可能被校时服务覆盖；本工具不会关闭自动校时。";
    else if(ntp=="unknown")message+="\n自动校时状态不确定，修改可能被覆盖。";
    auto acceptable=[&]{return self&&origin&&form&&OperationGate::accepts(token)&&self->generation==revision&&self->session==origin&&self->dialog==form&&self->connected&&self->active&&self->valid&&!self->busy&&self->dialogInitialized&&self->dateEdit&&self->timeEdit;};
    QPointer<AppMessageBox> confirm=new AppMessageBox(QMessageBox::Warning,"确认修改服务器时间",message,QMessageBox::Yes|QMessageBox::Cancel,form);
    confirm->setObjectName("serverClockConfirm");confirm->setTextFormat(Qt::PlainText);confirm->setDefaultButton(QMessageBox::Cancel);confirm->setEscapeButton(QMessageBox::Cancel);
    confirm->button(QMessageBox::Yes)->setText("确认修改");
    const auto answer=confirm->exec();if(confirm)confirm->deleteLater();
    if(answer!=QMessageBox::Yes||!acceptable())return;
    if(critical){
        const auto escapedName=profile.name.toHtmlEscaped(),escapedAddress=address.toHtmlEscaped(),escapedEnvironment=profile.environment.toHtmlEscaped(),escapedZone=zone.toHtmlEscaped(),escapedValue=value.toHtmlEscaped();
        const auto warning=QString("服务器：%1 (%2)<br>环境：<span style=\"color:#e53935; font-weight:bold;\">%3</span><br>服务器时区：%4<br>目标时间：%5<br><br>修改可能影响线上服务、证书校验、日志时间顺序和定时任务。请确认仍要修改。")
            .arg(escapedName,escapedAddress,escapedEnvironment,escapedZone,escapedValue);
        QPointer<AppMessageBox> second=new AppMessageBox(QMessageBox::Critical,"严重确认：修改服务器时间",warning,QMessageBox::Yes|QMessageBox::Cancel,form);
        second->setObjectName("serverClockCriticalConfirm");second->setTextFormat(Qt::RichText);second->setDefaultButton(QMessageBox::Cancel);second->setEscapeButton(QMessageBox::Cancel);
        second->button(QMessageBox::Yes)->setText("仍然修改");
        const auto secondAnswer=second->exec();if(second)second->deleteLater();
        if(secondAnswer!=QMessageBox::Yes||!acceptable())return;
    }
    self->operationFeedback.clear();self->feedbackText.clear();self->submit("set",{{"local",value},{"allowUnknown",ntp=="unknown"},{"allowActiveNtp",ntp=="yes"},{"lockEpoch",QVariant::fromValue(token)}});
}
}
