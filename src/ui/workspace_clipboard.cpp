#include "messagebox.h"
#include "../core/operationgate.h"
#include "workspace.h"
#include "fileclipboard.h"
#include "../core/profile.h"
#include "../core/fileoperationjournal.h"
#include "../core/filename.h"
#include <QClipboard>

namespace rs {
namespace {
struct MoveLock { QString identity, source, target; };
QHash<QString,MoveLock> &moveLocks() { static auto *locks=new QHash<QString,MoveLock>; return *locks; }
QHash<QByteArray,std::shared_ptr<FileClipboardBatch>> &internalDrags() {static auto *items=new QHash<QByteArray,std::shared_ptr<FileClipboardBatch>>;return *items;}
QString identity(const Profile &p) { return p.id + QChar(0x1f) + p.credentialKey(); }
bool within(const QString &path,const QString &root) { return path==root || (root=="/" ? path.startsWith('/') : path.startsWith(root+"/")); }
bool withinDestination(const QString &path,const QString &target) {
    if (within(path,target)) return true;
    const auto parent=remoteJoin(target,"..");const auto prefix=parent=="/"?parent:parent+'/';
    if(!path.startsWith(prefix))return false;
    const auto candidate=path.mid(prefix.size()).section('/',0,0),original=target.section('/',-1);
    // The lock is acquired before the worker stats the source type. Cover both
    // legal file and directory candidates, without locking unrelated siblings.
    return isNumberedFileName(original,candidate) || isNumberedFileName(original,candidate,true);
}
} // namespace
QByteArray Workspace::registerInternalDrag(const QVariantList &rows) {
    if(demo || !online || !session || authenticatedFingerprint.isEmpty() || rows.isEmpty() || rows.size()>256)return {};
    QStringList roots;
    for(const auto &value:rows) {
        const auto info=value.toMap();const auto path=info.value("path").toString();const auto mode=info.value("permissions").toUInt();
        if(!path.startsWith('/') || path=="/" || QDir::cleanPath(path)!=path || info.value("link").toBool() ||
           (!info.value("directory").toBool() && (mode&0170000)!=0100000))return {};
        roots<<path;
    }
    std::sort(roots.begin(),roots.end());QStringList unique;
    for(const auto &path:roots) {bool covered=false;for(const auto &root:unique)if(within(path,root)){covered=true;break;}if(!covered)unique<<path;}
    auto batch=std::make_shared<FileClipboardBatch>();batch->kind=FileClipboardBatch::Kind::Cut;batch->roots=unique;
    batch->owner=this;batch->origin=session;batch->generation=credentialGeneration;batch->sourceIdentity=identity(profile);
    batch->revision=repository?repository->revision(profile.id):0;
    const auto token=QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1();internalDrags().insert(token,batch);return token;
}
void Workspace::unregisterInternalDrag(const QByteArray &token) {internalDrags().remove(token);}
bool Workspace::validInternalDragSource(const QByteArray &token) const {
    const auto batch=internalDrags().value(token);if(!batch || !isValidInternalFileMoveToken(token) || !online || !session || shuttingDown)return false;
    auto source=batch->owner.data();
    if(!source || !source->online || source->session!=batch->origin || source->credentialGeneration!=batch->generation ||
       source->authenticatedFingerprint.isEmpty() || source->authenticatedFingerprint!=authenticatedFingerprint || identity(source->profile)!=identity(profile) ||
       (source->repository && source->repository->revision(source->profile.id)!=batch->revision))return false;
    return !batch->roots.isEmpty() && !batch->moving && !batch->failed;
}
bool Workspace::validInternalDrag(const QByteArray &token,const QString &target) const {
    if(!validInternalDragSource(token) || !target.startsWith('/') || QDir::cleanPath(target)!=target)return false;
    const auto batch=internalDrags().value(token);
    for(const auto &root:batch->roots)if(target.startsWith(root+'/'))return false;
    return true;
}
bool Workspace::internalDragNoop(const QByteArray &token,const QString &target) const {
    if(!validInternalDrag(token,target))return false;
    const auto batch=internalDrags().value(token);
    // A selected directory itself is neutral for the whole gesture, including
    // mixed roots: never move the other selected items into that directory.
    if(batch->roots.contains(target))return true;
    for(const auto &root:batch->roots)if(remoteJoin(target,root.section('/',-1))!=root)return false;
    return true;
}
void Workspace::acceptInternalDrag(const QByteArray &token,const QString &target) {
    if(OperationGate::locked())return;
    if(!validInternalDrag(token,target) || internalDragNoop(token,target))return;
    const auto batch=internalDrags().value(token);QPointer<Workspace> owner=this;
    QStringList roots;for(const auto &root:batch->roots)if(remoteJoin(target,root.section('/',-1))!=root)roots<<root;
    const auto lockEpoch=OperationGate::token();
    QTimer::singleShot(0,this,[owner,batch,target,roots,lockEpoch]{
        if(!OperationGate::accepts(lockEpoch))return;
        if(!owner || !batch || !batch->owner || !batch->origin)return;
        if(!QSettings().value("files/confirmInternalMove",true).toBool()){owner->pasteFiles(target,batch);return;}
        const auto name=roots.first().section('/',-1);
        auto dialog=new AppMessageBox(QMessageBox::Question,"移动文件",roots.size()==1
            ?QString("是否将‘%1’移动到‘%2’？").arg(name,target)
            :QString("是否将‘%1’等 %2 项移动到‘%3’？").arg(name).arg(roots.size()).arg(target),QMessageBox::NoButton,owner);
        dialog->setAttribute(Qt::WA_DeleteOnClose);auto move=dialog->addButton("移动",QMessageBox::AcceptRole);
        auto cancel=dialog->addButton("取消",QMessageBox::RejectRole);dialog->setDefaultButton(cancel);dialog->setEscapeButton(cancel);
        auto skip=new QCheckBox("不再询问",dialog);dialog->setCheckBox(skip);
        QObject::connect(dialog,&QDialog::finished,owner,[owner,batch,target,dialog,move,skip,lockEpoch](int){
            if(!OperationGate::accepts(lockEpoch))return;
            if(owner && dialog->clickedButton()==move) {if(skip->isChecked())QSettings().setValue("files/confirmInternalMove",false);owner->pasteFiles(target,batch);}
        });dialog->open();
    });
}
namespace {
QList<Workspace *> workspaces(const Profile &p) {
    QList<Workspace *> result;
    for (auto widget:QApplication::allWidgets()) if (auto w=qobject_cast<Workspace *>(widget))
        if (identity(w->profile)==identity(p)) result<<w;
    return result;
}
}
bool Workspace::clipboardPathLocked(const QString &path) const {
    for (const auto &lock:moveLocks())
        if (lock.identity==identity(profile) && (within(path,lock.source) || withinDestination(path,lock.target))) return true;
    return false;
}
bool Workspace::clipboardMutationBlocked(const QString &path) const {
    for (const auto &lock:moveLocks()) if(lock.identity==identity(profile) &&
        (within(path,lock.source) || within(lock.source,path) || withinDestination(path,lock.target) || within(lock.target,path)))return true;
    return false;
}
bool Workspace::clipboardCanReplace(int id,const QString &path) const {
    const auto batch=clipboardHolds.value(id);
    if(batch && batch->roots.contains(path))return false;
    for(auto w:workspaces(profile)) if(w->editors.contains(path))return false;
    return true;
}
void Workspace::updateMovedEditors(const QString &oldPath,const QString &newPath) {
    const auto keys=editors.keys();
    for (const auto &key:keys) if (within(key,oldPath)) {
        auto e=editors.take(key);
        if (e) {e->setPath(newPath+key.mid(oldPath.size()));editors[e->path]=e;updateEditorTitle(e);}
    }
}
void Workspace::applyJournalUndo(const QVariantMap &effect) {
    const auto from=effect.value("old").toString(),to=effect.value("path").toString();
    if(from.isEmpty() || authenticatedFingerprint.isEmpty())return;
    for(auto w:workspaces(profile)) {
        if(w->authenticatedFingerprint!=authenticatedFingerprint)continue;
        const auto kind=effect.value("kind").toString();
        if(kind=="create" || kind=="uploadNew") {
            for(const auto &path:w->editors.keys())if(within(path,from))if(auto editor=w->editors.value(path))
                editor->setProperty("remoteRemovedByUndo",true);
            w->syncFileMutation(from,true);
            w->taskSummary->setText("新建的远端项目已撤销；打开的草稿保留。");
            continue;
        }
        if(to.isEmpty())continue;
        // A destination editor must be protected before submitting undo. Preserve
        // both documents if a concurrent UI change nevertheless introduces one.
        bool collision=false;
        for(const auto &key:w->editors.keys()) if(within(key,from)) {
            const auto destination=to+key.mid(from.size());
            if(destination!=key && w->editors.contains(destination)) {collision=true;break;}
        }
        if(!collision)w->updateMovedEditors(from,to);
        w->syncFileMutation(from,true);
        w->requestDirectory(remoteJoin(to,".."));
        if(collision)w->taskSummary->setText("撤销已应用，但目标编辑器已打开；两个草稿均已保留，请核查路径。");
    }
}
void Workspace::requestJournalSnapshot(std::function<void(const QVariantMap &,const QString &)> callback) {
    if(!online || !session || authenticatedFingerprint.isEmpty()) {callback({},"请先连接并认证服务器。");return;}
    Job job;job.type="journalSnapshot";job.id=nextJob++;journalCallbacks.insert(job.id,std::move(callback));session->submit(job);
}
void Workspace::undoLastFileOperation() {
    if(OperationGate::locked())return;
    QPointer<Workspace> owner=this;QPointer<Session> origin=session;const auto fingerprint=authenticatedFingerprint;
    requestJournalSnapshot([owner,origin,fingerprint](const QVariantMap &result,const QString &error){
        if(!owner || !origin || owner->session!=origin || owner->authenticatedFingerprint!=fingerprint)return;
        if(!error.isEmpty()){owner->taskSummary->setText(error);return;}
        const auto batches=result.value("journal").toMap().value("batches").toList();QVariantMap selected;
        for(const auto &value:batches) {
            const auto batch=value.toMap();if(batch.value("open").toBool()){owner->taskSummary->setText("仍有文件操作批次未完成，请先完成或核查。");return;}
            for(const auto &entry:batch.value("effects").toList())if(QStringList{"prepared","unknown","undo_prepared","undo_unknown"}.contains(entry.toMap().value("state").toString())) {
                owner->taskSummary->setText("操作结果未知，请刷新核实远端；关闭该服务器的相关连接后重连可清空撤销历史。");return;
            }
            for(const auto &entry:batch.value("effects").toList())
                if(entry.toMap().value("state").toString()=="committed")selected=batch;
        }
        if(selected.isEmpty()){owner->taskSummary->setText("没有可撤销的文件操作。");return;}
        const auto effects=selected.value("effects").toList();QStringList protectedPaths;
        for(const auto &value:effects) {
            const auto effect=value.toMap();if(effect.value("state").toString()!="committed")continue;
            const auto kind=effect.value("kind").toString(),source=effect.value("source").toString(),target=effect.value("target").toString();
            if(kind=="delete" || kind=="overwrite") {owner->taskSummary->setText("最近批次包含永久删除或覆盖，不能撤销。");return;}
            for(const auto &path:{source,target})if(!path.isEmpty() && !protectedPaths.contains(path))protectedPaths<<path;
            if(kind=="move" || kind=="rename")for(auto w:workspaces(owner->profile))if(w->authenticatedFingerprint==fingerprint)
                for(const auto &path:w->editors.keys())if(within(path,source)) {owner->taskSummary->setText("原位置有打开的编辑器，请关闭后撤销。");return;}
        }
        for(auto w:workspaces(owner->profile))if(w->authenticatedFingerprint==fingerprint) {
            for(const auto &path:protectedPaths)if(w->clipboardMutationBlocked(path)){owner->taskSummary->setText("相关文件正在操作，请稍后重试。");return;}
            auto affected=[&](const QString &path){for(const auto &p:protectedPaths)if(within(path,p)||within(p,path))return true;return false;};
            for(const auto &path:w->pendingReads)if(affected(path)){owner->taskSummary->setText("相关文件正在读取，请稍后撤销。");return;}
            for(auto editor:w->pendingSaves)if(editor && affected(editor->path)){owner->taskSummary->setText("相关文件正在保存，草稿保留，请稍后撤销。");return;}
        }
        auto submit=[owner,origin,fingerprint,selected,protectedPaths]{
            if(!owner || !origin || owner->session!=origin || !owner->online || owner->authenticatedFingerprint!=fingerprint)return;
            auto lockIds=std::make_shared<QStringList>();for(const auto &path:protectedPaths) {
                const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);moveLocks()[id]={identity(owner->profile),path,path};lockIds->append(id);
            }
            auto connection=std::make_shared<QMetaObject::Connection>();
            auto unlock=[lockIds,connection]{QObject::disconnect(*connection);for(const auto &id:*lockIds)moveLocks().remove(id);lockIds->clear();};
            *connection=QObject::connect(owner,&QObject::destroyed,origin,[unlock]{unlock();},Qt::SingleShotConnection);
            Job job;job.type="journalUndo";job.id=owner->nextJob++;job.options={{"operationBatch",selected.value("id")}};
            owner->journalCallbacks[job.id]=[owner,unlock](const QVariantMap &,const QString &reason){
                unlock();if(owner)owner->taskSummary->setText(reason.isEmpty()?"文件操作已撤销。":"撤销未完全完成，已成功的项目已更新；请刷新核实远端："+reason);
            };
            owner->taskSummary->setText("正在撤销文件操作…");origin->submit(job);
        };
        submit();
    });
}
void Workspace::completeClipboardJob(int id,const QVariantMap &result,const QString &error) {
    auto done=clipboardCompletions.take(id); clipboardValid.remove(id); clipboardHolds.remove(id);
    if (done) done(result,error);
}
void Workspace::cancelClipboardJobs(std::shared_ptr<FileClipboardBatch> only) {
    const auto ids=clipboardCompletions.keys();
    for (int id:ids) {
        if (only && clipboardHolds.value(id)!=only) continue;
        if (id==activeTransfer) {
            cancelledTransfers.insert(id);
            if (transferSession) transferSession->cancelTransfer();
        } else {
            transferQueue.removeAll(id); pausedTransfers.remove(id);
            if (transferItems.contains(id)) transferItems[id]->setText(1,"剪贴板任务已取消");
            completeClipboardJob(id,{},"剪贴板任务已取消或来源失效");
        }
    }
}
void Workspace::copyFileSelection(bool cut,const QList<QVariantMap> &captured) {
    if(OperationGate::locked())return;
    if (demo || !online || !session || shuttingDown) {taskSummary->setText("请连接服务器后使用文件剪贴板。");return;}
    QList<QVariantMap> selected=captured;
    if (selected.isEmpty()) {
        if (files->currentItem() && files->currentItem()->data(0,ParentNavigationRole).toBool()) {
            taskSummary->setText("上一级是导航项，不能复制或剪切。");return;
        }
        for (auto row:files->selectedItems()) {
            if (row->data(0,ParentNavigationRole).toBool() || row->data(0,FileDraftRole).toBool()) {
                taskSummary->setText("导航项和新建占位项不能复制或剪切。");return;
            }
            selected<<row->data(0,Qt::UserRole).toMap();
        }
    }
    if (selected.isEmpty()) {taskSummary->setText("请先选择文件或目录。");return;}
    QStringList paths;
    for (const auto &info:selected) {
        const auto path=info.value("path").toString(); const auto name=info.value("name").toString();
        const auto mode=info.value("permissions").toUInt();
        if (!path.startsWith('/') || QDir::cleanPath(path)!=path || path=="/" ||
            name.isEmpty() || name=="." || name==".." || name.contains('/') || name.contains('\\') ||
            path.section('/',-1)!=name || !info.contains("directory") || info.value("link").toBool() ||
            (!info.value("directory").toBool() && (!info.contains("permissions") || (mode&0170000)!=0100000))) {
            taskSummary->setText("选择包含链接或类型不明的项目，未更改剪贴板。");return;
        }
        paths<<path;
    }
    std::sort(paths.begin(),paths.end()); QStringList roots;
    for (const auto &p:paths) {bool covered=false;for(const auto &root:roots)if(within(p,root)){covered=true;break;}if(!covered)roots<<p;}
    if (roots.size()>256 || (!cut && roots.size()+pendingTransfers()>256)) {taskSummary->setText("传输队列最多容纳 256 项，请分批复制。");return;}
    QString error; auto batch=FileClipboard::instance().create(cut?FileClipboardBatch::Kind::Cut:FileClipboardBatch::Kind::Copy,&error);
    if(!batch){taskSummary->setText(error);return;}
    batch->roots=roots; batch->owner=this; batch->origin=session; batch->generation=credentialGeneration;
    batch->sourceIdentity=identity(profile);
    batch->revision=repository?repository->revision(profile.id):0;
    if(cut) {FileClipboard::instance().offer(batch);taskSummary->setText("已剪切：仅可在同一服务器内移动；跨设备请复制。");return;}
    for(int n=0;n<roots.size();++n) {
        const auto slot=QDir(batch->spool).filePath(QString::number(n));
        if(!QDir().mkdir(slot)){batch->discard();taskSummary->setText("无法创建剪贴板暂存目录。");return;}
        batch->staged<<QDir(slot).filePath(roots[n].section('/',-1));
    }
    batch->remaining=roots.size();
    FileClipboard::instance().offer(batch);
    auto group=new QTreeWidgetItem(downloadList,{QString("准备复制到剪贴板 · %1 项").arg(roots.size()),"等待"});group->setExpanded(true);
    const QPersistentModelIndex groupIndex(downloadList->indexFromItem(group));
    QPointer<Workspace> owner=this; QPointer<Session> origin=session;
    for(int n=0;n<roots.size();++n) {
        Job j;j.id=nextJob++;j.type="download";j.path=roots[n];j.destination=batch->staged[n];j.collision="overwrite";j.options["clipboardStage"]=true;
        transfers[j.id]=j;transferQueue.enqueue(j.id);transferOrder<<j.id;clipboardHolds[j.id]=batch;
        auto item=new QTreeWidgetItem(group,{j.path.section('/',-1),"等待剪贴板准备"});item->setData(0,Qt::UserRole,j.id);transferItems[j.id]=item;
        clipboardValid[j.id]=[owner,origin,batch]{return owner && origin && owner->online && !owner->shuttingDown && owner->session==origin && owner->credentialGeneration==batch->generation && (!owner->repository || owner->repository->revision(owner->profile.id)==batch->revision);};
        clipboardCompletions[j.id]=[owner,batch,groupIndex](const QVariantMap &result,const QString &reason){
            if(!reason.isEmpty() || result.value("failed").toInt() || result.value("skipped").toInt()) {
                batch->failed=true;if(batch->error.isEmpty())batch->error=reason.isEmpty()?QString("有项目失败或被跳过：")+result.value("errors").toStringList().join("；"):reason;
            }
            if(--batch->remaining==0) {
                QString status;
                if(batch->failed) {batch->discard();status="剪贴板准备失败，未发布："+batch->error;}
                else if(!FileClipboard::instance().publish(batch)) {batch->discard();status="剪贴板已更改，准备结果未发布。";}
                else status="剪贴板已就绪，可在本地粘贴";
                if(owner) {owner->taskSummary->setText(status);if(groupIndex.isValid())if(auto group=owner->downloadList->itemFromIndex(groupIndex))group->setText(0,status);}
            } else if(owner)owner->taskSummary->setText(QString("正在准备剪贴板 · 剩余 %1 项").arg(batch->remaining));
        };
    }
    taskSummary->setText("正在准备剪贴板，完成后才可在本地粘贴。");
    transferPanel->show();transferPanel->raise();trimTransferHistory();ensureTransferSession();emit stateChanged();
}
QString Workspace::filePasteError(const QString &target,const QMimeData *mime,
                                 const std::shared_ptr<FileClipboardBatch> &batch,QStringList *local) const {
    if(demo || !online || !session || shuttingDown)return "请连接服务器后粘贴。";
    if(!target.startsWith('/') || QDir::cleanPath(target)!=target)return "没有有效的粘贴目标目录。";
    if(mime && mime->hasFormat(FileClipboard::mimeType()) && !batch)return "剪贴板令牌无效，请重新复制。";
    if(batch && batch->failed)return "复制准备失败，请重新复制。";
    if(batch && batch->kind==FileClipboardBatch::Kind::Cut) {
        auto source=batch->owner.data();
        if(!source || !batch->origin || !source->online || source->shuttingDown || source->session!=batch->origin || source->credentialGeneration!=batch->generation ||
           identity(source->profile)!=identity(profile) || source->authenticatedFingerprint.isEmpty() || source->authenticatedFingerprint!=authenticatedFingerprint ||
           (source->repository && source->repository->revision(source->profile.id)!=batch->revision))return "剪切来源已失效或属于另一服务器；请使用复制。";
        if(batch->moving)return "剪切任务正在进行。";
        if(batch->roots.isEmpty())return "剪切项目已移动。";
        if(pendingTransfers()+batch->roots.size()>256)return "传输队列已满，请稍后重试。";
        bool any=false;
        for(const auto &path:batch->roots) {
            if(within(target,path))return "不能将目录粘贴到自身或子目录。";
            any|=remoteJoin(target,path.section('/',-1))!=path;
        }
        return any?QString():QString("来源已在目标目录，无需移动。");
    }
    if(batch && (!batch->ready || batch->remaining!=0))return "剪贴板仍在准备，请完成后再粘贴。";
    if(batch) {
        QList<QUrl> expected;for(const auto &path:batch->staged)expected<<QUrl::fromLocalFile(path);
        if(!mime || mime->urls()!=expected)return "剪贴板内容已改变，请重新复制。";
        if(batch->sourceIdentity==identity(profile))for(const auto &root:batch->roots)
            if(within(target,root))return "不能将目录粘贴到自身或子目录。";
    }
    if(!mime || !mime->hasUrls() || mime->urls().isEmpty() || mime->urls().size()>256)return "剪贴板中没有可上传的本地文件或目录。";
    QStringList paths;
    for(const auto &url:mime->urls()) {
        const QFileInfo f(url.toLocalFile());
        if(!url.isLocalFile() || !f.exists() || f.isSymLink() || f.isJunction() || (!f.isFile() && !f.isDir()))return "剪贴板包含无效路径或链接，未上传。";
        if(!paths.contains(f.absoluteFilePath()))paths<<f.absoluteFilePath();
    }
    if(pendingTransfers()+paths.size()>256)return "传输队列已满，请稍后重试。";
    if(local)*local=paths;
    return {};
}
QString Workspace::selectedFileTarget() const {
    QString target;
        auto current=files->currentItem();
        if(current && (current->data(0,ParentNavigationRole).toBool() || current->data(0,FileDraftRole).toBool())) {
            return {};
        }
        if(current && files->selectedItems().size()==1) {
            const auto info=current->data(0,Qt::UserRole).toMap();
            if(info.value("link").toBool()){return {};}
            const auto path=info.value("path").toString();
            if(path.startsWith('/'))target=info.value("directory").toBool()?path:remoteJoin(path,"..");
        }
        if(target.isEmpty())target=directory;
    return target.startsWith('/')&&QDir::cleanPath(target)==target?target:QString();
}
void Workspace::pasteFiles(const QString &capturedTarget,std::shared_ptr<FileClipboardBatch> captured) {
    if(OperationGate::locked())return;
    if(demo || !online || !session || shuttingDown){taskSummary->setText("请连接服务器后粘贴。");return;}
    QString target=capturedTarget;
    if(target.isEmpty()) {
        target=selectedFileTarget();
    }
    if(!target.startsWith('/') || QDir::cleanPath(target)!=target){taskSummary->setText("没有有效的粘贴目标目录。");return;}
    const auto mime=captured?nullptr:QApplication::clipboard()->mimeData();
    auto batch=captured?captured:FileClipboard::instance().current();
    QStringList local;const auto error=filePasteError(target,mime,batch,&local);
    if(!error.isEmpty()){taskSummary->setText(error);return;}
    if(batch && batch->kind==FileClipboardBatch::Kind::Cut) {
        auto source=batch->owner.data();
        QStringList roots;
        for(const auto &path:batch->roots) {
            const auto destination=remoteJoin(target,path.section('/',-1));
            if(destination==path)continue;
            if(within(target,path)){taskSummary->setText("不能将目录粘贴到自身或子目录。");return;}
            roots<<path;
        }
        if(roots.isEmpty()){taskSummary->setText("来源已在目标目录，无需移动。");return;}
        QStringList destinations;
        for(const auto &root:roots)destinations<<remoteJoin(target,root.section('/',-1));
        for(const auto &destination:destinations) for(const auto &root:batch->roots)
            if(within(destination,root) || within(root,destination)) {taskSummary->setText("移动目标与剪切来源重叠，未移动。");return;}
        for(auto w:workspaces(profile)) {
            auto affected=[&](const QString &path) {for(const auto &root:roots)if(within(path,root))return true;for(const auto &dest:destinations)if(withinDestination(path,dest))return true;return false;};
            for(const auto &path:w->pendingReads) if(affected(path)){taskSummary->setText("相关文件正在读取，请稍后移动。");return;}
            for(auto e:w->pendingSaves) if(e && affected(e->path)){taskSummary->setText("相关文件正在保存，请稍后移动。");return;}
            for(const auto &path:w->editors.keys()) for(const auto &dest:destinations) if(withinDestination(path,dest)) {
                taskSummary->setText("目标文件已在编辑器中打开，请关闭后移动或选择其他目标。");return;
            }
        }
        batch->moving=true; batch->remaining=roots.size();
        auto sourceConnection=std::make_shared<QMetaObject::Connection>();
        *sourceConnection=connect(source,&Workspace::clipboardSourceInvalidated,this,[this,batch]{cancelClipboardJobs(batch);},Qt::SingleShotConnection);
        auto group=new QTreeWidgetItem(uploadList,{QString("移动 · %1 项").arg(roots.size()),"等待"});group->setExpanded(true);
        const auto operationBatch=FileOperationJournal::newId();
        QPointer<Workspace> owner=this, originOwner=source; QPointer<Session> origin=batch->origin;
        for(const auto &path:roots) {
            const auto lockId=QUuid::createUuid().toString(QUuid::WithoutBraces);moveLocks()[lockId]={identity(profile),path,remoteJoin(target,path.section('/',-1))};
            Job j;j.id=nextJob++;j.type="move";j.path=path;j.destination=remoteJoin(target,path.section('/',-1));j.collision="ask";
            j.options["operationBatch"]=operationBatch;
            j.options["operationBatchOpen"]=true;
            transfers[j.id]=j;transferQueue.enqueue(j.id);transferOrder<<j.id;clipboardHolds[j.id]=batch;
            auto item=new QTreeWidgetItem(group,{path.section('/',-1),"等待移动"});item->setData(0,Qt::UserRole,j.id);transferItems[j.id]=item;
            clipboardValid[j.id]=[owner,originOwner,origin,batch]{return owner && owner->online && !owner->shuttingDown && originOwner && origin && originOwner->online && originOwner->session==origin && originOwner->credentialGeneration==batch->generation && (!originOwner->repository || originOwner->repository->revision(originOwner->profile.id)==batch->revision);};
            clipboardCompletions[j.id]=[owner,originOwner,batch,path,lockId,sourceConnection,operationBatch](const QVariantMap &result,const QString &error){
                moveLocks().remove(lockId);
                if(error.isEmpty() && result.contains("path")) {
                    batch->roots.removeAll(path);
                    if(originOwner)for(auto w:workspaces(originOwner->profile))if(!originOwner->authenticatedFingerprint.isEmpty() && w->authenticatedFingerprint==originOwner->authenticatedFingerprint){w->updateMovedEditors(path,result["path"].toString());w->syncFileMutation(path,true);w->requestDirectory(remoteJoin(result["path"].toString(),".."));}
                } else if(owner)owner->taskSummary->setText("移动失败或结果未知，请刷新确认；未主动删除来源："+error);
                if(--batch->remaining==0){batch->moving=false;QObject::disconnect(*sourceConnection);
                    if(originOwner && originOwner->online && originOwner->session) {Job seal;seal.type="journalSeal";seal.id=originOwner->nextJob++;seal.options["operationBatch"]=operationBatch;originOwner->session->submit(seal);}}
            };
        }
        transferPanel->show();transferPanel->raise();trimTransferHistory();ensureTransferSession();emit stateChanged();return;
    }
    if(queueTransfers(local,true,target,true,batch))taskSummary->setText("已排队上传副本；本地来源保留。");
}
} // namespace rs
