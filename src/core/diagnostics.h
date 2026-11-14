#pragma once
#include <QtCore>

namespace rs {
// Only these structured events reach disk. Never pass SSH errors, output or profile data.
enum class DiagEvent {
    Started, Stopped, Heartbeat, UiStall, Overflow,
    UploadSubmit, UploadBegin, UploadDone, UploadFailed, UploadCancel,
    SessionStop, DeleteConfirm, DeleteAccepted, DeleteCancelled, DeleteSubmit,
    DeleteBegin, DeleteDone, DeleteFailed, DeleteUiBegin, DeleteUiFailedBegin, DeleteUiEnd,
    ListRequest, ListSubmit, ListBegin, ListDone, ListFailed, ListResult, ListDiscard,
    RenderBegin, RenderSort, RenderFilter, RenderLayout, RenderEnd, RemoveBegin, RemoveEnd, RefreshUpload,
    MonitorUiBegin, MonitorUiEnd, ProcessSnapshotBegin, ProcessSnapshotEnd,
    TerminalFeedBegin, TerminalFeedEnd, TerminalResizeBegin, TerminalResizeEnd,
    TerminalScreenState, TerminalClear, TerminalGrid,
    FilePressBegin, FilePressEnd, FileHitBegin, FileHitEnd, FileBasePressBegin, FileBasePressEnd,
    FileReleaseBegin, FileReleaseEnd, FileExpandedBegin, FileExpandedEnd, LockStartup, MainWindowStartup,
    ConnectionWait, ConnectionResult, ConnectionExit,
    FileActivate, DownloadRequested, DownloadQueued, TransferStart,
    UpdateRequest, UpdateResponse, UpdateRetry, UpdateDone, UpdateFailed
};
enum class ConnectionStage { TcpConnect, Handshake, Authentication, Sftp, ChannelOpen, Exec, MonitorRead, ChannelClose, PtyRead, PtyWrite, Keepalive, Stop };
struct ConnectionDiagnosticState {
    ConnectionStage stage=ConnectionStage::TcpConnect;
    int mode=0,sshError=0,timeoutMs=0;
    qint64 elapsedMs=0;
    bool failed=false,voluntary=false;
};
struct DiagnosticDispatchState {
    int event = 0, depth = 0;
    const char *receiver = "none", *owner = "none";
    qint64 started = 0;
};
struct TerminalDiagnosticState {
    quint64 terminal = 0;
    int columns = 0, rows = 0, cursorRow = 0, cursorColumn = 0;
    int historyRows = 0, scroll = 0, scrollMaximum = 0;
    bool alternate = false;
};
class DiagnosticDispatch;
class DiagnosticLog final : public QThread {
  public:
    // Construction/start is explicit on the UI thread after --data-dir is applied.
    static DiagnosticLog &instance();
    static QString directory();
    bool startLogging(QString *error = nullptr);
    static bool validateDirectory(const QString &path, QString *error = nullptr);
    void stopLogging();
    void record(DiagEvent event, quint64 session = 0, int job = 0,
                const QString &remotePath = {}, qint64 count = 0,
                const TerminalDiagnosticState *terminal = nullptr,const ConnectionDiagnosticState *connection = nullptr);
    ~DiagnosticLog() override;
  protected:
    void run() override;
  private:
    friend class DiagnosticTests;
    friend class DiagnosticDispatch;
    DiagnosticLog();
    struct Context { DiagEvent event = DiagEvent::Started; quint64 session = 0; int job = 0; QByteArray path; };
    QByteArray row(DiagEvent, const Context &, qint64 count, bool ui, qint64 age = 0,
                   const DiagnosticDispatchState *dispatch = nullptr,
                   const TerminalDiagnosticState *terminal = nullptr,const ConnectionDiagnosticState *connection = nullptr) const;
    QMutex mutex;
    QWaitCondition wake;
    QList<QByteArray> queue;
    qint64 queuedBytes = 0, dropped = 0, lastPulse = 0;
    Context lastUi;
    DiagnosticDispatchState currentDispatch;
    QElapsedTimer clock;
    QTimer pulse;
    QString folder;
    bool accepting = false, stopping = false;
    static constexpr int MaxRecords = 512, MaxQueueBytes = 256 * 1024;
    qint64 maxFileBytes = 2 * 1024 * 1024;
};
void diagnose(DiagEvent event, quint64 session = 0, int job = 0,
              const QString &remotePath = {}, qint64 count = 0);
void diagnoseTerminal(DiagEvent, const TerminalDiagnosticState &);
void diagnoseConnection(DiagEvent,quint64,const ConnectionDiagnosticState &);
// Stack-local nesting; only fixed metadata is copied under the queue mutex.
// No per-event record/encoding, and no receiver pointer escapes the notify scope.
class DiagnosticDispatch final {
  public:
    DiagnosticDispatch(QObject *receiver, QEvent *event);
    ~DiagnosticDispatch();
  private:
    DiagnosticLog *log = nullptr;
    DiagnosticDispatchState previous;
};
class DiagnosticScope {
  public:
    DiagnosticScope(DiagEvent begin, DiagEvent end, quint64 tag, int job,
                    QString path, qint64 count = 0) : end(end), tag(tag), job(job), path(std::move(path)), count(count) {
        diagnose(begin, tag, job, this->path, count);
    }
    ~DiagnosticScope() { diagnose(end, tag, job, path, count); }
  private:
    DiagEvent end; quint64 tag; int job; QString path; qint64 count;
};
} // namespace rs
