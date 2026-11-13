#pragma once
#include "uizoom.h"
#include "animatedtree.h"
#include "uploadconflictdialog.h"
#include "../core/credentials.h"
#include "../core/session.h"
#include "editorarea.h"
#include "terminal.h"
#include "processtable.h"
#include "listeningtable.h"
#include "ports.h"
#include "services.h"
#include "serverclock.h"
#include "filedrag.h"
#include "bookmarkpanel.h"
#include "serverstatus.h"
#include <QtWidgets>
namespace rs {
struct FileClipboardBatch;
inline constexpr int FileDraftRole = Qt::UserRole + 1;
inline constexpr int DirectoryLoadedRole = Qt::UserRole + 2;
inline constexpr int ParentNavigationRole = Qt::UserRole + 3;
struct UploadTarget {
    QString path, error;
    bool valid() const { return !path.isEmpty() && error.isEmpty(); }
};
class NetworkPlot : public QWidget {
  public:
    explicit NetworkPlot(QWidget *parent = nullptr) : QWidget(parent) {
        uiFixedHeight(this,84);
        setObjectName("networkPlot");
    }
    void add(const QHash<QString, QPair<double, double>> &rates) {
        const auto now = QDateTime::currentMSecsSinceEpoch();
        for (auto it = rates.cbegin(); it != rates.cend(); ++it) {
            auto &samples = series[it.key()];
            samples.append({now, it.value()});
            while (!samples.isEmpty() && (samples.front().first < now - 60000 || samples.size() > 32))
                samples.removeFirst();
        }
        update();
    }
    void select(const QString &name) {
        selected = name;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    friend class AppearanceTests;
    QString selected;
    QHash<QString, QVector<QPair<qint64, QPair<double, double>>>> series;
};
class FileView : public AnimatedTreeWidget {
    Q_OBJECT
  public:
    explicit FileView(QWidget *parent = nullptr);
    ~FileView() override;
    UploadTarget uploadTarget(QPoint point) const;
    UploadTarget internalMoveTarget(QPoint point) const;
    QTreeWidgetItem *rowAt(QPoint point) const;
    void setUploadRoot(const QString &path);
    void clearUploadPreview();
    std::function<bool(const QByteArray &, const QString &)> internalMoveValid;
    std::function<bool(const QByteArray &, const QString &)> internalMoveNoop;
    std::function<bool(const QByteArray &)> internalMoveSourceValid;
  signals:
    void uploadDropped(const QStringList &files, const QString &directory);
    void uploadPreviewTarget(const QString &directory, bool active);
    void remoteDragRequested(const QVariantList &files);
    void parentNavigationActivated();
    void internalMoveDropped(const QByteArray &token, const QString &directory);

  protected:
    void startDrag(Qt::DropActions) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void paintEvent(QPaintEvent *) override;
    bool viewportEvent(QEvent *) override;

  private:
    friend class FileTransferTests;
    QRubberBand *rubber;
    QPoint origin;
    bool selecting = false;
    bool navigationPress = false;
    QString uploadRoot, uploadMessage;
    bool uploadPreview = false, uploadAllowed = false;
    bool internalPreview = false;
    QPersistentModelIndex uploadRow;
    QStringList localUploadPaths(const QMimeData *) const;
    bool previewUpload(QPoint, const QMimeData *, Qt::DropActions);
    bool previewInternalMove(QPoint, const QMimeData *, Qt::DropActions);
    QRect uploadTargetRect() const;
};
class ConnectionNotice;
class Workspace : public QWidget {
    Q_OBJECT
  public:
    Workspace(Profile profile, std::optional<QByteArray> secret, bool demo = false, QWidget *parent = nullptr,
              ConnectionRepository *repository = nullptr, DirectoryBookmarkStore *bookmarks = nullptr);
    ~Workspace() override;
    const Profile profile;
    bool isConnected() const {
        return online;
    }
    bool isConnecting() const { return mainPending; }
    bool hasMainConnection() const { return online || mainPending || (session && session->isRunning()); }
    bool dirty() const;
    int dirtyFileCount() const;
    QByteArray closeSignature() const;
    QByteArray transferSignature() const;
    bool canApproveExit(const QByteArray &signature) const;
    void discardExitDrafts();
    bool saveForExit();
    int terminalCount() const {
        return terminals.size();
    }
    int pendingTransfers() const;
    int tunnelCount() const;
    bool hasServiceConnection() const;
    void showPorts();
    void startPortRule(const QString &id);
    void reconnect();
    void disconnectHost();
    bool requestClose();
    bool waitingToClose() const {
        return closing;
    }
    void shutdown();
    void setActive(bool active);
    void refreshTheme();
    void updateCredentials(const Profile &updated, const std::optional<QByteArray> &secret);
    void openBookmark(const QString &id, BookmarkOpen mode = BookmarkOpen::Both);
    enum class Command { NewFile,NewFolder,Save,SaveAll,Upload,Refresh,Undo,Redo,Cut,Copy,Paste,Find,
        Status,Explorer,Processes,Listening,TerminalPanel,Ports,Services,NewTerminal,SplitTerminal,CloseTerminal,ClearTerminal,Reconnect,Disconnect };
    bool commandEnabled(Command command,QWidget *focus) const;
    void executeCommand(Command command,QWidget *focus);
  signals:
    void stateChanged();
    void readyToClose();
    void shutdownComplete();
    void closeCancelled();
    void clipboardSourceInvalidated();
    void searchDocumentOpened(rs::Editor *editor);
    void aboutToShutdown();
    void searchPageCloseRequested();
    void searchPageFloatRequested();

  private:
    friend class DocumentSearchController;
    friend class DocumentSearchTests;
    friend class NativeFileDragTests;
    friend class CredentialTests;
    friend class EditorAreaTests;
    friend class ProcessTableTests;
    friend class EditorMemoryTests;
    friend class WorkspaceTests;
    friend class AppearanceTests;
    friend class PortsTests;
    friend class ServicesTests;
    friend class FileTransferTests;
    friend class BookmarkTests;
    friend class AppLockTests;
    friend class MenuTests;
    struct Term {
        int id, group;
        Terminal *view;
        QWidget *frame;
        QString name;
        bool running = true;
        QString startupDirectory;
        bool manualName = false;
        quint64 lastFocus = 0;
        bool restorePending=false;
    };
    Session *session = nullptr, *transferSession = nullptr;
    QString authenticatedFingerprint, transferFingerprint;
    TunnelService *tunnelService = nullptr;
    QHash<QString,TunnelRule> queuedTunnelStarts;
    QHash<QString,TunnelRule> submittedTunnelRules;
    QSet<QString> wantedTunnels, submittedTunnels, manualTunnelStarts;
    QSet<QString> restoringTunnels;
    QHash<QString,QString> tunnelRestoreFailures;
    bool tunnelRetiring = false, tunnelAuthRetry = false, connectionAutoStarted = false;
    std::optional<QByteArray> credential;
    QPointer<ConnectionRepository> repository;
    QPointer<DirectoryBookmarkStore> bookmarkStore;
    BookmarkTree *bookmarkTree=nullptr;
    QLabel *bookmarkStatus=nullptr;
    struct BookmarkIntent { QString id,path; BookmarkOpen mode=BookmarkOpen::Both; int job=0; QPointer<Session> origin; };
    std::optional<BookmarkIntent> bookmarkIntent;
    struct BookmarkGroup { QString id, identity, path, name; };
    QHash<int,BookmarkGroup> bookmarkGroups;
    quint64 terminalFocusSerial=0;
    void openBookmarkTerminal(const DirectoryBookmark &bookmark);
    void syncBookmarkGroups();
    QString terminalLabel(const Term &term, bool includeGroup=false) const;
    void dispatchBookmark();
    void cancelBookmarkIntent();
    bool completeBookmark(int id,const QVariantMap &result);
    bool bookmarkIdentityValid() const;
    bool preparingCredentials = false, passwordWasRejected = false, keyNeedsReview = false,
         rememberCredential = false, persistCredential = false;
    quint64 credentialGeneration = 0;
    bool demo = false, online = false, active = true, closing = false;
    int nextJob = 1, nextTerm = 1, nextGroup = 1, focusedTerm = 0, activeTransfer = 0;
    QHash<int, Term> terminals;
    QMap<int, QSplitter *> terminalGroups;
    QHash<int, QString> pendingReads;
    QHash<int, QPointer<Editor>> pendingSaves;
    QHash<int, QPointer<Editor>> pendingConflicts;
    QSet<Editor *> closeAfterSave;
    QHash<QString, QPointer<Editor>> editors;
    QHash<QWidget *, QDialog *> floating;
    QSet<QWidget *> closingWindows;
    QHash<int, Job> transfers;
    QHash<int, QTreeWidgetItem *> transferItems;
    QQueue<int> transferQueue;
    QList<int> transferOrder;
    QSet<int> pausedTransfers, cancelledTransfers;
    QHash<QString, QSet<int>> uploadJournalPending;
    QSet<QString> uploadJournalUncertain;
    QSet<int> uploadJournalSealJobs;
    void finishUploadJournalRoot(int id, bool uncertain = false);
    QTimer transferIdleTimer;
    bool transferReady = false, transferRetiring = false, transferStopping = false;
    QPointer<UploadConflictDialog> uploadConflictDialog;
    struct DeferredConflict { QPointer<Session> worker;int id;quint64 nonce;QString path;bool canOverwrite; };
    std::optional<DeferredConflict> deferredConflict;
    void showUploadConflict(Session *, int id, quint64 nonce, const QString &path, bool canOverwrite);
    QString directory, homeDirectory;
    QStringList history;
    int historyIndex = -1;
    struct DirectoryRequest {
        QString path;
        QPersistentModelIndex parent;
        quint64 generation;
        bool navigation = false, historical = false;
    };
    QHash<int, DirectoryRequest> directoryRequests;
    QSet<QString> refreshDirectories;
    QHash<QString, int> newestDirectoryRequest;
    QHash<int, QString> fileMutations;
    QHash<int, std::function<void(const QVariantMap &, const QString &)>> journalCallbacks;
    quint64 directoryGeneration = 0;
    struct EntryDraft {
        QString parentPath;
        QPersistentModelIndex parent, row;
        QPointer<QLineEdit> editor;
        bool folder = false, waiting = false;
        int job = 0;
    } entryDraft;
    int latestNavigation = 0;
    bool shuttingDown = false, deleteConfirming = false;
    QStackedWidget *sidebar, *addressStack, *terminalStack;
    FileView *files = nullptr;
    QLineEdit *pathEdit = nullptr, *fileFilter = nullptr;
    bool showHiddenFiles = true;
    QWidget *breadcrumbs = nullptr, *terminalPanel = nullptr, *editorPanel = nullptr;
    QToolButton *maximizeTerminal, *currentTerminal, *splitTerminal, *closeCurrentTerminal;
    QWidget *terminalRowActions;
    QTabBar *bottomTabs = nullptr;
    QStackedWidget *bottomContents = nullptr;
    QWidget *terminalActions = nullptr;
    PortsPanel *portsPanel = nullptr;
    ServicesPanel *servicesPanel = nullptr;
    QVector<QPointer<Session>> serviceWorkers;
    ServerClock *serverClock = nullptr;
    QPointer<Session> clockSession;
    void ensureServerClock();
    void stopServerClock();
    bool servicesReading = false;
    quint64 servicesGeneration = 0;
    void ensureServices();
    Session *serviceSession(QObject *owner, Session::Mode mode);
    void stopServices();
    int terminalRowId = 0;
    bool terminalMaximized = false;
    int savedTerminalHeight = 280;
    qreal savedTerminalRatio = 1.0 / 3.0;
    bool restoringWorkspaceLayout = true, layoutReset = false;
    bool hadEditorPages = false, editorEmptyCheckQueued = false;
    bool layoutPersistenceEnabled = true, layoutSaved = false;
    QString activeSystemPage;
    void restoreWorkspaceLayout();
    void saveWorkspaceLayout();
    void pagePresented(QWidget *page);
    void scheduleEditorEmptyCheck();
    void applyEditorEmptyCheck();
    qreal terminalManagerBaseWidth=170;
    void sizeTerminalManager();
    QString preferredNic;
    QHBoxLayout *crumbLayout;
    QLabel *connectionStatus, *serverSummary, *serverPlatform, *serverArchitecture, *taskSummary;
    ResourceSummary *resourceSummary;
    NetworkRateSummary *networkRate;
    QComboBox *nic;
    NetworkPlot *networkPlot;
    ProcessTable *miniProcesses, *processes;
    AnimatedTreeWidget *terminalManager = nullptr;
    QTreeWidget *uploadList = nullptr, *downloadList = nullptr;
    EditorArea *editArea;
    QSplitter *verticalSplit;
    QWidget *processPage = nullptr;
    QWidget *listeningPage = nullptr;
    ListeningTable *miniListening = nullptr, *listeningTable = nullptr;
    QLabel *listeningStatus = nullptr;
    QLabel *listeningHealth = nullptr;
    QLabel *miniListeningState = nullptr;
    QPointer<Session> listeningSession;
    bool listeningReady=false, listeningPending=false;
    bool listeningAuthBlocked=false;
    QElapsedTimer listeningClock;
    listening::Snapshot listeningSnapshot;
    void requestListeningSnapshot();
    void requestMonitorSnapshot();
    void markMonitoringStale(const QString &reason);
    void stopListening(const QString &reason);
    void applyListeningSnapshot(const listening::Snapshot &snapshot);
    void showListeningPage();
    void listeningAuthRejected();
    QFrame *transferPanel;
    QTimer monitorTimer;
    bool monitorPending = false;
    bool mainResponseWaiting=false;
    quint64 previousTotal = 0, previousIdle = 0, memoryTotal = 0;
    QHash<QString, QPair<quint64, quint64>> lastNetwork;
    QElapsedTimer networkClock;
    QHash<QString, QPair<double, double>> networkRates;
    ConnectionNotice *connectionLoading=nullptr,*connectionBar=nullptr;
    bool mainPending=false,everConnected=false,connectionDismissed=false;
    quint64 mainAttempt=0;
    QString mainPhase;
    void beginMainConnection();
    void mainConnectionState(const QString &phase,bool pending);
    void syncConnectionNotice();
    void cancelMainConnection();
    void resetLocalPreferences();
    void buildUi();
    void showProcessPage();
    void attachSession(Session *s, bool transfer = false);
    void promptCredential(bool transfer = false, bool tunnel = false, bool service = false);
    void startSession(bool transfer = false);
    QString tunnelStartError() const;
    void ensureTunnels();
    void requestTunnelStart(const TunnelRule &rule);
    void queueTunnelStart(const TunnelRule &rule, bool manual);
    void restorePortRules();
    void showTunnelFailure(const QString &id, const QString &reason);
    void reportTunnelRestoreFailures();
    void stopTunnel(const QString &id);
    void restartTunnel(const TunnelRule &rule);
    void stopTunnels();
    void cancelQueuedTunnels(const QString &reason);
    void attachTunnels(TunnelService *service);
    void navigate(const QString &path, bool historical = false);
    void renderDirectory(const QVariantMap &result, int id);
    void syncParentNavigation();
    void requestDirectory(const QString &path, bool navigation = false, bool historical = false);
    void refreshFiles();
    void refreshUploaded(const Job &job);
    QTreeWidgetItem *fileItem(const QString &path) const;
    void syncFileMutation(const QString &path, bool removed, int job = 0);
    void filterFiles();
    void updateBreadcrumbs();
    void editDirectoryPath();
    void fileMenu(QPoint point);
    void deleteSelectedFiles();
    void deleteEntries(QStringList paths);
    void createEntry(bool folder, const QString &target = {});
    void showEntryDraft();
    void submitEntryDraft();
    bool finishEntryDraft();
    void cancelEntryDraft();
    void entryDraftError(const QString &reason);
    void renameEntry();
    void renameFile(const QVariantMap &info);
    void properties(const QVariantMap &info);
    void openFile(const QString &path);
    void saveEditor(Editor *editor, bool overwrite = false);
    void uploadFilesTo(const QString &target);
    QString selectedFileTarget() const;
    void resolveConflict(Editor *editor, const QVariantMap &remote);
    void closePage(QWidget *page);
    void movePage(QWidget *page, EditorTabs *target = nullptr);
    void floatPage(QWidget *page);
    void detachPage(QWidget *page);
    void updateEditorTitle(Editor *editor);
    EditorTabs *groupFor(QWidget *page) const;
    void addTerminal(int group = 0, const QString &startDirectory = {});
    void restoreTerminalSessions();
    int restoringDirectoryRequest=0;
    void closeTerminal(int id);
    void activateTerminal(int id, bool focus = false);
    void renameTerminal(int id, const QString &name);
    void updateTerminalRowActions();
    void rebuildTerminals();
    void terminalMenu(QPoint point);
    void placeTerminal(int id, int group);
    void setTerminalMaximized(bool maximized);
    void hideTerminalPanel();
    void monitor(const QByteArray &data, qint64 latency);
    std::function<bool(QWidget *,const std::shared_ptr<FileDragBatch> &,const QPixmap &)> fileDragExecutor=dragRemoteFiles;
    QVector<std::weak_ptr<FileDragBatch>> fileDrags;
    QHash<int,std::function<void(QString)>> dragCompletions;
    QHash<int,std::function<bool()>> dragValid;
    QHash<int,std::function<void(const QVariantMap &, const QString &)>> clipboardCompletions;
    QHash<int,std::function<bool()>> clipboardValid;
    QHash<int,std::shared_ptr<FileClipboardBatch>> clipboardHolds;
    QHash<int,std::shared_ptr<FileClipboardBatch>> clipboardUploadHolds;
    void copyFileSelection(bool cut, const QList<QVariantMap> &selection = {});
    void pasteFiles(const QString &target = {}, std::shared_ptr<FileClipboardBatch> captured = {});
    QString filePasteError(const QString &target, const QMimeData *mime,
                          const std::shared_ptr<FileClipboardBatch> &batch, QStringList *local = nullptr) const;
    QByteArray registerInternalDrag(const QVariantList &rows);
    static void unregisterInternalDrag(const QByteArray &token);
    bool validInternalDrag(const QByteArray &token, const QString &target) const;
    bool internalDragNoop(const QByteArray &token, const QString &target) const;
    bool validInternalDragSource(const QByteArray &token) const;
    void acceptInternalDrag(const QByteArray &token, const QString &target);
    void cancelClipboardJobs(std::shared_ptr<FileClipboardBatch> only = {});
    void completeClipboardJob(int id, const QVariantMap &result, const QString &error);
    bool clipboardPathLocked(const QString &path) const;
    bool clipboardMutationBlocked(const QString &path) const;
    bool clipboardCanReplace(int id, const QString &path) const;
    void updateMovedEditors(const QString &oldPath, const QString &newPath);
    void applyJournalUndo(const QVariantMap &effect);
    void requestJournalSnapshot(std::function<void(const QVariantMap &, const QString &)> callback);
    void undoLastFileOperation();
    void dragFiles(const QVariantList &files);
    std::shared_ptr<FileDragBatch> prepareFileDrag(const QVariantList &files,QString *error=nullptr);
    void cancelFileDrags();
    bool queueTransfers(const QStringList &paths, bool upload, const QString &remote = {},
                        bool clipboardPaste = false, std::shared_ptr<FileClipboardBatch> hold = {});
    void startTransfer();
    void ensureTransferSession();
    void trimTransferHistory();
    void updateTransferBatches();
    void transferMenu(QTreeWidget *list, QPoint position);
    void demoData();
    void fail(const QString &type, int id, const QString &reason);

  protected:
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    bool eventFilter(QObject *object, QEvent *event) override;
};
} // namespace rs
