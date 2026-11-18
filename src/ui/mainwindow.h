#pragma once
#include "windowstyle.h"
#include "workspace.h"
namespace rs {
class DocumentSearchController;
class UpdateController;
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(bool demo = false, std::shared_ptr<SecretBackend> backend = systemSecretBackend(),UpdateController *updates = nullptr);
    void requestExit();
    void showFromTray();
    static QIcon trayUpdateIcon();
    QString migrationDestination() const { return finalExit ? migrationTarget : QString(); }
    DocumentSearchController *documentSearch() const { return advancedSearch; }
    UpdateController *updateController() const { return updates; }
    bool exitWasConfirmed() const { return finalExit; }
  signals:
    void exitAborted();
    void exitCompleted();

  protected:
    void closeEvent(QCloseEvent *) override;
    bool eventFilter(QObject *,QEvent *) override;

  private:
    friend class CredentialTests;
    friend class EditorAreaTests;
    friend class EditorMemoryTests;
    friend class AppearanceTests;
    friend class BookmarkTests;
    friend class AppLockTests;
    friend class MenuTests;
    ConnectionRepository repository;
    DirectoryBookmarkStore bookmarks;
    QVector<Profile> &profiles = repository.profiles;
    QHash<QString, Workspace *> workspaces;
    QTabWidget *tabs;
    QTreeWidget *groups;
    SafeTreeWidget *connections;
    static constexpr int tunnelColumn = 5;
    qreal connectionColumns[tunnelColumn + 1]={180,260,110,80,110,74};
    bool scalingColumns=false,customConnectionColumns=false;
    void fitConnectionColumns();
    QHash<QString, std::optional<TunnelCounts>> connectionTunnels;
    bool tunnelRulesReadable = true;
    void trackWorkspaceTunnels(Workspace *workspace);
    void updateConnectionTunnels(const QString &id, std::optional<TunnelCounts> counts);
    void setConnectionTunnelCell(QTreeWidgetItem *item) const;
    QLabel *detail;
    QLabel *detailName;
    QWidget *detailFields;
    QList<QLabel *> detailValues;
    QPushButton *detailOpen;
    QToolButton *detailEdit;
    QLineEdit *search;
    QString filter = "全部连接";
    bool exiting = false;
    bool exitAfterCredentials = false;
    bool exitAwaitingUpdate = false;
    bool exitSaveForUpdate = false;
    bool finalExit = false;
    QString migrationTarget;
    QSet<Workspace *> exitSavePending;
    QHash<Workspace *,QByteArray> exitTransfers;
    bool exitSaveFailed = false;
    quint64 exitSaveEpoch = 0;
    QSystemTrayIcon *tray = nullptr;
    QList<QPointer<QWidget>> trayWindows;
    QPointer<QWidget> trayModal;
    void hideToTray();
    void workspaceSaveFinished(Workspace *workspace,bool success);
    void finishConfirmedExit();
    QSet<Workspace *> stopping;
    void refresh();
    void refreshDetail();
    void editProfile(const QString &id = {});
    void open(const Profile &profile, std::optional<QByteArray> secret = std::nullopt,
              const QString &bookmarkId = {}, BookmarkOpen mode = BookmarkOpen::Both);
    void activateConnectionItem(QTreeWidgetItem *item);
    void connectionMenu(QPoint p);
    void configurePorts(const QString &id);
    void settings(int initialCategory = 0);
    void closeWorkspace(Workspace *workspace, bool alreadyApproved = false);
    void configureWorkspaceTab(Workspace *workspace);
    void continueExit();
    void importConfig();
    void persist();
    void buildApplicationMenu();
    void changeTheme(bool light);
    QPointer<QWidget> commandFocus;
    DocumentSearchController *advancedSearch = nullptr;
    UpdateController *updates = nullptr;
    Profile *find(const QString &id);
};
} // namespace rs
