#include "security.h"
#include "operationgate.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <io.h>
#endif
namespace rs {
namespace {
bool privateFile(const QString &name,int descriptor=-1) {
#ifdef Q_OS_WIN
    HANDLE token=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;
    DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);
    QByteArray info(int(size),0);
    const bool got=GetTokenInformation(token,TokenUser,info.data(),size,&size);CloseHandle(token);
    if(!got)return false;
    auto user=reinterpret_cast<TOKEN_USER *>(info.data());
    EXPLICIT_ACCESSW access{};access.grfAccessPermissions=GENERIC_ALL;access.grfAccessMode=SET_ACCESS;
    access.grfInheritance=NO_INHERITANCE;access.Trustee.TrusteeForm=TRUSTEE_IS_SID;
    access.Trustee.TrusteeType=TRUSTEE_IS_USER;access.Trustee.ptstrName=static_cast<LPWSTR>(user->User.Sid);
    PACL acl=nullptr;if(SetEntriesInAclW(1,&access,nullptr,&acl)!=ERROR_SUCCESS)return false;
    QString target=name;
    if(descriptor>=0){
        wchar_t path[32768];const auto length=GetFinalPathNameByHandleW(reinterpret_cast<HANDLE>(_get_osfhandle(descriptor)),path,32768,FILE_NAME_NORMALIZED);
        if(!length||length>=32768){LocalFree(acl);return false;}target=QString::fromWCharArray(path,int(length));
    }
    // QFile's CRT handle need not carry WRITE_DAC; reopen the actual temporary pathname as its owner.
    const auto result=SetNamedSecurityInfoW(const_cast<wchar_t *>(reinterpret_cast<const wchar_t *>(target.utf16())),SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,acl,nullptr);
    LocalFree(acl);return result==ERROR_SUCCESS;
#else
    Q_UNUSED(descriptor);
    return QFile::setPermissions(name,QFileDevice::ReadOwner|QFileDevice::WriteOwner);
#endif
}
QByteArray derive(const QByteArray &password,const QByteArray &salt,int iterations) {
    QByteArray value(32,0);
    if(PKCS5_PBKDF2_HMAC(password.constData(),int(password.size()),reinterpret_cast<const unsigned char *>(salt.constData()),int(salt.size()),iterations,EVP_sha256(),32,reinterpret_cast<unsigned char *>(value.data()))!=1)value.clear();
    return value;
}
bool validPassword(const QByteArray &password) {
    const auto text=QString::fromUtf8(password);
    return text.size()>=6&&text.size()<=128&&!text.contains(QChar::Null)&&text.toUtf8()==password;
}
}
void SecurityStore::wipe(QByteArray &bytes){bytes.detach();if(!bytes.isEmpty())OPENSSL_cleanse(bytes.data(),size_t(bytes.size()));bytes.clear();}
SecurityStore::SecurityStore(const QString &path,QObject *parent):QObject(parent),directory(path),fileName(QDir(path).filePath("application-lock.json")){load();}
SecurityStore::~SecurityStore(){if(worker){worker->wait();delete worker;}}
void SecurityStore::load(){
    QFile file(fileName);if(!file.exists()&&!QFileInfo::exists(QDir(directory).filePath("application-lock.required")))return;
    active=true;problem="应用锁配置损坏或无法读取。请保留此文件并从可信备份恢复；界面重置不会解除锁定。";
    if(!file.open(QIODevice::ReadOnly)||file.size()>4096)return;
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||!doc.isObject())return;
    const auto o=doc.object();if(o["version"]!=1||!o["enabled"].isBool())return;
    if(!o["enabled"].toBool()){
        if(o.contains("salt")||o.contains("verifier")||o.contains("iterations")||o.contains("kdf"))return;
        active=false;problem.clear();return;
    }
    if(o["kdf"]!="PBKDF2-HMAC-SHA256"||o["iterations"]!=600000)return;
    const auto s=o["salt"].toString().toLatin1(),v=o["verifier"].toString().toLatin1();
    salt=QByteArray::fromBase64(s);verifier=QByteArray::fromBase64(v);
    if(salt.size()!=32||verifier.size()!=32||salt.toBase64()!=s||verifier.toBase64()!=v)return;
    problem.clear();
}
bool SecurityStore::write(bool enabled,const QByteArray &s,const QByteArray &v,QString &error){
    published=false;
    QSaveFile file(fileName);file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly)){error="无法写入应用锁配置，原设置保持不变。";return false;}
#ifdef Q_OS_WIN
    if(!privateFile(fileName,file.handle())){file.cancelWriting();error="无法限制应用锁文件权限，未保存。";return false;}
#else
    if(!file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)){file.cancelWriting();error="无法限制应用锁文件权限，未保存。";return false;}
#endif
    QJsonObject o{{"version",1},{"enabled",enabled}};
    if(enabled){o["kdf"]="PBKDF2-HMAC-SHA256";o["iterations"]=iterations;o["salt"]=QString::fromLatin1(s.toBase64());o["verifier"]=QString::fromLatin1(v.toBase64());}
    const auto bytes=QJsonDocument(o).toJson();
    if(file.write(bytes)!=bytes.size()||!file.commit()){error="应用锁配置保存失败，原设置保持不变。";return false;}
    published=true;
    // QSaveFile applies permissions to its temporary file; also secure the published inode.
    if(!privateFile(fileName)){error="应用锁配置已写入，但无法确认私有权限。请检查数据目录权限。";active=true;problem=error;return false;}
    // A durable, non-secret marker distinguishes a never-configured installation from a lost verifier.
    const auto marker=QDir(directory).filePath("application-lock.required");
    if(QFileInfo::exists(marker)&&!QFileInfo(marker).isFile()){error="密码保护已写入，但保护标记不是有效文件。请检查数据目录。";return false;}
    if(!QFileInfo::exists(marker)){
        QSaveFile required(marker);required.setDirectWriteFallback(false);
        if(!required.open(QIODevice::WriteOnly)){error="密码保护已写入，但保护标记写入失败。请使用刚设置的密码解锁，并检查数据目录权限。";return false;}
#ifdef Q_OS_WIN
        const bool secured=privateFile(marker,required.handle());
#else
        const bool secured=required.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
#endif
        if(!secured||required.write("1\n")!=2||!required.commit()){error="密码保护已写入，但私有保护标记保存失败。请使用刚设置的密码解锁，并检查数据目录权限。";return false;}
    }
    return true;
}
int SecurityStore::retryAfterMs()const{return throttle.hasExpired()?0:int(qMin<qint64>(throttle.remainingTime(),30000));}
void SecurityStore::request(Action action,QByteArray current,QByteArray replacement){
    const auto operation=OperationGate::token();
    auto fail=[&](QString message){wipe(current);wipe(replacement);emit completed(action,false,message);};
    if(working){fail("正在校验，请稍候。");return;}
    if(action!=Verify&&!OperationGate::accepts(operation)){fail("应用已锁定，设置操作已取消。");return;}
    if(damaged()){fail(problem);return;}
    if(retryAfterMs()){fail(QString("尝试过多，请在 %1 秒后重试。").arg((retryAfterMs()+999)/1000));return;}
    if((action==Enable&&active)||(action!=Enable&&!active)){fail("应用锁状态已变化，请重新操作。");return;}
    if((action==Enable||action==Change)&&!validPassword(replacement)){fail("密码需要 6–128 个字符，不能包含空字符。");return;}
    working=true;
    const auto requestId=++serial;
    struct Result{bool matched=false,ok=true;QByteArray salt,value;};auto result=std::make_shared<Result>();
    const auto oldSalt=salt,oldVerifier=verifier;const int rounds=iterations;
    worker=QThread::create([action,current=std::move(current),replacement=std::move(replacement),oldSalt,oldVerifier,rounds,result]()mutable{
        if(action!=Enable){auto actual=derive(current,oldSalt,rounds);result->matched=actual.size()==32&&CRYPTO_memcmp(actual.constData(),oldVerifier.constData(),32)==0;wipe(actual);}
        else result->matched=true;
        if(result->matched&&(action==Enable||action==Change)){
            result->salt=QByteArray(32,0);result->ok=RAND_bytes(reinterpret_cast<unsigned char *>(result->salt.data()),32)==1;
            if(result->ok){result->value=derive(replacement,result->salt,rounds);result->ok=result->value.size()==32;}
        }
        wipe(current);wipe(replacement);
    });
    connect(worker,&QThread::finished,this,[this,action,result,operation,requestId]{
        auto finished=worker;worker=nullptr;finished->deleteLater();working=false;
        if(requestId!=serial){emit completed(action,false,"密码设置操作已取消。");return;}
        if(!result->matched){++failures;throttle=QDeadlineTimer(qMin(30000,1000*(1<<qMin(failures-1,5))));emit completed(action,false,"密码不正确。请稍后重试。");return;}
        QString error;
        if(action!=Verify&&!OperationGate::accepts(operation)){emit completed(action,false,"锁定期间密码设置操作已取消，请解锁后重试。");return;}
        bool ok=result->ok;
        if(ok&&action!=Verify)ok=write(action!=Disable,result->salt,result->value,error);
        if(ok){failures=0;throttle=QDeadlineTimer(0);if(action!=Verify){active=action!=Disable;salt=result->salt;verifier=result->value;emit changed();}}
        if(!ok&&published){load();emit changed();}
        emit completed(action,ok,ok?QString():error.isEmpty()?QString("密码计算失败，设置未更改。"):error);
    });worker->start();
}
}
