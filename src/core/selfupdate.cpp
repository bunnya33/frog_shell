#include "selfupdate.h"
#include <limits>
#include <utility>
#include <vector>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <sddl.h>
#include <aclapi.h>
#endif

namespace rs::selfupdate {
namespace {
constexpr quint64 MaxPackage = 1024ull * 1024 * 1024;
constexpr int MaxJson = 32768;
Result result(Code c, const QString &s) { return {c,s}; }
Result good() { return result(Code::Ok,QString()); }
bool canceled(const Canceled &f) { return f && f(); }
bool tokenValid(const QString &s) {
    static const QRegularExpression rx("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    return rx.match(s).hasMatch();
}
bool decimal(const QString &s, quint64 *v) {
    if(s.isEmpty() || s.size()>20) return false;
    for(const QChar c:s) if(c<'0'||c>'9') return false;
    bool ok=false;*v=s.toULongLong(&ok);return ok;
}
QByteArray digest(const QString &s) {
    if(s.size()!=64) return {};
    for(QChar c:s) if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return {};
    return QByteArray::fromHex(s.toLatin1());
}
QJsonObject fileJson(const FileIdentity &v) {
    return {{"volume",QString::number(v.volume)},{"index",QString::number(v.index)},
        {"bytes",QString::number(v.bytes)},{"modified",QString::number(v.modified)},
        {"sha256",QString::fromLatin1(v.sha256.toHex())}};
}
bool parseFile(const QJsonObject &o,FileIdentity &v,bool hashed=true) {
    if(!decimal(o.value("volume").toString(),&v.volume)||!decimal(o.value("index").toString(),&v.index)||
       !decimal(o.value("bytes").toString(),&v.bytes)||!decimal(o.value("modified").toString(),&v.modified))return false;
    v.sha256=digest(o.value("sha256").toString());return !hashed||v.sha256.size()==32;
}
QJsonObject processJson(const ProcessIdentity &v) {return {{"pid",QString::number(v.pid)},{"created",QString::number(v.created)},{"image",v.imagePath}};}
bool parseProcess(const QJsonObject &o,ProcessIdentity &v) {
    quint64 pid=0;if(!decimal(o.value("pid").toString(),&pid)||!pid||pid>(std::numeric_limits<quint32>::max)())return false;
    v.pid=quint32(pid);v.imagePath=o.value("image").toString();return decimal(o.value("created").toString(),&v.created)&&v.created;
}
bool sameId(const FileIdentity &a,const FileIdentity &b) {return a.volume==b.volume&&a.index==b.index;}
bool sameFile(const FileIdentity &a,const FileIdentity &b) {
    return sameId(a,b)&&a.bytes==b.bytes&&a.modified==b.modified&&a.sha256==b.sha256;
}
struct Plan {
    QString token,dir,target,source,helper;
    ProcessIdentity app,launcher;
    FileIdentity oldFile,download,targetDirectory,transactionDirectory;
    quint64 ready=0,commit=0,cancel=0,appHandle=0,launcherHandle=0;
    QString planPath() const {return dir+"/install.json";}
    QString receipt() const {return dir+"/status.json";}
    QString incoming() const {return QFileInfo(target).absolutePath()+"/.frogshell-update-"+token+".new";}
    QString backup() const {return QFileInfo(target).absolutePath()+"/.frogshell-update-"+token+".old";}
};
QJsonObject planJson(const Plan &p) {
    return {{"schema",1},{"token",p.token},{"directory",p.dir},{"target",p.target},{"source",p.source},{"helper",p.helper},
        {"app",processJson(p.app)},{"launcher",processJson(p.launcher)},{"old",fileJson(p.oldFile)},
        {"download",fileJson(p.download)},{"targetDirectory",fileJson(p.targetDirectory)},
        {"transactionDirectory",fileJson(p.transactionDirectory)},
        {"ready",QString::number(p.ready)},{"commit",QString::number(p.commit)},{"cancel",QString::number(p.cancel)},
        {"appHandle",QString::number(p.appHandle)},{"launcherHandle",QString::number(p.launcherHandle)}};
}
#ifdef Q_OS_WIN
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    Handle()=default;explicit Handle(HANDLE v):h(v){}
    ~Handle(){reset();}
    Handle(Handle &&v) noexcept:h(std::exchange(v.h,INVALID_HANDLE_VALUE)){}
    Handle &operator=(Handle &&v) noexcept {if(this!=&v){reset();h=std::exchange(v.h,INVALID_HANDLE_VALUE);}return *this;}
    Handle(const Handle &)=delete;Handle &operator=(const Handle &)=delete;
    bool valid() const{return h&&h!=INVALID_HANDLE_VALUE;}
    void reset(){if(valid())CloseHandle(h);h=INVALID_HANDLE_VALUE;}
};
quint64 ft(const FILETIME &v){return (quint64(v.dwHighDateTime)<<32)|v.dwLowDateTime;}
QString normalized(const QString &path) {
    const QString p=QDir::fromNativeSeparators(path);
    if(p.size()<4||p.size()>30000||!p[0].isLetter()||p.mid(1,2)!=":/"||QDir::cleanPath(p)!=p)return {};
    const auto parts=p.mid(3).split('/');
    for(const auto &part:parts) {
        if(part.isEmpty()||part=="."||part==".."||part.endsWith('.')||part.endsWith(' '))return {};
        for(const auto c:part)if(c.unicode()<32||QStringLiteral(":*?\"<>|").contains(c))return {};
    }
    return p;
}
std::wstring native(const QString &s){return (QStringLiteral("\\\\?\\")+QDir::toNativeSeparators(s)).toStdWString();}
bool samePath(const QString &a,const QString &b){return QDir::fromNativeSeparators(a).compare(QDir::fromNativeSeparators(b),Qt::CaseInsensitive)==0;}
bool within(const QString &path,const QString &root){return samePath(path,root)||QDir::fromNativeSeparators(path).startsWith(QDir::fromNativeSeparators(root)+"/",Qt::CaseInsensitive);}
QString finalPath(HANDLE h) {
    std::vector<wchar_t> b(32768);const DWORD n=GetFinalPathNameByHandleW(h,b.data(),DWORD(b.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!n||n>=b.size())return {};
    QString s=QString::fromWCharArray(b.data(),int(n));if(s.startsWith("\\\\?\\"))s.remove(0,4);
    return QDir::fromNativeSeparators(s);
}
bool information(HANDLE h,FileIdentity &v,bool directory=false) {
    BY_HANDLE_FILE_INFORMATION o{};if(!GetFileInformationByHandle(h,&o)||(o.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    if(bool(o.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory||(!directory&&o.nNumberOfLinks!=1))return false;
    v.volume=o.dwVolumeSerialNumber;v.index=(quint64(o.nFileIndexHigh)<<32)|o.nFileIndexLow;
    v.bytes=directory?0:(quint64(o.nFileSizeHigh)<<32)|o.nFileSizeLow;v.modified=ft(o.ftLastWriteTime);return true;
}
struct Pins {std::vector<Handle> handles;FileIdentity leaf;};
bool pinDirectory(const QString &path,Pins &out) {
    if(normalized(path).isEmpty())return false;
    QStringList parts;QString p=path;
    while(p.size()>3){parts.prepend(p);p=QFileInfo(p).dir().absolutePath();}
    parts.prepend(path.left(3));
    for(const auto &part:parts) {
        const auto n=native(part);Handle h(CreateFileW(n.c_str(),FILE_READ_ATTRIBUTES|FILE_TRAVERSE,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        FileIdentity id;if(!h.valid()||!information(h.h,id,true)||!samePath(finalPath(h.h),part))return false;
        out.leaf=id;out.handles.push_back(std::move(h));
    }
    return true;
}
Handle openFile(const QString &path,DWORD access=GENERIC_READ,DWORD share=FILE_SHARE_READ,DWORD *error=nullptr) {
    const auto n=native(path);const HANDLE raw=CreateFileW(n.c_str(),access,share,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    const DWORD failure=raw==INVALID_HANDLE_VALUE?GetLastError():ERROR_SUCCESS;if(error)*error=failure;return Handle(raw);
}
Result hashHandle(HANDLE h,FileIdentity *out,const Canceled &cancel={}) {
    FileIdentity before;if(!information(h,before)||before.bytes==0||before.bytes>MaxPackage)return result(Code::InvalidInput,"文件类型、链接数或大小不符合安全更新要求。");
    LARGE_INTEGER zero{};if(!SetFilePointerEx(h,zero,nullptr,FILE_BEGIN))return result(Code::IoError,"无法读取更新文件。");
    QCryptographicHash hash(QCryptographicHash::Sha256);QByteArray block(1024*1024,Qt::Uninitialized);quint64 total=0;
    for(;;) {
        if(canceled(cancel))return result(Code::Canceled,"安装准备已取消。");
        DWORD read=0;if(!ReadFile(h,block.data(),DWORD(block.size()),&read,nullptr))return result(Code::IoError,"读取更新文件失败。");
        if(!read)break;total+=read;if(total>MaxPackage)return result(Code::InvalidInput,"更新文件过大。");
        hash.addData(QByteArrayView(block.constData(),read));
    }
    FileIdentity after;if(!information(h,after)||!sameId(before,after)||before.bytes!=after.bytes||before.modified!=after.modified||total!=before.bytes)
        return result(Code::Changed,"校验期间文件已变化。");
    after.sha256=hash.result();*out=after;return good();
}
Result inspectFile(const QString &path,FileIdentity *out,const Canceled &cancel={},DWORD sharing=FILE_SHARE_READ) {
    const QString p=normalized(path);if(p.isEmpty())return result(Code::InvalidInput,"需要无父路径跳转的本地绝对路径。");
    Pins pins;if(!pinDirectory(QFileInfo(p).absolutePath(),pins))return result(Code::Changed,"路径含链接、联接点或发生变化。");
    auto h=openFile(p,GENERIC_READ,sharing);if(!h.valid())return result(Code::InUse,"文件被占用或不可读取。");
    if(!samePath(finalPath(h.h),p))return result(Code::Changed,"文件路径已变化。");return hashHandle(h.h,out,cancel);
}
Result processInfo(HANDLE h,ProcessIdentity *out) {
    FILETIME create{},exit{},kernel{},user{};std::vector<wchar_t>b(32768);DWORD size=DWORD(b.size());
    const DWORD pid=GetProcessId(h);
    if(!pid||!GetProcessTimes(h,&create,&exit,&kernel,&user)||!QueryFullProcessImageNameW(h,0,b.data(),&size))return result(Code::Changed,"无法验证实际进程身份。");
    const QString path=normalized(QString::fromWCharArray(b.data(),int(size)));
    if(path.isEmpty())return result(Code::Changed,"进程映像路径不支持自动更新。");
    *out={pid,ft(create),path};return good();
}
bool sameProcess(const ProcessIdentity &a,const ProcessIdentity &b){return a.pid==b.pid&&a.created==b.created&&samePath(a.imagePath,b.imagePath);}
Handle openProcess(const ProcessIdentity &want) {
    Handle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,want.pid));ProcessIdentity got;
    if(!h.valid()||!processInfo(h.h,&got).ok()||!sameProcess(want,got))return {};return h;
}
DWORD parentPid(DWORD pid) {
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));if(!snap.valid())return 0;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(Process32FirstW(snap.h,&entry))do{if(entry.th32ProcessID==pid)return entry.th32ParentProcessID;}while(Process32NextW(snap.h,&entry));return 0;
}
struct Security {
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,FALSE};
    Security() {
        Handle token;HANDLE raw=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw))return;token=Handle(raw);
        DWORD size=0;GetTokenInformation(token.h,TokenUser,nullptr,0,&size);if(!size||size>65536)return;QByteArray bytes(int(size),Qt::Uninitialized);
        if(!size||!GetTokenInformation(token.h,TokenUser,bytes.data(),size,&size))return;
        LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(bytes.data())->User.Sid,&sid))return;
        const std::wstring sddl=L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;"+std::wstring(sid)+L")";LocalFree(sid);
        if(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))attributes.lpSecurityDescriptor=descriptor;
    }
    ~Security(){if(descriptor)LocalFree(descriptor);}
    bool valid()const{return descriptor;}
};
bool ntfs(const QString &path) {
    wchar_t fs[32]{};const auto root=QDir::toNativeSeparators(path.left(3)).toStdWString();
    return GetVolumeInformationW(root.c_str(),nullptr,0,nullptr,nullptr,nullptr,fs,32)&&QString::fromWCharArray(fs)=="NTFS";
}
Result writeJson(const QString &path,const QJsonObject &o,bool newOnly=false) {
    const QByteArray bytes=QJsonDocument(o).toJson(QJsonDocument::Compact);if(bytes.size()>MaxJson)return result(Code::InvalidInput,"本机更新计划过大。");
    Pins pins;if(normalized(path).isEmpty()||!pinDirectory(QFileInfo(path).absolutePath(),pins))return result(Code::Changed,"更新状态目录已变化。");
    Security security;if(!security.valid())return result(Code::IoError,"无法建立私有更新权限。");const auto n=native(path);
    const bool mutableStatus=QFileInfo(path).fileName()=="status.json";
    Handle h;
    for(int attempt=0;attempt<12;++attempt) {
        h=Handle(CreateFileW(n.c_str(),GENERIC_WRITE|GENERIC_READ,mutableStatus?(FILE_SHARE_READ|FILE_SHARE_WRITE):FILE_SHARE_READ,&security.attributes,newOnly?CREATE_NEW:OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(h.valid()||!mutableStatus||GetLastError()!=ERROR_SHARING_VIOLATION)break;
        Sleep(10);
    }
    FileIdentity id;if(!h.valid()||!information(h.h,id)||!samePath(finalPath(h.h),path))return result(Code::IoError,"无法安全写入更新状态。");
    LARGE_INTEGER zero{};DWORD written=0;
    if(!SetFilePointerEx(h.h,zero,nullptr,FILE_BEGIN)||!WriteFile(h.h,bytes.constData(),DWORD(bytes.size()),&written,nullptr)||written!=DWORD(bytes.size())||!SetEndOfFile(h.h)||!FlushFileBuffers(h.h))return result(Code::IoError,"保存更新状态失败。");
    return good();
}
Result readJson(const QString &path,QJsonObject *out,Handle *retained=nullptr) {
    Pins pins;if(normalized(path).isEmpty()||!pinDirectory(QFileInfo(path).absolutePath(),pins))return result(Code::Changed,"本机更新路径不安全。");
    auto h=openFile(path,GENERIC_READ,QFileInfo(path).fileName()=="status.json"?(FILE_SHARE_READ|FILE_SHARE_WRITE):FILE_SHARE_READ);FileIdentity id;
    if(!h.valid()||!information(h.h,id)||id.bytes==0||id.bytes>MaxJson||!samePath(finalPath(h.h),path))return result(Code::InvalidInput,"本机更新计划无效或不可读取。");
    QByteArray bytes(int(id.bytes),Qt::Uninitialized);DWORD read=0;
    if(!ReadFile(h.h,bytes.data(),DWORD(bytes.size()),&read,nullptr)||read!=DWORD(bytes.size()))return result(Code::IoError,"读取更新计划失败。");
    QJsonParseError e;auto doc=QJsonDocument::fromJson(bytes,&e);if(e.error!=QJsonParseError::NoError||!doc.isObject())return result(Code::InvalidInput,"更新计划格式无效。");
    *out=doc.object();if(retained)*retained=std::move(h);return good();
}
Result parsePlan(const QString &path,Plan *p,Handle *retained=nullptr) {
    QJsonObject o;auto r=readJson(path,&o,retained);if(!r.ok())return r;
    p->token=o.value("token").toString();p->dir=o.value("directory").toString();p->target=o.value("target").toString();p->source=o.value("source").toString();p->helper=o.value("helper").toString();
    if(o.value("schema").toInt()!=1||!tokenValid(p->token)||QFileInfo(p->dir).fileName()!="install-"+p->token||
       !samePath(path,p->planPath())||normalized(p->target).isEmpty()||normalized(p->source).isEmpty()||normalized(p->helper).isEmpty()||
       !samePath(p->source,p->dir+"/payload.exe")||!p->target.endsWith(".exe",Qt::CaseInsensitive)||within(p->target,QFileInfo(p->helper).absolutePath())||
       !parseProcess(o.value("app").toObject(),p->app)||!parseProcess(o.value("launcher").toObject(),p->launcher)||
       !samePath(p->launcher.imagePath,p->target)||!samePath(p->app.imagePath,p->helper)||p->app.pid==p->launcher.pid||
       !parseFile(o.value("old").toObject(),p->oldFile)||!parseFile(o.value("download").toObject(),p->download)||
       !parseFile(o.value("targetDirectory").toObject(),p->targetDirectory,false)||!parseFile(o.value("transactionDirectory").toObject(),p->transactionDirectory,false)||
       !decimal(o.value("ready").toString(),&p->ready)||!decimal(o.value("commit").toString(),&p->commit)||
       !decimal(o.value("cancel").toString(),&p->cancel)||!decimal(o.value("appHandle").toString(),&p->appHandle)||!decimal(o.value("launcherHandle").toString(),&p->launcherHandle))
        return result(Code::InvalidInput,"本机更新计划字段无效。");
    Pins target,transaction;if(!pinDirectory(QFileInfo(p->target).absolutePath(),target)||!sameId(target.leaf,p->targetDirectory)||
       !pinDirectory(p->dir,transaction)||!sameId(transaction.leaf,p->transactionDirectory))return result(Code::Changed,"更新根目录已变化。");
    return good();
}
Result status(const Plan &p,const QString &phase,const Result &r,const FileIdentity &installed={}) {
    QJsonObject previous;QJsonArray events;
    if(readJson(p.receipt(),&previous).ok())events=previous.value("events").toArray();
    while(events.size()>=16)events.removeFirst();
    events.append(QJsonObject{{"phase",phase},{"code",int(r.code)},{"message",r.message}});
    return writeJson(p.receipt(),{{"schema",1},{"token",p.token},{"phase",phase},{"code",int(r.code)},
        {"message",r.message},{"backup",p.backup()},{"installed",fileJson(installed)},{"events",events}});
}
bool removeHeld(HANDLE h,DWORD *error=nullptr) {
    FILE_DISPOSITION_INFO d{};d.DeleteFile=TRUE;const BOOL ok=SetFileInformationByHandle(h,FileDispositionInfo,&d,sizeof(d));
    const DWORD failure=ok?ERROR_SUCCESS:GetLastError();if(error)*error=failure;return ok!=0;
}
Result removeKnownFile(const QString &path,const FileIdentity *expected=nullptr) {
    DWORD openError=0;auto h=openFile(path,GENERIC_READ|DELETE,FILE_SHARE_READ,&openError);
    if(!h.valid()) {
        if(openError==ERROR_FILE_NOT_FOUND||openError==ERROR_PATH_NOT_FOUND)return good();
        return result(Code::InUse,"自有更新暂存文件仍被占用。");
    }
    FileIdentity now;if(!information(h.h,now))return result(Code::Changed,"更新暂存文件类型变化，保守保留。");
    if(!samePath(finalPath(h.h),path)||(expected&&(!sameId(*expected,now)||expected->bytes!=now.bytes||expected->modified!=now.modified)))return result(Code::Changed,"更新暂存文件变化，保守保留。");
    return removeHeld(h.h)?good():result(Code::IoError,"暂未能回收自有更新文件。");
}
Result cleanupPayload(const Plan &p) {
    Pins pins;if(!pinDirectory(p.dir,pins)||!sameId(pins.leaf,p.transactionDirectory)||!samePath(p.source,p.dir+"/payload.exe"))return result(Code::Changed,"暂存目录变化，保留待检查。");
    return removeKnownFile(p.source,&p.download);
}
Result copyPrivatePayload(const QString &source,const FileIdentity &expected,const QString &destination,FileIdentity *copied,const Canceled &cancel) {
    Pins pins;if(!pinDirectory(QFileInfo(source).absolutePath(),pins))return result(Code::Changed,"下载目录已变化。");
    auto from=openFile(source);FileIdentity current;auto r=from.valid()?hashHandle(from.h,&current,cancel):result(Code::InUse,"下载包不可读取。");
    if(!r.ok())return r;if(!sameFile(current,expected))return result(Code::Changed,"下载包已变化。");
    Security security;if(!security.valid())return result(Code::IoError,"无法建立私有下载副本权限。");const auto n=native(destination);
    Handle to(CreateFileW(n.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,FILE_SHARE_READ,&security.attributes,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!to.valid())return result(Code::IoError,"无法保留自有更新包。");
    LARGE_INTEGER zero{};SetFilePointerEx(from.h,zero,nullptr,FILE_BEGIN);QByteArray buffer(1024*1024,Qt::Uninitialized);
    for(;;) {
        if(canceled(cancel)){removeHeld(to.h);return result(Code::Canceled,"更新准备已取消。");}
        DWORD count=0,written=0;if(!ReadFile(from.h,buffer.data(),DWORD(buffer.size()),&count,nullptr)){removeHeld(to.h);return result(Code::IoError,"复制下载包失败。");}
        if(!count)break;
        if(!WriteFile(to.h,buffer.data(),count,&written,nullptr)||count!=written){removeHeld(to.h);return result(Code::IoError,"保存自有更新包失败。");}
    }
    if(!FlushFileBuffers(to.h)){removeHeld(to.h);return result(Code::IoError,"刷新自有更新包失败。");}
    r=hashHandle(to.h,copied,cancel);if(!r.ok()||copied->sha256!=expected.sha256||copied->bytes!=expected.bytes){removeHeld(to.h);return r.ok()?result(Code::Changed,"自有更新包复核失败。"):r;}
    return good();
}
Result cleanupPrepared(const Plan &p) {
    auto r=cleanupPayload(p);if(!r.ok())return r;
    Plan disk;r=parsePlan(p.planPath(),&disk);if(!r.ok())return r;
    if(QJsonDocument(planJson(disk)).toJson()!=QJsonDocument(planJson(p)).toJson())return result(Code::Changed,"取消时计划已变化，保守保留。");
    Pins pins;if(!pinDirectory(p.dir,pins)||!sameId(pins.leaf,p.transactionDirectory))return result(Code::Changed,"取消时目录已变化。");
    r=removeKnownFile(p.receipt());if(!r.ok())return r;r=removeKnownFile(p.planPath());if(!r.ok())return r;
    r=removeKnownFile(p.dir+"/claimed");if(!r.ok())return r;
    pins.handles.pop_back();const auto n=native(p.dir);
    Handle dir(CreateFileW(n.c_str(),DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
    FileIdentity id;if(!dir.valid()||!information(dir.h,id,true)||!sameId(id,p.transactionDirectory)||!removeHeld(dir.h))return result(Code::IoError,"空事务目录未能回收；其中未知文件已保留。");
    return good();
}
bool renameHeld(HANDLE h,const QString &path,DWORD *error=nullptr) {
    // Win32 consumes a NUL-terminated DOS path while FileNameLength excludes
    // that terminator. Omitting its storage can make name conversion read past
    // the allocation (intermittent ERROR_INVALID_NAME / unintended names).
    const auto n=native(path);const size_t size=offsetof(FILE_RENAME_INFO,FileName)+(n.size()+1)*sizeof(wchar_t);
    std::vector<unsigned char> storage(size);auto info=reinterpret_cast<FILE_RENAME_INFO *>(storage.data());
    info->ReplaceIfExists=FALSE;info->RootDirectory=nullptr;info->FileNameLength=DWORD(n.size()*sizeof(wchar_t));
    memcpy(info->FileName,n.data(),info->FileNameLength);info->FileName[n.size()]=L'\0';
    const BOOL ok=SetFileInformationByHandle(h,FileRenameInfo,info,DWORD(size));
    const DWORD failure=ok?ERROR_SUCCESS:GetLastError();if(error)*error=failure;return ok!=0;
}
Result renameOld(const Plan &p,HANDLE old,const Canceled &cancel) {
    DWORD previousError=0;
    for(int attempt=0;attempt<16;++attempt) {
        if(canceled(cancel))return result(Code::Canceled,"安装切换前已取消。");
        FileIdentity identity;auto r=hashHandle(old,&identity,cancel);if(!r.ok())return r;
        Pins parent;if(!sameFile(identity,p.oldFile)||!samePath(finalPath(old),p.target)||
            !pinDirectory(QFileInfo(p.target).absolutePath(),parent)||!sameId(parent.leaf,p.targetDirectory))return result(Code::Changed,"备份前目标句柄或父目录已变化。");
        DWORD error=0;
        if(renameHeld(old,p.backup(),&error))return result(Code::Ok,QStringLiteral("[step=rename-old attempt=%1 previous-win32=%2 held-volume=%3 held-index=%4]").arg(attempt+1).arg(previousError).arg(identity.volume).arg(identity.index));
        previousError=error;
        if((error!=ERROR_SHARING_VIOLATION&&error!=ERROR_LOCK_VIOLATION)||attempt==15)
            return result((error==ERROR_SHARING_VIOLATION||error==ERROR_LOCK_VIOLATION)?Code::InUse:(error==ERROR_INVALID_NAME?Code::InvalidInput:Code::IoError),QStringLiteral("外层程序未能备份 [step=rename-old win32=%1 attempt=%2 volume=%3 index=%4]。")
                .arg(error).arg(attempt+1).arg(identity.volume).arg(identity.index));
        Sleep(50);
    }
    return result(Code::InUse,"备份重命名重试结束。");
}
Result verifyOrigin(const PortableOrigin &origin,const ProcessIdentity &app,bool fixtureTree,Handle *launcherOut,Handle *appOut) {
    if(!origin.singleExecutable||normalized(origin.launcher.imagePath).isEmpty()||!origin.launcher.imagePath.endsWith(".exe",Qt::CaseInsensitive)||origin.originalFile.sha256.size()!=32)
        return result(Code::ManualUpdate,"目录包或缺少可验证的外层启动信息，请手动更新。");
    auto launcher=openProcess(origin.launcher),main=openProcess(app);
    if(!launcher.valid()||!main.valid()||origin.launcher.pid==app.pid||origin.launcher.created>app.created||
       (!fixtureTree&&parentPid(app.pid)!=origin.launcher.pid)||within(origin.launcher.imagePath,QFileInfo(app.imagePath).absolutePath()))
        return result(Code::Changed,"实际启动器、应用进程或原始路径不匹配。");
    FileIdentity file;auto r=inspectFile(origin.launcher.imagePath,&file,{},FILE_SHARE_READ|FILE_SHARE_DELETE);
    if(!r.ok())return r;if(!sameFile(origin.originalFile,file))return result(Code::Changed,"原始外层程序身份或摘要已变化。");
    *launcherOut=std::move(launcher);*appOut=std::move(main);return good();
}
Result waitForExit(HANDLE app,HANDLE launcher,const Canceled &cancel,int timeoutMs) {
    QElapsedTimer timer;timer.start();
    for(;;) {
        if(canceled(cancel))return result(Code::Canceled,"等待退出已取消，未安装更新。");
        const DWORD a=WaitForSingleObject(app,0),b=WaitForSingleObject(launcher,0);
        if(a==WAIT_FAILED||b==WAIT_FAILED)return result(Code::Changed,"无法等待实际应用进程。");
        if(a==WAIT_OBJECT_0&&b==WAIT_OBJECT_0)return good();
        if(timer.elapsed()>=timeoutMs)return result(Code::TimedOut,"应用或启动器尚未退出，本次未安装更新。");
        Sleep(20);
    }
}
Result copyPackage(const Plan &p,Handle &incoming,const Canceled &cancel) {
    auto source=openFile(p.source);if(!source.valid())return result(Code::InUse,"下载包被占用。");
    FileIdentity downloaded;auto r=hashHandle(source.h,&downloaded,cancel);if(!r.ok())return r;
    if(!sameFile(downloaded,p.download))return result(Code::Changed,"下载包身份或摘要已变化。");
    Security security;if(!security.valid())return result(Code::IoError,"无法建立更新暂存权限。");const auto n=native(p.incoming());
    incoming=Handle(CreateFileW(n.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE|WRITE_DAC,FILE_SHARE_READ,&security.attributes,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    if(!incoming.valid())return result(Code::IoError,"无法在安装目录创建同卷更新暂存文件。");
    LARGE_INTEGER zero{};if(!SetFilePointerEx(source.h,zero,nullptr,FILE_BEGIN))return result(Code::IoError,"无法读取下载包。");
    QByteArray buffer(1024*1024,Qt::Uninitialized);
    for(;;) {
        if(canceled(cancel))return result(Code::Canceled,"复制更新已取消。");
        DWORD nread=0,written=0;if(!ReadFile(source.h,buffer.data(),DWORD(buffer.size()),&nread,nullptr))return result(Code::IoError,"复制更新包失败。");
        if(!nread)break;if(!WriteFile(incoming.h,buffer.data(),nread,&written,nullptr)||written!=nread)return result(Code::IoError,"写入更新包失败。");
    }
    if(!FlushFileBuffers(incoming.h))return result(Code::IoError,"刷新更新文件失败。");
    FileIdentity copied;r=hashHandle(incoming.h,&copied,cancel);if(!r.ok())return r;
    if(copied.sha256!=p.download.sha256||copied.bytes!=p.download.bytes||copied.volume!=p.oldFile.volume)return result(Code::Changed,"同卷更新文件复核失败。");
    return good();
}
bool preserveAcl(HANDLE oldFile,HANDLE incoming) {
    PACL dacl=nullptr;PSECURITY_DESCRIPTOR sd=nullptr;
    const DWORD got=GetSecurityInfo(oldFile,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&dacl,nullptr,&sd);
    if(got!=ERROR_SUCCESS)return false;
    const DWORD put=SetSecurityInfo(incoming,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,dacl,nullptr);
    LocalFree(sd);return put==ERROR_SUCCESS;
}
Result rollback(const Plan &p,Handle &old,Handle &installed,bool installedAtTarget,const QString &why,const FileIdentity &expected={}) {
    if(installedAtTarget) {
        DWORD error=0;
        for(int attempt=0;!installed.valid()&&attempt<20;++attempt) {
            installed=openFile(p.target,GENERIC_READ|DELETE,FILE_SHARE_READ,&error);if(installed.valid())break;
            if(error!=ERROR_SHARING_VIOLATION&&error!=ERROR_LOCK_VIOLATION)break;
            Pins parent;if(!pinDirectory(QFileInfo(p.target).absolutePath(),parent)||!sameId(parent.leaf,p.targetDirectory))break;
            Sleep(50);
        }
        FileIdentity now;
        const auto checked=installed.valid()?hashHandle(installed.h,&now):result(Code::InUse,"open-new");
        const bool matches=checked.ok()&&now.sha256==p.download.sha256&&now.bytes==p.download.bytes&&(!expected.index||sameFile(expected,now));
        bool removed=false;if(matches)removed=removeHeld(installed.h,&error);
        if(!matches||!removed) {
            auto r=result(Code::RecoveryRequired,QStringLiteral("新程序无法安全移除，旧程序保留在备份路径 [step=rollback-new win32=%1 open=%2 hash=%3 identity=%4 expected-index=%5 actual-index=%6 expected-modified=%7 actual-modified=%8]。")
                .arg(error).arg(installed.valid()).arg(checked.ok()).arg(matches).arg(expected.index).arg(now.index).arg(expected.modified).arg(now.modified)+why);status(p,"recovery-required",r);return r;
        }
        installed.reset();
    }
    DWORD oldError=0;if(!old.valid())old=openFile(p.backup(),GENERIC_READ|DELETE,FILE_SHARE_READ,&oldError);
    FileIdentity original;
    const auto oldHash=old.valid()?hashHandle(old.h,&original):result(Code::IoError,"open-backup");
    if(!old.valid()||!oldHash.ok()||!sameFile(original,p.oldFile)) {
        auto r=result(Code::RecoveryRequired,QStringLiteral("旧备份校验未通过 [step=rollback-old-check win32=%1 open=%2 hash=%3 expected-index=%4 actual-index=%5]。").arg(oldError).arg(old.valid()).arg(oldHash.ok()).arg(p.oldFile.index).arg(original.index)+why);status(p,"recovery-required",r);return r;
    }
    if(!renameHeld(old.h,p.target,&oldError)) {
        auto r=result(Code::RecoveryRequired,QStringLiteral("旧程序自动恢复受阻 [step=rollback-old-rename win32=%1 volume=%2 index=%3]。").arg(oldError).arg(original.volume).arg(original.index)+why);status(p,"recovery-required",r);return r;
    }
    old.reset();auto r=result(Code::RolledBack,"安装失败，已恢复旧程序。"+why);status(p,"rolled-back",r);return r;
}
QString quote(const QString &arg) {
    QString out="\"";int slashes=0;
    for(QChar c:arg){if(c=='\\'){++slashes;continue;}if(c=='\"')out+=QString(slashes*2+1,'\\');else out+=QString(slashes,'\\');slashes=0;out+=c;}
    out+=QString(slashes*2,'\\');return out+'\"';
}
std::vector<wchar_t> environment() {
    auto env=QProcessEnvironment::systemEnvironment();for(const auto &key:env.keys())if(key.startsWith("RABBITSHELL_PORTABLE_",Qt::CaseInsensitive))env.remove(key);
    QStringList entries=env.toStringList();entries.sort(Qt::CaseInsensitive);QString block;
    for(const auto &entry:entries){block+=entry;block+=QChar(0);}block+=QChar(0);
    return std::vector<wchar_t>(reinterpret_cast<const wchar_t *>(block.utf16()),reinterpret_cast<const wchar_t *>(block.utf16())+block.size());
}
Result launchInstalled(const Plan &p,Handle *child) {
    QString cmd=quote(QDir::toNativeSeparators(p.target))+" "+quote(ReceiptArgument)+" "+quote(p.receipt())+" "+quote(ReceiptTokenArgument)+" "+quote(p.token);
    auto command=cmd.toStdWString(),app=QDir::toNativeSeparators(p.target).toStdWString(),dir=QDir::toNativeSeparators(QFileInfo(p.target).absolutePath()).toStdWString();
    auto env=environment();STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(app.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_UNICODE_ENVIRONMENT,env.data(),dir.c_str(),&startup,&process)){const DWORD error=GetLastError();return result(Code::IoError,QStringLiteral("新程序无法启动 [step=create-new-process win32=%1]。").arg(error));}
    Handle thread(process.hThread);*child=Handle(process.hProcess);return good();
}
struct EngineOptions {int waitMs=120000;Canceled cancel;int failAt=0;bool fixture=false;};
Result installEngine(const Plan &p,HANDLE app,HANDLE launcher,const EngineOptions &opt) {
    auto r=status(p,"waiting",good());if(!r.ok())return r;
    r=waitForExit(app,launcher,opt.cancel,opt.waitMs);if(!r.ok()){status(p,"not-installed",r);return r;}
    Pins targetPins,transactionPins;
    if(!pinDirectory(QFileInfo(p.target).absolutePath(),targetPins)||!sameId(targetPins.leaf,p.targetDirectory)||
       !pinDirectory(p.dir,transactionPins)||!sameId(transactionPins.leaf,p.transactionDirectory))return result(Code::Changed,"退出后安装目录已变化。");
    DWORD oldOpenError=0;Handle old=openFile(p.target,GENERIC_READ|DELETE|READ_CONTROL,FILE_SHARE_READ,&oldOpenError);FileIdentity actual;
    if(!old.valid()){r=result((oldOpenError==ERROR_SHARING_VIOLATION||oldOpenError==ERROR_LOCK_VIOLATION)?Code::InUse:Code::IoError,QStringLiteral("外层程序不可安全打开，未替换 [step=open-old win32=%1]。").arg(oldOpenError));status(p,"not-installed",r);return r;}
    r=hashHandle(old.h,&actual,opt.cancel);if(!r.ok()||!sameFile(actual,p.oldFile)){r=result(Code::Changed,"退出后原始程序身份或摘要已变化。");status(p,"not-installed",r);return r;}
    Handle incoming;r=copyPackage(p,incoming,opt.cancel);
    if(!r.ok()){if(incoming.valid())removeHeld(incoming.h);status(p,"not-installed",r);return r;}
    if(!preserveAcl(old.h,incoming.h)){removeHeld(incoming.h);r=result(Code::IoError,"无法保留原程序访问权限。");status(p,"not-installed",r);return r;}
    if(canceled(opt.cancel)){removeHeld(incoming.h);r=result(Code::Canceled,"安装已取消，旧程序保持原位。");status(p,"not-installed",r);return r;}
    // Handle-based same-volume rename: no overwrite of an unrelated path in the
    // rename gap. Original handle is retained until rollback is no longer needed.
    r=status(p,"before-backup",good());if(!r.ok()){removeHeld(incoming.h);return r;}
    r=renameOld(p,old.h,opt.cancel);
    if(!r.ok()){removeHeld(incoming.h);status(p,"not-installed",r);return r;}
    if(!samePath(finalPath(old.h),p.backup())){removeHeld(incoming.h);incoming.reset();return rollback(p,old,incoming,false,"备份句柄的实际路径不匹配。");}
    r=status(p,"backed-up",r);
    if(!r.ok()||opt.failAt==1||canceled(opt.cancel)){removeHeld(incoming.h);incoming.reset();return rollback(p,old,incoming,false,"更新文件尚未启用。");}
    DWORD newRenameError=0;
    if(!renameHeld(incoming.h,p.target,&newRenameError)){removeHeld(incoming.h);incoming.reset();return rollback(p,old,incoming,false,QStringLiteral("无法启用更新文件 [step=rename-new win32=%1]。").arg(newRenameError));}
    FileIdentity installed;r=hashHandle(incoming.h,&installed);
    if(!r.ok()||installed.sha256!=p.download.sha256||!samePath(finalPath(incoming.h),p.target)||opt.failAt==2)return rollback(p,old,incoming,true,"启用后校验或实际路径不匹配。");
    r=status(p,"installed-awaiting-startup",good(),installed);
    if(!r.ok())return rollback(p,old,incoming,true,"无法保存安装回执。");
    incoming.reset();old.reset();
    if(opt.failAt==3){Handle no;return rollback(p,old,no,true,"启动测试失败。",installed);}
    if(opt.fixture)return good(); // Production has no flag or CLI to enter this branch.
    Handle launched;r=launchInstalled(p,&launched);
    if(!r.ok()){Handle no;return rollback(p,old,no,true,r.message,installed);}
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<30000) {
        auto acknowledged=[](const QJsonObject &o){const auto phase=o.value("phase").toString();return phase=="complete"||phase=="startup-confirmed";};
        QJsonObject receipt;if(readJson(p.receipt(),&receipt).ok()&&acknowledged(receipt))return good();
        if(WaitForSingleObject(launched.h,0)==WAIT_OBJECT_0){
            if(readJson(p.receipt(),&receipt).ok()&&acknowledged(receipt))return good();
            launched.reset();Handle no;return rollback(p,old,no,true,"新启动器在启动确认前退出。",installed);
        }
        Sleep(50);
    }
    // A live locked screen is allowed to acknowledge immediately. A live app
    // without acknowledgement keeps the backup; it is never killed/rolled back.
    return good();
}
#endif
}

struct PreparedInstall::State {
    Plan plan;
#ifdef Q_OS_WIN
    Handle app,launcher;
#endif
    bool started=false;
};
struct Access {
    static auto &state(PreparedInstall &p){return p.d;}
    static auto makeState(){return std::make_unique<PreparedInstall::State>();}
};
PreparedInstall::PreparedInstall()=default;
PreparedInstall::~PreparedInstall(){if(d&&!d->started)discardPreparedInstall(this);}
PreparedInstall::PreparedInstall(PreparedInstall &&) noexcept=default;
PreparedInstall &PreparedInstall::operator=(PreparedInstall &&other) noexcept {if(this!=&other){if(d&&!d->started)discardPreparedInstall(this);d=std::move(other.d);}return *this;}
bool PreparedInstall::valid() const{return d&&!d->started;}
QString PreparedInstall::planPath() const{return d?d->plan.planPath():QString();}
QString PreparedInstall::receiptPath() const{return d?d->plan.receipt():QString();}
QString PreparedInstall::transactionToken() const{return d?d->plan.token:QString();}
Result discardPreparedInstall(PreparedInstall *p){
    if(!p||!Access::state(*p))return good();
    auto state=std::move(Access::state(*p));
#ifdef Q_OS_WIN
    if(state->started)return result(Code::InvalidInput,"助手已接手，回收交由助手完成。");
    return cleanupPrepared(state->plan);
#else
    return good();
#endif
}
Result originFromEnvironment(const QProcessEnvironment &env,PortableOrigin *out) {
    if(!out)return result(Code::InvalidInput,"缺少启动信息输出。");*out={};
    const QString pre="RABBITSHELL_PORTABLE_ORIGIN_";
    if(env.value(pre+"KIND")!="single-exe-v1")return result(Code::ManualUpdate,"此运行方式需手动下载更新。");
    quint64 pid=0;PortableOrigin v;v.launcher.imagePath=env.value(pre+"PATH");
    if(!decimal(env.value(pre+"PID"),&pid)||!pid||pid>(std::numeric_limits<quint32>::max)()||!decimal(env.value(pre+"CREATED"),&v.launcher.created)||
       !decimal(env.value(pre+"VOLUME"),&v.originalFile.volume)||!decimal(env.value(pre+"INDEX"),&v.originalFile.index)||
       !decimal(env.value(pre+"BYTES"),&v.originalFile.bytes)||!decimal(env.value(pre+"MODIFIED"),&v.originalFile.modified)||
       (v.originalFile.sha256=digest(env.value(pre+"SHA256"))).size()!=32)return result(Code::ManualUpdate,"原始启动信息不完整，请手动更新。");
    v.singleExecutable=true;v.launcher.pid=quint32(pid);v.launcher.imagePath=QDir::fromNativeSeparators(v.launcher.imagePath);*out=v;return good();
}
Result currentProcessIdentity(ProcessIdentity *out) {
#ifdef Q_OS_WIN
    if(out)return processInfo(GetCurrentProcess(),out);
#else
    Q_UNUSED(out);
#endif
    return result(Code::ManualUpdate,"此平台暂不支持安全自动安装，请手动更新。");
}
Result snapshotFile(const QString &path,FileIdentity *out,const Canceled &cancel) {
#ifdef Q_OS_WIN
    if(out)return inspectFile(path,out,cancel,FILE_SHARE_READ|FILE_SHARE_DELETE);
#else
    Q_UNUSED(path);Q_UNUSED(out);Q_UNUSED(cancel);
#endif
    return result(Code::ManualUpdate,"此平台暂不支持安全自动安装，请手动更新。");
}
namespace {
Result prepareImpl(const PortableOrigin &origin,const ProcessIdentity &app,const VerifiedUpdate &update,const QString &base,PreparedInstall *out,const Canceled &cancel,bool fixtureTree) {
#ifdef Q_OS_WIN
    if(!out||out->valid()||update.sha256.size()!=32||!update.bytes||update.bytes>MaxPackage)return result(Code::InvalidInput,"更新包参数无效。");
    if(QSysInfo::currentCpuArchitecture()!="x86_64"||!ntfs(origin.launcher.imagePath))return result(Code::ManualUpdate,"此平台、架构或文件系统尚未验证，请手动更新。");
    Handle launcher,main;auto r=verifyOrigin(origin,app,fixtureTree,&launcher,&main);if(!r.ok())return r;
    const QString source=normalized(update.filePath),staging=normalized(base);
    if(source.isEmpty()||staging.isEmpty()||samePath(source,origin.launcher.imagePath)||samePath(source,app.imagePath))return result(Code::InvalidInput,"更新下载或暂存路径无效。");
    FileIdentity downloaded;r=inspectFile(source,&downloaded,cancel);if(!r.ok())return r;
    if(downloaded.sha256!=update.sha256||downloaded.bytes!=update.bytes)return result(Code::Changed,"下载包摘要或长度复核失败。");
    Pins parent,target;if(!pinDirectory(staging,parent)||!pinDirectory(QFileInfo(origin.launcher.imagePath).absolutePath(),target))return result(Code::Changed,"安装或暂存目录不安全。");
    const QString token=QUuid::createUuid().toString(QUuid::WithoutBraces),dir=staging+"/install-"+token;
    Security security;const auto n=native(dir);if(!security.valid()||!CreateDirectoryW(n.c_str(),&security.attributes))return result(Code::IoError,"无法创建私有更新计划目录。");
    Pins transaction;if(!pinDirectory(dir,transaction))return result(Code::Changed,"私有更新目录发生变化。");
    auto state=Access::makeState();auto &p=state->plan;
    p.token=token;p.dir=dir;p.target=normalized(origin.launcher.imagePath);p.source=dir+"/payload.exe";p.helper=app.imagePath;p.app=app;p.launcher=origin.launcher;
    p.oldFile=origin.originalFile;p.targetDirectory=target.leaf;p.transactionDirectory=transaction.leaf;
    r=copyPrivatePayload(source,downloaded,p.source,&p.download,cancel);if(!r.ok())return r;
    state->launcher=std::move(launcher);state->app=std::move(main);
    r=writeJson(p.planPath(),planJson(p),true);if(!r.ok()){cleanupPayload(p);return r;}
    r=status(p,"prepared",good());if(!r.ok()){cleanupPayload(p);removeKnownFile(p.planPath());return r;}
    if(canceled(cancel)){cleanupPrepared(p);return result(Code::Canceled,"更新意图已取消，未启动安装助手。");}
    Access::state(*out)=std::move(state);return good();
#else
    Q_UNUSED(origin);Q_UNUSED(app);Q_UNUSED(update);Q_UNUSED(base);Q_UNUSED(out);Q_UNUSED(cancel);Q_UNUSED(fixtureTree);
    return result(Code::ManualUpdate,"此平台暂不支持安全自动安装，请手动更新。");
#endif
}
}
Result prepareInstall(const PortableOrigin &origin,const VerifiedUpdate &update,const QString &base,PreparedInstall *out,const Canceled &cancel) {
    ProcessIdentity app;auto r=currentProcessIdentity(&app);if(!r.ok())return r;return prepareImpl(origin,app,update,base,out,cancel,false);
}

Result startAfterMainLoopExit(PreparedInstall *prepared,quint32 *helperPid) {
#ifdef Q_OS_WIN
    if(!prepared||!prepared->valid())return result(Code::InvalidInput,"没有已准备的安装意图。");
    auto &s=*Access::state(*prepared);auto &p=s.plan;ProcessIdentity current;
    if(!currentProcessIdentity(&current).ok()||!sameProcess(current,p.app))return result(Code::Changed,"安装意图不属于当前实际应用进程。");
    Plan disk;auto r=parsePlan(p.planPath(),&disk);if(!r.ok()||QJsonDocument(planJson(disk)).toJson()!=QJsonDocument(planJson(p)).toJson())return result(Code::Changed,"准备后的安装计划已变化。");
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};Handle ready(CreateEventW(&sa,TRUE,FALSE,nullptr)),commit(CreateEventW(&sa,TRUE,FALSE,nullptr)),cancel(CreateEventW(&sa,TRUE,FALSE,nullptr));
    Handle app,launcher;HANDLE a=nullptr,l=nullptr;
    if(!ready.valid()||!commit.valid()||!cancel.valid()||!DuplicateHandle(GetCurrentProcess(),s.app.h,GetCurrentProcess(),&a,0,TRUE,DUPLICATE_SAME_ACCESS)||
       !DuplicateHandle(GetCurrentProcess(),s.launcher.h,GetCurrentProcess(),&l,0,TRUE,DUPLICATE_SAME_ACCESS)){if(a)CloseHandle(a);if(l)CloseHandle(l);return result(Code::IoError,"无法准备助手进程句柄。");}
    app=Handle(a);launcher=Handle(l);
    p.ready=quint64(reinterpret_cast<quintptr>(ready.h));p.commit=quint64(reinterpret_cast<quintptr>(commit.h));p.cancel=quint64(reinterpret_cast<quintptr>(cancel.h));
    p.appHandle=quint64(reinterpret_cast<quintptr>(app.h));p.launcherHandle=quint64(reinterpret_cast<quintptr>(launcher.h));
    r=writeJson(p.planPath(),planJson(p));if(!r.ok())return r;
    HANDLE handles[]{ready.h,commit.h,cancel.h,app.h,launcher.h};SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    if(!bytes||bytes>65536)return result(Code::IoError,"无法创建助手句柄白名单。");
    std::vector<unsigned char> storage(bytes);auto attrs=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!InitializeProcThreadAttributeList(attrs,1,0,&bytes))return result(Code::IoError,"助手初始化失败。");
    const bool updated=UpdateProcThreadAttribute(attrs,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr)!=0;
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESHOWWINDOW;startup.StartupInfo.wShowWindow=SW_HIDE;startup.lpAttributeList=attrs;
    PROCESS_INFORMATION process{};auto application=QDir::toNativeSeparators(p.helper).toStdWString();
    auto command=(quote(QDir::toNativeSeparators(p.helper))+" "+quote(HelperArgument)+" "+quote(p.planPath())).toStdWString();
    const bool launched=updated&&CreateProcessW(application.c_str(),command.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW,nullptr,nullptr,&startup.StartupInfo,&process);
    DeleteProcThreadAttributeList(attrs);
    if(!launched){r=result(Code::IoError,"无法启动本机安装助手，旧程序未改变。");status(p,"not-installed",r);return r;}
    Handle child(process.hProcess),thread(process.hThread);s.started=true;
    HANDLE wait[]{ready.h,child.h};const DWORD got=WaitForMultipleObjects(2,wait,FALSE,10000);
    if(got!=WAIT_OBJECT_0||!SetEvent(commit.h)){SetEvent(cancel.h);if(got==WAIT_OBJECT_0+1)s.started=false;r=result(Code::IoError,"助手未完成安全准备，本次不会安装。");status(p,"not-installed",r);return r;}
    if(helperPid)*helperPid=process.dwProcessId;return good();
#else
    Q_UNUSED(prepared);Q_UNUSED(helperPid);return result(Code::ManualUpdate,"此平台请手动更新。");
#endif
}
int runInstallHelper(const QString &path) {
#ifdef Q_OS_WIN
    Plan p;Handle planHandle;auto r=parsePlan(path,&p,&planHandle);if(!r.ok())return 20;
    ProcessIdentity self;if(!currentProcessIdentity(&self).ok()||!samePath(self.imagePath,p.helper)||parentPid(self.pid)!=p.app.pid)return 21;
    auto from=[](quint64 value){return reinterpret_cast<HANDLE>(quintptr(value));};
    HANDLE app=from(p.appHandle),launcher=from(p.launcherHandle),ready=from(p.ready),commit=from(p.commit),cancel=from(p.cancel);
    ProcessIdentity ai,li;if(!processInfo(app,&ai).ok()||!processInfo(launcher,&li).ok()||!sameProcess(ai,p.app)||!sameProcess(li,p.launcher)||parentPid(p.app.pid)!=p.launcher.pid)return 22;
    struct PayloadCleanup {const Plan &p;~PayloadCleanup(){cleanupPayload(p);}} cleanup{p};
    // Inherited handles must be real waitable handles. No external path/token
    // alone can authorize installation by manually invoking this internal mode.
    if(WaitForSingleObject(cancel,0)==WAIT_FAILED||WaitForSingleObject(commit,0)==WAIT_FAILED)return 23;
    Pins pins;if(!pinDirectory(p.dir,pins)||!sameId(pins.leaf,p.transactionDirectory))return 24;
    const auto claimPath=native(p.dir+"/claimed");Handle claim(CreateFileW(claimPath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!claim.valid())return 25;
    FileIdentity old,download;
    if(!inspectFile(p.target,&old,{},FILE_SHARE_READ|FILE_SHARE_DELETE).ok()||!sameFile(old,p.oldFile)||!inspectFile(p.source,&download).ok()||!sameFile(download,p.download))return 26;
    if(!SetEvent(ready))return 27;
    HANDLE approval[]{cancel,commit};if(WaitForMultipleObjects(2,approval,FALSE,10000)!=WAIT_OBJECT_0+1)return 28;
    EngineOptions options;options.cancel=[cancel]{return WaitForSingleObject(cancel,0)==WAIT_OBJECT_0;};
    r=installEngine(p,app,launcher,options);return r.ok()?0:int(r.code)+30;
#else
    Q_UNUSED(path);return 20;
#endif
}
Result readInstallStatus(const QString &path,InstallStatus *out) {
#ifdef Q_OS_WIN
    if(!out||QFileInfo(path).fileName()!="status.json")return result(Code::InvalidInput,"更新回执路径无效。");
    Plan p;auto r=parsePlan(QFileInfo(path).absolutePath()+"/install.json",&p);if(!r.ok())return r;
    QJsonObject o;r=readJson(path,&o);if(!r.ok())return r;
    const int code=o.value("code").toInt(-1);if(o.value("schema").toInt()!=1||o.value("token").toString()!=p.token||code<0||code>int(Code::RecoveryRequired))return result(Code::InvalidInput,"更新回执无效。");
    *out={o.value("phase").toString(),o.value("message").toString(),p.backup(),Code(code)};return good();
#else
    Q_UNUSED(path);Q_UNUSED(out);return result(Code::ManualUpdate,"此平台请手动更新。");
#endif
}
Result acknowledgeNormalStartup(const QString &path,const QString &token,const PortableOrigin &origin) {
#ifdef Q_OS_WIN
    Plan p;auto r=parsePlan(QFileInfo(path).absolutePath()+"/install.json",&p);if(!r.ok())return r;
    if(!samePath(path,p.receipt())||token!=p.token)return result(Code::InvalidInput,"启动回执不匹配。");
    QJsonObject o;r=readJson(path,&o);if(!r.ok())return r;
    if(o.value("phase").toString()=="complete")return good();
    const auto phase=o.value("phase").toString();
    FileIdentity installed;if((phase!="installed-awaiting-startup"&&phase!="startup-confirmed")||!parseFile(o.value("installed").toObject(),installed))return result(Code::InvalidInput,"此更新无需启动确认。");
    ProcessIdentity app;r=currentProcessIdentity(&app);if(!r.ok())return r;Handle launch,main;
    r=verifyOrigin(origin,app,false,&launch,&main);if(!r.ok())return r;
    if(!samePath(origin.launcher.imagePath,p.target)||origin.originalFile.sha256!=p.download.sha256||!sameFile(origin.originalFile,installed))return result(Code::Changed,"当前启动程序不是已安装版本。");
    Pins parent;if(!pinDirectory(QFileInfo(p.target).absolutePath(),parent)||!sameId(parent.leaf,p.targetDirectory))return result(Code::Changed,"安装目录已变化。");
    r=status(p,"startup-confirmed",good(),installed);if(!r.ok())return r;
    DWORD backupOpenError=0;auto backup=openFile(p.backup(),GENERIC_READ|DELETE,FILE_SHARE_READ,&backupOpenError);FileIdentity old;
    if(!backup.valid()&&(backupOpenError==ERROR_FILE_NOT_FOUND||backupOpenError==ERROR_PATH_NOT_FOUND))return status(p,"complete",good(),installed);
    if(!backup.valid()||!hashHandle(backup.h,&old).ok()||!sameFile(old,p.oldFile)||!removeHeld(backup.h)) {
        r=result(Code::RecoveryRequired,"更新已启动，旧备份暂未能安全回收。");status(p,"startup-confirmed",r,installed);return r;
    }
    backup.reset();return status(p,"complete",good(),installed);
#else
    Q_UNUSED(path);Q_UNUSED(token);Q_UNUSED(origin);return result(Code::ManualUpdate,"此平台请手动更新。");
#endif
}
#ifdef SELFUPDATE_FIXTURE_TESTS
namespace fixture {
Result prepare(const PortableOrigin &origin,const ProcessIdentity &app,const VerifiedUpdate &update,const QString &base,PreparedInstall *out,const Canceled &cancel){return prepareImpl(origin,app,update,base,out,cancel,true);}
Result install(PreparedInstall *prepared,const Options &opt) {
#ifdef Q_OS_WIN
    if(!prepared||!prepared->valid())return result(Code::InvalidInput,"fixture plan missing");
    auto &s=*Access::state(*prepared);s.started=true;EngineOptions options;options.fixture=true;options.failAt=int(opt.failAt);options.waitMs=opt.waitMs;options.cancel=opt.canceled;
    auto r=installEngine(s.plan,s.app.h,s.launcher.h,options);cleanupPayload(s.plan);return r;
#else
    Q_UNUSED(prepared);Q_UNUSED(opt);return result(Code::ManualUpdate,"fixture platform unsupported");
#endif
}
}
#endif
}
