#include "../core/operationgate.h"
#include "workspace.h"
namespace rs {
bool Workspace::bookmarkIdentityValid() const {
    if(!bookmarkStore || !bookmarkStore->accepts(profile.id))return false;
    if(!repository)return true;
    const auto current=repository->find(profile.id);
    return current&&current->credentialKey()==profile.credentialKey();
}
void Workspace::cancelBookmarkIntent() {
    if(bookmarkIntent && bookmarkIntent->job){
        directoryRequests.remove(bookmarkIntent->job);
        if(newestDirectoryRequest.value(bookmarkIntent->path)==bookmarkIntent->job)newestDirectoryRequest.remove(bookmarkIntent->path);
    }
    if(bookmarkIntent&&bookmarkStatus&&bookmarkStatus->text().startsWith("正在确认目录："))bookmarkStatus->hide();
    bookmarkIntent.reset();
}
void Workspace::openBookmark(const QString &id,BookmarkOpen mode) {
    if(OperationGate::locked()||shuttingDown||closing)return;
    if(!bookmarkIdentityValid()){bookmarkStatus->setText("连接已删除或身份已更改，请重新打开工作区。");bookmarkStatus->show();return;}
    const auto b=bookmarkStore->entry(profile.id,id);if(!b)return;
    if(bookmarkIntent&&bookmarkIntent->id==id&&bookmarkIntent->mode==mode)return;
    cancelBookmarkIntent();
    bookmarkIntent=BookmarkIntent{id,b->path,mode,0,{}};
    bookmarkStatus->setText("正在确认目录："+b->path);bookmarkStatus->show();
    if(demo){if(mode!=BookmarkOpen::Terminal){directory=b->path;updateBreadcrumbs();sidebar->setCurrentIndex(1);}if(mode!=BookmarkOpen::Directory){bottomTabs->setCurrentIndex(0);if(mode==BookmarkOpen::Terminal)addTerminal(0,b->path);else openBookmarkTerminal(*b);}bookmarkIntent.reset();bookmarkStatus->hide();return;}
    if(online)dispatchBookmark();
    else if(!hasMainConnection()&&!preparingCredentials)reconnect();
}
void Workspace::dispatchBookmark() {
    if(OperationGate::locked()||!bookmarkIntent||!online||!session||!bookmarkIdentityValid()){cancelBookmarkIntent();return;}
    if(!finishEntryDraft()){cancelBookmarkIntent();return;}
    const auto intent=*bookmarkIntent;
    Job job;job.type="list";job.path=intent.path;job.id=nextJob++;
    bookmarkIntent->origin=session;bookmarkIntent->job=job.id;
    if(intent.mode!=BookmarkOpen::Terminal){
        ++directoryGeneration;directoryRequests.clear();newestDirectoryRequest.clear();refreshDirectories.clear();
        directoryRequests[job.id]={intent.path,{},directoryGeneration,true,false};
        newestDirectoryRequest[intent.path]=job.id;latestNavigation=job.id;
    }
    // submit can synchronously emit failed when its bounded queue is full.
    session->submit(job);
}
bool Workspace::completeBookmark(int id,const QVariantMap &result) {
    if(!bookmarkIntent||bookmarkIntent->job!=id)return false;
    const auto intent=*bookmarkIntent;
    if(OperationGate::locked()||intent.origin!=session||!online||!bookmarkIdentityValid()){cancelBookmarkIntent();return true;}
    const auto current=bookmarkStore->entry(profile.id,intent.id);
    if(!current||current->path!=intent.path){cancelBookmarkIntent();return true;}
    // The successful SFTP listing is the validation. Empty directories are valid.
    // Do not dispatch a shell from an obsolete navigation response.
    if(intent.mode!=BookmarkOpen::Terminal &&
       (!directoryRequests.contains(id)||directoryRequests[id].generation!=directoryGeneration||latestNavigation!=id||newestDirectoryRequest.value(intent.path)!=id)){
        cancelBookmarkIntent();return true;
    }
    bookmarkIntent.reset();
    if(intent.mode!=BookmarkOpen::Terminal){renderDirectory(result,id);sidebar->setCurrentIndex(1);}
    if(intent.mode!=BookmarkOpen::Directory){bottomTabs->setCurrentIndex(0);if(intent.mode==BookmarkOpen::Terminal)addTerminal(0,intent.path);else openBookmarkTerminal(*current);}
    bookmarkStatus->hide();return true;
}
void Workspace::syncBookmarkGroups() {
    for(auto it=bookmarkGroups.begin();it!=bookmarkGroups.end();) {
        const auto entry=bookmarkStore?bookmarkStore->entry(profile.id,it->id):std::nullopt;
        if(!bookmarkIdentityValid()||!entry||entry->path!=it->path||it->identity!=profile.credentialKey())it=bookmarkGroups.erase(it);
        else {it->name=entry->name();++it;}
    }
    rebuildTerminals();
}
void Workspace::openBookmarkTerminal(const DirectoryBookmark &bookmark) {
    if(!bookmarkIdentityValid())return;
    syncBookmarkGroups();
    int group=0,best=0;quint64 recent=0;
    for(auto it=bookmarkGroups.cbegin();it!=bookmarkGroups.cend();++it)
        if(it->id==bookmark.id&&it->identity==profile.credentialKey()&&it->path==bookmark.path){group=it.key();break;}
    if(group)for(const auto &term:terminals)if(term.group==group&&term.running&&(!best||term.lastFocus>recent)){best=term.id;recent=term.lastFocus;}
    if(best){activateTerminal(best,true);return;}
    if(!group)group=nextGroup++;
    const int id=nextTerm;
    addTerminal(group,bookmark.path);
    if(terminals.contains(id)){
        bookmarkGroups[group]={bookmark.id,profile.credentialKey(),bookmark.path,bookmark.name()};
        rebuildTerminals();activateTerminal(id,true);
    }
}
QString Workspace::terminalLabel(const Term &term,bool includeGroup) const {
    QString name=term.name;
    if(!term.manualName&&!term.startupDirectory.isEmpty())name+=" · "+(term.startupDirectory=="/"?QString("/"):term.startupDirectory.section('/',-1));
    if(includeGroup&&bookmarkGroups.contains(term.group))name=bookmarkGroups[term.group].name+" / "+name;
    return name+(term.running?QString():QString(" [已结束]"));
}
}
