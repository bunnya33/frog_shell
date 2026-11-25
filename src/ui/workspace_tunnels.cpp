#include "messagebox.h"
#include "../core/operationgate.h"
#include "workspace.h"
#include "sshprompts.h"
#include <algorithm>
namespace rs {
std::optional<TunnelCounts> Workspace::tunnelCounts() const { return portsPanel->tunnelCounts(); }
int Workspace::tunnelCount() const {
    QSet<QString> ids=wantedTunnels;
    for(const auto &rule:portsPanel->rules()) {
        const auto state=portsPanel->state(rule.id);
        if(state=="running" || state=="connecting" || state=="starting" || state=="stopping" ||
           state=="unresponsive" || state=="reconnecting")ids.insert(rule.id);
    }
    return qMax(int(ids.size()),tunnelService?tunnelService->activeCount():0);
}
void Workspace::showPorts() {bottomTabs->setCurrentIndex(1);terminalPanel->show();}
void Workspace::startPortRule(const QString &id) {
    showPorts();if(const auto rule=portsPanel->rule(id))requestTunnelStart(*rule);
}
QString Workspace::tunnelStartError() const {
    if(OperationGate::locked())return "应用已锁定。";
    if(shuttingDown)return "工作区正在关闭。";
    if(demo)return "预览模式不连接服务器，不能启动端口转发。";
    if(repository) {
        const auto current=repository->find(profile.id);
        if(!current)return "连接配置已删除，不能启动新的转发。";
        if(current->credentialKey()!=profile.credentialKey())return "连接身份已更改，请关闭旧工作区后重新打开。";
    }
    return {};
}
void Workspace::requestTunnelStart(const TunnelRule &rule) {
    queueTunnelStart(rule,true);
}
void Workspace::restorePortRules() {
    if(connectionAutoStarted || shuttingDown || OperationGate::locked())return;
    connectionAutoStarted=true;
    for(const auto &rule:portsPanel->rules())
        if(rule.autoStart && !wantedTunnels.contains(rule.id) &&
           (portsPanel->state(rule.id)!="running" || tunnelRetiring || !tunnelService))queueTunnelStart(rule,false);
}
void Workspace::showTunnelFailure(const QString &id, const QString &reason) {
    if(shuttingDown || closing || OperationGate::locked())return;
    const auto rule=portsPanel->rule(id);if(!rule)return;
    auto box=new AppMessageBox(QMessageBox::Warning,"无法启动端口转发",
        "本地监听："+tunnelEndpoint(rule->localAddress,rule->localPort)+"\n远端目标："+
        tunnelEndpoint(rule->remoteAddress,rule->remotePort)+"\n\n"+reason,QMessageBox::Ok,this);
    box->setObjectName("tunnelStartFailure");box->setTextFormat(Qt::PlainText);
    box->setAttribute(Qt::WA_DeleteOnClose);box->open();
}
void Workspace::reportTunnelRestoreFailures() {
    if(!restoringTunnels.isEmpty() || tunnelRestoreFailures.isEmpty())return;
    const auto failed=tunnelRestoreFailures;tunnelRestoreFailures.clear();
    if(shuttingDown || closing || OperationGate::locked())return;
    QStringList lines;int count=0;
    for(auto it=failed.cbegin();it!=failed.cend();++it)if(const auto rule=portsPanel->rule(it.key())) {
        ++count;if(lines.size()<8)lines<<tunnelEndpoint(rule->localAddress,rule->localPort)+"\n"+it.value();
    }
    if(!count)return;
    if(count>lines.size())lines<<QString("另有 %1 项，详情见各行提示。").arg(count-lines.size());
    auto box=new AppMessageBox(QMessageBox::Warning,"部分端口转发未能恢复",
        QString("本次有 %1 项端口转发未能启动。\n\n").arg(count)+lines.join("\n\n")+
        "\n\n启用状态已保留，下次连接会再次尝试。可从端口菜单取消自动恢复。",QMessageBox::Ok,this);
    box->setObjectName("tunnelRestoreFailure");box->setTextFormat(Qt::PlainText);
    box->setAttribute(Qt::WA_DeleteOnClose);box->open();
}
void Workspace::queueTunnelStart(const TunnelRule &rule, bool manual) {
    if(rule.profileId!=profile.id || !portsPanel->rule(rule.id))return;
    const auto error=tunnelStartError();
    if(!error.isEmpty()){portsPanel->setRuleState(rule.id,"failed",error);return;}
    if(wantedTunnels.contains(rule.id))return;
    if(manual)manualTunnelStarts.insert(rule.id);else restoringTunnels.insert(rule.id);
    wantedTunnels.insert(rule.id);queuedTunnelStarts.insert(rule.id,rule);
    portsPanel->setRuleState(rule.id,"connecting");emit stateChanged();ensureTunnels();
}
void Workspace::cancelQueuedTunnels(const QString &reason, bool preserveRecovery) {
    const auto pending=queuedTunnelStarts.keys();
    for(const auto &id:pending){
        if (preserveRecovery && recoveringTunnels.contains(id)) {
            portsPanel->setRuleState(id, "reconnecting", "解锁后继续恢复 SSH 转发");
            continue;
        }
        queuedTunnelStarts.remove(id);
        recoveringTunnels.remove(id);restoringTunnels.remove(id);manualTunnelStarts.remove(id);
        wantedTunnels.remove(id);portsPanel->setRuleState(id,"failed",reason);
    }
    if (preserveRecovery) tunnelReconnectTimer.stop();
    if(recoveringTunnels.isEmpty()){tunnelReconnectTimer.stop();tunnelReconnectDelaySeconds=1;}
    QTimer::singleShot(0,this,&Workspace::reportTunnelRestoreFailures);
    tunnelAuthRetry=false;emit stateChanged();
}
void Workspace::ensureTunnels() {
    if(queuedTunnelStarts.isEmpty() || shuttingDown || tunnelRetiring || tunnelReconnectTimer.isActive() ||
       OperationGate::locked())return;
    const auto error=tunnelStartError();if(!error.isEmpty()){cancelQueuedTunnels(error);return;}
    if(preparingCredentials)return;
    if(!tunnelService) {
        const bool backgroundOnly=std::all_of(queuedTunnelStarts.keyBegin(),queuedTunnelStarts.keyEnd(),
            [this](const QString &id){return recoveringTunnels.contains(id);});
        if(keyNeedsReview){
            if(backgroundOnly)cancelQueuedTunnels("请检查密钥，再手动启动转发。");
            else promptCredential(false,true);
            return;
        }
        if(!credential && profile.authentication!="agent") {
            if(repository && rememberCredential && !passwordWasRejected) {
                preparingCredentials=true;const auto generation=credentialGeneration;const auto lockEpoch=OperationGate::token();
                for(const auto &id:queuedTunnelStarts.keys())portsPanel->setRuleState(id,"connecting","正在读取保存的凭据…");
                repository->read(profile,this,[this,generation,lockEpoch,backgroundOnly](SecretResult result,quint64 token){
                    if(!OperationGate::accepts(lockEpoch))return;preparingCredentials=false;
                    if(shuttingDown || queuedTunnelStarts.isEmpty())return;
                    if(!repository || repository->revision(profile.id)!=token || generation!=credentialGeneration || !tunnelStartError().isEmpty()) {
                        cancelQueuedTunnels("连接配置已变化，请重新启动转发。");return;
                    }
                    if(result.status==SecretResult::Found){credential=std::move(result.value);ensureTunnels();}
                    else if(backgroundOnly)cancelQueuedTunnels("无法读取已保存的 SSH 凭据，请手动启动转发。");
                    else {if(result.status==SecretResult::Error)AppMessageBox::warning(this,"无法读取保存密码",result.error);promptCredential(false,true);}
                });
            } else if(backgroundOnly)cancelQueuedTunnels("请重新输入 SSH 凭据，再启动转发。");
            else promptCredential(false,true);
            return;
        }
        Profile target=profile;
        if(repository)if(const auto current=repository->find(profile.id))target=*current;
        tunnelService=new TunnelService(target,credential.value_or(QByteArray()),this);
        attachTunnels(tunnelService);
    }
    const auto pending=queuedTunnelStarts;queuedTunnelStarts.clear();
    for(auto it=pending.cbegin();it!=pending.cend();++it)
        if(wantedTunnels.contains(it.key()) && portsPanel->rule(it.key())) {
            if (recoveringTunnels.contains(it.key()))
                portsPanel->setRuleState(it.key(), "reconnecting", "正在重新连接 SSH");
            submittedTunnels.insert(it.key());submittedTunnelRules.insert(it.key(),it.value());tunnelService->startRule(it.value());
        }
}
void Workspace::scheduleTunnelReconnect() {
    if (queuedTunnelStarts.isEmpty() || tunnelReconnectTimer.isActive() || shuttingDown || closing) return;
    if (OperationGate::locked()) {
        cancelQueuedTunnels("应用已锁定，请解锁后手动启动。", true);
        return;
    }
    const auto error = tunnelStartError();
    if (!error.isEmpty()) { cancelQueuedTunnels(error); return; }
    const int delay = tunnelReconnectDelaySeconds;
    tunnelReconnectDelaySeconds = qMin(60, delay * 2);
    for (const auto &id : queuedTunnelStarts.keys()) {
        if (!recoveringTunnels.contains(id)) continue;
        portsPanel->setRuleState(id, "reconnecting", tunnelReconnectReason + QString("\n%1 秒后重试").arg(delay));
    }
    tunnelReconnectTimer.start(delay * 1000);
}
void Workspace::stopTunnel(const QString &id) {
    if(OperationGate::locked() || shuttingDown)return;
    portsPanel->rememberEnabled(id,false);
    manualTunnelStarts.remove(id);restoringTunnels.remove(id);tunnelRestoreFailures.remove(id);
    QTimer::singleShot(0,this,&Workspace::reportTunnelRestoreFailures);
    queuedTunnelStarts.remove(id);wantedTunnels.remove(id);
    recoveringTunnels.remove(id);
    if (recoveringTunnels.isEmpty()) {
        tunnelReconnectDelaySeconds = 1;
        if (tunnelReconnectTimer.isActive()) {
            tunnelReconnectTimer.stop();
            QTimer::singleShot(0, this, &Workspace::ensureTunnels);
        }
    }
    if(tunnelService && submittedTunnels.contains(id)) {
        bool keep=false;for(const auto &submitted:submittedTunnels)if(wantedTunnels.contains(submitted)){keep=true;break;}
        if(tunnelService->activeCount()<=1 || !keep)tunnelRetiring=true;
        portsPanel->setRuleState(id,"stopping");tunnelService->stopRule(id);
    } else portsPanel->setRuleState(id,"stopped");
    emit stateChanged();
}
void Workspace::restartTunnel(const TunnelRule &rule) {
    const auto error=tunnelStartError();
    if(!error.isEmpty()){portsPanel->setRuleState(rule.id,portsPanel->state(rule.id),error);return;}
    manualTunnelStarts.insert(rule.id);
    wantedTunnels.insert(rule.id);queuedTunnelStarts[rule.id]=rule;
    if(tunnelService && submittedTunnels.contains(rule.id)) {
        portsPanel->setRuleState(rule.id,"stopping");
        if(tunnelService->activeCount()<=1)tunnelRetiring=true;
        tunnelService->stopRule(rule.id);
    } else ensureTunnels();
    emit stateChanged();
}
void Workspace::stopTunnels() {
    const auto queued=queuedTunnelStarts.keys();queuedTunnelStarts.clear();wantedTunnels.clear();manualTunnelStarts.clear();
    recoveringTunnels.clear();tunnelReconnectTimer.stop();tunnelReconnectDelaySeconds=1;
    restoringTunnels.clear();tunnelRestoreFailures.clear();tunnelAuthRetry=false;
    for(const auto &id:queued)if(portsPanel->state(id)!="running")portsPanel->setRuleState(id,"stopped");
    if(tunnelService){tunnelRetiring=true;tunnelService->stopAll();}
    emit stateChanged();
}
void Workspace::attachTunnels(TunnelService *service) {
    const auto generation=credentialGeneration;
    const auto token=repository?repository->revision(profile.id):0;
    const auto attempted=credential;
    const bool remember=rememberCredential,shouldPersist=persistCredential;
    connect(service,&TunnelService::authenticated,this,[this,service,generation,token,attempted,remember,shouldPersist]{
        if(service!=tunnelService)return;
        if (OperationGate::locked()) {
            for (const auto &id : submittedTunnels)
                if (recoveringTunnels.contains(id))
                    if (const auto rule = portsPanel->rule(id)) queuedTunnelStarts[id] = *rule;
            cancelQueuedTunnels("应用已锁定，请解锁后手动启动。", true);
            tunnelRetiring = true;
            service->stopAll();
            return;
        }
        if(shuttingDown || generation!=credentialGeneration || !tunnelStartError().isEmpty() ||
           (repository && token!=repository->revision(profile.id))) {
            cancelQueuedTunnels("连接配置或凭据已变化，转发未启动。");
            wantedTunnels.clear();tunnelRetiring=true;service->stopAll();return;
        }
        passwordWasRejected=false;keyNeedsReview=false;
        if(attempted && repository && shouldPersist && persistCredential) {
            persistCredential=false;
            repository->authenticated(profile,*attempted,remember,token,this,[this,token](bool ok,const QString &error){
                if(!ok && repository && token==repository->revision(profile.id))AppMessageBox::warning(this,"密码未保存",error+"\n本次连接仍可使用。");
            });
        }
    });
    auto retry=[this,service,generation](bool key){
        if(service!=tunnelService || shuttingDown || generation!=credentialGeneration)return;
        if(key)keyNeedsReview=true;
        else {if(credential)credential->fill('\0');credential.reset();passwordWasRejected=true;++credentialGeneration;}
        for (const auto &id : wantedTunnels) {
            if (recoveringTunnels.contains(id)) continue;
            if (const auto rule = portsPanel->rule(id)) queuedTunnelStarts[id] = *rule;
        }
        tunnelAuthRetry = !queuedTunnelStarts.isEmpty();
    };
    connect(service,&TunnelService::passwordRejected,this,[retry]{retry(false);});
    connect(service,&TunnelService::keyAuthenticationNeedsInput,this,[retry]{retry(true);});
    connect(service,&TunnelService::hostKey,this,[this,service,generation,token](const QString &fingerprint,bool changed){
        if (!changed && !submittedTunnels.isEmpty() && std::all_of(submittedTunnels.cbegin(), submittedTunnels.cend(),
            [this](const QString &id){return recoveringTunnels.contains(id);})) {
            service->trust(false);
            return;
        }
        QPointer<TunnelService> guarded=service;
        requestHostTrust(this,profile,fingerprint,changed,[this,guarded,generation,token]{
            return guarded && guarded==tunnelService && !wantedTunnels.isEmpty() &&
                   generation==credentialGeneration && !shuttingDown && tunnelStartError().isEmpty() &&
                   (!repository || repository->revision(profile.id)==token);
        },[guarded](bool accepted){if(guarded)guarded->trust(accepted);});
    });
    connect(service,&TunnelService::ruleTraffic,this,[this,service](const QString &id,quint64,quint64,quint64 up,quint64 down){
        if(service==tunnelService && wantedTunnels.contains(id) && !shuttingDown)portsPanel->setRuleTraffic(id,up,down);
    });
    connect(service, &TunnelService::transportLost, this, [this,service](const QString &reason) {
        if (service != tunnelService || shuttingDown || closing ||
            (!OperationGate::locked() && !tunnelStartError().isEmpty())) return;
        tunnelReconnectReason = reason;
        for (const auto &id : submittedTunnels) {
            if (!wantedTunnels.contains(id) || queuedTunnelStarts.contains(id)) continue;
            const auto state = portsPanel->state(id);
            if (state != "running" && state != "unresponsive" && !recoveringTunnels.contains(id)) continue;
            if (const auto rule = portsPanel->rule(id)) {
                recoveringTunnels.insert(id);
                queuedTunnelStarts[id] = *rule;
                portsPanel->setRuleState(id, "reconnecting", reason);
            }
        }
        if (!recoveringTunnels.isEmpty()) tunnelRetiring = true;
    });
    connect(service,&TunnelService::ruleStateChanged,this,[this,service](const QString &id,const QString &state,const QString &reason){
        if(service!=tunnelService)return;
        if ((state == "connecting" || state == "starting") && recoveringTunnels.contains(id)) return;
        if (state == "failed" && OperationGate::locked() && recoveringTunnels.contains(id) && wantedTunnels.contains(id))
            if (const auto rule = portsPanel->rule(id)) queuedTunnelStarts[id] = *rule;
        if(state=="running") {
            if(!wantedTunnels.contains(id) || queuedTunnelStarts.contains(id) || shuttingDown || tunnelRetiring) {
                service->stopRule(id);return;
            }
            if(portsPanel->state(id)!="running" && portsPanel->state(id)!="unresponsive") {
                const auto current=portsPanel->rule(id);const auto submitted=submittedTunnelRules.constFind(id);
                if(!current || submitted==submittedTunnelRules.cend() ||
                   current->localAddress!=submitted->localAddress || current->localPort!=submitted->localPort ||
                   current->remoteAddress!=submitted->remoteAddress || current->remotePort!=submitted->remotePort ||
                   OperationGate::locked()) {
                    wantedTunnels.remove(id);manualTunnelStarts.remove(id);service->stopRule(id);return;
                }
                portsPanel->rememberEnabled(id,true);
                if (recoveringTunnels.remove(id) && recoveringTunnels.isEmpty()) tunnelReconnectDelaySeconds = 1;
                manualTunnelStarts.remove(id);
                restoringTunnels.remove(id);QTimer::singleShot(0,this,&Workspace::reportTunnelRestoreFailures);
            }
        }
        if(state=="stopped" || state=="failed"){submittedTunnels.remove(id);submittedTunnelRules.remove(id);}
        if((state=="stopped" || state=="failed") && queuedTunnelStarts.contains(id) && wantedTunnels.contains(id)) {
            if (!recoveringTunnels.contains(id))
                portsPanel->setRuleState(id,"connecting",tunnelAuthRetry?"等待认证…":"正在应用新配置…");
            if(service->activeCount()>0 && !tunnelRetiring && !tunnelAuthRetry)QTimer::singleShot(0,this,&Workspace::ensureTunnels);
            else tunnelRetiring=true;
        } else {
            const bool notify=state=="failed" && wantedTunnels.contains(id) && manualTunnelStarts.remove(id);
            if((state=="stopped" || state=="failed") && restoringTunnels.remove(id)) {
                if(state=="failed" && wantedTunnels.contains(id))tunnelRestoreFailures.insert(id,reason);
                QTimer::singleShot(0,this,&Workspace::reportTunnelRestoreFailures);
            }
            portsPanel->setRuleState(id,state,reason);
            if(state=="stopped" || state=="failed"){
                wantedTunnels.remove(id);manualTunnelStarts.remove(id);
                if (recoveringTunnels.remove(id) && recoveringTunnels.isEmpty()) tunnelReconnectDelaySeconds=1;
            }
            if(notify)showTunnelFailure(id,reason);
        }
        if((state=="stopped" || state=="failed") && service->activeCount()==0)tunnelRetiring=true;
        emit stateChanged();
    });
    connect(service,&TunnelService::idle,this,[this,service]{
        if(service!=tunnelService){service->deleteLater();return;}
        tunnelService=nullptr;tunnelRetiring=false;submittedTunnels.clear();submittedTunnelRules.clear();service->deleteLater();
        const bool retry=tunnelAuthRetry;tunnelAuthRetry=false;emit stateChanged();
        if(shuttingDown)return;
        if(!queuedTunnelStarts.isEmpty()) {
            if(retry && !preparingCredentials)promptCredential(false,true);
            else if (!recoveringTunnels.isEmpty()) scheduleTunnelReconnect();
            else QTimer::singleShot(0,this,&Workspace::ensureTunnels);
        }
    });
}
} // namespace rs
