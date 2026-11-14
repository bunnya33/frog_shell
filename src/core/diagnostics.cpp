#include "diagnostics.h"
#include "profile.h"
#include <algorithm>
#include <atomic>
#include <utility>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <exception>
#include <cstdlib>
#endif

namespace rs {
namespace {
std::atomic<DiagnosticLog *> activeLog = nullptr;
#ifdef Q_OS_WIN
// One pre-opened, exclusively owned slot per process. Rotation and directory
// creation happen only on startup; the fatal path never enters Qt or allocates.
HANDLE crashFile = INVALID_HANDLE_VALUE;
std::atomic_flag crashWriting = ATOMIC_FLAG_INIT;
bool crashRecorded = false; // Protected by crashWriting.
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
std::terminate_handler previousTerminate = nullptr;

struct CrashLine {
    char bytes[512]; unsigned size = 0;
    void ch(char c) noexcept { if (size < sizeof(bytes)) bytes[size++] = c; }
    void text(const char *s) noexcept { while (*s) ch(*s++); }
    void decimal(unsigned long value, unsigned width = 1) noexcept {
        char digits[16]; unsigned n = 0;
        do { digits[n++] = char('0' + value % 10); value /= 10; } while (value && n < sizeof(digits));
        while (n < width && n < sizeof(digits)) digits[n++] = '0';
        while (n) ch(digits[--n]);
    }
    void hex(unsigned long long value) noexcept {
        char digits[16]; unsigned n = 0;
        do { digits[n++] = "0123456789abcdef"[value & 15]; value >>= 4; } while (value && n < sizeof(digits));
        while (n) ch(digits[--n]);
    }
};
void writeCrash(const char *kind, DWORD code, const void *address) noexcept {
    if (crashWriting.test_and_set(std::memory_order_acquire)) return;
    if (crashFile != INVALID_HANDLE_VALUE && !crashRecorded) {
        crashRecorded = true;
        CrashLine line; SYSTEMTIME time{}; GetSystemTime(&time);
        line.text("{\"v\":\"" RABBITSHELL_DIAGNOSTIC_VERSION "\",\"event\":\""); line.text(kind);
        line.text("\",\"utc\":\""); line.decimal(time.wYear,4); line.ch('-'); line.decimal(time.wMonth,2); line.ch('-');
        line.decimal(time.wDay,2); line.ch('T'); line.decimal(time.wHour,2); line.ch(':'); line.decimal(time.wMinute,2);
        line.ch(':'); line.decimal(time.wSecond,2); line.ch('.'); line.decimal(time.wMilliseconds,3);
        line.text("Z\",\"pid\":"); line.decimal(GetCurrentProcessId()); line.text(",\"tid\":"); line.decimal(GetCurrentThreadId());
        if (code) {
            line.text(",\"code\":\"0x"); line.hex(code); line.ch('"');
        }
        if (address) {
            MEMORY_BASIC_INFORMATION memory{};
            if (VirtualQuery(address, &memory, sizeof(memory)) && memory.Type == MEM_IMAGE) {
                wchar_t path[MAX_PATH]{};
                const DWORD length = GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase), path, MAX_PATH);
                if (length && length < MAX_PATH) {
                    const wchar_t *base = path;
                    for (DWORD i = 0; i < length; ++i) if (path[i] == L'\\' || path[i] == L'/') base = path + i + 1;
                    line.text(",\"module\":\"");
                    for (unsigned i = 0; base[i] && i < 100; ++i) {
                        const wchar_t c = base[i];
                        line.ch((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                                (c >= L'0' && c <= L'9') || c == L'.' || c == L'_' || c == L'-' ? char(c) : '_');
                    }
                    line.text("\",\"offset\":\"0x");
                    line.hex(reinterpret_cast<quintptr>(address) - reinterpret_cast<quintptr>(memory.AllocationBase)); line.ch('"');
                }
            }
        }
        line.text("}\n"); DWORD written = 0;
        WriteFile(crashFile, line.bytes, line.size, &written, nullptr);
    }
    crashWriting.clear(std::memory_order_release);
}
LONG WINAPI crashFilter(EXCEPTION_POINTERS *info) noexcept {
    if (info && info->ExceptionRecord)
        writeCrash("UnhandledException", info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    return previousFilter ? previousFilter(info) : EXCEPTION_CONTINUE_SEARCH;
}
[[noreturn]] void crashTerminate() noexcept {
    writeCrash("Terminate", 0, nullptr);
    if (previousTerminate) previousTerminate();
    std::abort();
}
bool startCrashCapture(const QString &folder) {
    // A concurrently executing fatal handler owns this handle until process exit.
    if (crashFile != INVALID_HANDLE_VALUE) return true;
    if (!QDir().mkpath(folder)) return false;
    struct Slot { QString path; QDateTime modified; bool empty; };
    QList<Slot> candidates;
    for (int i = 0; i < 16; ++i) {
        const QString path = QDir(folder).filePath(QString("crash-%1.log").arg(i, 2, 10, QLatin1Char('0')));
        const QFileInfo info(path); candidates.append({path, info.lastModified(), !info.exists() || info.size() == 0});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Slot &a, const Slot &b) {
        if (a.empty != b.empty) return a.empty;
        return a.modified < b.modified;
    });
    for (const auto &slot : candidates) {
        const QString native = QDir::toNativeSeparators(slot.path);
        crashFile = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_WRITE, FILE_SHARE_READ,
                                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (crashFile != INVALID_HANDLE_VALUE) break;
    }
    if (crashFile == INVALID_HANDLE_VALUE) return false;
    crashRecorded = false;
    previousFilter = SetUnhandledExceptionFilter(crashFilter);
    previousTerminate = std::set_terminate(crashTerminate);
    return true;
}
void stopCrashCapture() {
    if (crashFile == INVALID_HANDLE_VALUE) return;
    const auto filter = SetUnhandledExceptionFilter(previousFilter);
    if (filter != crashFilter) SetUnhandledExceptionFilter(filter);
    const auto handler = std::set_terminate(previousTerminate);
    if (handler != crashTerminate) std::set_terminate(handler);
    if (!crashWriting.test_and_set(std::memory_order_acquire)) {
        CloseHandle(crashFile); crashFile = INVALID_HANDLE_VALUE;
        crashWriting.clear(std::memory_order_release);
    }
}
#endif
const char *name(DiagEvent event) {
    switch (event) {
#define E(x) case DiagEvent::x: return #x;
        E(Started) E(Stopped) E(Heartbeat) E(UiStall) E(Overflow)
        E(UploadSubmit) E(UploadBegin) E(UploadDone) E(UploadFailed) E(UploadCancel)
        E(SessionStop) E(DeleteConfirm) E(DeleteAccepted) E(DeleteCancelled) E(DeleteSubmit)
        E(DeleteBegin) E(DeleteDone) E(DeleteFailed) E(DeleteUiBegin) E(DeleteUiFailedBegin) E(DeleteUiEnd)
        E(ListRequest) E(ListSubmit) E(ListBegin) E(ListDone) E(ListFailed) E(ListResult) E(ListDiscard)
        E(RenderBegin) E(RenderSort) E(RenderFilter) E(RenderLayout) E(RenderEnd) E(RemoveBegin) E(RemoveEnd) E(RefreshUpload)
        E(MonitorUiBegin) E(MonitorUiEnd) E(ProcessSnapshotBegin) E(ProcessSnapshotEnd)
        E(TerminalFeedBegin) E(TerminalFeedEnd) E(TerminalResizeBegin) E(TerminalResizeEnd)
        E(TerminalScreenState) E(TerminalClear) E(TerminalGrid)
        E(FilePressBegin) E(FilePressEnd) E(FileHitBegin) E(FileHitEnd) E(FileBasePressBegin) E(FileBasePressEnd)
        E(FileReleaseBegin) E(FileReleaseEnd) E(FileExpandedBegin) E(FileExpandedEnd) E(LockStartup) E(MainWindowStartup)
        E(ConnectionWait) E(ConnectionResult) E(ConnectionExit)
        E(FileActivate) E(DownloadRequested) E(DownloadQueued) E(TransferStart)
        E(UpdateRequest) E(UpdateResponse) E(UpdateRetry) E(UpdateDone) E(UpdateFailed)
#undef E
    }
    return "Unknown";
}
const char *receiverClass(const QObject *object) {
    if(!object)return "none";
    // Literal whitelist; no objectName, dynamic property, path or arbitrary text.
    for(auto meta=object->metaObject();meta;meta=meta->superClass())
        for(const auto allowed:{"rs::FileView","rs::Workspace","rs::Terminal","rs::ProcessTable","rs::MainWindow",
            "QTipLabel","QLabel","QTreeWidget","QTableWidget","QHeaderView","QWidget","QWidgetWindow",
            "QWindow","QApplication","QTimer","QPropertyAnimation","QStyleAnimation","QObject"})
            if(qstrcmp(meta->className(),allowed)==0)return allowed;
    return "other";
}
}
DiagnosticDispatch::DiagnosticDispatch(QObject *receiver,QEvent *event) {
    log=activeLog.load();
    if(!log || QThread::currentThread()!=qApp->thread()){log=nullptr;return;}
    const auto type=receiverClass(receiver),owner=receiverClass(receiver?receiver->parent():nullptr);
    QMutexLocker lock(&log->mutex);
    if(!log->accepting){log=nullptr;return;}
    previous=log->currentDispatch;
    log->currentDispatch={int(event->type()),previous.depth+1,type,owner,log->clock.elapsed()};
}
DiagnosticDispatch::~DiagnosticDispatch() {
    if(log){QMutexLocker lock(&log->mutex);log->currentDispatch=previous;}
}
DiagnosticLog::DiagnosticLog() {
    pulse.setInterval(500);
    connect(&pulse, &QTimer::timeout, &pulse, [this] {
        QMutexLocker lock(&mutex);
        lastPulse = clock.elapsed();
    });
}
DiagnosticLog::~DiagnosticLog() { stopLogging(); activeLog = nullptr; }
DiagnosticLog &DiagnosticLog::instance() { static DiagnosticLog value; return value; }
QString DiagnosticLog::directory() {
    const auto configured=QSettings().value("storage/logDirectory").toString();
    if(!configured.isEmpty())return configured;
    auto base=qEnvironmentVariable("RABBITSHELL_DATA_DIR");
    if(base.isEmpty())base=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath("logs");
}
bool DiagnosticLog::validateDirectory(const QString &path,QString *error) {
    if(!QDir::isAbsolutePath(path)||!QDir().mkpath(path)) { if(error)*error="无法创建日志目录。";return false; }
    QTemporaryFile probe(QDir(path).filePath(".frogshell-log-XXXXXX"));
    if(!probe.open()||probe.write("x",1)!=1||!probe.flush()) { if(error)*error="日志目录不可写。";return false; }
    QFile output(QDir(path).filePath("diagnostic.jsonl"));
    if(output.exists()&&!output.open(QIODevice::WriteOnly|QIODevice::Append)) { if(error)*error="无法写入诊断日志。";return false; }
    return true;
}
bool DiagnosticLog::startLogging(QString *error) {
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    if (isRunning()) return true;
    folder = directory();
    if(!validateDirectory(folder,error))return false;
    QFile output(QDir(folder).filePath("diagnostic.jsonl"));
    if(!output.open(QIODevice::WriteOnly|QIODevice::Append)){if(error)*error="无法写入诊断日志。";return false;}
    output.close();
    clock.start();
#ifdef Q_OS_WIN
    try { if(!startCrashCapture(folder)){if(error)*error="无法创建崩溃日志。";return false;} } catch (...) { if(error)*error="无法创建崩溃日志。";return false; }
#endif
    { QMutexLocker lock(&mutex); stopping = false; accepting = true; lastPulse = 0; lastUi = {}; currentDispatch = {}; }
    activeLog = this; pulse.start(); start();
    record(DiagEvent::Started);
    return true;
}
void DiagnosticLog::stopLogging() {
    if (!isRunning()) return;
    pulse.stop(); record(DiagEvent::Stopped);
    { QMutexLocker lock(&mutex); accepting = false; stopping = true; wake.wakeAll(); }
    activeLog = nullptr;
    wait();
#ifdef Q_OS_WIN
    stopCrashCapture();
#endif
}
QByteArray DiagnosticLog::row(DiagEvent event, const Context &context, qint64 count, bool ui, qint64 age,const DiagnosticDispatchState *dispatch,const TerminalDiagnosticState *terminal,const ConnectionDiagnosticState *connection) const {
    QJsonObject object{{"v", RABBITSHELL_DIAGNOSTIC_VERSION}, {"ms", clock.elapsed()},
        {"event", name(event)}, {"thread", ui ? "ui" : "worker"},
        {"session", QString::number(context.session, 16)}, {"job", context.job}, {"count", count}};
    if (!context.path.isEmpty()) object["pathHash"] = QString::fromLatin1(context.path);
    // Constant event/origin mapping only: never encode a filename or remote path.
    if(event==DiagEvent::FileActivate)object["origin"]="resource-double-click";
    if(event==DiagEvent::DownloadRequested || event==DiagEvent::DownloadQueued)object["origin"]="resource-menu";
    if(event==DiagEvent::TransferStart)object["origin"]=count==1?"resource-menu":count==2?"clipboard-preparation":count==3?"system-drag":"existing-queue";
    if (event == DiagEvent::UiStall || event == DiagEvent::Heartbeat) {
        object["lastUi"] = name(context.event); object["uiIdleMs"] = age;
    }
    if(event==DiagEvent::UiStall && dispatch) {
        object["dispatchEvent"]=dispatch->event;object["dispatchClass"]=dispatch->receiver;object["dispatchOwner"]=dispatch->owner;
        object["dispatchDepth"]=dispatch->depth;object["dispatchAgeMs"]=dispatch->depth?clock.elapsed()-dispatch->started:0;
    }
    if(terminal) {
        object["terminal"]=QString::number(terminal->terminal,16);
        object["columns"]=terminal->columns;object["rows"]=terminal->rows;
        object["cursorRow"]=terminal->cursorRow;object["cursorColumn"]=terminal->cursorColumn;
        object["historyRows"]=terminal->historyRows;object["scroll"]=terminal->scroll;
        object["scrollMaximum"]=terminal->scrollMaximum;object["alternate"]=terminal->alternate;
    }
    if(connection){
        object["stage"]=int(connection->stage);object["mode"]=connection->mode;object["sshErrno"]=connection->sshError;
        object["elapsedMs"]=connection->elapsedMs;object["timeoutMs"]=connection->timeoutMs;
        object["failed"]=connection->failed;object["voluntary"]=connection->voluntary;
    }
    auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    Q_ASSERT(bytes.size() < 512); bytes += '\n'; return bytes;
}
void DiagnosticLog::record(DiagEvent event, quint64 session, int job, const QString &path, qint64 count,const TerminalDiagnosticState *terminal,const ConnectionDiagnosticState *connection) {
    Context context{event,session,job,{}};
    // Bounded input, no raw names or paths on disk. The hash is for correlation only.
    if (!path.isEmpty()) context.path = QCryptographicHash::hash(QStringView(path).left(4096).toString().toUtf8(), QCryptographicHash::Sha256).toHex().left(24);
    const bool ui = QThread::currentThread() == qApp->thread();
    auto bytes = row(event, context, count, ui,0,nullptr,terminal,connection);
    QMutexLocker lock(&mutex);
    if (!accepting) return;
    if (ui) lastUi = context;
    if (queue.size() >= MaxRecords || queuedBytes + bytes.size() > MaxQueueBytes) { ++dropped; return; }
    queuedBytes += bytes.size(); queue.append(std::move(bytes));
    // Disk writes/rotation/flush never hold this lock. The writer batches at 1s.
}
void DiagnosticLog::run() {
    QDir().mkpath(folder);
    QFile file(QDir(folder).filePath("diagnostic.jsonl"));
    auto open = [&] { return file.open(QIODevice::WriteOnly | QIODevice::Append); };
    open(); qint64 lastHeartbeat = 0, lastStall = -5000; bool diskBlocked = false;
    for (;;) {
        QList<QByteArray> batch; Context context; DiagnosticDispatchState dispatch; qint64 lost, idle, now; bool done;
        {
            QMutexLocker lock(&mutex);
            if (!stopping) wake.wait(&mutex, 1000);
            batch.swap(queue); queuedBytes = 0; lost = std::exchange(dropped, 0);
            context = lastUi; dispatch = currentDispatch; now = clock.elapsed(); idle = now - lastPulse; done = stopping;
        }
        if (lost) batch.append(row(DiagEvent::Overflow, {}, lost, false));
        if (idle >= 2000 && now-lastStall >= 5000 && !done) {
            batch.append(row(DiagEvent::UiStall, context, 0, false, idle,&dispatch)); lastStall = now;
        }
        if (now-lastHeartbeat >= 5000 && !done) {
            batch.append(row(DiagEvent::Heartbeat, context, 0, false, idle)); lastHeartbeat = now;
        }
        for (const auto &bytes : batch) {
            if (diskBlocked) break;
            if (!file.isOpen() && !open()) break;
            if (file.size() + bytes.size() > maxFileBytes) {
                file.close();
                QDir dir(folder);
                // Refuse further writes if rotation fails; never grow past the cap.
                if ((dir.exists("diagnostic.2.jsonl") && !dir.remove("diagnostic.2.jsonl")) ||
                    (dir.exists("diagnostic.1.jsonl") && !dir.rename("diagnostic.1.jsonl", "diagnostic.2.jsonl")) ||
                    !dir.rename("diagnostic.jsonl", "diagnostic.1.jsonl")) { diskBlocked = true; break; }
                if (!open()) break;
            }
            if (file.write(bytes) != bytes.size()) { file.close(); break; }
        }
        if (file.isOpen() && !batch.isEmpty()) file.flush();
        if (done) break;
    }
}
void diagnose(DiagEvent event, quint64 tag, int job, const QString &path, qint64 count) {
    if (auto log = activeLog.load()) log->record(event, tag, job, path, count);
}
void diagnoseTerminal(DiagEvent event,const TerminalDiagnosticState &state) {
    if(event!=DiagEvent::TerminalScreenState&&event!=DiagEvent::TerminalClear&&event!=DiagEvent::TerminalGrid)return;
    if(auto log=activeLog.load())log->record(event,0,0,{},0,&state);
}
void diagnoseConnection(DiagEvent event,quint64 tag,const ConnectionDiagnosticState &state){if(auto log=activeLog.load())log->record(event,tag,0,{},0,nullptr,&state);}
} // namespace rs
