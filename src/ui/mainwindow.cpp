#include "messagebox.h"
#include "applock.h"
#include "uipreferences.h"
#include "mainwindow.h"
#include "styledcombobox.h"
#include "theme.h"
#include "icons.h"
#include "fontsettings.h"
#include "contentfont.h"
#include "fileicons.h"
#include "filecontextmenu.h"
#include "layoutmetrics.h"
#include "connectiondelegate.h"
#include "documentsearch.h"
#include "../core/diagnostics.h"
#include "../core/storage.h"
#include "../core/editorlimits.h"
#include "storageusagecontroller.h"
#include "cachecleanupcontroller.h"
#include "updatecontroller.h"
namespace rs {
namespace {
// Keep QLabel identity for existing settings probes while using a real text
// view for soft wrapping and copying paths without inserting extra characters.
class WrappedSettingsPath final : public QLabel {
  public:
    explicit WrappedSettingsPath(const QString &path) {
        auto policy=QSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);policy.setHeightForWidth(true);setSizePolicy(policy);setMinimumWidth(0);setWordWrap(true);setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);
        view=new QTextEdit(this);view->setReadOnly(true);view->setFrameShape(QFrame::NoFrame);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setLineWrapMode(QTextEdit::WidgetWidth);view->setWordWrapMode(QTextOption::WrapAnywhere);view->document()->setDocumentMargin(0);
        view->setStyleSheet("QTextEdit { background: transparent; border: 0; padding: 0; }");
        view->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);setPath(path);
    }
    void setPath(const QString &path) { QLabel::setText(path);view->setPlainText(path);updateGeometry(); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        QTextDocument measured;measured.setDocumentMargin(0);measured.setDefaultFont(view->font());QTextOption options;options.setWrapMode(QTextOption::WrapAnywhere);measured.setDefaultTextOption(options);
        measured.setPlainText(text());measured.setTextWidth(qMax(1,width));return qCeil(measured.size().height());
    }
    QSize minimumSizeHint() const override { return QSize(0,heightForWidth(qMax(1,width()))); }
  protected:
    void resizeEvent(QResizeEvent *event) override { QLabel::resizeEvent(event);view->setGeometry(rect()); }
    void paintEvent(QPaintEvent *) override {}
  private:
    QTextEdit *view;
};
class SaveDialog : public QDialog {
  public:
    using QDialog::QDialog;
    bool saving = false;
    void reject() override {
        if (!saving)
            QDialog::reject();
    }

  protected:
    void closeEvent(QCloseEvent *event) override {
        if (saving)
            event->ignore();
        else
            QDialog::closeEvent(event);
    }
};
} // namespace
QIcon MainWindow::trayUpdateIcon() {
    initializeBrandingResources();
    const QImage source(":/branding/frogshell-update.png");
    QIcon result;
    for(const int size:{16,20,24,32,40,48,64,96,128}) {
        result.addPixmap(QPixmap::fromImage(source.scaled(size,size,Qt::KeepAspectRatio,Qt::SmoothTransformation)));
    }
    return result;
}
MainWindow::MainWindow(bool demonstration, std::shared_ptr<SecretBackend> backend,UpdateController *updateController)
    : repository(this, std::move(backend)), bookmarks({},this) {
    updates=updateController?updateController:new UpdateController(UpdateController::runtimeConfig(),{},dataPath()+"/updates",this);
    connect(this,&MainWindow::exitAborted,updates,&UpdateController::cancelExitIntent);
    connect(this,&MainWindow::exitCompleted,updates,&UpdateController::confirmExit);
    connect(updates,&UpdateController::restartRequested,this,[this]{if(!migrationTarget.isEmpty()){updates->cancelExitIntent();return;}requestExit();});
    connect(updates,&UpdateController::settingsRequested,this,[this]{settings(6);});
    connect(updates,&UpdateController::updateAvailable,this,[this](const QString &version){statusBar()->showMessage("发现 FrogShell "+version+"，可从托盘菜单更新。",30000);});
    connect(&repository, &ConnectionRepository::idle, this, [this] {
        if (exitAfterCredentials) {
            exitAfterCredentials = false;
            const bool canceledUpdate=exitAwaitingUpdate&&!updates->restartPending();exitAwaitingUpdate=false;
            if(canceledUpdate){emit exitAborted();return;}
            requestExit();
        }
    });
    setProperty("rabbitUi",true);
    setWindowTitle("FrogShell");
    tray=new QSystemTrayIcon(QApplication::windowIcon(),this);tray->setObjectName("applicationTrayIcon");tray->setToolTip("FrogShell");
    auto trayMenu=new FileContextMenu(this);trayMenu->setObjectName("applicationTrayMenu");trayMenu->setProperty("applicationLockTrusted",true);trayMenu->setToolTipsVisible(true);
    trayMenu->addAction("显示面板",this,&MainWindow::showFromTray);auto trayLock=trayMenu->addAction("锁定");trayLock->setObjectName("trayLock");
    auto trayUpdate=trayMenu->addAction("更新 FrogShell");trayUpdate->setObjectName("trayUpdate");
    auto trayCancelUpdate=trayMenu->addAction("取消更新");trayCancelUpdate->setObjectName("trayCancelUpdate");
    trayMenu->addSeparator();trayMenu->addAction("退出",this,&MainWindow::requestExit);tray->setContextMenu(trayMenu);
    const auto baseTrayIcon=QApplication::windowIcon(),updateTrayIcon=trayUpdateIcon();
    auto syncTrayUpdate=[this,trayUpdate,trayCancelUpdate,baseTrayIcon,updateTrayIcon]{
        const bool available=updates->hasUpdate(),busy=updates->updateBusy(),locked=OperationGate::locked();
        tray->setIcon(available?updateTrayIcon:baseTrayIcon);
        tray->setToolTip(available?"FrogShell · 发现更新 "+updates->updateVersion():"FrogShell");
        trayUpdate->setVisible(available);trayUpdate->setEnabled(available&&!busy&&!locked);
        trayUpdate->setText(busy?updates->progressText():updates->automaticInstallSupported()&&updates->state()!=releaseupdate::State::ManualDownload?"更新 FrogShell "+updates->updateVersion():"手动更新 FrogShell "+updates->updateVersion());
        trayCancelUpdate->setVisible(available&&busy&&(!updates->restartPending()||updates->preparing()));trayCancelUpdate->setEnabled(!locked);
    };
    connect(updates,&UpdateController::changed,this,syncTrayUpdate);connect(trayMenu,&QMenu::aboutToShow,this,syncTrayUpdate);
    connect(trayUpdate,&QAction::triggered,updates,&UpdateController::requestUpdate);
    connect(trayCancelUpdate,&QAction::triggered,updates,&UpdateController::cancel);
    connect(updates,&UpdateController::manualUpdateRequested,this,[this]{showFromTray();updates->showAbout(this);});
    if(auto lock=AppLock::instance())connect(lock,&AppLock::lockedChanged,this,[syncTrayUpdate](bool){syncTrayUpdate();});
    syncTrayUpdate();
    auto syncTrayLock=[trayLock]{auto lock=AppLock::instance();const bool configured=lock&&lock->security().enabled();const bool locked=lock&&lock->locked();trayLock->setEnabled(configured&&!locked);trayLock->setToolTip(!configured?"请先在设置中启用应用密码":locked?"应用已锁定":"锁定应用");};
    connect(trayMenu,&QMenu::aboutToShow,this,syncTrayLock);if(auto lock=AppLock::instance()){connect(&lock->security(),&SecurityStore::changed,this,syncTrayLock);connect(lock,&AppLock::lockedChanged,this,syncTrayLock);}syncTrayLock();
    connect(trayLock,&QAction::triggered,this,[]{if(auto lock=AppLock::instance();lock&&lock->security().enabled()&&!lock->locked())lock->lock();});
    connect(tray,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason){if(reason==QSystemTrayIcon::Trigger||reason==QSystemTrayIcon::DoubleClick)showFromTray();});
    if(QSystemTrayIcon::isSystemTrayAvailable()){tray->show();qApp->setQuitOnLastWindowClosed(false);}
    resize(1440,900);
    setMinimumSize(800, 480);
    connect(&repository, &ConnectionRepository::changed, this,
            [this](const Profile &p, const std::optional<QByteArray> &secret) {
                if (auto w = workspaces.value(p.id))
                    w->updateCredentials(p, secret);
                refresh();
            });
    connect(&repository, &ConnectionRepository::removed, this, [this](const QString &id) {
        if (auto w = workspaces.value(id)) {
            Profile p = w->profile;
            p.rememberSecret = false;
            w->updateCredentials(p, std::nullopt);
        }
        refresh();
    });
    auto central = new QWidget;
    setCentralWidget(central);
    auto root = new QVBoxLayout(central);
    uiMargins(root,0,0,0,0);
    uiSpacing(root,0);
    auto shortcut = new QShortcut(QKeySequence("Ctrl+,"), this);
    connect(shortcut, &QShortcut::activated, this, [this]{settings();});
    tabs = new PanelTabs;
    advancedSearch=new DocumentSearchController(this);
    connect(advancedSearch,&DocumentSearchController::workspaceActivationRequested,this,[this](Workspace *workspace){if(tabs->indexOf(workspace)>=0)tabs->setCurrentWidget(workspace);});
    connect(advancedSearch,&DocumentSearchController::reopenSourceRequested,this,[this](const QString &id,const QString &identity,const QString &){
        if(OperationGate::locked())return;
        auto profile=find(id);
        if(!profile||profile->credentialKey()!=identity){advancedSearch->results()->setFeedback("来源服务器已删除或连接身份已变化，无法定位。");return;}
        open(*profile);
    });
    auto advancedShortcut=new QShortcut(QKeySequence("Ctrl+Shift+F"),this);advancedShortcut->setContext(Qt::ApplicationShortcut);
    connect(advancedShortcut,&QShortcut::activated,this,[this]{advancedSearch->openFor(QApplication::focusWidget());});
    buildApplicationMenu();
    tabs->tabBar()->setObjectName("serverTabs");
    tabs->setTabsClosable(true);
    tabs->setDocumentMode(true);
    tabs->setElideMode(Qt::ElideRight);
    tabs->setUsesScrollButtons(true);
    tabs->tabBar()->setDrawBase(false);
    root->addWidget(tabs, 1);
    auto manager = new QWidget;
    auto ml = new QVBoxLayout(manager);
    uiMargins(ml,0,0,0,0);
    uiSpacing(ml,0);
    auto split = new PanelSplitter;
    ml->addWidget(split,1);
    auto managerFooter=new PanelBand(24,true);setFrameRole(managerFooter,"footer");
    auto mf=new QHBoxLayout(managerFooter);uiMargins(mf,10,0,10,0);uiSpacing(mf,8);
    auto managerStatus=new QLabel("连接管理");managerStatus->setProperty("muted",true);bindUiFont(managerStatus,12);mf->addWidget(managerStatus);mf->addStretch();
    auto managerScale=new QLabel;bindUiFont(managerScale,12);UiZoom::instance().bind(managerScale,"zoomText",[managerScale]{managerScale->setText(QString::number(qRound(UiZoom::instance().factor()*100))+"%");});mf->addWidget(managerScale);ml->addWidget(managerFooter);
    groups = new SafeTreeWidget;
    groups->setHeaderHidden(true);
    groups->setObjectName("connectionGroups");groups->setProperty("sideTree",true);
    uiMaximumWidth(groups,320);
    uiMinimumWidth(groups,160);
    groups->setItemDelegate(new DensityDelegate(28,groups));
    split->addWidget(groups);
    auto center = new QWidget;
    auto cl = new QVBoxLayout(center);
    uiMargins(cl,18,14,18,14);uiSpacing(cl,8);
    auto headingRow=new QWidget;bandHeight(headingRow);
    auto headingLayout=new QHBoxLayout(headingRow);uiMargins(headingLayout,0,0,0,0);uiSpacing(headingLayout,0);
    auto heading = new QLabel("连接");bindUiFont(heading,13,QFont::Medium);
    headingLayout->addWidget(heading);headingLayout->addStretch();cl->addWidget(headingRow);
    auto actions = new QHBoxLayout;
    search = new QLineEdit;
    search->setPlaceholderText("搜索名称、主机、用户或分组");
    search->setClearButtonEnabled(true);
    search->setAccessibleName("搜索连接");
    bandHeight(search,32);
    actions->addWidget(search, 1);
    auto add = iconButton(Icon::Connection, "新建连接"), addGroup = iconButton(Icon::NewFolder, "新建分组"), import = iconButton(Icon::Import, "导入连接配置");
    
    headingLayout->addWidget(add);
    headingLayout->addWidget(addGroup);
    headingLayout->addWidget(import);
    cl->addLayout(actions);
    connections = new SafeTreeWidget;
    connections->setProperty("processTable",true);
    bindUiFont(connections,13);
    connections->setHeader(processHeader(false,connections));
    connections->header()->setSortIndicatorShown(false);
    connections->header()->setSectionsClickable(false);
    connections->setColumnCount(tunnelColumn+1);
    connections->setHeaderLabels({"名称", "地址", "用户", "环境", "状态", "隧道"});
    connections->headerItem()->setTextAlignment(3,Qt::AlignCenter);
    connections->headerItem()->setTextAlignment(tunnelColumn,Qt::AlignCenter);
    connections->headerItem()->setToolTip(tunnelColumn,"运行中的隧道数 / 已配置的隧道总数");
    connections->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connections->setSelectionBehavior(QAbstractItemView::SelectRows);connections->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connections->setUniformRowHeights(true);connections->setAllColumnsShowFocus(true);
    connections->setAlternatingRowColors(true);
    connections->setFrameShape(QFrame::NoFrame);
    connections->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    connections->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    connections->header()->setStretchLastSection(false);connections->header()->setSectionResizeMode(QHeaderView::Interactive);
    UiZoom::instance().bind(connections,"columns",[this] {
        QScopedValueRollback<bool> guard(scalingColumns,true);
        for (int column=0;column<connections->columnCount();++column)
            connections->setColumnWidth(column,uiPx(connectionColumns[column]));
        fitConnectionColumns();
    });
    connect(connections->header(),&QHeaderView::sectionResized,this,[this](int c,int,int size){if(!scalingColumns&&!UiZoom::instance().changing()){connectionColumns[c]=uiBase(size);customConnectionColumns=true;}});
    connections->setItemDelegate(new ConnectionDelegate(connections));
    UiZoom::instance().bind(connections,"density",[this] {
        connections->setIndentation(uiPx(18));
        connections->doItemsLayout();
    });
    connections->setContextMenuPolicy(Qt::CustomContextMenu);
    connections->setAccessibleName("服务器连接列表");connections->viewport()->installEventFilter(this);
    cl->addWidget(processCard(connections),1);
    split->addWidget(center);
    auto detailPanel=new QWidget;detailPanel->setObjectName("side");
    auto dl=new QVBoxLayout(detailPanel);uiMargins(dl,18,18,18,18);uiSpacing(dl,8);
    auto detailHead=new QHBoxLayout;uiMargins(detailHead,0,0,0,0);uiSpacing(detailHead,0);
    detailName=new QLabel;detailName->setTextFormat(Qt::PlainText);bindUiFont(detailName,13,QFont::Medium);detailName->setWordWrap(true);
    detailEdit=iconButton(Icon::Rename,"编辑连接");detailHead->addWidget(detailName,1);detailHead->addWidget(detailEdit);dl->addLayout(detailHead);
    detailFields=new QWidget;auto form=new QVBoxLayout(detailFields);uiMargins(form,0,8,0,0);uiSpacing(form,6);
    for(const auto &field:QStringList{"主机","端口","用户","环境","认证方式","初始目录"}){
        auto fieldRow=new QWidget;auto column=new QVBoxLayout(fieldRow);uiMargins(column,0,0,0,6);uiSpacing(column,6);
        auto caption=new QLabel(field);caption->setProperty("muted",true);bindUiFont(caption,12);column->addWidget(caption);
        auto value=new QLabel;value->setTextFormat(Qt::PlainText);value->setWordWrap(true);value->setTextInteractionFlags(Qt::TextSelectableByMouse);detailValues<<value;column->addWidget(value);form->addWidget(fieldRow);
    }
    dl->addWidget(detailFields);detailFields->hide();
    detail = new QLabel("选择连接查看详情");
    detail->setTextFormat(Qt::PlainText);
    detail->setWordWrap(true);
    detail->setAlignment(Qt::AlignTop);
    detail->setObjectName("connectionDetail");
    detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dl->addWidget(detail);detail->setProperty("muted",true);bindUiFont(detail,12);
    detailOpen=button("连接");detailOpen->setProperty("primary",true);bandHeight(detailOpen,32);dl->addWidget(detailOpen);dl->addStretch();
    auto detailScroll=new QScrollArea;detailScroll->setFrameShape(QFrame::NoFrame);detailScroll->setWidgetResizable(true);detailScroll->setWidget(detailPanel);uiMinimumWidth(detailScroll,200);
    split->addWidget(detailScroll);split->setStretchFactor(1,1);
    split->setSizes({220,920,300});
    connect(detailOpen,&QPushButton::clicked,this,[this]{if(auto item=connections->currentItem())activateConnectionItem(item);});
    connect(detailEdit,&QToolButton::clicked,this,[this]{if(auto item=connections->currentItem())editProfile(item->data(0,Qt::UserRole).toString());});
    tabs->addTab(manager, "连接管理");
    replaceTabButton(tabs->tabBar(), 0, QTabBar::RightSide, nullptr);
    replaceTabButton(tabs->tabBar(), 0, QTabBar::LeftSide, nullptr);
    connect(search, &QLineEdit::textChanged, this, &MainWindow::refresh);
    connect(add, &QToolButton::clicked, this, [this] { editProfile(); });
    connect(import, &QToolButton::clicked, this, &MainWindow::importConfig);
    connect(addGroup, &QToolButton::clicked, this, [this] {
        bool ok;
        auto name = QInputDialog::getText(this, "新建分组", "分组名称", QLineEdit::Normal, {}, &ok).trimmed();
        if (ok && !name.isEmpty()) {
            auto list = QSettings().value("connections/groups").toStringList();
            if (!list.contains(name))
                list << name;
            QSettings().setValue("connections/groups", list);
            refresh();
        }
    });
    connect(groups, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *i, int) {
        filter = i->text(0);
        refresh();
    });
    connect(connections, &QTreeWidget::itemSelectionChanged, this, &MainWindow::refreshDetail);
    refreshDetail();
    connect(connections,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem *item,int){activateConnectionItem(item);});
    connect(connections,&QWidget::customContextMenuRequested,this,&MainWindow::connectionMenu);
    auto enter=new QShortcut(QKeySequence(Qt::Key_Return),connections);enter->setContext(Qt::WidgetWithChildrenShortcut);
    connect(enter,&QShortcut::activated,this,[this]{
        const auto items=connections->selectedItems();
        if(items.size()==1){activateConnectionItem(items.first());return;}
        for(auto i:items)if(i->parent()){statusBar()->showMessage("请仅选择连接行进行批量连接。",5000);return;}
        QStringList ids;for(auto i:items)ids<<i->data(0,Qt::UserRole).toString();
        for(const auto &id:ids)if(auto p=find(id)){const auto copy=*p;open(copy);}
    });
    auto rename=new QShortcut(QKeySequence(Qt::Key_F2),connections);rename->setContext(Qt::WidgetWithChildrenShortcut);
    connect(rename,&QShortcut::activated,this,[this]{
        auto item=connections->currentItem();if(!item||item->parent())return;
        const auto id=item->data(0,Qt::UserRole).toString();auto p=find(id);if(!p)return;
        bool ok=false;const auto name=QInputDialog::getText(this,"重命名连接","显示名称",QLineEdit::Normal,p->name,&ok);
        if(ok&&!name.trimmed().isEmpty())if(auto current=find(id)){current->name=name.trimmed();persist();}
    });
    connect(&bookmarks,&DirectoryBookmarkStore::changed,this,[this]{refresh();});
    connect(&bookmarks,&DirectoryBookmarkStore::saveFailed,this,[this](const QString &error){statusBar()->showMessage(error,10000);});
    connect(tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (index > 0)
            closeWorkspace(qobject_cast<Workspace *>(tabs->widget(index)));
    });
    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
        for (auto w : workspaces)
            w->setActive(tabs->indexOf(w) == index);
        auto w = qobject_cast<Workspace *>(tabs->widget(index));
        setWindowTitle(w ? w->profile.identity() + " — FrogShell" : "FrogShell");
    });
    tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabs->tabBar(), &QWidget::customContextMenuRequested, this, [this](QPoint p) {
        int index = tabs->tabBar()->tabAt(p);
        if (index <= 0)
            return;
        auto w = qobject_cast<Workspace *>(tabs->widget(index));
        QMenu m;
        m.addAction(w->hasMainConnection() || w->tunnelCount()>0 || w->hasServiceConnection() ? "断开连接" : "重新连接", this, [w] {
            if (w->hasMainConnection() || w->tunnelCount()>0 || w->hasServiceConnection())
                w->disconnectHost();
            else
                w->reconnect();
        });
        m.addSeparator();
        m.addAction("关闭工作区", this, [this, w] { closeWorkspace(w); });
        m.exec(tabs->tabBar()->mapToGlobal(p));
    });
    QString tunnelConfigError;
    for (const auto &rule : loadAllTunnelRules(&tunnelConfigError)) {
        auto &counts = connectionTunnels[rule.profileId];
        if (!counts) counts.emplace();
        ++counts->total;
    }
    tunnelRulesReadable = tunnelConfigError.isEmpty();
    refresh();
    restoreGeometry(QSettings().value("window/geometry").toByteArray());
    keepWindowOnScreen(this);
    if (demonstration) {
        Profile p;
        p.id = "native-preview";
        p.name = "staging-api";
        p.host = "192.0.2.21";
        p.user = "demo";
        auto w = new Workspace(p, {}, true, nullptr, &repository, &bookmarks);
        workspaces[p.id] = w;
        trackWorkspaceTunnels(w);
        advancedSearch->registerWorkspace(w);
        tabs->addTab(w, "[测试] staging-api · 预览");
        configureWorkspaceTab(w);
        tabs->setCurrentWidget(w);
        connect(w,&Workspace::readyToClose,this,[this,w]{workspaceSaveFinished(w,true);});
        connect(w,&Workspace::closeCancelled,this,[this,w]{workspaceSaveFinished(w,false);});
    }
}
void MainWindow::fitConnectionColumns(){
    if(customConnectionColumns)return;
    QScopedValueRollback<bool> guard(scalingColumns,true);
    int fixed=0;
    for (int column=0;column<connections->columnCount();++column)
        if (column!=1) fixed+=connections->columnWidth(column);
    connections->setColumnWidth(1,qMax(uiPx(148),connections->viewport()->width()-fixed));
}
void MainWindow::trackWorkspaceTunnels(Workspace *workspace) {
    const auto update = [this,workspace] {
        updateConnectionTunnels(workspace->profile.id,workspace->tunnelCounts());
    };
    connect(workspace,&Workspace::tunnelCountsChanged,this,update);
    update();
}
void MainWindow::updateConnectionTunnels(const QString &id,std::optional<TunnelCounts> counts) {
    connectionTunnels.insert(id,counts);
    for (int row=0;row<connections->topLevelItemCount();++row) {
        auto item=connections->topLevelItem(row);
        if (item->data(0,Qt::UserRole).toString()!=id) continue;
        setConnectionTunnelCell(item);
        break;
    }
}
void MainWindow::setConnectionTunnelCell(QTreeWidgetItem *item) const {
    const auto id = item->data(0,Qt::UserRole).toString();
    const auto known = connectionTunnels.constFind(id);
    const auto counts = known!=connectionTunnels.cend() ? *known :
        (tunnelRulesReadable ? std::optional<TunnelCounts>(TunnelCounts{}) : std::nullopt);
    item->setTextAlignment(tunnelColumn,Qt::AlignCenter);
    item->setText(tunnelColumn,counts ? QString("%1/%2").arg(counts->running).arg(counts->total) : QString("—"));
    item->setToolTip(tunnelColumn,counts ? QString("运行中 %1 个，共配置 %2 个隧道").arg(counts->running).arg(counts->total)
                                       : QString("无法读取隧道配置"));
}
bool MainWindow::eventFilter(QObject *object,QEvent *event){
    if(connections && object==connections->viewport() && event->type()==QEvent::Resize)fitConnectionColumns();
    return QMainWindow::eventFilter(object,event);
}
Profile *MainWindow::find(const QString &id) {
    for (auto &p : profiles)
        if (p.id == id)
            return &p;
    return nullptr;
}
void MainWindow::persist() {
    QString error;
    if (!saveProfiles(profiles, &error))
        AppMessageBox::critical(this, "配置保存失败", error);
    refresh();
}
void MainWindow::refresh() {
    QSet<QString> profileIds;for(const auto &p:profiles)profileIds.insert(p.id);bookmarks.setProfiles(profileIds);
    auto key=[](QTreeWidgetItem *i){return qMakePair(i?i->data(0,Qt::UserRole).toString():QString(),i?i->data(0,BookmarkIdRole).toString():QString());};
    const auto current=key(connections->currentItem());QSet<QPair<QString,QString>> selected;
    QSet<QString> expanded;for(int i=0;i<connections->topLevelItemCount();++i)if(connections->topLevelItem(i)->isExpanded())expanded.insert(connections->topLevelItem(i)->data(0,Qt::UserRole).toString());
    for(auto i:connections->selectedItems())selected.insert(key(i));
    const int v=connections->verticalScrollBar()->value(),h=connections->horizontalScrollBar()->value();
    QSignalBlocker block(connections);
    QHash<QString,QTreeWidgetItem*> old;
    for(int i=0;i<connections->topLevelItemCount();++i){auto item=connections->topLevelItem(i);old[item->data(0,Qt::UserRole).toString()]=item;}
    int row=0;bool viewChanged=false;
    for(const auto &p:profiles){
        auto item=old.take(p.id);if(!item)item=new QTreeWidgetItem;
        const int previous=connections->indexOfTopLevelItem(item);
        if(previous!=row){viewChanged=true;if(previous>=0)connections->takeTopLevelItem(previous);connections->insertTopLevelItem(row,item);}++row;
        const auto w=workspaces.value(p.id);
        const auto state = w && w->isConnected() ? ConnectionState::Connected
                         : w && w->isConnecting() ? ConnectionState::Connecting : ConnectionState::Disconnected;
        const auto status = state == ConnectionState::Connected ? QStringLiteral("已连接")
                          : state == ConnectionState::Connecting ? QStringLiteral("连接中") : QStringLiteral("未连接");
        const QStringList values={p.name,p.host+(p.port==22?"":":"+QString::number(p.port)),p.user,p.environment,status};
        item->setData(0,Qt::UserRole,p.id);
        for(int c=0;c<values.size();++c){item->setText(c,values[c]);item->setToolTip(c,Qt::convertFromPlainText(values[c]));}
        setConnectionTunnelCell(item);
        item->setData(4,ConnectionStateRole,int(state));
        if(w && state==ConnectionState::Disconnected)item->setToolTip(4,"工作区已打开，SSH 未连接");
        const auto q=search->text().trimmed();
        const bool visible=(q.isEmpty()||(p.name+" "+p.host+" "+p.user+" "+p.group).contains(q,Qt::CaseInsensitive))&&
            (filter!="收藏"||p.favorite)&&(filter!="最近使用"||p.lastUsed.isValid())&&
            (filter=="全部连接"||filter=="最近使用"||filter=="收藏"||filter==(p.group.isEmpty()?"未分组":p.group));
        if(item->isHidden()==visible){viewChanged=true;connections->invalidateAccessibleRows();item->setHidden(!visible);}
        QHash<QString,QTreeWidgetItem*> children;for(int c=0;c<item->childCount();++c)children[item->child(c)->data(0,BookmarkIdRole).toString()]=item->child(c);
        int position=0;for(const auto &b:bookmarks.entries(p.id)){
            auto child=children.take(b.id);if(!child)child=new QTreeWidgetItem;
            const int prev=item->indexOfChild(child);if(prev!=position){viewChanged=true;if(prev>=0)item->takeChild(prev);item->insertChild(position,child);}++position;
            child->setData(0,Qt::UserRole,p.id);child->setData(0,BookmarkIdRole,b.id);child->setText(0,b.name());child->setText(1,b.path);
            child->setToolTip(0,Qt::convertFromPlainText(b.name()));child->setToolTip(1,Qt::convertFromPlainText(b.path));child->setIcon(0,FileIconProvider::directory());
        }
        viewChanged|=!children.isEmpty();qDeleteAll(children);
    }
    viewChanged|=!old.isEmpty();qDeleteAll(old);
    for(QTreeWidgetItemIterator it(connections);*it;++it){auto i=*it;const auto k=key(i);if(!i->parent()&&i->isExpanded()!=expanded.contains(k.first)){viewChanged=true;i->setExpanded(expanded.contains(k.first));}const bool visible=!i->isHidden()&&(!i->parent()||!i->parent()->isHidden());i->setSelected(visible&&selected.contains(k));if(visible&&k==current)connections->setCurrentItem(i,0,QItemSelectionModel::NoUpdate);}
    if(viewChanged){connections->doItemsLayout();connections->invalidateAccessibleRows();}
    connections->verticalScrollBar()->setValue(v);connections->horizontalScrollBar()->setValue(h);
    QSignalBlocker groupBlock(groups);
    QStringList names = {"全部连接", "最近使用", "收藏", "未分组"};
    for (const auto &g : QSettings().value("connections/groups").toStringList())
        if (!names.contains(g))
            names << g;
    for (const auto &p : profiles)
        if (!p.group.isEmpty() && !names.contains(p.group))
            names << p.group;
    QStringList existingGroups;for(int i=0;i<groups->topLevelItemCount();++i)existingGroups<<groups->topLevelItem(i)->text(0);
    refreshDetail();
    if(existingGroups==names)return;
    groups->clear();
    for (const auto &name : names) {
        auto item = new QTreeWidgetItem(groups, {name});
        if (name == filter)
            groups->setCurrentItem(item);
    }
}
void MainWindow::refreshDetail() {
    const auto items=connections->selectedItems();detailFields->hide();detailName->clear();detailEdit->setEnabled(false);detailOpen->setEnabled(false);
    if(items.size()!=1){detail->setText(items.isEmpty()?"选择连接查看详情":"已选择多项；批量连接操作仅适用于连接行，请勿混选目录。");return;}
    const auto item=items.first();auto p=find(item->data(0,Qt::UserRole).toString());if(!p){detail->clear();return;}
    detailEdit->setEnabled(true);detailOpen->setEnabled(true);detailName->setText(p->name);
    if(item->parent()){
        const auto b=bookmarks.entry(p->id,item->data(0,BookmarkIdRole).toString());
        detail->setText(b?b->name()+"\n\n"+b->path:QString());detailOpen->setText("打开目录和终端组");return;
    }
    detailFields->show();const QStringList values{p->host,QString::number(p->port),p->user,p->environment,
        p->authentication=="key"?"私钥":p->authentication=="agent"?"SSH Agent":"密码",p->directory=="."?"当前账号主目录":p->directory};
    for(int i=0;i<detailValues.size();++i)detailValues[i]->setText(values.value(i));
    detail->setText(p->notes);detailOpen->setText(workspaces.contains(p->id)?"打开工作区":"连接");
}
void MainWindow::editProfile(const QString &id) {
    if(OperationGate::locked())return;
    Profile p;
    if (auto existing = find(id))
        p = *existing;
    const auto lockEpoch=OperationGate::token();
    SaveDialog d(this);
    d.setWindowTitle(id.isEmpty() ? "新建连接" : "编辑连接（仅用于后续连接）");
    auto layout = new QVBoxLayout(&d);
    auto formBody = new QWidget;
    auto form = new QFormLayout(formBody);
    form->setVerticalSpacing(12);
    auto name = new QLineEdit(p.name), host = new QLineEdit(p.host), user = new QLineEdit(p.user),
         dir = new QLineEdit(p.directory), key = new QLineEdit(p.keyPath), notes = new QLineEdit(p.notes),
         group = new QLineEdit(p.group), secret = new QLineEdit;
    name->setObjectName("profileName");
    host->setObjectName("profileHost");
    user->setObjectName("profileUser");
    secret->setEchoMode(QLineEdit::Password);
    secret->setObjectName("profileSecret");
    auto remember = new QCheckBox("保存密码");
    remember->setObjectName("profileRememberSecret");
    remember->setChecked(p.rememberSecret);
    auto secretLabel = new QLabel;
    auto port = new QSpinBox;
    port->setRange(1, 65535);
    port->setValue(p.port);
    auto auth = new StyledComboBox;
    auth->addItem("密码", "password");
    auth->addItem("私钥", "key");
    auth->addItem("SSH Agent", "agent");
    auth->setCurrentIndex(qMax(0, auth->findData(p.authentication)));
    auto environment = new StyledComboBox;
    environment->addItems({"测试", "生产", "个人"});
    environment->setCurrentText(p.environment);
    auto timeout = new QSpinBox;
    timeout->setRange(3, 60);
    timeout->setSuffix(" 秒");
    timeout->setValue(p.timeout);
    form->addRow("名称", name);
    form->addRow("主机", host);
    form->addRow("端口", port);
    form->addRow("用户名", user);
    form->addRow("认证方式", auth);
    form->addRow(secretLabel, secret);
    form->addRow({}, remember);
    auto keyRow = new QWidget;
    auto kl = new QHBoxLayout(keyRow);
    uiMargins(kl,0,0,0,0);
    kl->addWidget(key);
    auto browse = button("选择");
    kl->addWidget(browse);
    form->addRow("私钥文件", keyRow);
    form->addRow("分组", group);
    form->addRow("环境", environment);
    form->addRow("初始目录（. 为主目录）", dir);
    form->addRow("连接超时", timeout);
    form->addRow("备注", notes);
    auto formScroll = new QScrollArea;
    formScroll->setWidgetResizable(true);
    formScroll->setFrameShape(QFrame::NoFrame);
    formScroll->setWidget(formBody);
    layout->addWidget(formScroll, 1);
    auto info = new QLabel("密码与私钥口令不会写入配置文件。\n私钥内容不复制到应用目录。SSH Agent "
                           "支持取决于当前系统和密钥类型。");
    info->setWordWrap(true);
    info->setProperty("muted", true);
    layout->addWidget(info);
    auto error = new QLabel;
    error->setWordWrap(true);
    error->setStyleSheet("color:" + colors().red.name());
    layout->addWidget(error);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save);
    auto connectButton = buttons->addButton("保存并连接", QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    connect(browse, &QPushButton::clicked, &d, [&] {
        auto file = QFileDialog::getOpenFileName(&d, "选择私钥", QDir::homePath() + "/.ssh");
        if (!file.isEmpty())
            key->setText(file);
    });
    auto authChanged = [&] {
        keyRow->setEnabled(auth->currentData() == "key");
        secret->setEnabled(auth->currentData() != "agent");
        remember->setEnabled(auth->currentData() != "agent");
        secretLabel->setText(auth->currentData() == "key" ? "私钥口令" : "密码");
        remember->setText(auth->currentData() == "key" ? "保存私钥口令" : "保存密码");
        Profile candidate = p;
        candidate.host = host->text().trimmed();
        candidate.user = user->text().trimmed();
        candidate.port = port->value();
        candidate.authentication = auth->currentData().toString();
        candidate.keyPath = key->text();
        const bool retained = p.rememberSecret && candidate.credentialKey() == p.credentialKey();
        secret->setPlaceholderText(retained ? "已启用保存；留空保留系统库中的原密码"
                                   : candidate.authentication == "key" ? "无加密的私钥可留空"
                                                                       : "输入密码");
    };
    connect(auth, &QComboBox::currentIndexChanged, &d, authChanged);
    connect(host, &QLineEdit::textChanged, &d, authChanged);
    connect(user, &QLineEdit::textChanged, &d, authChanged);
    connect(key, &QLineEdit::textChanged, &d, authChanged);
    connect(port, &QSpinBox::valueChanged, &d, authChanged);
    authChanged();
    bool connectNow = false;
    std::optional<QByteArray> entered;
    auto save = [&](bool shouldConnect) {
        if(!OperationGate::accepts(lockEpoch))return;
        if (name->text().trimmed().isEmpty() || host->text().trimmed().isEmpty() ||
            user->text().trimmed().isEmpty()) {
            error->setText("请填写名称、主机和用户名。");
            return;
        }
        if (host->text().contains(QRegularExpression("[\\s/\\\\]")) ||
            user->text().contains(QRegularExpression("[\\r\\n]"))) {
            error->setText("主机或用户名包含无效字符。");
            return;
        }
        if (auth->currentData() == "key" && !QFileInfo::exists(key->text())) {
            error->setText("私钥文件不存在。");
            return;
        }
        Profile candidate = p;
        candidate.name = name->text().trimmed();
        candidate.host = host->text().trimmed();
        candidate.user = user->text().trimmed();
        candidate.port = port->value();
        candidate.authentication = auth->currentData().toString();
        candidate.keyPath = key->text();
        candidate.group = group->text().trimmed();
        candidate.environment = environment->currentText();
        candidate.directory = dir->text().isEmpty() ? "." : dir->text();
        candidate.notes = notes->text();
        candidate.timeout = timeout->value();
        candidate.rememberSecret = remember->isEnabled() && remember->isChecked();
        entered.reset();
        if (candidate.authentication != "agent" &&
            (!secret->text().isEmpty() ||
             (candidate.authentication == "key" &&
              (!p.rememberSecret || candidate.credentialKey() != p.credentialKey()))))
            entered = secret->text().toUtf8();
        buttons->setEnabled(false);
        formBody->setEnabled(false);
        d.saving = true;
        error->setText("正在保存…");
        repository.update(candidate, entered, &d,
                          [&, candidate, shouldConnect](bool ok, const QString &message) {
                              buttons->setEnabled(true);
                              formBody->setEnabled(true);
                              d.saving = false;
                              if (!ok) {
                                  error->setText(message);
                                  return;
                              }
                              p = candidate;
                              connectNow = shouldConnect;
                              d.accept();
                          });
    };
    connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, &d, [&] { save(false); });
    connect(connectButton, &QPushButton::clicked, &d, [&] { save(true); });
    d.resize(520, 550);
    if (d.exec() != QDialog::Accepted || !OperationGate::accepts(lockEpoch))
        return;
    if (connectNow)
        open(p, entered);
}
void MainWindow::open(const Profile &selectedProfile, std::optional<QByteArray> secret, const QString &bookmarkId, BookmarkOpen mode) {
    if(OperationGate::locked())return;
    const auto p=selectedProfile; // A credential dialog can mutate the repository in a nested event loop.
    if (auto w = workspaces.value(p.id)) {
        tabs->setCurrentWidget(w);
        if (w->profile.credentialKey() != p.credentialKey()) {
            AppMessageBox::information(
                this, "连接身份已更改",
                "旧工作区仍属于原服务器。请先关闭旧工作区，再连接新配置；关闭时会检查草稿与传输任务。");
            return;
        }
        w->updateCredentials(p, secret);
        if(!bookmarkId.isEmpty())w->openBookmark(bookmarkId,mode);
        else if (!w->isConnected())w->reconnect();
        return;
    }
    auto w = new Workspace(p, secret, false, nullptr, &repository, &bookmarks);
    workspaces[p.id] = w;
    trackWorkspaceTunnels(w);
    advancedSearch->registerWorkspace(w);
    tabs->addTab(w, "[" + p.environment + "] " + p.name);
    configureWorkspaceTab(w);
    tabs->setTabToolTip(tabs->indexOf(w), p.identity());
    tabs->setCurrentWidget(w);
    connect(w, &Workspace::stateChanged, this, [this, w] {
        int index = tabs->indexOf(w);
        if (index >= 0)
            tabs->setTabText(index, QString("%1 [%2] %3%4")
                                        .arg(w->isConnected() ? "●" : "○", w->profile.environment,
                                             w->profile.name, w->dirty() ? " *" : ""));
        refresh();
    });
    connect(w,&Workspace::readyToClose,this,[this,w]{workspaceSaveFinished(w,true);});
    connect(w,&Workspace::closeCancelled,this,[this,w]{workspaceSaveFinished(w,false);});
    if(!bookmarkId.isEmpty())w->openBookmark(bookmarkId,mode);
    if (auto saved = find(p.id)) {
        saved->lastUsed = QDateTime::currentDateTimeUtc();
        persist();
    }
}
void MainWindow::activateConnectionItem(QTreeWidgetItem *item) {
    if(!item)return;
    if(auto p=find(item->data(0,Qt::UserRole).toString())){const auto copy=*p;open(copy,std::nullopt,item->data(0,BookmarkIdRole).toString());}
}
void MainWindow::configureWorkspaceTab(Workspace *workspace) {
    const int index = tabs->indexOf(workspace);
    if (index < 1)
        return;
    tabs->setTabToolTip(index, workspace->profile.identity());
    auto close = iconButton(Icon::Close, "关闭工作区", tabs->tabBar(), IconSize::TabClose);
    close->setToolTip("关闭 " + workspace->profile.name + " 工作区");
    close->setAccessibleName(close->toolTip());
    QPointer<Workspace> guarded(workspace);
    connect(close, &QToolButton::clicked, this, [this, guarded] {
        if (guarded)
            closeWorkspace(guarded);
    });
    replaceTabButton(tabs->tabBar(), index, QTabBar::LeftSide, nullptr);
    replaceTabButton(tabs->tabBar(), index, QTabBar::RightSide, close);
    close->show();
}
void MainWindow::connectionMenu(QPoint p) {
    if(OperationGate::locked())return;
    auto item=connections->itemAt(p);if(!item)return;
    if(!item->isSelected()){connections->clearSelection();item->setSelected(true);connections->setCurrentItem(item);}
    if(item->parent()){
        const auto profileId=item->data(0,Qt::UserRole).toString(),id=item->data(0,BookmarkIdRole).toString();
        bookmarkMenu(this,&bookmarks,profileId,id,connections->viewport()->mapToGlobal(p),[this,profileId,id](BookmarkOpen mode){if(auto profile=find(profileId)){const auto copy=*profile;open(copy,std::nullopt,id,mode);}});return;
    }
    QStringList ids;QMenu m(this);
    for(auto selected:connections->selectedItems()){
        if(selected->parent()){m.addAction("请仅选择连接行进行批量操作")->setEnabled(false);m.exec(connections->viewport()->mapToGlobal(p));return;}
        ids<<selected->data(0,Qt::UserRole).toString();
    }
    if(ids.isEmpty())return;
    if(ids.size()>1)m.addAction("连接所选服务器",this,[this,ids]{for(const auto &id:ids)if(auto p=find(id)){const auto copy=*p;open(copy);}});
    if (ids.size() == 1) {
        QString id = ids[0];
        m.addAction("连接 / 切换工作区", this, [this, id] {
            if (auto p = find(id))
                open(*p);
        });
        if (auto w = workspaces.value(id))
            m.addAction("断开会话", w, &Workspace::disconnectHost);
        m.addSeparator();
        m.addAction("编辑连接", this, [this, id] { editProfile(id); });
        m.addAction("端口配置",this,[this,id]{configurePorts(id);});
        m.addAction("复制配置", this, [this, id] {
            if (auto existing = find(id)) {
                Profile p = existing->duplicate();
                p.name += " 副本";
                profiles << p;
                persist();
            }
        });
        m.addSeparator();
        m.addAction("收藏 / 取消收藏", this, [this, id] {
            if (auto p = find(id)) {
                p->favorite = !p->favorite;
                persist();
            }
        });
    } else m.addSeparator();
    m.addAction("移动到分组", this, [this, ids] {
        bool ok;
        auto group =
            QInputDialog::getText(this, "移动分组", "分组名称（留空为未分组）", QLineEdit::Normal, {}, &ok);
        if (ok) {
            for (auto id : ids)
                if (auto p = find(id))
                    p->group = group.trimmed();
            persist();
        }
    });
    m.addSeparator();
    if (ids.size()==1) {
        const auto id=ids.first();
        m.addAction("复制地址", this, [this, id] {
            if (auto p = find(id))
                QApplication::clipboard()->setText(p->user + "@" + p->host + ":" + QString::number(p->port));
        });
    }
    m.addAction("导出配置", this, [this, ids] {
        auto path = QFileDialog::getSaveFileName(this, "导出连接配置", {}, "JSON (*.json)");
        if (path.isEmpty())
            return;
        QJsonArray a;
        for (auto id : ids)
            if (auto p = find(id))
                a << p->exportedJson();
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) ||
            f.write(QJsonDocument(QJsonObject{{"version", 1}, {"connections", a}}).toJson()) < 0 ||
            !f.commit())
            AppMessageBox::warning(this, "导出失败", f.errorString());
    });
    m.addSeparator();
    m.addAction("删除连接配置…", this, [this, ids] {
        if (AppMessageBox::question(
                this, "删除配置",
                QString("删除所选 %1 个本地连接配置？\n不会删除远端文件；已打开的工作区保留到关闭。")
                    .arg(ids.size()),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return;
        for (const auto &id : ids)
            repository.remove(id, this, [this](bool ok, const QString &error) {
                if (!ok)
                    AppMessageBox::warning(this, "删除失败", error);
            });
    });
    m.exec(connections->viewport()->mapToGlobal(p));
}
void MainWindow::configurePorts(const QString &id) {
    if(OperationGate::locked())return;
    if(auto workspace=workspaces.value(id)){tabs->setCurrentWidget(workspace);workspace->showPorts();return;}
    auto value=find(id);if(!value)return;
    QDialog dialog(this);dialog.setWindowTitle("端口配置 · "+value->name);dialog.resize(620,360);
    auto layout=new QVBoxLayout(&dialog);auto panel=new PortsPanel(id,true);layout->addWidget(panel,1);
    connect(panel,&PortsPanel::configurationChanged,&dialog,[this,id,panel] {
        updateConnectionTunnels(id,panel->tunnelCounts());
    });
    auto close=new QDialogButtonBox(QDialogButtonBox::Close);layout->addWidget(close);
    connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QString requested;
    connect(panel,&PortsPanel::startRequested,&dialog,[&](const TunnelRule &rule){requested=rule.id;dialog.accept();});
    const auto lockEpoch=OperationGate::token();dialog.exec();
    if(!OperationGate::accepts(lockEpoch))return;
    if(!requested.isEmpty())if(auto current=find(id)){
        open(*current);
        if(auto workspace=workspaces.value(id)){
            workspace->showPorts();
            QTimer::singleShot(0,workspace,[workspace,requested,lockEpoch]{if(OperationGate::accepts(lockEpoch))workspace->startPortRule(requested);});
        }
    }
}
void MainWindow::settings(int initialCategory) {
    if(OperationGate::locked())return;
    QDialog d(this);
    d.setProperty("preservePortDraft",true);
    d.setWindowTitle("设置");
    auto outer = new QVBoxLayout(&d);uiMargins(outer,0,0,0,12);uiSpacing(outer,0);
    auto body=new QHBoxLayout;uiMargins(body,0,0,0,0);uiSpacing(body,0);outer->addLayout(body,1);
    auto sidebar=new QWidget;sidebar->setObjectName("settingsSidebar");uiFixedWidth(sidebar,180);body->addWidget(sidebar);
    auto sidebarLayout=new QVBoxLayout(sidebar);uiMargins(sidebarLayout,10,16,10,12);uiSpacing(sidebarLayout,12);
    auto sidebarTitle=new QLabel("设置");sidebarTitle->setProperty("heading",true);sidebarLayout->addWidget(sidebarTitle);
    auto categories=new QListWidget;categories->setObjectName("settingsCategories");
    const QList<QPair<QString,Icon>> categoryItems{{"外观与缩放",Icon::Settings},{"内容字体",Icon::Path},{"安全",Icon::Lock},{"文件编辑",Icon::Path},{"存储与日志",Icon::Info},{"重置",Icon::Refresh},{"更新",Icon::DownloadFile}};
    for(const auto &[name,glyph]:categoryItems)categories->addItem(new QListWidgetItem(icon(glyph),name));
    categories->setIconSize(uiSize(16,16));categories->setSpacing(uiPx(2));sidebarLayout->addWidget(categories,1);
    auto divider=new QWidget;setFrameRole(divider,"divider");uiFixedWidth(divider,1);body->addWidget(divider);
    auto pages=new QStackedWidget;pages->setObjectName("settingsPages");body->addWidget(pages,1);
    auto makePage=[pages](){auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setFrameShape(QFrame::NoFrame);auto content=new QWidget;content->setMinimumWidth(0);scroll->setWidget(content);auto l=new QVBoxLayout(content);uiMargins(l,24,24,24,20);uiSpacing(l,16);pages->addWidget(scroll);return l;};
    auto fontsLayout=makePage();auto fonts=new FontSettings;fontsLayout->addWidget(fonts);fontsLayout->addStretch();
    auto makeCard=[](QVBoxLayout *parent){auto card=new QWidget;setFrameRole(card,"card");auto layout=new QVBoxLayout(card);uiMargins(layout,16,16,16,16);uiSpacing(layout,12);parent->addWidget(card);return layout;};
    auto securityLayout=makePage();auto securityCard=makeCard(securityLayout);if(auto lock=AppLock::instance())lock->addSettings(securityCard,&d);securityLayout->addStretch();
    auto editLayout=makePage();auto editTitle=new QLabel("文件编辑");editTitle->setProperty("heading",true);editLayout->addWidget(editTitle);
    auto editCard=makeCard(editLayout);auto limitLabel=new QLabel("最大可编辑文件大小");editCard->addWidget(limitLabel);
    auto editLimit=new QSpinBox;editLimit->setObjectName("editorMaxFileMiB");editLimit->setRange(1,editorlimits::maximumMiB);editLimit->setSuffix(" MiB");editLimit->setValue(editorlimits::sizeMiB());editCard->addWidget(editLimit,0,Qt::AlignLeft);
    auto editHelp=new QLabel("限制针对远端读取的原始字节。较大的文件会增加本机内存占用。");editHelp->setWordWrap(true);editCard->addWidget(editHelp);editLayout->addStretch();
    connect(editLimit,qOverload<int>(&QSpinBox::valueChanged),&d,[](int value){QSettings().setValue("editor/maxFileMiB",value);});
    connect(&UiPreferences::instance(),&UiPreferences::defaultsRestored,&d,[editLimit]{QSignalBlocker block(editLimit);editLimit->setValue(editorlimits::sizeMiB());});
    auto l=makePage();
    auto logHeading=new QHBoxLayout;auto logTitle=new QLabel("存储与日志");logTitle->setProperty("heading",true);logHeading->addWidget(logTitle);logHeading->addStretch();auto clearCache=button("清理缓存");clearCache->setObjectName("clearApplicationCache");logHeading->addWidget(clearCache);auto refreshUsage=iconButton(Icon::Refresh,"刷新空间统计");refreshUsage->setObjectName("refreshStorageUsage");logHeading->addWidget(refreshUsage);l->addLayout(logHeading);
    auto storageCard=makeCard(l);storageCard->addWidget(new QLabel("配置数据目录"));
    auto currentData=new WrappedSettingsPath(QDir::toNativeSeparators(dataPath()));currentData->setObjectName("storageCurrentPath");storageCard->addWidget(currentData);
    auto dataUsage=new QLabel("配置与本地缓存：统计中…");dataUsage->setObjectName("storageDataUsage");dataUsage->setWordWrap(true);storageCard->addWidget(dataUsage);
    auto targetPath=new QLineEdit;targetPath->setReadOnly(true);targetPath->setObjectName("storageTargetPath");targetPath->setPlaceholderText("选择空目录");storageCard->addWidget(targetPath);
    auto storageButtons=new QHBoxLayout;auto chooseStorage=button("选择目录"),moveStorage=button("迁移并重启");chooseStorage->setObjectName("chooseStorageDirectory");moveStorage->setObjectName("migrateStorage");storageButtons->addWidget(chooseStorage);storageButtons->addWidget(moveStorage);storageButtons->addStretch();storageCard->addLayout(storageButtons);
    auto storageHint=new QLabel("迁移前将按正常退出流程处理未保存内容和任务，完成后重启。下载文件和外部私钥不会移动。");storageHint->setWordWrap(true);storageHint->setProperty("muted",true);storageCard->addWidget(storageHint);
    if(storageOverride()){chooseStorage->setEnabled(false);moveStorage->setEnabled(false);storageHint->setText("当前使用独立数据目录，配置目录迁移不可用。");}
    connect(chooseStorage,&QPushButton::clicked,&d,[&d,targetPath]{auto path=QFileDialog::getExistingDirectory(&d,"选择空的配置数据目录");if(!path.isEmpty())targetPath->setText(QDir::cleanPath(path));});
    moveStorage->setEnabled(moveStorage->isEnabled()&&!updates->restartPending());
    connect(moveStorage,&QPushButton::clicked,&d,[this,&d,targetPath]{if(updates->restartPending()){AppMessageBox::information(&d,"无法迁移","请先取消重启更新。");return;}QString error;const auto target=targetPath->text();if(!validateStorageTarget(dataPath(),target,&error)){AppMessageBox::warning(&d,"无法迁移",error);return;}migrationTarget=target;updates->setMigrationPending(true);d.accept();QTimer::singleShot(0,this,[this]{requestExit();if(!exiting&&exitSavePending.isEmpty()&&!exitAfterCredentials&&!finalExit){migrationTarget.clear();updates->setMigrationPending(false);}});});
    auto logCard=makeCard(l);
    auto logPath=new WrappedSettingsPath(QDir::toNativeSeparators(DiagnosticLog::directory()));
    logPath->setObjectName("diagnosticLogPath");
    auto logEnabled=new QCheckBox("启用诊断与崩溃日志");logEnabled->setObjectName("diagnosticLoggingEnabled");logEnabled->setChecked(DiagnosticLog::instance().isRunning());logCard->addWidget(logEnabled);auto logDescription=new QLabel("默认关闭；开启后记录有限的诊断信息用于排查问题。");logDescription->setWordWrap(true);logDescription->setProperty("muted",true);logCard->addWidget(logDescription);logCard->addWidget(logPath);
    auto logUsage=new QLabel("日志占用：统计中…");logUsage->setObjectName("storageLogUsage");logUsage->setWordWrap(true);logCard->addWidget(logUsage);
    auto usageNote=new QLabel("按文件大小合计应用配置、草稿、本地缓存及文件形式的偏好；系统注册表不计。日志仅含本应用的诊断、崩溃和退出日志。不跟随链接；受权限或数量限制时显示部分结果。两项可能位于同一目录，不相加。");usageNote->setObjectName("storageUsageScope");usageNote->setWordWrap(true);usageNote->setProperty("muted",true);l->addWidget(usageNote);
    auto usage=new StorageUsageController(dataUsage,logUsage,&d);
    auto refreshSpace=[usage]{usage->refresh(dataPath(),QSettings().fileName(),DiagnosticLog::directory());};
    connect(refreshUsage,&QToolButton::clicked,&d,refreshSpace);
    auto logs=new QHBoxLayout;auto chooseLogs=button("选择日志目录"),openLogs=button("打开日志目录"),copyLogs=button("复制日志路径");chooseLogs->setObjectName("chooseDiagnosticLogDirectory");
    openLogs->setObjectName("openDiagnosticLogs");copyLogs->setObjectName("copyDiagnosticLogPath");
    logs->addWidget(chooseLogs);logs->addWidget(openLogs);logs->addWidget(copyLogs);logs->addStretch();logCard->addLayout(logs);
    auto cleanup=new CacheCleanupController(&d);
    connect(clearCache,&QPushButton::clicked,&d,[cleanup]{cleanup->start(dataPath(),DiagnosticLog::directory());});
    connect(cleanup,&CacheCleanupController::busyChanged,&d,[=](bool busy){
        clearCache->setEnabled(!busy);refreshUsage->setEnabled(!busy);logEnabled->setEnabled(!busy);chooseLogs->setEnabled(!busy);
        chooseStorage->setEnabled(!busy&&!storageOverride());moveStorage->setEnabled(!busy&&!storageOverride()&&!updates->restartPending());
        if(busy)usage->leave();
    });
    connect(cleanup,&CacheCleanupController::finished,&d,[=]{if(categories->currentRow()==4&&!OperationGate::locked())refreshSpace();});
    connect(updates,&UpdateController::changed,&d,[this,cleanup,moveStorage]{moveStorage->setEnabled(!cleanup->isBusy()&&!storageOverride()&&!updates->restartPending());});
    connect(categories,&QListWidget::currentRowChanged,&d,[=](int row){if(row==4){if(!cleanup->isBusy())refreshSpace();}else{usage->leave();cleanup->cancel();}});
    if(auto lock=AppLock::instance())connect(lock,&AppLock::lockedChanged,&d,[cleanup](bool locked){if(locked)cleanup->cancel();});
    connect(logEnabled,&QCheckBox::clicked,&d,[&d,logEnabled](bool enabled){
        QString error;auto &log=DiagnosticLog::instance();const bool wasRunning=log.isRunning();
        QSettings s;const bool had=s.contains("storage/logEnabled");const auto old=s.value("storage/logEnabled");
        if(enabled&&!log.startLogging(&error)){QSignalBlocker block(logEnabled);logEnabled->setChecked(wasRunning);AppMessageBox::warning(&d,"无法启用日志",error);return;}
        if(!enabled)log.stopLogging();s.setValue("storage/logEnabled",enabled);s.sync();
        if(s.status()!=QSettings::NoError){
            QSettings restore;if(had)restore.setValue("storage/logEnabled",old);else restore.remove("storage/logEnabled");restore.sync();
            if(wasRunning&&!log.isRunning())log.startLogging();else if(!wasRunning&&log.isRunning())log.stopLogging();
            QSignalBlocker block(logEnabled);logEnabled->setChecked(log.isRunning());AppMessageBox::warning(&d,"设置未保存","无法保存日志设置，请检查配置目录权限。");
        }
    });
    connect(chooseLogs,&QPushButton::clicked,&d,[&d,logPath,logEnabled,refreshSpace]{
        auto path=QFileDialog::getExistingDirectory(&d,"选择日志目录",DiagnosticLog::directory());if(path.isEmpty())return;
        QString error;if(!DiagnosticLog::validateDirectory(path,&error)){AppMessageBox::warning(&d,"无法使用日志目录",error);return;}
        auto &log=DiagnosticLog::instance();const bool running=log.isRunning();QSettings s;const bool had=s.contains("storage/logDirectory");const auto old=s.value("storage/logDirectory");
        auto restore=[&]{if(log.isRunning())log.stopLogging();QSettings previous;if(had)previous.setValue("storage/logDirectory",old);else previous.remove("storage/logDirectory");previous.sync();if(running)log.startLogging();QSignalBlocker block(logEnabled);logEnabled->setChecked(log.isRunning());};
        if(running)log.stopLogging();s.setValue("storage/logDirectory",path);
        if(running&&!log.startLogging(&error)){restore();AppMessageBox::warning(&d,"无法使用日志目录",error);return;}
        s.sync();if(s.status()!=QSettings::NoError){restore();AppMessageBox::warning(&d,"设置未保存","无法保存日志目录，请检查配置目录权限。");return;}
        logPath->setPath(QDir::toNativeSeparators(DiagnosticLog::directory()));
        refreshSpace();
    });
    connect(openLogs,&QPushButton::clicked,&d,[]{const auto path=DiagnosticLog::directory();QDir().mkpath(path);QDesktopServices::openUrl(QUrl::fromLocalFile(path));});
    connect(copyLogs,&QPushButton::clicked,&d,[]{QApplication::clipboard()->setText(QDir::toNativeSeparators(DiagnosticLog::directory()));});
    l->addStretch();auto resetLayout=makePage();auto resetTitle=new QLabel("重置");resetTitle->setProperty("heading",true);resetLayout->addWidget(resetTitle);auto resetCard=makeCard(resetLayout);resetCard->addWidget(new QLabel("将界面偏好恢复默认设置。"));
    auto updateLayout=makePage();updateLayout->addWidget(updates->createSettingsPage());
    auto done = button("完成");
    auto bottom=new QHBoxLayout;auto reset=button("重置所有设置");reset->setObjectName("resetAllSettings");
    resetCard->addWidget(reset,0,Qt::AlignLeft);resetLayout->addStretch();
    uiMargins(bottom,12,8,12,0);bottom->addStretch();bottom->addWidget(done);outer->addLayout(bottom);
    connect(categories,&QListWidget::currentRowChanged,&d,[pages,fonts](int row){if(row<0)return;fonts->showZoomCategory(row==0);pages->setCurrentIndex(row<2?0:row-1);});
    categories->setCurrentRow(qBound(0,initialCategory,categories->count()-1));
    connect(reset,&QPushButton::clicked,&d,[this,&d]{
        AppMessageBox confirm(QMessageBox::Warning,"重置所有设置",
            "恢复主题、字体、界面缩放及本地界面偏好（列宽、网卡、隐藏文件、下载位置、窗口记忆）。\n"
            "保留连接、分组、密码、端口规则、服务器信任记录、打开文件、终端进程和传输。\n"
            "当前窗口恢复默认大小；后续关闭时仍会正常保存窗口位置。",
            QMessageBox::Ok|QMessageBox::Cancel,&d);
        confirm.setObjectName("resetAllConfirmation");confirm.setDefaultButton(QMessageBox::Cancel);
        confirm.setEscapeButton(QMessageBox::Cancel);confirm.button(QMessageBox::Ok)->setText("重置所有设置");
        if(confirm.exec()!=QMessageBox::Ok)return;
        UiPreferences::instance().reset();
        if(isMaximized()||isFullScreen())showNormal();resize(1440,900);keepWindowOnScreen(this);
    });
    connect(done, &QPushButton::clicked, &d, &QDialog::accept);
    const auto available = screen()->availableGeometry().size() - QSize(48,80);
    d.resize(qMin(720,available.width()),qMin(760,available.height()));
    d.exec();
}
void MainWindow::closeWorkspace(Workspace *w, bool approved) {
    if(OperationGate::locked())return;
    if (!w || stopping.contains(w))
        return;
    if (!approved && !w->requestClose()) {
        if (!w->waitingToClose()) {
            exiting = false; migrationTarget.clear();updates->setMigrationPending(false);emit exitAborted();
        }
        return;
    }
    stopping.insert(w);
    connect(w, &Workspace::shutdownComplete, this, [this, w] {
        stopping.remove(w);
        workspaces.remove(w->profile.id);
        auto counts=w->tunnelCounts();
        if (counts) counts->running=0;
        updateConnectionTunnels(w->profile.id,counts);
        tabs->removeTab(tabs->indexOf(w));
        w->deleteLater();
        refresh();
        if (exiting)
            QTimer::singleShot(0, this, &MainWindow::continueExit);
    });
    w->shutdown();
}
void MainWindow::continueExit() {
    if(OperationGate::locked())return;
    if (workspaces.isEmpty()) {
        exiting = false;
        finalExit=true;emit exitCompleted();if(tray)tray->hide();qApp->setQuitOnLastWindowClosed(true);
        close();
        return;
    }
    auto w = workspaces.begin().value();
    tabs->setCurrentWidget(w);
    closeWorkspace(w);
}
void MainWindow::hideToTray(){
    trayWindows.clear();trayModal=QApplication::activeModalWidget();
    for(auto w:QApplication::topLevelWidgets())if(w!=this&&w->isVisible()){
        for(auto parent=w->parentWidget();parent;parent=parent->parentWidget())if(parent==this){trayWindows.append(w);break;}
    }
    for(auto w:trayWindows)if(w)w->hide();hide();
}
void MainWindow::showFromTray(){
    show();if(isMinimized())showNormal();raise();activateWindow();
    const auto windows=std::exchange(trayWindows,QList<QPointer<QWidget>>{});for(auto w:windows)if(w)w->show();
    auto modal=trayModal?trayModal.data():QApplication::activeModalWidget();if(modal&&modal->isVisible()){modal->raise();modal->activateWindow();}trayModal.clear();
    if(OperationGate::locked())if(auto lock=AppLock::instance())lock->activate();
}
void MainWindow::requestExit(){
    if(finalExit||exiting||!exitSavePending.isEmpty())return;
    const auto clearAbortedMigration=qScopeGuard([this]{if(!exiting&&!finalExit&&exitSavePending.isEmpty()&&!exitAfterCredentials){migrationTarget.clear();updates->setMigrationPending(false);emit exitAborted();}});
    if(OperationGate::locked()){showFromTray();return;}
    if(repository.pending()) {exitAfterCredentials=true;exitAwaitingUpdate=updates->restartPending();statusBar()->showMessage("正在完成系统凭据存储，请稍候再退出。");return;}
    showFromTray();if(auto modal=QApplication::activeModalWidget();modal&&modal!=this){modal->raise();modal->activateWindow();return;}
    const auto epoch=OperationGate::token();const bool updateExit=updates->restartPending();
    QHash<Workspace *,QByteArray> signatures;int dirty=0,transfers=0;
    for(auto w:workspaces){signatures.insert(w,w->closeSignature());dirty+=w->dirtyFileCount();transfers+=w->pendingTransfers();}
    QString summary="是否关闭 FrogShell？";if(dirty)summary+=QString("\n%1 个未保存文件").arg(dirty);if(transfers)summary+=QString("\n%1 个未完成的传输").arg(transfers);
    const auto choices=dirty?QMessageBox::StandardButtons(QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel):QMessageBox::StandardButtons(QMessageBox::Close|QMessageBox::Cancel);
    AppMessageBox confirm(QMessageBox::Warning,"退出 FrogShell",summary,choices,this);
    if(dirty){confirm.button(QMessageBox::Save)->setText("保存并退出");confirm.button(QMessageBox::Discard)->setText("不保存并退出");}
    confirm.setDefaultButton(QMessageBox::Cancel);confirm.exec();
    const auto choice=confirm.standardButton(confirm.clickedButton());
    if(!OperationGate::accepts(epoch)||(updateExit&&!updates->restartPending())||(choice!=QMessageBox::Close&&choice!=QMessageBox::Save&&choice!=QMessageBox::Discard))return;
    if(signatures.size()!=workspaces.size()){AppMessageBox::information(this,"状态已变化","工作区已变化，请重新确认退出。");return;}
    for(auto w:workspaces)if(!signatures.contains(w)||!w->canApproveExit(signatures.value(w))){AppMessageBox::information(this,"状态已变化","文件、保存或传输状态已变化，请重新确认退出。");return;}
    if(choice==QMessageBox::Save){
        exitSaveEpoch=epoch;exitSaveForUpdate=updateExit;exitSaveFailed=false;exitTransfers.clear();
        for(auto w:workspaces){exitTransfers[w]=w->transferSignature();if(w->dirty())exitSavePending.insert(w);}
        const auto pending=exitSavePending;for(auto w:pending)if(!w->saveForExit())workspaceSaveFinished(w,false);
        if(!exitSavePending.isEmpty())statusBar()->showMessage("正在保存文件，完成后退出…");
        return;
    }
    for(auto w:workspaces)w->discardExitDrafts();finishConfirmedExit();
}
void MainWindow::workspaceSaveFinished(Workspace *workspace,bool success){
    if(!exitSavePending.contains(workspace)){if(success)closeWorkspace(workspace,true);else exiting=false;return;}
    exitSavePending.remove(workspace);if(!success)exitSaveFailed=true;
    if(!exitSavePending.isEmpty())return;
    if(!OperationGate::accepts(exitSaveEpoch)||(exitSaveForUpdate&&!updates->restartPending())||exitTransfers.size()!=workspaces.size())exitSaveFailed=true;
    exitSaveForUpdate=false;
    for(auto w:workspaces)if(!exitTransfers.contains(w)||w->dirty()||w->transferSignature()!=exitTransfers.value(w)||!w->canApproveExit(w->closeSignature()))exitSaveFailed=true;
    exitTransfers.clear();if(exitSaveFailed){migrationTarget.clear();updates->setMigrationPending(false);emit exitAborted();statusBar()->showMessage("保存未全部完成或状态已变化；窗口和草稿已保留。");return;}
    finishConfirmedExit();
}
void MainWindow::finishConfirmedExit(){
    exiting=true;QSettings().setValue("window/geometry",saveGeometry());
    const auto targets=workspaces.values();for(auto w:targets)closeWorkspace(w,true);
    if(targets.isEmpty())continueExit();
}
void MainWindow::closeEvent(QCloseEvent *e) {
    if(finalExit){QMainWindow::closeEvent(e);return;}
    e->ignore();if(tray&&tray->isVisible()&&QSystemTrayIcon::isSystemTrayAvailable())hideToTray();else requestExit();
}
void MainWindow::importConfig() {
    if(OperationGate::locked())return;
    auto file =
        QFileDialog::getOpenFileName(this, "导入 SSH config 或 FrogShell JSON", QDir::homePath() + "/.ssh");
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024) {
        AppMessageBox::warning(this, "无法导入", "无法读取文件，或文件超过 1 MiB。");
        return;
    }
    auto bytes = f.readAll();
    QVector<Profile> incoming;
    QStringList warnings;
    auto json = QJsonDocument::fromJson(bytes);
    if (json.isObject()) {
        for (auto v : json.object()["connections"].toArray()) {
            auto p = Profile::fromJson(v.toObject());
            p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            p.rememberSecret = false;
            incoming << p;
        }
    } else {
        Profile current;
        bool has = false, bad = false;
        auto finish = [&] {
            if (has) {
                if (!bad && !current.host.isEmpty() && !current.user.isEmpty())
                    incoming << current;
                else
                    warnings << current.name + "：已跳过（配置复杂、通配符或缺少用户）";
            }
        };
        for (auto line : QString::fromUtf8(bytes).split('\n')) {
            line = line.trimmed();
            if (line.isEmpty() || line.startsWith('#'))
                continue;
            auto tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            auto key = tokens.takeFirst().toLower();
            QString value = tokens.join(' ');
            if (value.startsWith('"') && value.endsWith('"'))
                value = value.mid(1, value.size() - 2);
            if (key == "host") {
                finish();
                current = Profile();
                current.name = value;
                current.host = value;
                has = true;
                bad = tokens.size() != 1 || value.contains('*') || value.contains('?') || value.contains('!');
            } else if (has) {
                if (key == "hostname")
                    current.host = value;
                else if (key == "user")
                    current.user = value;
                else if (key == "port")
                    current.port = value.toInt();
                else if (key == "identityfile") {
                    current.authentication = "key";
                    current.keyPath = value.startsWith("~/") ? QDir::homePath() + value.mid(1) : value;
                } else {
                    bad = true;
                    warnings << current.name + "：暂不支持 " + key;
                }
            } else
                warnings << "全局配置未应用：" + key;
        }
        finish();
    }
    QStringList preview;
    for (const auto &p : incoming)
        preview << p.name + " — " + p.user + "@" + p.host + ":" + QString::number(p.port);
    if (incoming.isEmpty()) {
        AppMessageBox::information(this, "导入结果", "没有可导入的完整连接。\n" + warnings.join('\n'));
        return;
    }
    if (AppMessageBox::question(
            this, "导入预览",
            preview.join('\n') + "\n\n相同主机、端口、用户的配置将跳过。\n" + warnings.join('\n'),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    for (const auto &p : incoming) {
        bool duplicate = false;
        for (const auto &existing : profiles)
            if (p.host == existing.host && p.user == existing.user && p.port == existing.port)
                duplicate = true;
        if (!duplicate && !p.host.isEmpty() && !p.user.isEmpty() && p.port > 0 && p.port <= 65535)
            profiles << p;
    }
    persist();
}
} // namespace rs
