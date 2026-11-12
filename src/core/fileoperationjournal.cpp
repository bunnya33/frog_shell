#include "fileoperationjournal.h"

namespace rs {
namespace {
constexpr qsizetype maxBytes=4*1024*1024;
constexpr int maxBatches=100, maxEffects=4096;
QMutex &journalMutex(){static QMutex mutex;return mutex;}
struct MemoryHistory {QJsonObject root;int users=0;};
QHash<QString,MemoryHistory> &histories(){static QHash<QString,MemoryHistory> entries;return entries;}
bool absolute(const QString &p) {return p.startsWith('/') && p!="/" && !p.contains(QChar::Null) && QDir::cleanPath(p)==p;}
bool overlap(const QString &a,const QString &b) {
    return !a.isEmpty() && !b.isEmpty() && (a==b || a.startsWith(b+'/') || b.startsWith(a+'/'));
}
bool fail(QString *error,const QString &message){if(error)*error=message;return false;}
}
QString FileOperationJournal::newId(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
FileOperationJournal::FileOperationJournal(const Profile &p,const QString &hostKey) {
    if(hostKey.isEmpty() || !hostKey.startsWith("SHA256:"))return;
    const auto bytes=(p.credentialKey()+QChar('\n')+hostKey).toUtf8();
    identity=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    path=QDir(dataPath()).filePath("file-operation-locks/"+identity+".lock");
    QMutexLocker guard(&journalMutex());auto &entry=histories()[identity];
    if(entry.users++==0)entry.root={{"version",1},{"identity",identity},{"batches",QJsonArray{}}};
}
FileOperationJournal::~FileOperationJournal(){if(identity.isEmpty())return;QMutexLocker guard(&journalMutex());auto it=histories().find(identity);if(it!=histories().end() && --it->users==0)histories().erase(it);}
std::unique_ptr<QLockFile> FileOperationJournal::acquireMutationGuard(QString *error) const {
    if(!valid()) {fail(error,"服务器身份尚未通过认证");return {};}
    const auto folder=QFileInfo(path).absolutePath();
    if(!QDir().mkpath(folder) || QFileInfo(folder).isSymLink() || QFileInfo(folder).isJunction()) {
        fail(error,"无法访问操作记录目录");return {};
    }
    auto guard=std::make_unique<QLockFile>(path);
    if(!guard->tryLock(0)) {fail(error,"同一服务器有文件操作进行中，请稍后重试");return {};}
    return guard;
}
bool FileOperationJournal::mutate(const std::function<bool(QJsonObject &,QString *)> &fn,QString *error) const {
    if(!valid())return fail(error,"服务器身份尚未通过认证，不能记录文件操作");
    QMutexLocker threadGuard(&journalMutex());
    auto it=histories().find(identity);if(it==histories().end())return fail(error,"操作记录会话已结束");
    auto root=it->root;
    if(!fn(root,error))return false;
    const auto bytes=QJsonDocument(root).toJson(QJsonDocument::Compact);
    if(bytes.size()>maxBytes)return fail(error,"本地操作记录已满，无法继续记录文件操作");
    it->root=std::move(root);
    return true;
}
bool FileOperationJournal::snapshot(QJsonObject *out,QString *error) const {
    if(!out || !valid())return fail(error,"服务器身份尚未通过认证");
    QMutexLocker threadGuard(&journalMutex());
    const auto it=histories().constFind(identity);if(it==histories().cend())return fail(error,"操作记录会话已结束");
    *out=it->root;return true;
}
bool FileOperationJournal::prepare(const QString &batchId,const QString &kind,const QString &source,const QString &target,const QVariantMap &attributes,QString *effectId,QString *error,bool keepOpen) const {
    if(batchId.isEmpty() || kind.isEmpty() || (!source.isEmpty() && !absolute(source)) || (!target.isEmpty() && !absolute(target)))return fail(error,"无效的操作记录路径或身份");
    const auto id=newId();
    const bool ok=mutate([&](QJsonObject &root,QString *why){
        auto batches=root.value("batches").toArray();int effects=0,found=-1;
        for(int i=0;i<batches.size();++i) {
            auto batch=batches[i].toObject();if(batch.value("id").toString()==batchId)found=i;
            const auto entries=batch.value("effects").toArray();effects+=entries.size();
            for(const auto &entry:entries) {
                const auto e=entry.toObject();const auto state=e.value("state").toString();
                if(state!="prepared" && state!="unknown" && state!="undo_prepared" && state!="undo_unknown")continue;
                for(const auto &a:{source,target})for(const auto &b:{e.value("source").toString(),e.value("target").toString(),e.value("undoSource").toString(),e.value("undoTarget").toString()})
                    if(overlap(a,b))return fail(why,"相关操作结果未知，请刷新核实远端；关闭该服务器的相关连接后重连可清空撤销历史");
            }
        }
        if(found<0 && batches.size()>=maxBatches) {
            const auto old=batches.first().toObject();
            if(old.value("open").toBool())return fail(why,"较早的文件操作仍未完成，请先核查");
            effects-=old.value("effects").toArray().size();batches.removeFirst();
        }
        if(effects>=maxEffects)return fail(why,"本地操作记录已满，无法继续记录文件操作");
        QJsonObject batch; if(found<0){batch={{"id",batchId},{"kind",kind},{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"open",keepOpen},{"effects",QJsonArray{}}};found=batches.size();batches.append(batch);}else {batch=batches[found].toObject();if(keepOpen)batch["open"]=true;}
        auto entries=batch.value("effects").toArray();
        entries.append(QJsonObject{{"id",id},{"kind",kind},{"source",source},{"target",target},{"attributes",QJsonObject::fromVariantMap(attributes)},{"state","prepared"}});
        batch["effects"]=entries;batches[found]=batch;root["batches"]=batches;return true;
    },error);
    if(ok && effectId)*effectId=id;return ok;
}
bool FileOperationJournal::sealBatch(const QString &id,QString *error) const {
    return mutate([&](QJsonObject &root,QString *why){
        auto batches=root.value("batches").toArray();
        for(int i=0;i<batches.size();++i) {auto batch=batches[i].toObject();if(batch.value("id").toString()!=id)continue;
            for(const auto &entry:batch.value("effects").toArray())if(entry.toObject().value("state").toString()=="prepared")
                return fail(why,"批次仍有未完成的文件操作");
            batch["open"]=false;batches[i]=batch;root["batches"]=batches;return true;
        }
        return true; // A wholly rejected batch has no effect or journal entry.
    },error);
}
bool FileOperationJournal::beginUndo(const QString &requestedBatch,QVariantMap *effect,QString *error) const {
    if(!effect)return fail(error,"缺少撤销结果容器");
    QVariantMap chosen;
    const bool ok=mutate([&](QJsonObject &root,QString *why){
        auto batches=root.value("batches").toArray();int selectedBatch=-1,selectedEffect=-1;
        for(int i=0;i<batches.size();++i) {
            const auto batch=batches[i].toObject();
            if(batch.value("open").toBool())return fail(why,"仍有文件操作批次未完成，请先完成或核查");
            const auto entries=batch.value("effects").toArray();
            for(const auto &value:entries) {const auto state=value.toObject().value("state").toString();
                if(state=="prepared" || state=="unknown" || state=="undo_prepared" || state=="undo_unknown")
                    return fail(why,"操作结果未知，请刷新核实远端；关闭该服务器的相关连接后重连可清空撤销历史");
            }
        }
        for(int i=batches.size()-1;i>=0 && selectedBatch<0;--i){const auto batch=batches[i].toObject();
            const auto entries=batch.value("effects").toArray();for(int n=entries.size()-1;n>=0;--n)
                if(entries[n].toObject().value("state").toString()=="committed"){selectedBatch=i;selectedEffect=n;break;}
        }
        if(selectedBatch<0)return fail(why,"没有可撤销的文件操作");
        auto batch=batches[selectedBatch].toObject();
        if(!requestedBatch.isEmpty() && batch.value("id").toString()!=requestedBatch)return fail(why,"只能先撤销最近的操作批次");
        auto entries=batch.value("effects").toArray();auto entry=entries[selectedEffect].toObject();
        for(const auto &value:entries) {const auto item=value.toObject();const auto kind=item.value("kind").toString();
            if(item.value("state").toString()=="committed" && (kind=="delete" || kind=="overwrite"))return fail(why,"最近批次包含永久删除或覆盖，不能撤销");}
        const auto kind=entry.value("kind").toString();QString from,to;
        if(kind=="create" || kind=="uploadNew") {from=entry.value("target").toString();}
        else if(kind=="rename" || kind=="move") {from=entry.value("target").toString();to=entry.value("source").toString();}
        else return fail(why,"此操作类型无法自动撤销");
        if(!absolute(from) || (!to.isEmpty() && !absolute(to)))return fail(why,"操作记录路径无效，无法撤销");
        entry["undoSource"]=from;entry["undoTarget"]=to;
        entry["state"]="undo_prepared";entry["batchId"]=batch.value("id");
        entries[selectedEffect]=entry;batch["effects"]=entries;batches[selectedBatch]=batch;root["batches"]=batches;
        chosen=entry.toVariantMap();return true;
    },error);
    if(ok)*effect=chosen;return ok;
}
bool FileOperationJournal::transition(const QString &id,const QString &expected,const QString &state,const QVariantMap &changes,QString *error) const {
    if(!QStringList{"committed","failed","unknown","undone","prepared","undo_prepared","undo_unknown"}.contains(state))return fail(error,"无效的操作状态");
    return mutate([&](QJsonObject &root,QString *why){
        auto batches=root.value("batches").toArray();
        for(int i=0;i<batches.size();++i){auto batch=batches[i].toObject();auto entries=batch.value("effects").toArray();
            for(int j=0;j<entries.size();++j){auto item=entries[j].toObject();if(item.value("id").toString()!=id)continue;
                if(item.value("state").toString()!=expected)return fail(why,"操作状态已变化，请刷新记录");
                item["state"]=state;for(auto it=changes.cbegin();it!=changes.cend();++it)item[it.key()]=QJsonValue::fromVariant(it.value());
                entries[j]=item;batch["effects"]=entries;batches[i]=batch;root["batches"]=batches;return true;
            }
        }return fail(why,"找不到操作记录");
    },error);
}
} // namespace rs
