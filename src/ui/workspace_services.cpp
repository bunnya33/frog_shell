#include "messagebox.h"
#include "../core/operationgate.h"
#include "workspace.h"
#include "sshprompts.h"
namespace rs {
bool Workspace::hasServiceConnection() const {
    if(servicesReading || servicesPanel->pendingConnection())return true;
    for(auto s:serviceWorkers)if(s&&s->isRunning())return true;
    return false;
}
void Workspace::stopServices(){stopServerClock();++servicesGeneration;servicesReading=false;servicesPanel->stopLocal();for(auto s:serviceWorkers)if(s)s->stop();}
void Workspace::ensureServerClock() {
    if(!serverClock || clockSession || demo || !online || shuttingDown || closing ||
       !active || !isVisible() || OperationGate::locked() || (repository&&repository->pending()))return;
    // The interactive Files session can own PTYs. serviceCommand deliberately
    // refuses that mode; isolate clock reads/writes in a command-only worker.
    auto s=serviceSession(this,Session::Mode::Commands);
    if(!s)return;
    clockSession=s;serverClock->setActive(true);serverClock->setSession(s);
    connect(s,&QThread::finished,this,[this,s]{
        if(clockSession!=s)return;
        clockSession=nullptr;serverClock->setConnected(false);serverClock->setSession(nullptr);
    });
    s->start();
}
void Workspace::stopServerClock() {
    if(serverClock){serverClock->setConnected(false);serverClock->setSession(nullptr);}
    if(clockSession){auto s=clockSession;clockSession=nullptr;s->stop();}
}
void Workspace::ensureServices() {
    auto error=tunnelStartError();
    if(!error.isEmpty()){servicesPanel->connectionError(error);return;}
    if(servicesReading)return;
    if(preparingCredentials){servicesPanel->connectionError("正在处理认证，请完成后手动刷新服务。");return;}
    if(keyNeedsReview){promptCredential(false,false,true);return;}
    if(!credential&&profile.authentication!="agent") {
        if(repository&&rememberCredential&&!passwordWasRejected){
            servicesReading=true;const auto lockEpoch=OperationGate::token();const auto generation=credentialGeneration,serviceEpoch=servicesGeneration;
            repository->read(profile,this,[this,generation,serviceEpoch,lockEpoch](SecretResult r,quint64 token){
                if(serviceEpoch!=servicesGeneration||!OperationGate::accepts(lockEpoch))return;
                servicesReading=false;
                if(shuttingDown||!servicesPanel->pendingConnection()||!tunnelStartError().isEmpty()||generation!=credentialGeneration||!repository||repository->revision(profile.id)!=token){servicesPanel->connectionError("连接身份或凭据已变化；请重新打开工作区。");return;}
                if(r.status==SecretResult::Found){credential=std::move(r.value);ensureServices();}
                else promptCredential(false,false,true);
            });
        }else promptCredential(false,false,true);
        return;
    }
    auto s=serviceSession(this,Session::Mode::Commands);
    if(!s){servicesPanel->connectionError("服务连接不可用，请重新认证。");return;}
    servicesPanel->setSession(s);s->start();emit stateChanged();
}
Session *Workspace::serviceSession(QObject *owner,Session::Mode mode) {
    if(!tunnelStartError().isEmpty()||(!credential&&profile.authentication!="agent"))return nullptr;
    auto s=new Session(profile,credential.value_or(QByteArray()),owner,mode);
    serviceWorkers.erase(std::remove_if(serviceWorkers.begin(),serviceWorkers.end(),[](const auto &s){return s.isNull();}),serviceWorkers.end());serviceWorkers<<s;
    const auto generation=credentialGeneration,epoch=servicesGeneration;
    const auto token=repository?repository->revision(profile.id):0;
    const auto attempted=credential;
    const bool remember=rememberCredential,shouldPersist=persistCredential;
    if(mode==Session::Mode::Commands)servicesPanel->mutationGuard=[this,generation,epoch,token]{
        const auto why=tunnelStartError();if(!why.isEmpty())return why;
        return generation!=credentialGeneration||epoch!=servicesGeneration||(repository&&repository->revision(profile.id)!=token)?QString("服务连接身份或凭据已变化，请重新连接后操作。"):QString();
    };
    connect(s,&Session::hostKey,this,[this,s,generation,epoch,token](const QString &fp,bool changed){
        QPointer<Session> safe=s;
        requestHostTrust(this,profile,fp,changed,[this,safe,generation,epoch,token]{return safe&&!shuttingDown&&generation==credentialGeneration&&epoch==servicesGeneration&&tunnelStartError().isEmpty()&&(!repository||repository->revision(profile.id)==token);},[safe](bool yes){if(safe)safe->trust(yes);});
    });
    connect(s,&Session::authenticated,this,[this,s,generation,epoch,token,attempted,remember,shouldPersist]{
        if(shuttingDown||epoch!=servicesGeneration||generation!=credentialGeneration||!tunnelStartError().isEmpty()||(repository&&repository->revision(profile.id)!=token)){s->stop();return;}
        passwordWasRejected=false;keyNeedsReview=false;
        if(repository&&attempted&&shouldPersist&&persistCredential){
            persistCredential=false;auto current=repository->find(profile.id);
            if(current&&(remember||current->rememberSecret))repository->authenticated(profile,*attempted,remember,token,this,[this,token](bool ok,const QString &why){if(!ok&&repository&&repository->revision(profile.id)==token)AppMessageBox::warning(this,"密码未保存",why+"\n本次服务会话仍可使用。");});
        }
    });
    connect(s,&Session::passwordRejected,this,[this,generation]{if(generation!=credentialGeneration)return;stopServerClock();if(credential)credential->fill('\0');credential.reset();passwordWasRejected=true;++credentialGeneration;});
    connect(s,&Session::keyAuthenticationNeedsInput,this,[this,generation]{if(generation==credentialGeneration){stopServerClock();keyNeedsReview=true;}});
    connect(s,&QThread::finished,this,&Workspace::stateChanged);
    if(owner==this)connect(s,&QThread::finished,s,&QObject::deleteLater);
    return s;
}
}
