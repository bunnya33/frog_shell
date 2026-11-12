#include "operationgate.h"
#include "session.h"
#include "sshtrust.h"
#include "diagnostics.h"
#include "fileoperationjournal.h"
#include "filename.h"
#include "listeningports.h"
#include "serverclock.h"
#include "editorlimits.h"
#include <QTcpSocket>
#include <memory>
#include <optional>
#include <limits>
#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <cerrno>
#endif

namespace rs {
namespace {
void require(bool ok, const QString &why) {
    if (!ok)
        throw why;
}
QString sftpReason(LIBSSH2_SFTP *s) {
    switch (libssh2_sftp_last_error(s)) {
    case LIBSSH2_FX_PERMISSION_DENIED:
        return "权限不足";
    case LIBSSH2_FX_NO_SUCH_FILE:
        return "文件或目录不存在";
    case LIBSSH2_FX_FILE_ALREADY_EXISTS:
        return "目标已存在";
    default:
        return "SFTP 操作失败（代码 " + QString::number(libssh2_sftp_last_error(s)) + "）";
    }
}
} // namespace
Session::Session(Profile p, QByteArray credential, QObject *parent, Mode m)
    : QThread(parent), profile(std::move(p)), secret(std::move(credential)), mode(m) {startEpoch=OperationGate::token();}
Session::~Session() {
    stop();
    wait();
    secret.fill('\0');
}
void Session::submit(const Job &j) {
    if(OperationGate::locked()&&(j.type=="input"||j.type=="open-terminal"||j.type=="delete"||j.type=="rename"||j.type=="mkdir"||j.type=="chmod"||j.type=="save"||j.type=="touch")){emit failed(j.type,j.id,"应用已锁定，操作已取消。");return;}
    QMutexLocker lock(&mutex);
    if (stopping)
        return;
    if (j.type == "resize") {
        for (auto &pending : jobs)
            if (pending.type == j.type && pending.id == j.id) {
                pending = j;
                return;
            }
    }
    // Ordinary work retains its small budget. One larger save may wait alongside
    // it, bounded separately, so a large editor does not starve terminal input.
    constexpr qsizetype ordinaryLimit = 8 * 1024 * 1024;
    constexpr qsizetype largeSaveLimit = 512 * 1024 * 1024;
    qsizetype largePending = 0;
    for (const auto &pending : jobs)
        if (pending.type == "save" && pending.data.size() > ordinaryLimit)
            largePending += pending.data.size();
    const bool largeSave = j.type == "save" && j.data.size() > ordinaryLimit;
    const bool full = largeSave ? (largePending != 0 || j.data.size() > largeSaveLimit)
                                : (j.data.size() > ordinaryLimit - (queuedJobBytes - largePending));
    if (jobs.size() >= 256 || full) {
        lock.unlock();
        emit failed(j.type, j.id, "操作队列已满，请稍后重试");
        return;
    }
    queuedJobBytes += j.data.size();
    if (j.type == "upload" || j.type == "download" || j.type == "move")
        transferCancelled = false;
    jobs.push_back(j);
    if(j.type=="upload")diagnose(DiagEvent::UploadSubmit,diagnosticTag,j.id,j.path);
    else if(j.type=="delete")diagnose(DiagEvent::DeleteSubmit,diagnosticTag,j.id,j.path);
    else if(j.type=="list")diagnose(DiagEvent::ListSubmit,diagnosticTag,j.id,j.path);
    wake.wakeAll();
}
void Session::stop() {
    stopRequested=true;
    diagnose(DiagEvent::SessionStop,diagnosticTag);
    QMutexLocker lock(&mutex);
    stopping = true;
    wake.wakeAll();
}
void Session::cancelTransfer() {
    diagnose(DiagEvent::UploadCancel,diagnosticTag);
    QMutexLocker lock(&mutex);transferCancelled=true;wake.wakeAll();
}
bool Session::hasUploadConflict(int id,quint64 nonce) {
    QMutexLocker lock(&mutex);
    return conflictWaiting && conflictJob==id && conflictNonce==nonce && conflictAnswer.isEmpty() && !stopping && !transferCancelled;
}
bool Session::answerUploadConflict(int id, quint64 nonce, const QString &choice, bool applyToBatch) {
    QMutexLocker lock(&mutex);
    if(!conflictWaiting || conflictJob!=id || conflictNonce!=nonce || !conflictAnswer.isEmpty() || stopping || transferCancelled)return false;
    conflictAnswer=QStringList{"overwrite","keep","skip","cancel"}.contains(choice)?choice:"cancel";
    conflictBatch=applyToBatch;wake.wakeAll();return true;
}
QString Session::uploadChoice(const Job &job, const QString &path, bool canOverwrite) {
    auto choice=job.collision;
    const auto batch=job.options.value("uploadBatch").toString();
    if(batch!=policyBatch){policyBatch=batch;batchPolicy.clear();}
    if(choice=="ask" && !batch.isEmpty() && !batchPolicy.isEmpty())choice=batchPolicy;
    // A batch overwrite decision never authorizes deleting/replacing a directory/link.
    if(choice=="overwrite" && !canOverwrite) choice="ask";
    if(choice=="ask") {
        QElapsedTimer waiting;waiting.start();
        transferMeter.resetSample();
        emit transferStatistics(job.id,transferMeter.bytes,transferMeter.activeMs(),0,false);
        quint64 nonce;
        {
            QMutexLocker lock(&mutex);
            conflictJob=job.id;nonce=++conflictNonce;conflictWaiting=true;conflictAnswer.clear();conflictBatch=false;
        }
        emit uploadConflict(job.id,nonce,path,canOverwrite);
        bool apply=false;
        {
            QMutexLocker lock(&mutex);
            while(conflictAnswer.isEmpty() && !stopping && !transferCancelled)wake.wait(&mutex);
            choice=conflictAnswer.isEmpty()?"cancel":conflictAnswer;apply=conflictBatch;
            conflictWaiting=false;conflictJob=0;conflictAnswer.clear();
        }
        emit uploadConflictClosed(job.id,nonce);
        transferMeter.excludedMs+=waiting.elapsed();transferMeter.resetSample();transferMeter.lastReportMs=transferMeter.activeMs();
        if(apply && !batch.isEmpty() && choice!="cancel")batchPolicy=choice; // one current batch only
    }
    if(choice=="cancel") {diagnose(DiagEvent::UploadCancel,diagnosticTag,job.id,path);transferCancelled=true;throw QString(job.type=="move"?"移动已取消，来源保留":"上传已取消");}
    require(choice=="keep" || choice=="skip" || (choice=="overwrite" && canOverwrite),"未授权替换此上传目标");
    checkTransfer();return choice;
}
QVariantMap Session::uploadStat(const QString &path) {
    LIBSSH2_SFTP_ATTRIBUTES a{};
    const auto rc=sshWait([&]{return libssh2_sftp_lstat(sftp,path.toUtf8().constData(),&a);});
    if(rc!=0) {
        require(rc==LIBSSH2_ERROR_SFTP_PROTOCOL && libssh2_sftp_last_error(sftp)==LIBSSH2_FX_NO_SUCH_FILE && !transportFailed(),"无法检查上传目标："+path+" · "+sftpReason(sftp));
        return {};
    }
    require(a.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS,"服务器未返回上传目标的类型/权限："+path);
    return {{"permissions",quint32(a.permissions)},{"flags",qulonglong(a.flags)},{"uid",quint32(a.uid)},{"gid",quint32(a.gid)},
            {"directory",bool(LIBSSH2_SFTP_S_ISDIR(a.permissions))},{"link",bool(LIBSSH2_SFTP_S_ISLNK(a.permissions))},
            {"size",qulonglong(a.filesize)},{"modified",qlonglong(a.mtime)}};
}
void Session::trust(bool accepted) {
    QMutexLocker lock(&mutex);
    trustAnswer = accepted&&!OperationGate::locked() ? 1 : -1;
    wake.wakeAll();
}
QString Session::error() const {
    char *msg = nullptr;
    libssh2_session_last_error(ssh, &msg, nullptr, 0);
    return msg ? QString::fromUtf8(msg) : "SSH 连接失败";
}
void Session::traceStage(ConnectionStage value,int timeout){connectionStage=value;stageTimeout=timeout;stageClock.start();}
void Session::traceResult(DiagEvent event,bool failed){
    ConnectionDiagnosticState state{connectionStage,int(mode),ssh?libssh2_session_last_errno(ssh):0,stageTimeout,
        stageClock.isValid()?stageClock.elapsed():0,failed,stopRequested.load()};
    diagnoseConnection(event,diagnosticTag,state);
}
void Session::waitSocket(){
    fd_set rd,wr;FD_ZERO(&rd);FD_ZERO(&wr);const auto fd=libssh2_socket_t(descriptor);FD_SET(fd,&rd);
    const bool outbound=ssh&&(libssh2_session_block_directions(ssh)&LIBSSH2_SESSION_BLOCK_OUTBOUND);if(outbound)FD_SET(fd,&wr);
    timeval delay{0,50000};const int rc=select(int(fd+1),&rd,outbound?&wr:nullptr,nullptr,&delay);
    if(rc>0&&FD_ISSET(fd,&rd)){char byte;const auto n=recv(fd,&byte,1,MSG_PEEK);if(n==0)throw QString("TCP 连接已由对端关闭");}
    if(rc<0){
#ifdef Q_OS_WIN
        if(WSAGetLastError()!=WSAEINTR)throw QString("操作系统报告 TCP 连接不可用");
#else
        if(errno!=EINTR)throw QString("操作系统报告 TCP 连接不可用");
#endif
    }
}
void Session::drainTerminalInput(){
    QMutexLocker lock(&mutex);
    for(auto it=jobs.begin();it!=jobs.end();){
        if(it->type=="input"&&channels.contains(it->id)){
            if(pendingWrites[it->id].size()+it->data.size()<=256*1024)pendingWrites[it->id]+=it->data;
            else emit failed(it->type,it->id,"终端输入队列已满");
            queuedJobBytes-=it->data.size();it=jobs.erase(it);
        }else if((it->type=="close-terminal"||it->type=="resize")&&channels.contains(it->id)){
            if(it->type=="close-terminal")closingTerminals.insert(it->id);else pendingSizes[it->id]=QSize(it->columns,it->rows);
            queuedJobBytes-=it->data.size();it=jobs.erase(it);
        }else ++it;
    }
}
void Session::pumpTerminals(bool waiting){
    QScopedValueRollback<bool> guard(pumping,true);
    const auto saved=connectionStage;const auto savedClock=stageClock;const int savedTimeout=stageTimeout;
    for(auto it=channels.begin();it!=channels.end();){
        const int id=it.key();auto ch=it.value();auto &chunk=writingChunks[id];auto &pending=pendingWrites[id];
        if(freeingTerminals.contains(id)){
            traceStage(ConnectionStage::ChannelClose);const auto rc=libssh2_channel_free(ch);
            if(rc==0){pendingWrites.remove(id);writingChunks.remove(id);closingTerminals.remove(id);freeingTerminals.remove(id);pendingSizes.remove(id);writingSizes.remove(id);it=channels.erase(it);emit terminalClosed(id);continue;}
            if(rc!=LIBSSH2_ERROR_EAGAIN)throw error();++it;continue;
        }
        if(chunk.isEmpty()&&!pending.isEmpty()&&!closingTerminals.contains(id)){chunk=pending.left(16384);pending.remove(0,chunk.size());}
        if(!chunk.isEmpty()){
            traceStage(ConnectionStage::PtyWrite);const auto n=libssh2_channel_write(ch,chunk.constData(),size_t(chunk.size()));
            if(n>0)chunk.remove(0,n);else if(n<0&&n!=LIBSSH2_ERROR_EAGAIN)throw error();
        }
        if(chunk.isEmpty()&&!closingTerminals.contains(id)){
            if(!writingSizes.contains(id)&&pendingSizes.contains(id))writingSizes[id]=pendingSizes.take(id);
            if(writingSizes.contains(id)){const auto size=writingSizes[id];const auto rc=libssh2_channel_request_pty_size(ch,size.width(),size.height());if(rc!=LIBSSH2_ERROR_EAGAIN)writingSizes.remove(id);}
        }
        if(queuedOutput<256*1024){char data[16384];for(int stream=0;stream<2;++stream){
            traceStage(ConnectionStage::PtyRead);const auto n=libssh2_channel_read_ex(ch,stream,data,sizeof(data));
            if(n>0){queuedOutput+=n;emit terminalOutput(id,QByteArray(data,n));}else if(n<0&&n!=LIBSSH2_ERROR_EAGAIN)throw error();
        }}
        if(libssh2_channel_eof(ch)||(closingTerminals.contains(id)&&chunk.isEmpty())){
            freeingTerminals.insert(id);
            traceStage(ConnectionStage::ChannelClose);const auto rc=libssh2_channel_free(ch);
            if(rc==0){pendingWrites.remove(id);writingChunks.remove(id);closingTerminals.remove(id);freeingTerminals.remove(id);pendingSizes.remove(id);writingSizes.remove(id);it=channels.erase(it);emit terminalClosed(id);continue;}
            if(rc!=LIBSSH2_ERROR_EAGAIN)throw error();
        }
        ++it;
    }
    // Do not start a different outgoing packet while an earlier API is waiting
    // to finish its packet. libssh2 returns EAGAIN for that case; the caller
    // resumes the same original API and buffers before attempting it again.
    if(!waiting&&keepaliveClock.isValid()&&keepaliveClock.elapsed()>20000){
        traceStage(ConnectionStage::Keepalive);int next=0;const auto rc=libssh2_keepalive_send(ssh,&next);
        if(rc<0&&rc!=LIBSSH2_ERROR_EAGAIN)throw error();if(rc==0)keepaliveClock.restart();
    }
    connectionStage=saved;stageClock=savedClock;stageTimeout=savedTimeout;
}
void Session::closeHandle(LIBSSH2_SFTP_HANDLE *handle) noexcept {
    try{if(!stopping)sshWait([&]{return libssh2_sftp_close_handle(handle);});else libssh2_sftp_close_handle(handle);}catch(...){}
}
bool Session::initialize() {
    if(!OperationGate::accepts(startEpoch))throw QString("连接请求已因锁定失效。");
    static const int initialized = libssh2_init(0);
    require(initialized == 0, "SSH 库初始化失败");
    ssh = libssh2_session_init();
    require(ssh, "无法创建 SSH 会话");
    // Prefer modern fixed-group/ECDH methods. Unsupported methods are filtered by libssh2.
    libssh2_session_method_pref(ssh, LIBSSH2_METHOD_KEX,
                                "curve25519-sha256,curve25519-sha256@libssh.org,ecdh-sha2-nistp256,"
                                "diffie-hellman-group16-sha512,diffie-hellman-group14-sha256");
    libssh2_session_set_blocking(ssh, 0);
    libssh2_session_set_timeout(ssh, profile.timeout * 1000);
    emit phase("SSH 握手…");
    traceStage(ConnectionStage::Handshake,profile.timeout*1000);
    if (sshWait([&]{return libssh2_session_handshake(ssh, libssh2_socket_t(descriptor));}) != 0)
        throw QString("SSH 握手失败：") + error();
    traceResult(DiagEvent::ConnectionResult);
    auto host = sshtrust::check(ssh, profile);
    if (host.status == sshtrust::Result::Error)
        throw QString("主机身份校验失败：") + host.reason;
    if (host.status == sshtrust::Result::Changed) {
        emit hostKey(host.fingerprint, true);
        throw QString("主机指纹变化，已停止连接。请核实服务器身份。");
    }
    if (host.status == sshtrust::Result::Unknown) {
        emit phase("等待确认主机指纹");
        emit hostKey(host.fingerprint, false);
        {
            QMutexLocker lock(&mutex);
            while (!trustAnswer && !stopping)
                wake.wait(&mutex, 250);
        }
        if (trustAnswer != 1 || stopping)
            throw QString("已取消连接");
        const auto accepted = sshtrust::check(ssh, profile, true);
        if (accepted.status == sshtrust::Result::Changed) {
            emit hostKey(accepted.fingerprint, true);
            throw QString("主机指纹变化，已停止连接。请核实服务器身份。");
        }
        require(accepted.status == sshtrust::Result::Trusted, "主机身份校验失败：" + accepted.reason);
    }
    if (stopping)
        return false;
    emit phase("正在认证…");
    auto user = profile.user.toUtf8();
    traceStage(ConnectionStage::Authentication,profile.timeout*1000);
    int auth = -1;
    if (profile.authentication == "key")
        auth = sshWait([&]{return libssh2_userauth_publickey_fromfile_ex(ssh, user.constData(), unsigned(user.size()), nullptr,
                                                      QFile::encodeName(profile.keyPath).constData(),
                                                      secret.constData());});
    else if (profile.authentication == "agent") {
        auto agent = libssh2_agent_init(ssh);
        if (agent) {
            if (libssh2_agent_connect(agent) == 0 && libssh2_agent_list_identities(agent) == 0) {
                libssh2_agent_publickey *id = nullptr, *prev = nullptr;
                while (libssh2_agent_get_identity(agent, &id, prev) == 0) {
                    if (sshWait([&]{return libssh2_agent_userauth(agent, user.constData(), id);}) == 0) {
                        auth = 0;
                        break;
                    }
                    prev = id;
                }
            }
            libssh2_agent_disconnect(agent);
            libssh2_agent_free(agent);
        }
    } else
        auth = sshWait([&]{return libssh2_userauth_password_ex(ssh, user.constData(), unsigned(user.size()), secret.constData(),
                                            unsigned(secret.size()), nullptr);});
    secret.fill('\0');
    secret.clear();
    if (auth == LIBSSH2_ERROR_AUTHENTICATION_FAILED && profile.authentication == "password")
        emit passwordRejected();
    if (auth != 0 && profile.authentication == "key" && !transportFailed())
        emit keyAuthenticationNeedsInput();
    require(auth == 0, "认证失败：" + error());
    traceResult(DiagEvent::ConnectionResult);
    operationJournal=std::make_unique<FileOperationJournal>(profile,host.fingerprint);
    emit authenticatedHostIdentity(host.fingerprint);
    emit authenticated();
    libssh2_keepalive_config(ssh, 1, 20);
    established=true;keepaliveClock.start();libssh2_session_set_timeout(ssh,0);
    // Upstream 0 means a 60s packet timeout, not unlimited. Use its largest
    // supported interval; the application never abandons an established API
    // merely because a local response threshold elapsed.
    libssh2_session_set_read_timeout(ssh,(std::numeric_limits<long>::max)());
    if (mode == Mode::Files) {
        traceStage(ConnectionStage::Sftp);sftp = sshWait([&]{return libssh2_sftp_init(ssh);});
        require(sftp, "无法打开 SFTP：" + error());
    }
    emit connected();
    return true;
}
void Session::run() {
    if(!OperationGate::accepts(startEpoch)){secret.fill(0);emit disconnected("应用已锁定，新连接已取消。");return;}
    QString reason = "已断开";
    QTcpSocket socket;
    try {
        traceStage(ConnectionStage::TcpConnect,profile.timeout*1000);
        emit phase("连接 " + profile.host + "…");
        socket.connectToHost(profile.host, quint16(profile.port));
        if (!socket.waitForConnected(profile.timeout * 1000))
            throw socket.errorString();
        traceResult(DiagEvent::ConnectionResult);
        socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
        descriptor = socket.socketDescriptor();
        if (initialize()) {
            while (!stopping) {
                std::optional<Job> job;
                {QMutexLocker lock(&mutex);if(!jobs.empty()){job=std::move(jobs.front());queuedJobBytes-=job->data.size();jobs.pop_front();}}
                libssh2_session_set_blocking(ssh,0);
                if(job){
                    traceStage(ConnectionStage::Sftp);
                    try{perform(*job);}
                    catch(const QString &e){if(transportFailed()||stopping)throw e;traceResult(DiagEvent::ConnectionResult,true);emit failed(job->type,job->id,e);}
                }
                libssh2_session_set_blocking(ssh,0);pumpTerminals();
                QMutexLocker lock(&mutex);if(jobs.empty()&&!stopping)wake.wait(&mutex,channels.isEmpty()?unsigned(qMax<qint64>(1,20001-keepaliveClock.elapsed())):12);
            }
        }
    } catch (const QString &e) {
        reason = e;traceResult(DiagEvent::ConnectionResult,!stopRequested);
    }
    if(stopRequested)traceStage(ConnectionStage::Stop);
    traceResult(DiagEvent::ConnectionExit,!stopRequested);
    // Unblock cleanup without releasing/reusing the descriptor while libssh2
    // still owns references to it. QTcpSocket closes it after SSH is freed.
    if(descriptor>=0)shutdown(libssh2_socket_t(descriptor),2);
    if (ssh) {
        libssh2_session_set_blocking(ssh, 1);
        libssh2_session_set_timeout(ssh, 500);
        for (auto ch : channels)
            libssh2_channel_free(ch);
        channels.clear();
        if (sftp)
            libssh2_sftp_shutdown(sftp);
        sftp = nullptr;
        libssh2_session_disconnect(ssh, "RabbitShell disconnect");
        libssh2_session_free(ssh);
        ssh = nullptr;
    }
    socket.abort();descriptor=-1;
    secret.fill('\0');
    secret.clear();
    operationJournal.reset();
    emit disconnected(reason);
}
QVariantMap Session::stat(const QString &path) {
    LIBSSH2_SFTP_ATTRIBUTES a{};
    require(sshWait([&]{return libssh2_sftp_lstat(sftp, path.toUtf8().constData(), &a);}) == 0, sftpReason(sftp));
    return {{"size", qulonglong(a.filesize)},
            {"permissions", quint32(a.permissions)},
            {"uid", quint32(a.uid)},
            {"gid", quint32(a.gid)},
            {"modified", qlonglong(a.mtime)},
            {"directory", bool(LIBSSH2_SFTP_S_ISDIR(a.permissions))},
            {"link", bool(LIBSSH2_SFTP_S_ISLNK(a.permissions))}};
}
QVariantList Session::list(const QString &path) {
    Handle dir{this,sshWait([&]{return libssh2_sftp_opendir(sftp, path.toUtf8().constData());})};
    require(dir.p, sftpReason(sftp));
    QVariantList entries;
    char name[4096];
    LIBSSH2_SFTP_ATTRIBUTES a{};
    while (!stopping) {
        a = {}; // readdir may omit fields; never reuse the previous entry's attributes.
        auto n = sshWait([&]{return libssh2_sftp_readdir(dir.p, name, sizeof(name), &a);});
        require(n >= 0, sftpReason(sftp));
        if (n == 0)
            break;
        require(a.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS,"服务器未返回目录项的类型/权限");
        QString file = QString::fromUtf8(name, n);
        if (file == "." || file == "..")
            continue;
        require(!file.contains('/') && !file.contains('\\'), "目录包含无法安全映射的文件名");
        entries.append(QVariantMap{{"name", file},
                                   {"path", remoteJoin(path, file)},
                                   {"size", (a.flags & LIBSSH2_SFTP_ATTR_SIZE) ? QVariant::fromValue(qulonglong(a.filesize)) : QVariant()},
                                   {"permissions", quint32(a.permissions)},
                                   {"uid", quint32(a.uid)},
                                   {"gid", quint32(a.gid)},
                                   {"modified", qlonglong(a.mtime)},
                                   {"directory", bool(LIBSSH2_SFTP_S_ISDIR(a.permissions))},
                                   {"link", bool(LIBSSH2_SFTP_S_ISLNK(a.permissions))}});
        require(entries.size() <= 10000, "目录超过 10,000 项，已停止加载以控制内存；请打开更具体的目录。");
    }
    return entries;
}
QByteArray Session::readFile(const QString &path, quint64 max, bool hashOnly) {
    auto info = stat(path);
    require(!info["directory"].toBool(), "不能将目录作为文件打开");
    const auto limitMessage = hashOnly
        ? QString("CONFLICT:远端文件超过冲突校验上限 %1 MiB，草稿已保留；请下载后处理").arg(max / editorlimits::MiB)
        : QString("文件超过编辑上限 %1 MiB，请在设置 → 文件编辑中调整，或下载后处理").arg(max / editorlimits::MiB);
    require(info["size"].toULongLong() <= max, limitMessage);
    Handle h{this,sshWait([&]{return libssh2_sftp_open(sftp, path.toUtf8().constData(), LIBSSH2_FXF_READ, 0);})};
    require(h.p, sftpReason(sftp));
    QByteArray bytes;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    quint64 total = 0;
    char chunk[65536];
    while (!stopping) {
        auto n = sshWait([&]{return libssh2_sftp_read(h.p, chunk, sizeof(chunk));});
        require(n >= 0, sftpReason(sftp));
        if (!n)
            break;
        require(quint64(n) <= max - total, limitMessage);
        total += quint64(n);
        if (hashOnly) hash.addData(QByteArrayView(chunk, n));
        else bytes.append(chunk, n);
    }
    require(!stopping, "操作已取消");
    return hashOnly ? hash.result() : bytes;
}
services::Result Session::serviceCommand(const services::Command &c) {
    require(mode==Mode::Commands&&channels.isEmpty(),"有限时命令仅允许在独立采集/服务连接执行");
    services::Result r;
    LIBSSH2_CHANNEL *ch = nullptr;
    QElapsedTimer clock; clock.start();
    libssh2_session_set_blocking(ssh, 0);
    bool warned=false;const auto clearWaiting=qScopeGuard([&]{if(warned){waitingNotice=false;if(!r.unknown)traceResult(DiagEvent::ConnectionResult);emit responseWaiting(false);}});
    auto alive = [&] {
        if(!warned&&clock.elapsed()>=5000){warned=true;waitingNotice=true;traceResult(DiagEvent::ConnectionWait);emit responseWaiting(true);}
        return !stopping && clock.elapsed() < c.timeoutMs;
    };
    auto retry = [&](auto function) {
        int rc;
        do { rc = function(); if (rc == LIBSSH2_ERROR_EAGAIN) QThread::msleep(4); }
        while (rc == LIBSSH2_ERROR_EAGAIN && alive());
        return rc;
    };
    traceStage(ConnectionStage::ChannelOpen,c.timeoutMs);
    while (alive() && !ch) {
        ch = libssh2_channel_open_session(ssh);
        if (!ch && libssh2_session_last_errno(ssh) != LIBSSH2_ERROR_EAGAIN) break;
        if (!ch) QThread::msleep(4);
    }
    bool ok = ch && alive();
    if (ok){traceStage(ConnectionStage::Exec,c.timeoutMs);ok = retry([&] {return libssh2_channel_exec(ch, c.text.constData());}) == 0;}
    qsizetype pos=0;
    while (ok && pos < c.input.size() && alive()) {
        auto n=libssh2_channel_write(ch,c.input.constData()+pos,size_t(c.input.size()-pos));
        if(n>0)pos+=n;else if(n!=LIBSSH2_ERROR_EAGAIN){ok=false;break;}else QThread::msleep(4);
    }
    if(ok)ok=retry([&]{return libssh2_channel_send_eof(ch);})==0;
    traceStage(ConnectionStage::MonitorRead,c.timeoutMs);char buffer[8192];
    while(ok && alive()) {
        bool data=false;
        for(int stream=0;stream<2;++stream) {
            auto n=libssh2_channel_read_ex(ch,stream,buffer,sizeof(buffer));
            if(n>0){(stream?r.err:r.out).append(buffer,n);data=true;}
            else if(n<0 && n!=LIBSSH2_ERROR_EAGAIN){ok=false;break;}
        }
        if(r.out.size()+r.err.size()>c.outputLimit){ok=false;break;}
        if(!data && libssh2_channel_eof(ch))break;
        if(!data)QThread::msleep(4);
    }
    ok = ok && alive();
    if(ok){traceStage(ConnectionStage::ChannelClose,c.timeoutMs);ok=retry([&]{return libssh2_channel_close(ch);})==0;}
    if(ok)ok=retry([&]{return libssh2_channel_wait_closed(ch);})==0;
    if(ok) {
        char *signal=nullptr;
        libssh2_channel_get_exit_signal(ch,&signal,nullptr,nullptr,nullptr,nullptr,nullptr);
        if(signal){libssh2_free(ssh,signal);ok=false;}
        else r.code=libssh2_channel_get_exit_status(ch);
    }
    r.unknown=!ok;
    if(!ok)traceResult(DiagEvent::ConnectionResult,true);
    r.out.truncate(c.outputLimit);r.err.truncate(qMax(0,c.outputLimit-int(r.out.size())));
    libssh2_session_set_blocking(ssh,1);libssh2_session_set_timeout(ssh,500);
    if(ch)libssh2_channel_free(ch);
    libssh2_session_set_timeout(ssh,5000);
    // An interrupted nonblocking protocol operation must never be reused or replayed.
    if(!ok)stopping=true;
    return r;
}
QByteArray Session::execute(const QByteArray &command) {
    traceStage(ConnectionStage::ChannelOpen);auto ch=sshWait([&]{return libssh2_channel_open_session(ssh);});require(ch,error());
    traceStage(ConnectionStage::Exec);
    if(sshWait([&]{return libssh2_channel_exec(ch,command.constData());})!=0){sshWait([&]{return libssh2_channel_free(ch);});throw error();}
    traceStage(ConnectionStage::MonitorRead);QByteArray result;QElapsedTimer timer;timer.start();bool warned=false;
    try{
        while(!stopping&&!libssh2_channel_eof(ch)){
            bool received=false;char chunk[8192];
            for(int stream=0;stream<2;++stream){const auto n=libssh2_channel_read_ex(ch,stream,chunk,sizeof(chunk));
                if(n>0){result.append(chunk,n);received=true;require(result.size()<=4*1024*1024,"命令输出超过 4 MiB 上限");}
                else if(n<0&&n!=LIBSSH2_ERROR_EAGAIN)throw error();
            }
            if(!warned&&timer.elapsed()>=8000){warned=true;waitingNotice=true;traceResult(DiagEvent::ConnectionWait);emit responseWaiting(true);}
            drainTerminalInput();pumpTerminals(true);if(!received)waitSocket();
        }
        if(stopping)throw QString("操作已停止");
    }catch(...){if(!stopping)sshWait([&]{return libssh2_channel_free(ch);});throw;}
    traceStage(ConnectionStage::ChannelClose);sshWait([&]{return libssh2_channel_free(ch);});
    if(warned){waitingNotice=false;traceResult(DiagEvent::ConnectionResult);emit responseWaiting(false);}return result;
}
void Session::TransferMeter::begin(int taskId) {
    id = taskId;
    bytes = sampledBytes = 0;
    excludedMs = sampledMs = lastReportMs = 0;
    clock.start();
}
qint64 Session::TransferMeter::activeMs() const {
    return clock.isValid() ? qMax<qint64>(0, clock.elapsed() - excludedMs) : 0;
}
void Session::TransferMeter::resetSample() {
    sampledBytes = bytes;
    sampledMs = activeMs();
}
void Session::reportTransfer(bool final) {
    const auto elapsed = transferMeter.activeMs();
    if (!final && elapsed - transferMeter.lastReportMs < 120) return;
    const auto interval = elapsed - transferMeter.sampledMs;
    const double rate = interval > 0 ? double(transferMeter.bytes - transferMeter.sampledBytes) * 1000 / interval : 0;
    emit transferStatistics(transferMeter.id, transferMeter.bytes, elapsed, rate, final);
    transferMeter.lastReportMs = elapsed;
    transferMeter.resetSample();
}
void Session::checkTransfer() {
    if (paused && !stopping && !transferCancelled) {
        QElapsedTimer pauseClock;
        pauseClock.start();
        transferMeter.resetSample();
        emit transferStatistics(transferMeter.id, transferMeter.bytes, transferMeter.activeMs(), 0, false);
        while (paused && !stopping && !transferCancelled) QThread::msleep(30);
        transferMeter.excludedMs += pauseClock.elapsed();
        transferMeter.resetSample();
        transferMeter.lastReportMs = transferMeter.activeMs();
    }
    require(!stopping && !transferCancelled, "传输已取消");
}
bool Session::transportFailed() const {
    const int code = ssh ? libssh2_session_last_errno(ssh) : 0;
    return code == LIBSSH2_ERROR_SOCKET_SEND || code == LIBSSH2_ERROR_SOCKET_RECV ||
           code == LIBSSH2_ERROR_SOCKET_DISCONNECT || code == LIBSSH2_ERROR_TIMEOUT ||
           code == LIBSSH2_ERROR_SOCKET_TIMEOUT;
}
void Session::entryResult(const Job &job, const QString &path, const QString &state, const QString &reason) {
    if (state == "已完成")
        ++transferredFiles;
    else if (state == "已跳过")
        ++skippedFiles;
    else {
        ++failedFiles;
        if (transferErrors.size() < 20)
            transferErrors << path + ": " + reason;
    }
    if (transferEntryEvents++ < 200)
        emit transferEntry(job.id, path, state, reason);
}
void Session::transferChild(const Job &job, const QString &remote, const QString &local, int depth) {
    try {
        transfer(job, remote, local, depth);
    } catch (const QString &reason) {
        if (stopping || transferCancelled || transportFailed())
            throw;
        entryResult(job, remote, "失败", reason);
    }
}
void Session::transfer(const Job &j, const QString &remote, const QString &local, int depth) {
    require(depth < 64, "目录嵌套超过 64 层");
    checkTransfer();
    if(j.type=="upload"){upload(j,remote,local,depth);return;}
    const bool download = true;
    const auto remoteInfo = download ? stat(remote) : QVariantMap{};
    QFileInfo localInfo(local);
    if(download&&j.options.value("nativeDrop").toBool()){
        require(!remoteInfo["directory"].toBool()&&!remoteInfo["link"].toBool()&&LIBSSH2_SFTP_S_ISREG(remoteInfo["permissions"].toUInt()),"拖出源已不再是普通文件，请刷新。");
        require(remoteInfo["size"].toULongLong()==j.options.value("dragSize").toULongLong()&&remoteInfo["modified"].toLongLong()==j.options.value("dragModified").toLongLong(),"拖出文件已改变，请刷新后重新拖出。");
    }
    if (download) {
        require(!localInfo.isSymLink() && !localInfo.isJunction(), "本地目标是链接，已停止写入：" + local);
#ifdef Q_OS_WIN
        const auto name = localInfo.fileName();
        static const QRegularExpression reserved("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$",
                                                 QRegularExpression::CaseInsensitiveOption);
        require(!name.contains(QRegularExpression("[<>:\"|?*]")) && !name.endsWith('.') &&
                    !name.endsWith(' ') && !reserved.match(name).hasMatch(),
                "远端名称无法安全保存到 Windows：" + name);
#endif
    }
    require(download ? !remoteInfo["link"].toBool() : !localInfo.isSymLink(),
            "为避免越界与循环，未传输符号链接：" + remote);
    if (j.options.value("clipboardStage").toBool()) {
        require(remoteInfo["directory"].toBool() || LIBSSH2_SFTP_S_ISREG(remoteInfo["permissions"].toUInt()),
                "剪贴板来源不是普通文件或目录：" + remote);
        require(!localInfo.exists(), "剪贴板暂存名称发生碰撞：" + local);
    }
    if(remoteInfo["directory"].toBool()) {
        require(QDir().mkpath(local),"无法创建本地目录："+local);
        QSet<QString> stageNames;
        for(const auto &v:list(remote)) {
            const auto e=v.toMap();
            if (j.options.value("clipboardStage").toBool()) {
                const auto name=e["name"].toString().toCaseFolded();
                if (stageNames.contains(name)) { entryResult(j,e["path"].toString(),"失败","本地文件名碰撞"); continue; }
                stageNames.insert(name);
            }
            if(e["link"].toBool()){entryResult(j,e["path"].toString(),"已跳过","不递归跟随符号链接");continue;}
            transferChild(j,e["path"].toString(),QDir(local).filePath(e["name"].toString()),depth+1);
        }
        return;
    }
    QString target=local;
    if(QFileInfo::exists(target)) {
        if(j.collision=="skip"){entryResult(j,target,"已跳过","目标已存在");return;}
        if(j.collision=="keep")for(int n=1;QFileInfo::exists(target);++n){require(n<10000,"无法选择不重名的目标");target=QDir(localInfo.path()).filePath(numberedFileName(localInfo.fileName(),n));}
    }
    const quint64 total=remoteInfo["size"].toULongLong();quint64 done=0;
    emit transferProgress(j.id,remote,0,total);char chunk[65536];
        require(!QFileInfo(target).isSymLink(), "目标是符号链接，已停止写入");
        QStorageInfo storage(QFileInfo(target).absolutePath());
        if (storage.isValid() && storage.isReady() && storage.bytesAvailable() >= 0)
            require(quint64(storage.bytesAvailable()) >= total, "本地磁盘剩余空间不足：" + target);
        QSaveFile out(target);
        require(out.open(QIODevice::WriteOnly), out.errorString());
        Handle in{this,sshWait([&]{return libssh2_sftp_open(sftp, remote.toUtf8().constData(), LIBSSH2_FXF_READ, 0);})};
        require(in.p, sftpReason(sftp));
        for (;;) {
            checkTransfer();
            auto n = sshWait([&]{return libssh2_sftp_read(in.p, chunk, sizeof(chunk));});
            require(n >= 0, sftpReason(sftp));
            if (!n)
                break;
            transferMeter.bytes += quint64(n); // Successful SFTP read, even if local commit later fails.
            require(out.write(chunk, n) == n, out.errorString());
            done += quint64(n);
            if (transferMeter.activeMs() - transferMeter.lastReportMs >= 120)
                emit transferProgress(j.id, remote, done, total);
            reportTransfer();
        }
        if(j.options.value("nativeDrop").toBool() || j.options.value("clipboardStage").toBool()){
            const auto after=stat(remote);
            require(done==total&&after["size"]==remoteInfo["size"]&&after["modified"]==remoteInfo["modified"],"下载期间源文件发生变化，请刷新后重新拖出。");
        }
        require(out.commit(), out.errorString());
    emit transferProgress(j.id,target,done,total);entryResult(j,target,"已完成");
}
void Session::upload(const Job &j,const QString &remote,const QString &local,int depth) {
    require(remote.startsWith('/') && !remote.contains(QChar::Null),"上传缺少明确的绝对目标路径");
    const bool covered=j.options.value("journalCovered").toBool();
    const auto batch=j.options.value("operationBatch").toString();
    auto prepareUpload=[&](const QString &kind,const QString &source,const QString &target,const QVariantMap &attributes) {
        QString id,why;
        require(operationJournal && !batch.isEmpty(),"上传缺少操作记录批次");
        const bool ok=operationJournal->prepare(batch,kind,source,target,attributes,&id,&why,true);
        require(ok,why);
        return id;
    };
    auto finishUpload=[&](const QString &id,const QString &state,const QVariantMap &changes=QVariantMap{}) {
        QString why;const bool ok=operationJournal->transition(id,"prepared",state,changes,&why);
        require(ok,"上传结果待核查："+why);
    };
    QFileInfo source(local);
    require(source.exists() && !source.isSymLink() && !source.isJunction() && (source.isDir() || source.isFile()),"本地上传源不是普通文件或目录："+local);
    const auto parent=uploadStat(remoteJoin(remote,".."));
    require(parent["directory"].toBool() && !parent["link"].toBool(),"上传目标父目录不可用或是链接");
    QString target=remote;
    auto unique=[&](const QString &original) {
        for(int n=1;n<10000;++n){checkTransfer();const auto candidate=remoteJoin(remoteJoin(original,".."),numberedFileName(original.section('/',-1),n,source.isDir()));if(uploadStat(candidate).isEmpty())return candidate;}
        throw QString("无法选择不重名的目标");
    };
    auto existing=uploadStat(target);
    bool overwrite=false;
    auto resolve=[&]() {
        const bool regular=!existing["link"].toBool() && LIBSSH2_SFTP_S_ISREG(existing["permissions"].toUInt()) && source.isFile();
        const auto choice=uploadChoice(j,target,regular);
        if(choice=="skip"){entryResult(j,target,"已跳过","目标已存在");return false;}
        if(choice=="keep"){target=unique(remote);existing.clear();overwrite=false;}
        else overwrite=true;
        return true;
    };
    if(source.isDir()) {
        bool ready=false,created=false;
        for(int attempt=0;attempt<8;++attempt) {
            checkTransfer();
            if(!existing.isEmpty()) {
                if(existing["directory"].toBool() && !existing["link"].toBool() &&
                   !(depth == 0 && j.options.value("clipboardPaste").toBool())){ready=true;break;}
                if(!resolve())return;
            }
            QString effect;if(!covered)effect=prepareUpload("uploadNew",{},target,{{"directory",true}});
            if(sshWait([&]{return libssh2_sftp_mkdir(sftp,target.toUtf8().constData(),0755);})==0){
                if(!covered)finishUpload(effect,"committed",{{"attributes",uploadStat(target)}});
                ready=true;created=true;break;
            }
            if(!covered)finishUpload(effect,transportFailed()?"unknown":"failed");
            require(!transportFailed(),"创建目录结果待核查");
            existing=uploadStat(target);require(!existing.isEmpty(),"无法创建远端目录："+sftpReason(sftp));
        }
        require(ready,"上传目标反复变化，请刷新后重试");
        Job childJob=j;if(covered || created)childJob.options["journalCovered"]=true;
        QDirIterator it(local,QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System);
        while(it.hasNext()) {
            it.next();
            if(it.fileInfo().isSymLink() || it.fileInfo().isJunction()){entryResult(j,it.filePath(),"已跳过","不递归跟随符号链接");continue;}
            transferChild(childJob,remoteJoin(target,it.fileName()),it.filePath(),depth+1);
        }
        return;
    }
    if(!existing.isEmpty() && !resolve())return;
    const quint64 total=quint64(source.size());quint64 done=0;
    LIBSSH2_SFTP_STATVFS disk{};auto parentBytes=remoteJoin(target,"..").toUtf8();
    if(sshWait([&]{return libssh2_sftp_statvfs(sftp,parentBytes.constData(),size_t(parentBytes.size()),&disk);})==0 && disk.f_frsize)
        require(total/disk.f_frsize+(total%disk.f_frsize?1:0)<=disk.f_bavail,"远端磁盘剩余空间不足："+target);
    QFile in(local);require(in.open(QIODevice::ReadOnly),in.errorString());
    const QString tmp=target+".rabbitshell-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".part";
    try {
        {
            Handle out{this,sshWait([&]{return libssh2_sftp_open(sftp,tmp.toUtf8().constData(),LIBSSH2_FXF_WRITE|LIBSSH2_FXF_CREAT|LIBSSH2_FXF_EXCL,0600);})};
            require(out.p,sftpReason(sftp));char chunk[65536];emit transferProgress(j.id,target,0,total);
            for(;;) {
                checkTransfer();auto n=in.read(chunk,sizeof(chunk));require(n>=0,in.errorString());if(!n)break;
                qint64 offset=0;
                while(offset<n) {
                    checkTransfer();auto w=sshWait([&]{return libssh2_sftp_write(out.p,chunk+offset,size_t(n-offset));});require(w>0,sftpReason(sftp));
                    offset+=w;done+=quint64(w);transferMeter.bytes+=quint64(w);
                    if(transferMeter.activeMs()-transferMeter.lastReportMs>=120)emit transferProgress(j.id,target,done,total);
                    reportTransfer();
                }
            }
        }
        bool committed=false;
        for(int attempt=0;attempt<8;++attempt) {
            checkTransfer();
            if(overwrite) {
                existing=uploadStat(target);
                require(existing.isEmpty() || (!existing["link"].toBool() && LIBSSH2_SFTP_S_ISREG(existing["permissions"].toUInt())),"目标类型已变化，未替换："+target);
            }
            LIBSSH2_SFTP_ATTRIBUTES attrs{};attrs.flags=LIBSSH2_SFTP_ATTR_PERMISSIONS;
            attrs.permissions=existing.isEmpty()?0644:existing["permissions"].toUInt()&07777;
            if(!existing.isEmpty() && (existing["flags"].toULongLong() & LIBSSH2_SFTP_ATTR_UIDGID)) {
                attrs.flags|=LIBSSH2_SFTP_ATTR_UIDGID;attrs.uid=existing["uid"].toUInt();attrs.gid=existing["gid"].toUInt();
            }
            require(sshWait([&]{return libssh2_sftp_setstat(sftp,tmp.toUtf8().constData(),&attrs);})==0,"无法保留目标权限："+sftpReason(sftp));
            const auto from=tmp.toUtf8(),to=target.toUtf8();
            const bool replacing=overwrite && !existing.isEmpty();
            QString effect;if(!covered || replacing)effect=prepareUpload(replacing?"overwrite":"uploadNew",{},target,{});
            const auto rc=replacing?sshWait([&]{return libssh2_sftp_posix_rename(sftp,from.constData(),to.constData());}):
                sshWait([&]{return libssh2_sftp_rename_ex(sftp,from.constData(),unsigned(from.size()),to.constData(),unsigned(to.size()),0);});
            if(rc==0){if(!effect.isEmpty())finishUpload(effect,"committed",replacing?QVariantMap{}:QVariantMap{{"attributes",uploadStat(target)}});committed=true;break;}
            const auto commitReason=sftpReason(sftp);
            if(!effect.isEmpty())finishUpload(effect,transportFailed()?"unknown":"failed");
            require(!overwrite && !transportFailed(),"原子提交失败，原文件未主动删除："+commitReason);
            existing=uploadStat(target);
            require(!existing.isEmpty(),"原子提交失败，原文件未主动删除："+commitReason);
            if(!resolve()){sshWait([&]{return libssh2_sftp_unlink(sftp,tmp.toUtf8().constData());});return;}
        }
        require(committed,"上传目标反复变化，未提交，请刷新后重试");
    } catch(...) {sshWait([&]{return libssh2_sftp_unlink(sftp,tmp.toUtf8().constData());});throw;}
    emit transferProgress(j.id,target,done,total);entryResult(j,target,"已完成");
}
void Session::remove(const QString &path, bool recursive, int depth) {
    require(depth < 64 && !stopping, "删除已停止");
    require(QDir::cleanPath(path) != "/" && path != "." && !path.isEmpty(), "不允许删除此根路径");
    auto s = stat(path);
    if (s["directory"].toBool() && !s["link"].toBool()) {
        require(recursive, "目录删除需要递归确认");
        for (const auto &v : list(path))
            remove(v.toMap()["path"].toString(), true, depth + 1);
        require(sshWait([&]{return libssh2_sftp_rmdir(sftp, path.toUtf8().constData());}) == 0, sftpReason(sftp));
    } else
        require(sshWait([&]{return libssh2_sftp_unlink(sftp, path.toUtf8().constData());}) == 0, sftpReason(sftp));
}
void Session::perform(const Job &j) {
    const bool tracked=j.type=="upload" || j.type=="delete" || j.type=="list";
    const auto begin=j.type=="upload"?DiagEvent::UploadBegin:j.type=="delete"?DiagEvent::DeleteBegin:DiagEvent::ListBegin;
    const auto end=j.type=="upload"?DiagEvent::UploadDone:j.type=="delete"?DiagEvent::DeleteDone:DiagEvent::ListDone;
    const auto failed=j.type=="upload"?DiagEvent::UploadFailed:j.type=="delete"?DiagEvent::DeleteFailed:DiagEvent::ListFailed;
    bool success=false;
    if(tracked)diagnose(begin,diagnosticTag,j.id,j.path);
    const auto guard=qScopeGuard([&]{if(tracked&&!success)diagnose(failed,diagnosticTag,j.id,j.path);});
    QVariantMap result;
    const auto operationBatch=j.options.value("operationBatch").toString().isEmpty() ?
        FileOperationJournal::newId() : j.options.value("operationBatch").toString();
    // Serialize mutations across both sessions/processes. In particular, no new
    // batch may enter between effects of one undo. QLockFile releases on unwind;
    // session history is held only in memory and does not survive a crash.
    std::unique_ptr<QLockFile> mutationGuard;
    if(QStringList{"journalUndo","upload","delete","mkdir","touch","rename","move","save","chmod"}.contains(j.type)) {
        require(operationJournal && operationJournal->valid(),"服务器身份未认证，无法执行文件操作");
        QString why;mutationGuard=operationJournal->acquireMutationGuard(&why);require(bool(mutationGuard),why);
    }
    auto prepare=[&](const QString &kind,const QString &source,const QString &target,const QVariantMap &attributes) {
        require(operationJournal && operationJournal->valid(),"服务器身份未认证，无法记录文件操作");
        QString effect,reason;
        const bool prepared=operationJournal->prepare(operationBatch,kind,source,target,attributes,&effect,&reason,
                                                      j.options.value("operationBatchOpen").toBool());
        require(prepared,reason);
        return effect;
    };
    auto finish=[&](const QString &effect,const QString &state,const QVariantMap &changes={}) {
        QString reason;
        const bool saved=operationJournal->transition(effect,"prepared",state,changes,&reason);
        require(saved,"远端操作结果待核查："+reason);
    };
    if (j.type == "journalSeal") {
        require(operationJournal && operationJournal->valid(),"服务器身份尚未认证");
        QString why;const bool sealed=operationJournal->sealBatch(j.options.value("operationBatch").toString(),&why);
        require(sealed,why);result["operationBatch"]=j.options.value("operationBatch");
    } else if (j.type == "journalSnapshot") {
        require(operationJournal && operationJournal->valid(),"服务器身份尚未认证");
        QJsonObject snapshot;QString why;const bool read=operationJournal->snapshot(&snapshot,&why);require(read,why);
        result["journal"]=snapshot.toVariantMap();
    } else if (j.type == "journalUndo") {
        require(operationJournal && operationJournal->valid(),"服务器身份尚未认证");
        QString batchId=j.options.value("operationBatch").toString(); QVariantList restored;
        for(int count=0;count<4096;++count) {
            QVariantMap effect;QString why;
            const bool begun=operationJournal->beginUndo(batchId,&effect,&why);
            if(!begun) {
                if(!restored.isEmpty() && why=="只能先撤销最近的操作批次")break;
                if(!restored.isEmpty() && why=="没有可撤销的文件操作")break;
                throw why;
            }
            batchId=effect.value("batchId").toString();const auto id=effect.value("id").toString();
            const auto from=effect.value("undoSource").toString(),to=effect.value("undoTarget").toString();
            const auto saved=effect.value("attributes").toMap();
            auto mark=[&](const QString &state,const QVariantMap &changes={}) {
                QString error;const bool ok=operationJournal->transition(id,"undo_prepared",state,changes,&error);
                require(ok,"撤销结果待核查："+error);
            };
            try {
                const auto current=uploadStat(from);
                require(!current.isEmpty(),"撤销来源不存在，请刷新核查");
                const auto oldMode=saved.value("permissions").toUInt(),newMode=current.value("permissions").toUInt();
                require((oldMode&0170000)==(newMode&0170000),"撤销来源类型已变化");
                const auto kind=effect.value("kind").toString();
                if((kind=="create" || kind=="uploadNew") && !current.value("directory").toBool()) {
                    require(!current.value("link").toBool() && LIBSSH2_SFTP_S_ISREG(newMode),"仅能撤销新建普通文件或空目录");
                    const auto flags=saved.value("flags").toULongLong(), nowFlags=current.value("flags").toULongLong();
                    require((flags & LIBSSH2_SFTP_ATTR_SIZE) && (flags & LIBSSH2_SFTP_ATTR_ACMODTIME) &&
                            (nowFlags & LIBSSH2_SFTP_ATTR_SIZE) && (nowFlags & LIBSSH2_SFTP_ATTR_ACMODTIME) &&
                            saved.value("size")==current.value("size") && saved.value("modified")==current.value("modified"),
                            "文件已变化或属性不完整，不能撤销；请检查后正常删除");
                }
                if(!to.isEmpty()) {
                    require(uploadStat(to).isEmpty(),"原位置已存在，无法覆盖，请刷新核查");
                    const auto parent=uploadStat(remoteJoin(to,".."));
                    require(parent.value("directory").toBool() && !parent.value("link").toBool(),"原父目录不可用或是链接");
                }
            } catch(const QString &) {mark("committed");throw;}
            const auto src=from.toUtf8(),dst=to.toUtf8();const auto kind=effect.value("kind").toString();
            const int rc=(kind=="create" || kind=="uploadNew")
                ? (saved.value("directory").toBool()?sshWait([&]{return libssh2_sftp_rmdir(sftp,src.constData());}):sshWait([&]{return libssh2_sftp_unlink(sftp,src.constData());}))
                : sshWait([&]{return libssh2_sftp_rename_ex(sftp,src.constData(),unsigned(src.size()),dst.constData(),unsigned(dst.size()),0);});
            if(rc!=0) {const auto reason=sftpReason(sftp);mark(transportFailed()?"undo_unknown":"committed");
                if((kind=="create" || kind=="uploadNew") && saved.value("directory").toBool())throw QString("目录非空或无法删除，未递归删除：")+reason;
                throw QString("撤销未完成：")+reason;}
            const QVariantMap applied{{"old",from},{"path",to},{"kind",effect.value("kind")}};
            emit journalUndoApplied(j.id,applied);
            mark("undone");restored<<applied;
        }
        result={{"operationBatch",batchId},{"effects",restored}};
    } else if (j.type == "service") {
        result = services::run(j.path, j.options, [this](const services::Command &c) { return serviceCommand(c); });
    } else if (j.type == "open-terminal") {
        traceStage(ConnectionStage::ChannelOpen);
        auto ch = sshWait([&]{return libssh2_channel_open_session(ssh);});
        require(ch, error());
        traceStage(ConnectionStage::Exec);
        if (sshWait([&]{return libssh2_channel_request_pty_ex(ch, "xterm-256color", 14, nullptr, 0, j.columns, j.rows, 0, 0);}) !=
                0 ||
            sshWait([&]{return libssh2_channel_shell(ch);}) != 0) {
            sshWait([&]{return libssh2_channel_free(ch);});
            throw error();
        }
        channels[j.id] = ch;
        if (!j.path.isEmpty() && j.path != ".")
            pendingWrites[j.id] = "cd -- " + shellQuote(j.path).toUtf8() + " || printf '%s\\n' " + shellQuote("[启动目录不可用，未切换目录；shell 停留在初始目录。]").toUtf8()+"\r";
    } else if (j.type == "input") {
        if (channels.contains(j.id)) {
            require(pendingWrites[j.id].size() + j.data.size() <= 256 * 1024, "终端输入队列已满");
            pendingWrites[j.id] += j.data;
        }
        return;
    } else if (j.type == "resize") {
        if(channels.contains(j.id))pendingSizes[j.id]=QSize(j.columns,j.rows);
        return;
    } else if (j.type == "close-terminal") {
        if(channels.contains(j.id))closingTerminals.insert(j.id);
        return;
    } else if (j.type == "list") {
        char resolved[4096];
        auto p = j.path.toUtf8();
        auto n = sshWait([&]{return libssh2_sftp_realpath(sftp, p.constData(), resolved, sizeof(resolved));});
        require(n > 0, sftpReason(sftp));
        QString path = QString::fromUtf8(resolved, n);
        result = {{"path", path}, {"entries", list(path)}};
    } else if (j.type == "read") {
        auto data = readFile(j.path, editorlimits::sizeBytes());
        require(!data.contains('\0'), "二进制文件不能作为文本编辑，请下载后打开");
        result = stat(j.path);
        result["path"] = j.path;
        result["data"] = data;
        result["hash"] = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    } else if (j.type == "save") {
        if (!j.overwrite) {
            auto current = readFile(j.path, 512 * editorlimits::MiB, true);
            require(current == j.expectedHash,
                    "CONFLICT:远端文件已变化，草稿已保留");
        }
        auto attrs = stat(j.path);
        require(!attrs["link"].toBool(), "符号链接不能直接替换，请打开链接的实际目标进行编辑");
        require((attrs["permissions"].toUInt() & 0222) != 0, "文件没有写权限，草稿已保留");
        QString tmp = j.path + ".rabbitshell-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".part";
        try {
            {
                Handle h{this,sshWait([&]{return libssh2_sftp_open(sftp, tmp.toUtf8().constData(),
                                           LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL, 0600);})};
                require(h.p, sftpReason(sftp));
                qsizetype pos = 0;
                while (pos < j.data.size()) {
                    require(!stopping, "保存已取消");
                    auto n = sshWait([&]{return libssh2_sftp_write(h.p, j.data.constData() + pos,
                                                size_t(qMin<qsizetype>(65536, j.data.size() - pos)));});
                    require(n > 0, sftpReason(sftp));
                    pos += n;
                }
            }
            LIBSSH2_SFTP_ATTRIBUTES a{};
            a.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS | LIBSSH2_SFTP_ATTR_UIDGID;
            a.permissions = attrs["permissions"].toUInt() & 07777;
            a.uid = attrs["uid"].toUInt();
            a.gid = attrs["gid"].toUInt();
            if (sshWait([&]{return libssh2_sftp_setstat(sftp, tmp.toUtf8().constData(), &a);}) != 0)
                throw QString("无法保留原文件权限和所有者：") + sftpReason(sftp);
            if (!j.overwrite)
                require(readFile(j.path, 512 * editorlimits::MiB, true) ==
                            j.expectedHash,
                        "CONFLICT:写入期间远端文件再次变化，草稿已保留");
            if (sshWait([&]{return libssh2_sftp_posix_rename(sftp, tmp.toUtf8().constData(), j.path.toUtf8().constData());}) != 0)
                throw QString("服务器无法完成原子替换，原文件未主动删除：") + sftpReason(sftp);
        } catch (...) {
            sshWait([&]{return libssh2_sftp_unlink(sftp, tmp.toUtf8().constData());});
            throw;
        }
        result = {{"path", j.path},
                  {"hash", QCryptographicHash::hash(j.data, QCryptographicHash::Sha256)},
                  {"data", j.data}};
    } else if (j.type == "mkdir") {
        const auto effect=prepare("create",{},j.path,{{"directory",true}});
        const auto rc=sshWait([&]{return libssh2_sftp_mkdir(sftp,j.path.toUtf8().constData(),0755);});
        if(rc!=0){const auto why=sftpReason(sftp);finish(effect,transportFailed()?"unknown":"failed");throw why;}
        const auto attributes=uploadStat(j.path);finish(effect,"committed",{{"attributes",attributes}});
        result={{"path",j.path},{"operationBatch",operationBatch}};
    } else if (j.type == "touch") {
        const auto effect=prepare("create",{},j.path,{{"directory",false}});
        Handle h{this,sshWait([&]{return libssh2_sftp_open(sftp, j.path.toUtf8().constData(),
                                   LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL, 0644);})};
        if(!h.p){const auto why=sftpReason(sftp);finish(effect,transportFailed()?"unknown":"failed");throw why;}
        sshWait([&]{return libssh2_sftp_close_handle(h.p);});h.p=nullptr;
        const auto attributes=uploadStat(j.path);finish(effect,"committed",{{"attributes",attributes}});
        result={{"path",j.path},{"operationBatch",operationBatch}};
    } else if (j.type == "rename") {
        const auto attrs=uploadStat(j.path);require(!attrs.isEmpty(),"重命名来源不存在");
        require(uploadStat(j.destination).isEmpty(),"目标已存在");
        const auto effect=prepare("rename",j.path,j.destination,attrs);
        const auto from=j.path.toUtf8(),to=j.destination.toUtf8();
        const auto rc=sshWait([&]{return libssh2_sftp_rename_ex(sftp,from.constData(),unsigned(from.size()),to.constData(),unsigned(to.size()),0);});
        if(rc!=0){const auto why=sftpReason(sftp);finish(effect,transportFailed()?"unknown":"failed");throw why;}
        finish(effect,"committed");
        result = {{"old", j.path}, {"path", j.destination},{"operationBatch",operationBatch}};
    } else if (j.type == "move") {
        transferMeter.begin(j.id);
        const auto source = QDir::cleanPath(j.path), requested = QDir::cleanPath(j.destination);
        require(source.startsWith('/') && source != "/" && requested.startsWith('/') && requested != "/" &&
                    source != requested && !requested.startsWith(source + '/'), "无效的移动目标");
        checkTransfer();
        const auto sourceInfo = uploadStat(source);
        require(!sourceInfo.isEmpty() && !sourceInfo["link"].toBool() &&
                    (sourceInfo["directory"].toBool() || LIBSSH2_SFTP_S_ISREG(sourceInfo["permissions"].toUInt())),
                "移动来源不可用或不是普通文件/目录");
        const auto parent = uploadStat(remoteJoin(requested,".."));
        require(parent["directory"].toBool() && !parent["link"].toBool(), "移动目标目录不可用或是链接");
        QString target=requested;
        for (int attempt=0;attempt<8;++attempt) {
            checkTransfer(); auto existing=uploadStat(target);
            if (!existing.isEmpty()) {
                const auto choice=uploadChoice(j,target,false);
                if (choice=="skip") throw QString("移动已取消，来源保留");
                if (choice=="keep") {
                    bool found=false;
                    for (int n=1;n<10000;++n) {
                        checkTransfer(); const auto candidate=remoteJoin(remoteJoin(requested,".."),numberedFileName(requested.section('/',-1),n,sourceInfo["directory"].toBool()));
                        if (uploadStat(candidate).isEmpty()) {target=candidate;found=true;break;}
                    }
                    require(found,"无法选择不重名的移动目标");
                } else throw QString("移动不允许替换目标");
            }
            checkTransfer();
            const auto currentSource=uploadStat(source);
            require(!currentSource.isEmpty() && !currentSource["link"].toBool() &&
                        currentSource["permissions"].toUInt()==sourceInfo["permissions"].toUInt(),
                    "移动来源类型已变化，未移动");
            const auto from=source.toUtf8(), to=target.toUtf8();
            const auto effect=prepare("move",source,target,sourceInfo);
            const auto rc=sshWait([&]{return libssh2_sftp_rename_ex(sftp,from.constData(),unsigned(from.size()),to.constData(),unsigned(to.size()),0);});
            if (rc==0) { finish(effect,"committed");result={{"old",source},{"path",target},{"fingerprint",sourceInfo},{"operationBatch",operationBatch}}; break; }
            const auto reason=sftpReason(sftp);
            finish(effect,transportFailed()?"unknown":"failed");
            require(!transportFailed(),"移动失败，未主动删除来源："+reason);
            require(!uploadStat(target).isEmpty(),"移动失败，未主动删除来源："+reason);
        }
        require(result.contains("path"),"移动目标反复变化，来源保留");
    } else if (j.type == "delete") {
        require(QDir::cleanPath(j.path)==j.path && j.path.startsWith('/') && j.path!="/", "不允许删除此路径");
        const auto attrs=uploadStat(j.path);require(!attrs.isEmpty(),"删除来源不存在");
        require(!attrs["directory"].toBool() || j.recursive,"目录删除需要递归确认");
        const auto effect=prepare("delete",j.path,{},attrs);
        try {remove(j.path,j.recursive);}catch(const QString &){finish(effect,"unknown");throw;}
        finish(effect,"committed");result={{"path",j.path},{"operationBatch",operationBatch}};
    } else if (j.type == "stat") {
        result = stat(j.path);
        result["path"] = j.path;
    } else if (j.type == "chmod") {
        LIBSSH2_SFTP_ATTRIBUTES a{};
        a.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
        a.permissions = j.permissions;
        require(sshWait([&]{return libssh2_sftp_setstat(sftp, j.path.toUtf8().constData(), &a);}) == 0, sftpReason(sftp));
    } else if (j.type == "upload" || j.type == "download") {
        transferredFiles = skippedFiles = failedFiles = transferEntryEvents = 0;
        transferErrors.clear();
        transferMeter.begin(j.id);
        Job transferJob=j;
        if(j.type=="upload")transferJob.options["operationBatch"]=operationBatch;
        const bool sealHere=j.type=="upload" && !j.options.value("operationBatchOpen").toBool();
        try {
            transfer(transferJob, j.path, j.destination);
            checkTransfer();
        } catch (const QString &reason) {
            reportTransfer(true); // Ordered before failed/disconnected; never a success result.
            if(sealHere && !transportFailed()) {
                QString why;if(!operationJournal->sealBatch(operationBatch,&why))throw reason+"；操作记录待核查："+why;
            }
            throw;
        }
        if(sealHere) {QString why;require(operationJournal->sealBatch(operationBatch,&why),"上传结果待核查："+why);}
        reportTransfer(true);
        result["payloadBytes"] = QVariant::fromValue(transferMeter.bytes);
        result["activeMs"] = transferMeter.activeMs();
        result["path"] = j.path;
        result["completed"] = transferredFiles;
        result["skipped"] = skippedFiles;
        result["failed"] = failedFiles;
        result["errors"] = transferErrors;
    } else if (j.type == "server-clock") {
        const auto token=j.options.value("lockEpoch").toULongLong();
        const auto permit=j.options.value("intent").value<serverclock::IntentPermit>();
        result=serverclock::run(j.path,j.options,[this,token](const services::Command &command){
                                    if(!OperationGate::accepts(token)){services::Result cancelled;cancelled.unknown=true;return cancelled;}
                                    return serviceCommand(command);
                                },
                                [token,permit]{return permit&&permit->load(std::memory_order_acquire)&&OperationGate::accepts(token);});
    } else if (j.type == "listening-ports") {
        const auto r=serviceCommand(listening::command());
        result["out"]=r.out;result["err"]=r.err;result["code"]=r.code;result["unknown"]=r.unknown;
    } else if (j.type == "monitor") {
        QElapsedTimer timer;
        timer.start();
        const QByteArray command="LC_ALL=C; printf '__OS__\\n'; cat /etc/os-release 2>/dev/null; printf '__ARCH__\\n'; uname -m 2>/dev/null; printf '__CPU__\\n'; head -n 1 /proc/stat; printf '__MEM__\\n'; cat "
                    "/proc/meminfo; printf '__UP__\\n'; cat /proc/uptime /proc/loadavg; printf '__NET__\\n'; "
                    "cat /proc/net/dev; printf '__DISK__\\n'; df -Pk /; printf '__PROC__\\n'; ps -eo "
                    "pid,user,pcpu,rss,stat,comm --sort=-pcpu | head -n 201";
        if(mode==Mode::Commands){services::Command c;c.text=command;c.timeoutMs=8000;c.outputLimit=4*1024*1024;const auto r=serviceCommand(c);result["data"]=r.out;result["unknown"]=r.unknown||r.code!=0;}
        else result["data"]=execute(command);
        result["latency"] = timer.elapsed();
    } else
        throw QString("未知任务：") + j.type;
    success=true;
    if(tracked)diagnose(end,diagnosticTag,j.id,j.path,j.type=="list"?result["entries"].toList().size():result["failed"].toInt());
    emit completed(j.type, j.id, result);
}
} // namespace rs
