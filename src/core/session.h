#pragma once
#include "profile.h"
#include "services.h"
#include "diagnostics.h"
#include <QThread>
#include <atomic>
#include <deque>
#include <memory>
#include <type_traits>
#include <libssh2.h>
#include <libssh2_sftp.h>

namespace rs {
class FileOperationJournal;
struct Job {
    QString type, path, destination;
    QByteArray data, expectedHash;
    int id = 0, columns = 80, rows = 24;
    quint32 permissions = 0644;
    bool overwrite = false, recursive = false;
    QString collision = "keep";
    QVariantMap options;
};
class Session final : public QThread {
    Q_OBJECT
  public:
    enum class Mode { Files, Commands };
    explicit Session(Profile profile, QByteArray secret, QObject *parent = nullptr, Mode mode = Mode::Files);
    ~Session() override;
    void submit(const Job &job);
    void stop();
    void trust(bool accepted);
    void acknowledge(qint64 bytes) {
        queuedOutput.fetch_sub(bytes);
    }
    void pauseTransfer(bool pause) {
        paused = pause;
    }
    void cancelTransfer();
    bool answerUploadConflict(int jobId, quint64 nonce, const QString &choice, bool applyToBatch = false);
    bool hasUploadConflict(int jobId, quint64 nonce);
    const Profile profile;
    const quint64 diagnosticTag = QRandomGenerator::global()->generate64();
  signals:
    void phase(const QString &message);
    void hostKey(const QString &fingerprint, bool changed);
    void connected();
    void authenticated();
    void authenticatedHostIdentity(const QString &fingerprint);
    // Only a server password rejection, never a transport/key-file/handshake failure.
    void passwordRejected();
    void keyAuthenticationNeedsInput();
    void disconnected(const QString &reason);
    void completed(const QString &type, int id, const QVariantMap &result);
    void failed(const QString &type, int id, const QString &reason);
    // Emitted immediately after each inverse rename, even if a later effect fails.
    void journalUndoApplied(int jobId, const QVariantMap &effect);
    void terminalOutput(int id, const QByteArray &data);
    void terminalClosed(int id);
    void responseWaiting(bool waiting);
    void transferProgress(int id, const QString &path, quint64 current, quint64 total);
    // Per-attempt payload and active task time (enumeration + commit included, queue/pause excluded).
    void transferStatistics(int id, quint64 bytes, qint64 activeMs, double bytesPerSecond, bool final);
    void transferEntry(int id, const QString &path, const QString &state, const QString &reason);
    void uploadConflict(int jobId, quint64 nonce, const QString &path, bool canOverwrite);
    void uploadConflictClosed(int jobId, quint64 nonce);

  protected:
    void run() override;

  private:
    friend class WorkspaceTests;
    friend class FileTransferTests;
    friend class BookmarkTests;
    friend class AppLockTests;
    friend class MenuTests;
    quint64 startEpoch=0;
    QByteArray secret;
    Mode mode;
    QMutex mutex;
    QWaitCondition wake;
    std::deque<Job> jobs;
    qsizetype queuedJobBytes = 0;
    std::atomic_bool stopping = false, paused = false, transferCancelled = false,stopRequested=false;
    std::atomic<qint64> queuedOutput = 0;
    int trustAnswer = 0;
    quint64 conflictNonce = 0;
    int conflictJob = 0;
    bool conflictWaiting = false, conflictBatch = false;
    QString conflictAnswer, policyBatch, batchPolicy;
    LIBSSH2_SESSION *ssh = nullptr;
    LIBSSH2_SFTP *sftp = nullptr;
    std::unique_ptr<FileOperationJournal> operationJournal;
    qintptr descriptor = -1;
    QHash<int, LIBSSH2_CHANNEL *> channels;
    QHash<int, QByteArray> pendingWrites;
    QHash<int,QByteArray> writingChunks;
    QSet<int> closingTerminals;
    QSet<int> freeingTerminals;
    QHash<int,QSize> pendingSizes,writingSizes;
    bool established=false,waitingNotice=false,pumping=false;
    ConnectionStage connectionStage=ConnectionStage::TcpConnect;
    QElapsedTimer stageClock;
    QElapsedTimer keepaliveClock;
    int stageTimeout=0;
    void traceStage(ConnectionStage,int timeout=0);
    void traceResult(DiagEvent,bool failed=false);
    void waitSocket();
    void pumpTerminals(bool waiting=false);
    void drainTerminalInput();
    void closeHandle(LIBSSH2_SFTP_HANDLE *) noexcept;
    struct Handle {Session *owner;LIBSSH2_SFTP_HANDLE *p=nullptr;~Handle(){if(p)owner->closeHandle(p);}};
    template<class F> auto sshWait(F action)->decltype(action()) {
        QElapsedTimer elapsed;elapsed.start();bool warned=false;
        for(;;){
            if(stopping)throw QString("操作已停止");
            auto value=action();
            const int code=[&]{if constexpr(std::is_pointer_v<decltype(value)>)return value?0:libssh2_session_last_errno(ssh);else return int(value);}();
            if(code!=LIBSSH2_ERROR_EAGAIN){if(warned){waitingNotice=false;traceResult(DiagEvent::ConnectionResult,code<0);emit responseWaiting(false);}return value;}
            if(!established&&elapsed.elapsed()>profile.timeout*1000)throw QString("建立连接等待超时");
            if(established&&elapsed.elapsed()>=5000&&!warned){warned=true;waitingNotice=true;traceResult(DiagEvent::ConnectionWait);emit responseWaiting(true);}
            if(established&&!pumping){drainTerminalInput();pumpTerminals(true);}
            waitSocket();
        }
    }
    struct TransferMeter {
        QElapsedTimer clock;
        quint64 bytes = 0, sampledBytes = 0;
        qint64 excludedMs = 0, sampledMs = 0, lastReportMs = 0;
        int id = 0;
        void begin(int taskId);
        qint64 activeMs() const;
        void resetSample();
    } transferMeter;
    void reportTransfer(bool final = false);
    int transferredFiles = 0, skippedFiles = 0, failedFiles = 0, transferEntryEvents = 0;
    QStringList transferErrors;
    bool initialize();
    void perform(const Job &job);
    QString error() const;
    QByteArray readFile(const QString &path, quint64 max, bool hashOnly = false);
    QVariantMap stat(const QString &path);
    QVariantMap uploadStat(const QString &path);
    QString uploadChoice(const Job &job, const QString &path, bool canOverwrite);
    QVariantList list(const QString &path);
    QByteArray execute(const QByteArray &command);
    services::Result serviceCommand(const services::Command &command);
    void transfer(const Job &job, const QString &remote, const QString &local, int depth = 0);
    void upload(const Job &job, const QString &remote, const QString &local, int depth);
    void transferChild(const Job &job, const QString &remote, const QString &local, int depth);
    void entryResult(const Job &job, const QString &path, const QString &state, const QString &reason = {});
    bool transportFailed() const;
    void remove(const QString &path, bool recursive, int depth = 0);
    void checkTransfer();
};
} // namespace rs
