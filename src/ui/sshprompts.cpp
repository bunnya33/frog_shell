#include "messagebox.h"
#include "../core/operationgate.h"
#include "sshprompts.h"
namespace rs {
namespace {
struct TrustRequest {
    QPointer<QWidget> owner;
    Profile profile;
    QString fingerprint;
    quint64 epoch;
    bool changed;
    std::function<bool()> valid;
    std::function<void(bool)> done;
    QString key() const { return profile.credentialKey()+"\n"+fingerprint+ (changed?"\nchanged":""); }
};
class TrustQueue : public QObject {
  public:
    TrustQueue() : QObject(qApp) {}
    QList<TrustRequest> pending;
    QPointer<QMessageBox> prompt;
    QString showing;
    quint64 nextToken=0,currentToken=0;
    bool advancing=false;
    void next() {
        if(prompt || advancing || pending.isEmpty())return;
        QScopedValueRollback<bool> advance(advancing,true);
        while(!pending.isEmpty() && (!pending.first().owner || !pending.first().valid()||!OperationGate::accepts(pending.first().epoch))) {
            auto r=pending.takeFirst();if(r.owner)r.done(false);
        }
        if(pending.isEmpty())return;
        const auto request=pending.first();showing=request.key();
        const auto token=currentToken=++nextToken;
        prompt=new AppMessageBox(request.changed?QMessageBox::Critical:QMessageBox::Question,
            request.changed?"主机身份变化":"首次连接 · 确认主机身份",
            request.profile.identity()+"\n\n"+request.fingerprint+
                (request.changed?"\n已中止连接。请通过可信渠道核实后管理 known_hosts。":"\n\n仅在确认这是预期服务器后选择“是”。"),
            request.changed?QMessageBox::Ok:QMessageBox::Yes|QMessageBox::No,request.owner);
        prompt->setTextFormat(Qt::PlainText);prompt->setDefaultButton(request.changed?QMessageBox::Ok:QMessageBox::No);
        prompt->setWindowModality(Qt::WindowModal);prompt->setAttribute(Qt::WA_DeleteOnClose);
        connect(prompt,&QDialog::finished,this,[this,token,changed=request.changed](int result){
            finish(token,!changed && result==QMessageBox::Yes);
        });
        // Parent destruction cancels the pending requests without retaining workers.
        connect(prompt,&QObject::destroyed,this,[this,token]{finish(token,false);});
        prompt->open();
    }
    void finish(quint64 token,bool accepted) {
        if(currentToken!=token)return;
        currentToken=0;prompt=nullptr;
        QList<TrustRequest> ready;
        for(int n=pending.size()-1;n>=0;--n)if(pending[n].key()==showing)ready.prepend(pending.takeAt(n));
        showing.clear();
        for(auto &r:ready)if(r.owner)r.done(accepted&&r.valid()&&OperationGate::accepts(r.epoch));
        QTimer::singleShot(0,this,[this]{next();});
    }
};
TrustQueue *queue() { static QPointer<TrustQueue> service; if(!service)service=new TrustQueue; return service; }
}
void requestHostTrust(QWidget *owner,const Profile &profile,const QString &fingerprint,bool changed,
                      std::function<bool()> valid,std::function<void(bool)> done) {
    auto service=queue();service->pending.append({owner,profile,fingerprint,OperationGate::token(),changed,std::move(valid),std::move(done)});
    // Let a previous QMessageBox finish hiding before showing the next modal window.
    QTimer::singleShot(0,service,[service]{service->next();});
}
}
