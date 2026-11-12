#include "workspace.h"
#include "../core/operationgate.h"
#include "icons.h"
#include "filecontextmenu.h"
#include "layoutmetrics.h"
namespace rs {
void Workspace::markMonitoringStale(const QString &reason){
    if(demo||shuttingDown)return;
    const auto previous=serverPlatform->property("lastPlatform").toString();serverPlatform->setText(previous.isEmpty()?QString("等待系统采样"):previous+" · 已过期");
    serverPlatform->setToolTip(reason);resourceSummary->setToolTip("系统采样已过期；保留上次结果。\n"+reason);
}
void Workspace::requestMonitorSnapshot(){
    if(demo||!online||shuttingDown||OperationGate::locked()||monitorPending||listeningAuthBlocked)return;
    if(!listeningSession){
        if(listeningClock.isValid()&&listeningClock.elapsed()<15000)return;
        monitorPending=true;requestListeningSnapshot();if(!listeningSession)monitorPending=false;
    }else if(listeningReady){monitorPending=true;Job job;job.type="monitor";listeningSession->submit(job);}
    else monitorPending=true;
}
void Workspace::stopListening(const QString &reason) {
    listeningReady=false;listeningPending=false;monitorPending=false;markMonitoringStale(reason);
    if(listeningSession)listeningSession->stop();
    if(miniListening)miniListening->setStatus(reason);
    if(listeningTable)listeningTable->setStatus(reason);
    if(listeningStatus)listeningStatus->setToolTip(reason);
    if(miniListeningState){miniListeningState->setText(listeningSnapshot.available?"已过期":"不可用");miniListeningState->setToolTip(reason);}
    if(listeningHealth){listeningHealth->setText(listeningSnapshot.available?"已过期":"不可用");listeningHealth->setToolTip(reason);}
}
void Workspace::listeningAuthRejected() {
    listeningAuthBlocked=true;stopListening("系统/端口采集认证被拒绝；已停止自动重试，请重连工作区或更新认证");
}
void Workspace::requestListeningSnapshot() {
    // Reuse the existing monitor tick. Never read credentials or open another
    // connection under the app lock. Already accepted reads may finish normally.
    if(demo||!online||shuttingDown||OperationGate::locked()||listeningPending||listeningAuthBlocked)return;
    const bool visible=active&&((sidebar->isVisible()&&sidebar->currentIndex()==0)||(listeningPage&&listeningPage->isVisible()));
    if(listeningClock.isValid()&&listeningClock.elapsed()<(visible?5000:15000))return;
    listeningClock.start();
    if(!listeningSession) {
        if(!tunnelStartError().isEmpty()||(!credential&&profile.authentication!="agent")){stopListening("端口采集暂不可用；请先完成工作区认证");return;}
        auto s=new Session(profile,credential.value_or(QByteArray()),this,Session::Mode::Commands);listeningSession=s;listeningPending=true;
        const auto generation=credentialGeneration;const auto attempt=mainAttempt;const auto lockEpoch=OperationGate::token();
        const auto fingerprint=authenticatedFingerprint;auto seen=std::make_shared<QString>();
        connect(s,&Session::hostKey,s,[s](const QString &,bool){s->trust(false);});
        auto valid=[this,s,generation,attempt]{return s==listeningSession&&generation==credentialGeneration&&attempt==mainAttempt&&!shuttingDown;};
        connect(s,&Session::responseWaiting,this,[this,valid](bool waiting){if(waiting&&valid()&&online&&monitorPending)markMonitoringStale("系统采样响应缓慢；保留上次结果，主 SSH 会话继续运行。");});
        connect(s,&Session::passwordRejected,this,[this,valid]{if(valid())listeningAuthRejected();});
        connect(s,&Session::keyAuthenticationNeedsInput,this,[this,valid]{if(valid())listeningAuthRejected();});
        connect(s,&Session::authenticatedHostIdentity,this,[seen](const QString &fp){*seen=fp;});
        connect(s,&Session::connected,this,[this,s,generation,attempt,lockEpoch,fingerprint,seen]{
            if(s!=listeningSession||!online||shuttingDown||generation!=credentialGeneration||attempt!=mainAttempt||!OperationGate::accepts(lockEpoch)||fingerprint.isEmpty()||*seen!=fingerprint){s->stop();return;}
            listeningReady=true;listeningPending=true;
            if(monitorPending){Job monitorJob;monitorJob.type="monitor";s->submit(monitorJob);}
            Job j;j.type="listening-ports";s->submit(j);
        });
        connect(s,&Session::completed,this,[this,s,generation,attempt](const QString &type,int,const QVariantMap &r){
            if(s!=listeningSession||generation!=credentialGeneration||attempt!=mainAttempt||!online)return;
            if(type=="monitor"){
                monitorPending=false;
                if(r["unknown"].toBool())markMonitoringStale("系统采集响应超过上限；采集连接将重建，交互会话保留。");
                else monitor(r["data"].toByteArray(),r["latency"].toLongLong());
                return;
            }
            if(type!="listening-ports")return;
            listeningPending=false;applyListeningSnapshot(listening::parse({r["out"].toByteArray(),r["err"].toByteArray(),r["code"].toInt(),r["unknown"].toBool()}));
        });
        connect(s,&Session::failed,this,[this,valid](const QString &,int,const QString &why){if(valid()){listeningPending=false;stopListening("端口采集失败；保留上次结果 · "+why);}});
        connect(s,&Session::disconnected,this,[this,valid](const QString &){if(valid()){listeningReady=false;listeningPending=false;if(online&&!listeningAuthBlocked)stopListening("端口采集连接已断开；保留上次结果");}});
        connect(s,&QThread::finished,this,[this,s]{if(s==listeningSession){listeningSession=nullptr;listeningReady=false;listeningPending=false;}s->deleteLater();});
        s->start();return;
    }
    if(listeningReady){listeningPending=true;Job j;j.type="listening-ports";listeningSession->submit(j);}
}
void Workspace::applyListeningSnapshot(const listening::Snapshot &value) {
    if(value.complete&&value.available)listeningSnapshot=value;
    miniListeningState->setText(value.complete&&value.available?QString("%1%2").arg(listeningSnapshot.endpoints.size()).arg(value.truncated?" · 部分":""):listeningSnapshot.available?"已过期":"不可用");miniListeningState->setToolTip(value.error);
    miniListeningState->setStyleSheet("color:"+colors().accent.name()+";font-weight:500;");
    miniListening->applySnapshot(value);if(listeningTable)listeningTable->applySnapshot(value);
    if(listeningStatus)listeningStatus->setToolTip(!value.error.isEmpty()?value.error:value.available?QString("来源：%1；进程为当前用户可见的归属之一，端点可能共享。").arg(value.source):QString("监听端口不可用"));
    if(listeningHealth){listeningHealth->setText(value.complete&&value.available?(value.truncated?QString("部分"):QString()):miniListeningState->text());listeningHealth->setToolTip(!value.error.isEmpty()?value.error:value.truncated?QString("采集结果不完整，仅显示已取得的端口。") : listeningStatus->toolTip());}
}
void Workspace::showListeningPage() {
    if(OperationGate::locked())return;
    if(!listeningPage) {
        listeningPage=new QWidget;listeningPage->setWindowTitle("监听端口");
        auto layout=new QVBoxLayout(listeningPage);uiMargins(layout,12,12,12,12);uiSpacing(layout,8);
        auto filterRow=new QWidget;auto filterLayout=new QHBoxLayout(filterRow);uiMargins(filterLayout,0,0,0,0);uiSpacing(filterLayout,8);
        auto filter=new QLineEdit;filter->setObjectName("listeningFilter");filter->setPlaceholderText("筛选协议 / 地址 / 端口 / 进程 / PID");filter->setClearButtonEnabled(true);bandHeight(filter,28);uiMaximumWidth(filter,360);filterLayout->addWidget(filter,1);
        listeningStatus=new QLabel;listeningStatus->setProperty("muted",true);bindUiFont(listeningStatus,12);filterLayout->addWidget(listeningStatus);filterLayout->addStretch();layout->addWidget(filterRow);
        listeningHealth=new QLabel;listeningHealth->setObjectName("listeningHealth");listeningHealth->setProperty("muted",true);bindUiFont(listeningHealth,12);listeningHealth->setText(miniListeningState->text());listeningHealth->setToolTip(miniListeningState->toolTip());filterLayout->insertWidget(filterLayout->count()-1,listeningHealth);
        listeningTable=new ListeningTable(false);listeningTable->countsChanged=[this](int shown,int total){listeningStatus->setText(QString("已采集端口 %1 / %2").arg(shown).arg(total));};listeningTable->countsChanged(0,0);
        connect(filter,&QLineEdit::textChanged,listeningTable,&ListeningTable::setFilterText);layout->addWidget(processCard(listeningTable),1);
        listeningTable->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(listeningTable,&QWidget::customContextMenuRequested,this,[this](QPoint pos){
            if(OperationGate::locked())return;auto cell=listeningTable->itemAt(pos);if(!cell)return;const int row=cell->row();
            const QString protocol=listeningTable->item(row,0)->text(),address=listeningTable->item(row,1)->text();
            const auto port=quint16(listeningTable->item(row,2)->text().toUInt());
            FileContextMenu menu;menu.setToolTipsVisible(true);auto action=menu.addAction(resourceMenuIcon(Icon::Connection),"端口映射");
            if(protocol!="TCP"){action->setEnabled(false);action->setToolTip("SSH 本地端口转发仅支持 TCP。");}
            else if(portsPanel->hasDraft()){action->setEnabled(false);action->setToolTip("请先保存或取消当前端口编辑。");}
            if(menu.exec(listeningTable->viewport()->mapToGlobal(pos))==action && action->isEnabled() && !OperationGate::locked()){
                if(portsPanel->ensureMapping(address,port)){terminalPanel->show();bottomTabs->setCurrentIndex(1);}
            }
        });
        listeningTable->applySnapshot(listeningSnapshot);
        listeningStatus->setToolTip(miniListening->accessibleDescription());
    }
    if(groupFor(listeningPage))editArea->activatePage(listeningPage);
    else if(auto window=floating.value(listeningPage)){pagePresented(listeningPage);window->show();window->raise();window->activateWindow();}
    else editArea->addPage(listeningPage,"监听端口");
    if(demo) {
        listening::Snapshot s;s.available=true;s.complete=true;s.source="离线模拟";
        s.endpoints={{"TCP","0.0.0.0",22,2281,"sshd"},{"TCP","::",443,21483,"nginx"},{"TCP","127.0.0.1",5432,1567,"postgres"},{"UDP","0.0.0.0",5353,0,{}}};applyListeningSnapshot(s);
    } else if(online)requestListeningSnapshot();else stopListening("SSH 未连接，等待采集");
}
}
