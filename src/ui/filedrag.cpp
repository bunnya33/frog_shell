#include "filedrag.h"
#include "theme.h"
#include "../core/filename.h"
namespace rs {
namespace {
QString safeName(QString name){
    for(auto &c:name)if(c.unicode()<32 || QString("<>:\"/\\|?*").contains(c))c='_';
    while(name.endsWith('.')||name.endsWith(' '))name.chop(1);
    if(name.isEmpty()||name=="."||name=="..")name="file";
    static const QRegularExpression reserved("^(CON|PRN|AUX|NUL|CONIN\\$|CONOUT\\$|COM[1-9¹²³]|LPT[1-9¹²³])(?:\\.|$)",QRegularExpression::CaseInsensitiveOption);
    if(reserved.match(name).hasMatch())name.prepend('_');
    // Leave room for a duplicate suffix and never split a UTF-16 pair.
    name=name.left(230);if(!name.isEmpty()&&name.back().isHighSurrogate())name.chop(1);
    while(name.endsWith('.')||name.endsWith(' '))name.chop(1);
    return name;
}
}
QByteArray internalFileMoveMimeType(){return "application/x-rabbitshell-internal-move";}
bool isValidInternalFileMoveToken(const QByteArray &token){
    if(token.size()!=36)return false;
    for(int i=0;i<36;++i){
        const char c=token[i];
        if(i==8||i==13||i==18||i==23){if(c!='-')return false;}
        else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    }
    return true;
}
QByteArray internalFileMoveToken(const QMimeData *mime){
    if(!mime)return {};
    const auto direct=mime->data(QString::fromLatin1(internalFileMoveMimeType()));
    if(isValidInternalFileMoveToken(direct))return direct;
    auto bridge=mime->data(QStringLiteral("application/x-qt-windows-mime;value=\"RabbitShell.InternalFileMove.v1\""));
    // Some Qt/Windows bridges expose GlobalSize padding. Accept at most 28 NULs,
    // and only on that bridge; the protocol payload itself is exactly 36 bytes.
    if(bridge.size()>36&&bridge.size()<=64){
        for(qsizetype i=36;i<bridge.size();++i)if(bridge[i]!='\0')return {};
        bridge.truncate(36);
    }
    return isValidInternalFileMoveToken(bridge)?bridge:QByteArray{};
}
std::shared_ptr<FileDragBatch> FileDragBatch::create(const QList<DragFile> &input,QString *error){
    auto fail=[&](QString why)->std::shared_ptr<FileDragBatch>{if(error)*error=why;return {};};
    if(input.isEmpty()||input.size()>256)return fail("一次最多拖出256个普通文件。");
    auto batch=std::make_shared<FileDragBatch>();if(!batch->spool.isValid())return fail("无法创建私有拖出暂存目录。");
    QSet<QString> paths,names;
    for(auto file:input){
        if(!file.remotePath.startsWith('/')||file.remotePath.contains(QChar::Null))return fail("远端文件路径无效。");
        if(paths.contains(file.remotePath))continue;paths.insert(file.remotePath);
        const auto base=safeName(file.name);file.name=base;int n=0;
        while(names.contains(file.name.toCaseFolded()))file.name=numberedFileName(base,++n);
        names.insert(file.name.toCaseFolded());batch->files<<file;batch->entries<<Entry{};
    }
    return batch;
}
QString FileDragBatch::stagePath(int i) const{return spool.filePath(QString::number(i)+".spool");}
bool FileDragBatch::ready(int i) const{QMutexLocker lock(&mutex);return i>=0&&i<entries.size()&&entries[i].done&&entries[i].error.isEmpty();}
void FileDragBatch::cancel(){
    if(cancelled.exchange(true))return;changed.wakeAll();auto self=shared_from_this();
    QMetaObject::invokeMethod(qApp,[self]{if(self->cancelJobs)self->cancelJobs();},Qt::QueuedConnection);
}
void FileDragBatch::finish(bool accepted){
    if(!accepted)cancel();auto self=shared_from_this();
    QMetaObject::invokeMethod(qApp,[self,accepted]{if(self->systemFinished)self->systemFinished(accepted);},Qt::QueuedConnection);
}
void FileDragBatch::request(int index){
    auto self=shared_from_this();
    QMetaObject::invokeMethod(qApp,[self,index]{
        if(self->cancelled){QMutexLocker lock(&self->mutex);self->entries[index].done=true;self->entries[index].error="已取消拖出";self->changed.wakeAll();return;}
        auto done=[self,index](QString error){QMutexLocker lock(&self->mutex);auto &entry=self->entries[index];if(entry.done)return;entry.error=std::move(error);entry.done=true;self->changed.wakeAll();};
        if(self->fetch)self->fetch(index,self->stagePath(index),done);else done("拖出来源已不可用");
    },Qt::QueuedConnection);
}
QString FileDragBatch::awaitFile(int index){
    if(index<0||index>=entries.size())return "无效文件索引";
    if(!released)return "尚未释放拖放";
    {QMutexLocker lock(&mutex);if(!entries[index].requested){entries[index].requested=true;request(index);}}
    if(QThread::currentThread()==qApp->thread()){
        QEventLoop loop;QTimer check;check.setInterval(20);
        QObject::connect(&check,&QTimer::timeout,&loop,[&]{QMutexLocker lock(&mutex);if(cancelled||entries[index].done)loop.quit();});
        check.start();{QMutexLocker lock(&mutex);if(!cancelled&&!entries[index].done){lock.unlock();loop.exec();}}
    }else{QMutexLocker lock(&mutex);while(!cancelled&&!entries[index].done)changed.wait(&mutex,100);}
    QMutexLocker lock(&mutex);return cancelled?QString("已取消拖出"):entries[index].error;
}
QPixmap fileDragBadge(const QIcon &icon,const QString &name,int count,qreal dpr){
    const auto font=uiFont();QFontMetrics fm(font);const auto text=fm.elidedText(name,Qt::ElideMiddle,uiPx(180))+(count>1?QString(" +%1").arg(count-1):QString());
    QSize size(qMax(uiPx(100),fm.horizontalAdvance(text)+uiPx(46)),uiPx(36));QPixmap pixmap(size*dpr);pixmap.setDevicePixelRatio(dpr);pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);p.setPen(colors().panelBorder);p.setBrush(colors().tabs);p.drawRoundedRect(QRectF(QPointF(.5,.5),QSizeF(size)-QSizeF(1,1)),uiReal(4),uiReal(4));
    icon.paint(&p,QRect(uiPx(8),uiPx(8),uiPx(20),uiPx(20)));p.setFont(font);p.setPen(colors().text);p.drawText(QRect(uiPx(36),0,size.width()-uiPx(40),size.height()),Qt::AlignVCenter,text);return pixmap;
}
#ifndef Q_OS_WIN
bool dragRemoteFiles(QWidget *,const std::shared_ptr<FileDragBatch> &,const QPixmap &){return false;}
#endif
}
