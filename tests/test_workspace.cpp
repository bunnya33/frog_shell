#include "../src/ui/workspace.h"
#include "../src/ui/theme.h"
#include "../src/ui/connectionnotice.h"
#include "../src/ui/fileicons.h"
#include "../src/ui/fileclipboard.h"
#include "../src/ui/uipreferences.h"
#include "../src/ui/windowstyle.h"
#include "../src/core/diagnostics.h"
#include <QClipboard>
#include <thread>
#include <QTcpServer>
#include <QtTest>
namespace rs {
class ExposedFileView : public FileView { public: using FileView::startDrag; };
class WorkspacePopupEvents : public QObject {
  public:
    int shows = 0, hides = 0;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type()==QEvent::Show) ++shows;
        if (event->type()==QEvent::Hide) ++hides;
        return false;
    }
};
class WorkspaceTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    void settle() {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
    Profile profile(QString id = "workspace-fixture") { Profile p; p.id = id; return p; }
    QVariantMap entry(QString path, bool folder = false, bool link = false) {
        return {{"path", path}, {"name", path.section('/', -1)}, {"directory", folder},
                {"link", link}, {"size", 42}, {"modified", 12345}};
    }
    void enableFiles(Workspace &w) {
        w.demo = false; w.online = true; w.directory = "/root";
        w.files->clear(); w.updateBreadcrumbs();
        w.session = new Session(w.profile, {}, &w); // queued-only worker; never started / no server
        w.attachSession(w.session);
        w.session->authenticatedHostIdentity("SHA256:synthetic");
        w.resize(1100, 720); w.show(); w.activateWindow(); settle();
    }
    QList<Job> takeJobs(Workspace &w) {
        QList<Job> result;
        while (!w.session->jobs.empty()) {
            result.append(w.session->jobs.front()); w.session->jobs.pop_front();
        }
        w.session->queuedJobBytes = 0;
        return result;
    }
    void deliver(Workspace &w, const QString &path, const QVariantList &entries, bool navigation = false) {
        w.requestDirectory(path, navigation);
        const auto id = w.nextJob - 1;
        w.session->completed("list", id, {{"path", path}, {"entries", entries}});
        settle();
    }
    QByteArray network(const QStringList &names) {
        QByteArray result = "__CPU__\ncpu 1 1 1 50\n__MEM__\nMemTotal: 1000 kB\nMemAvailable: 500 kB\n"
                            "__UP__\n100 0\n__NET__\n";
        for (const auto &name : names)
            result += name.toUtf8() + ": 1000 1 0 0 0 0 0 0 2000 1 0 0 0 0 0 0\n";
        return result + "__DISK__\n__PROC__\nPID USER %CPU RSS STAT COMMAND\n";
    }
    void verifyConnectionLogo(ConnectionNotice *notice,int baseSize) {
        auto label=notice->findChild<QLabel *>("connectionLogo"); QVERIFY(label);
        QVERIFY(!label->accessibleName().isEmpty());
        const auto pixmap=label->pixmap(); QVERIFY(!pixmap.isNull());
        const auto expected=applicationIcon().pixmap(uiSize(baseSize,baseSize),label->devicePixelRatioF());
        QCOMPARE(pixmap.toImage(),expected.toImage());
        const auto image=pixmap.toImage(); QVERIFY(image.hasAlphaChannel());
        QCOMPARE(image.pixelColor(0,0).alpha(),0);
    }
  private slots:
    void initTestCase() {
        QVERIFY(data.isValid());
        QCOMPARE(QGuiApplication::platformName(), QString("offscreen"));
        qputenv("RABBITSHELL_DATA_DIR", data.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellWorkspaceTests");
        QCoreApplication::setApplicationName("RabbitShellWorkspaceTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data.path());
        QApplication::setStyle("Fusion");
        for (auto font : {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/consola.ttf"})
            if (QFileInfo::exists(font)) QFontDatabase::addApplicationFont(font);
        QApplication::setFont(QFont("Microsoft YaHei UI", 10));
        applyTheme(false);
    }
    void init() { QSettings().clear(); }
    void workspaceLayoutRestoresOnlySystemPages() {
        const auto p = profile("layout-system");
        {
            Workspace w(p, {}, false); w.demo = true; w.resize(1100,720); w.show(); settle();
            QVERIFY(w.terminalMaximized); QVERIFY(w.editorPanel->isHidden());
            w.demo = false; w.showProcessPage(); w.showListeningPage(); w.demo = true;
            QVERIFY(!w.terminalMaximized);
            w.editArea->activatePage(w.processPage);
            auto file = new Editor(p.id,"/fixture.txt",QByteArray("fixture"),QByteArray());
            w.editors[file->path] = file; w.editArea->addPage(file,"fixture.txt");
            w.verticalSplit->setSizes({650,350}); settle();
            QVERIFY(w.groupFor(file));
        }
        {
            Workspace other(profile("layout-other"), {}, false); other.demo = true;
            QVERIFY(other.terminalMaximized); QVERIFY(!other.processPage); QVERIFY(!other.listeningPage);
        }
        {
            Workspace w(p, {}, false); w.demo = true; w.resize(1100,720); w.show(); settle();
            QVERIFY(w.groupFor(w.processPage)); QVERIFY(w.groupFor(w.listeningPage));
            QVERIFY(w.editors.isEmpty()); QVERIFY(!w.terminalMaximized);
            QCOMPARE(w.editArea->activeGroup()->currentWidget(), w.processPage);
            const auto sizes = w.verticalSplit->sizes();
            QVERIFY(sizes.size()==2 && sizes[0]>0 && sizes[1]>0);
            QVERIFY(qAbs(qreal(sizes[1])/(sizes[0]+sizes[1])-0.35)<0.08);
            w.closePage(w.processPage); w.closePage(w.listeningPage);
        }
        {
            Workspace w(p, {}, false); w.demo = true;
            QVERIFY(w.terminalMaximized); QVERIFY(!w.processPage); QVERIFY(!w.listeningPage);
        }
    }
    void workspaceFilePresentationAndReconnection() {
        const auto p = profile("layout-file");
        {
            Workspace w(p, {}, false); w.demo = true; w.resize(1100,720); w.show(); settle();
            QVERIFY(w.terminalMaximized);
            w.online = true;
            w.session = new Session(p, {}, &w); w.attachSession(w.session);
            w.openFile("/async.txt"); const int id = w.nextJob-1;
            w.session->completed("read", id, {{"path","/async.txt"},{"data",QByteArray("original")},{"permissions",0644}});
            auto file = w.editors.value("/async.txt"); QVERIFY(file); QVERIFY(!w.terminalMaximized);
            file->code->insertPlainText("draft"); const auto contents = file->code->toPlainText();
            w.setTerminalMaximized(true); w.openFile("/async.txt"); QVERIFY(!w.terminalMaximized);
            w.setTerminalMaximized(true);
            w.editArea->layoutChanged(); settle(); QVERIFY(w.terminalMaximized);
            w.session->disconnected("synthetic"); w.reconnect(); settle();
            QCOMPARE(w.editors.value("/async.txt"), file); QCOMPARE(file->code->toPlainText(),contents);
            QVERIFY(w.terminalMaximized);
        }
        {
            Workspace w(p, {}, false); w.demo = true;
            QVERIFY(w.terminalMaximized); QVERIFY(w.editors.isEmpty());
        }
    }
    void emptyEditorAreaMaximizesExistingBottomPanel() {
        const auto p = profile("layout-empty-editor");
        Workspace w(p, {}, false); w.demo = true; w.resize(1100,720); w.show(); settle();
        QVERIFY(w.terminals.isEmpty());
        auto first = new Editor(p.id,"/first.txt",QByteArray("one"),QByteArray());
        auto second = new Editor(p.id,"/second.txt",QByteArray("two"),QByteArray());
        w.editors[first->path] = first; w.editArea->addPage(first,"first.txt");
        w.editors[second->path] = second; w.editArea->addPage(second,"second.txt");
        w.editArea->splitActive(Qt::Horizontal); settle();
        QCOMPARE(w.editArea->groups().size(), 2); QVERIFY(!w.terminalMaximized);
        WorkspacePopupEvents visibility; w.editorPanel->installEventFilter(&visibility);
        w.editArea->mergeAll(); settle();
        QVERIFY(!w.terminalMaximized); QCOMPARE(visibility.hides, 0);
        w.editArea->splitActive(Qt::Horizontal); settle();
        w.closePage(first); settle();
        QVERIFY(w.groupFor(second)); QVERIFY(!w.terminalMaximized); QCOMPARE(visibility.hides, 0);
        w.verticalSplit->setSizes({600, 300}); settle();
        const auto before = w.verticalSplit->sizes();
        w.bottomTabs->setCurrentIndex(2);
        w.closePage(second); settle();
        QVERIFY(w.terminalMaximized); QCOMPARE(w.bottomTabs->currentIndex(), 2);
        QVERIFY(w.terminals.isEmpty());
        auto reopened = new Editor(p.id,"/reopened.txt",QByteArray("again"),QByteArray());
        w.editors[reopened->path] = reopened; w.editArea->addPage(reopened,"reopened.txt"); settle();
        QVERIFY(!w.terminalMaximized); QCOMPARE(w.verticalSplit->sizes(), before);
        QCOMPARE(w.bottomTabs->currentIndex(), 2);
        w.bottomTabs->setCurrentIndex(1);
        w.floatPage(reopened); settle(); QVERIFY(w.terminalMaximized); QCOMPARE(w.bottomTabs->currentIndex(), 1);
        w.openFile(reopened->path); settle(); QVERIFY(w.terminalMaximized);
        w.movePage(reopened); settle(); QVERIFY(!w.terminalMaximized); QVERIFY(w.groupFor(reopened));
        w.closePage(reopened); settle();
        QVERIFY(w.terminalMaximized);
        w.demo = false; w.showProcessPage();
        QVERIFY(!w.terminalMaximized); w.closePage(w.processPage); settle();
        QVERIFY(w.terminalMaximized); QVERIFY(w.terminals.isEmpty());
        w.showListeningPage(); QVERIFY(!w.terminalMaximized);
        w.floatPage(w.listeningPage); settle(); QVERIFY(w.terminalMaximized);
        w.showListeningPage(); QVERIFY(w.terminalMaximized);
        settle(); QVERIFY(w.terminalMaximized);
        w.movePage(w.listeningPage); QVERIFY(!w.terminalMaximized);
        w.floatPage(w.listeningPage); w.showListeningPage(); settle();
        QVERIFY(w.terminalMaximized);
        w.movePage(w.listeningPage);
        const int hidesBeforeReturn = visibility.hides;
        w.floatPage(w.listeningPage); w.movePage(w.listeningPage);
        settle(); QVERIFY(!w.terminalMaximized); QCOMPARE(visibility.hides, hidesBeforeReturn);
        w.demo = true;
    }
    void emptyEditorTransitionIsIsolatedAcrossWorkspaces() {
        const auto a = profile("layout-switch-a"), b = profile("layout-switch-b");
        {
            Workspace first(a, {}, false); first.demo = true;
            Workspace second(b, {}, false); second.demo = true;
            first.resize(1100,720); second.resize(1100,720); first.show(); settle();
            first.demo = false; first.showProcessPage(); first.demo = true;
            first.setActive(false); first.hide(); second.setActive(true); second.show(); settle();
            QVERIFY(!first.terminalMaximized); QVERIFY(second.terminalMaximized);
            second.demo = false; second.showListeningPage(); second.demo = true;
            second.setActive(false); second.hide(); first.setActive(true); first.show(); settle();
            QVERIFY(!first.terminalMaximized); QVERIFY(!second.terminalMaximized);
            first.closePage(first.processPage); // save before the queued check runs
            first.saveWorkspaceLayout(); QVERIFY(first.terminalMaximized);
            QVERIFY(!second.terminalMaximized);
        }
        {
            Workspace first(a, {}, false); first.demo = true;
            Workspace second(b, {}, false); second.demo = true;
            settle(); QVERIFY(first.terminalMaximized); QVERIFY(!first.processPage);
            QVERIFY(!second.terminalMaximized); QVERIFY(second.groupFor(second.listeningPage));
        }
    }
    void workspaceLayoutResetAndFloatingPage() {
        const auto p = profile("layout-reset");
        {
            Workspace w(p, {}, false); w.demo = true; w.resize(1100,720); w.show(); settle();
            w.demo = false; w.showListeningPage(); w.demo = true;
            w.floatPage(w.listeningPage); QVERIFY(w.floating.contains(w.listeningPage));
            w.setTerminalMaximized(true); w.shutdown();
        }
        {
            Workspace w(p, {}, false); w.demo = true; w.resize(1100,720); w.show(); settle();
            QVERIFY(w.groupFor(w.listeningPage)); QVERIFY(w.floating.isEmpty()); QVERIFY(w.terminalMaximized);
            UiPreferences::instance().reset();
        }
        {
            Workspace w(p, {}, false); w.demo = true;
            QVERIFY(!w.listeningPage); QVERIFY(w.terminalMaximized);
        }
    }
    void workspaceHiddenPanelAndSearchPresentation() {
        const auto p = profile("layout-hidden");
        {
            Workspace w(p, {}, false); w.demo = true; w.resize(1100,720); w.show(); settle();
            w.demo = false; w.showProcessPage(); w.demo = true;
            w.hideTerminalPanel(); QVERIFY(w.terminalPanel->isHidden());
        }
        {
            Workspace w(p, {}, false); w.demo = true; w.resize(1100,720); w.show(); settle();
            QVERIFY(w.groupFor(w.processPage)); QVERIFY(w.terminalPanel->isHidden());
            QVERIFY(!w.terminalMaximized);
            w.setTerminalMaximized(true);
            auto result = new QWidget; result->setProperty("documentSearchResults",true);
            w.editArea->addPage(result,"搜索结果"); QVERIFY(!w.terminalMaximized);
            w.editArea->removePage(result); delete result;
        }
    }
    void statusCardsKeepReadableWidthsAtZoom() {
        Workspace w(profile("status-width"),{},true); w.resize(1100,720); w.show();
        w.sidebar->setCurrentIndex(0);
        for (int level : {0,1,2}) {
            UiZoom::instance().setLevel(level); settle();
            auto split=w.findChild<QSplitter *>("workspaceSplit"); QVERIFY(split);
            QVERIFY(split->sizes().value(0)>=uiPx(310));
            const int valueWidth=w.resourceSummary->width()-uiPx(24)-uiPx(42);
            const QFontMetrics metrics(uiFont(13,QFont::Medium));
            QVERIFY(valueWidth>=metrics.horizontalAdvance(QString::fromUtf8("4.0 / 8.0 GiB · 50.0%")));
        }
        UiZoom::instance().setLevel(0);
    }
    void networkRatesStayWithinColumns() {
        for (bool light : {false,true}) {
            applyTheme(light);
            NetworkRateSummary rates; rates.resize(uiPx(155),uiPx(40)); rates.show(); settle();
            rates.setRates({}); const auto empty=rates.grab().toImage();
            rates.setRates(QPair<double,double>{1e13,2e13}); const auto filled=rates.grab().toImage();
            const int gap=qMin(rates.width(),uiPx(8)), start=(rates.width()-gap)/2;
            for (int x=start; x<start+gap; ++x)
                for (int y=uiPx(19); y<qMin(rates.height(),uiPx(39)); ++y)
                    QCOMPARE(filled.pixel(x,y),empty.pixel(x,y));
        }
        applyTheme(false);
    }
    void reopeningFloatingPagesRespectsDockedPages() {
        const auto p=profile("layout-floating-reopen");
        {
            Workspace w(p,{},false); w.demo=true; w.resize(1100,720); w.show(); settle();
            auto file=new Editor(p.id,"/floating.txt",QByteArray("fixture"),QByteArray());
            w.editors[file->path]=file; w.editArea->addPage(file,"floating.txt"); w.floatPage(file);
            w.setTerminalMaximized(true); w.openFile(file->path);
            QVERIFY(w.terminalMaximized); QVERIFY(w.floating.contains(file));
            w.demo=false; w.showProcessPage();
            w.setTerminalMaximized(true); w.openFile(file->path); QVERIFY(!w.terminalMaximized);
            w.showListeningPage();
            w.floatPage(w.processPage); w.floatPage(w.listeningPage);
            w.setTerminalMaximized(true); w.showProcessPage();
            QVERIFY(w.terminalMaximized); QCOMPARE(w.activeSystemPage,QString("process"));
            w.setTerminalMaximized(true); w.showListeningPage();
            QVERIFY(w.terminalMaximized); QCOMPARE(w.activeSystemPage,QString("listening"));
            w.demo=true;
        }
        {
            Workspace w(p,{},false); w.demo=true;
            QVERIFY(w.groupFor(w.processPage)); QVERIFY(w.groupFor(w.listeningPage));
            QCOMPARE(w.editArea->activeGroup()->currentWidget(),w.listeningPage);
            QVERIFY(w.editors.isEmpty());
        }
    }
    void clockIdentityInvalidationIsSynchronous(){
        struct UnusedSecrets final:SecretBackend{
            SecretResult read(const QString&)override{return {};}
            QString write(const QString&,const QByteArray&)override{return {};}
            QString remove(const QString&)override{return {};}
        };
        auto p=profile("clock-identity-revocation");p.name="只读测试来源";
        ConnectionRepository repository(nullptr,std::make_shared<UnusedSecrets>());repository.profiles={p};
        Workspace w(p,{},true,nullptr,&repository);w.resize(1100,720);w.show();settle();
        QSignalSpy invalidated(&repository,&ConnectionRepository::invalidated);
        for(int action=0;action<3;++action){
            auto worker=new Session(p,{},&w,Session::Mode::Commands);w.clockSession=worker;
            w.serverClock->setSession(worker);w.serverClock->setConnected(true);
            QVERIFY(!worker->jobs.empty());const int id=worker->jobs.front().id;
            worker->completed("server-clock",id,{{"ok",true},{"epoch",1704067200},{"offset",0},{"zone","UTC"},{"ntp","no"}});
            QVERIFY(w.serverClock->isEnabled());w.serverClock->click();
            QPointer<QDialog> form=w.findChild<QDialog*>("serverClockDialog");QVERIFY(form&&form->isVisible());
            if(action==0)repository.invalidate(p.id);
            else if(action==1)repository.update(p,std::nullopt,&w,[](bool,const QString&){});
            else repository.remove(p.id,&w,[](bool,const QString&){});
            QCOMPARE(invalidated.size(),action+1);QVERIFY(!w.clockSession);QVERIFY(worker->stopping.load());
            QVERIFY(!w.serverClock->isEnabled());QVERIFY(!form||!form->isVisible());
            worker->completed("server-clock",id,{{"ok",true},{"epoch",1704067200},{"offset",0},{"zone","STALE"},{"ntp","no"}});
            QCOMPARE(w.serverClock->text(),QString("服务器时间 —"));
            if(action){
                QVERIFY(repository.pending());w.demo=false;w.online=true;w.credential=QByteArray("fixture-only");
                w.ensureServerClock();QVERIFY(!w.clockSession);w.demo=true;w.online=false;
                QTRY_VERIFY_WITH_TIMEOUT(!repository.pending(),5000);
            }
            settle();
        }
    }
    void loopbackClockKeepsInteractiveSession(){
        const int port=qEnvironmentVariableIntValue("RABBITSHELL_CLOCK_PORT");
        if(!port)QSKIP("Requires isolated run_clock_integration.py fixture");
        auto p=profile("clock-workspace-integration");p.host="127.0.0.1";p.port=port;p.user="fixture";
        p.directory="/";p.name="时钟 · 私有回环";
        QTimer trust;connect(&trust,&QTimer::timeout,&trust,[]{
            if(auto box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                if(box->windowTitle().startsWith("首次连接"))box->button(QMessageBox::Yes)->click();
        });trust.start(10);
        Workspace w(p,QByteArray("fixture-only"),false);w.resize(1180,760);w.show();
        QTRY_VERIFY_WITH_TIMEOUT(w.online,15000);w.monitorTimer.stop();
        QVERIFY(w.session);QSignalSpy disconnected(w.session,&Session::disconnected);
        QTRY_VERIFY_WITH_TIMEOUT(w.clockSession,10000);
        auto clockSession=w.clockSession;QVERIFY(clockSession!=w.session);
        QCOMPARE(w.session->mode,Session::Mode::Files);QCOMPARE(clockSession->mode,Session::Mode::Commands);
        QTRY_VERIFY_WITH_TIMEOUT(w.serverClock->text().startsWith("2024-01-01 08:"),10000);
        QTRY_VERIFY_WITH_TIMEOUT(!w.terminals.isEmpty()&&w.terminals.cbegin().value().view->text().contains("fixture$"),10000);
        const auto terminal=w.terminals.cbegin().value();const auto initial=w.serverClock->text();
        Job input;input.type="input";input.id=terminal.id;input.data="CLOCK_PTY_BEFORE\r";w.session->submit(input);
        QTRY_VERIFY_WITH_TIMEOUT(terminal.view->text().contains("CLOCK_PTY_BEFORE"),3000);
        QTRY_VERIFY_WITH_TIMEOUT(w.serverClock->text()!=initial,2200);
        w.hide();QCOMPARE(w.serverClock->text(),QString("服务器时间 —"));
        w.show();QTRY_VERIFY_WITH_TIMEOUT(w.serverClock->text().startsWith("2024-01-01 08:"),10000);
        input.data="CLOCK_PTY_AFTER\r";w.session->submit(input);
        QTRY_VERIFY_WITH_TIMEOUT(terminal.view->text().contains("CLOCK_PTY_AFTER"),3000);
        QVERIFY(disconnected.isEmpty());QVERIFY(w.session->isRunning());QVERIFY(w.online);
        const auto output=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path());QVERIFY(QDir().mkpath(output));
        for(bool light:{false,true}){applyTheme(light);settle();QVERIFY(w.grab().save(output+(light?"/clock-workspace-light.png":"/clock-workspace-dark.png")));}
        w.shutdown();QTRY_VERIFY_WITH_TIMEOUT(!w.session->isRunning()&&(!clockSession||!clockSession->isRunning()),10000);
        QVERIFY(!w.clockSession);QCOMPARE(w.serverClock->text(),QString("服务器时间 —"));
    }
    void doubleClickOnlyNavigates_data(){
        QTest::addColumn<QString>("kind");QTest::addColumn<int>("column");
        QTest::newRow("folder-name")<<QString("folder")<<0;QTest::newRow("folder-size")<<QString("folder")<<1;
        QTest::newRow("nested-name")<<QString("nested")<<0;QTest::newRow("nested-size")<<QString("nested")<<1;
        QTest::newRow("parent-name")<<QString("parent")<<0;
    }
    void doubleClickOnlyNavigates(){
        QFETCH(QString,kind);QFETCH(int,column);Workspace w(profile("activation-regression"),{},true);enableFiles(w);
        w.sidebar->setCurrentIndex(1);deliver(w,"/root",{entry("/root/project",true)},true);
        auto target=w.fileItem("/root/project");
        if(kind=="nested"){
            target->setExpanded(true);deliver(w,"/root/project",{entry("/root/project/child",true)},false);
            target=w.fileItem("/root/project/child");
        }else if(kind=="parent")target=w.files->topLevelItem(0);
        QVERIFY(target);w.files->scrollToItem(target);settle();takeJobs(w);
        const auto rect=w.files->visualItemRect(target);
        const QPoint point(w.files->columnViewportPosition(column)+qMax(25,w.files->columnWidth(column)/2),rect.center().y());
        QSignalSpy drag(w.files,&FileView::remoteDragRequested);
        QTest::mouseClick(w.files->viewport(),Qt::LeftButton,{},point);QTest::mouseDClick(w.files->viewport(),Qt::LeftButton,{},point);settle();
        const auto jobs=takeJobs(w);QVERIFY(!jobs.isEmpty());for(const auto &job:jobs)QCOMPARE(job.type,QString("list"));
        const auto destination=kind=="parent"?QString("/"):kind=="nested"?QString("/root/project/child"):QString("/root/project");
        QVERIFY(std::any_of(jobs.cbegin(),jobs.cend(),[&](const Job &job){return job.path==destination;}));
        QVERIFY(w.transfers.isEmpty());QVERIFY(w.transferQueue.isEmpty());QCOMPARE(w.activeTransfer,0);
        QCOMPARE(w.downloadList->topLevelItemCount(),0);QCOMPARE(drag.count(),0);
        for(auto widget:QApplication::topLevelWidgets())QVERIFY(!qobject_cast<QFileDialog*>(widget)&&!qobject_cast<QInputDialog*>(widget));
    }
    void dismissResourceMenuDoesNotDownload(){
        Workspace w(profile("menu-dismiss-regression"),{},true);enableFiles(w);w.sidebar->setCurrentIndex(1);
        deliver(w,"/root",{entry("/root/project",true)},true);takeJobs(w);
        const auto point=w.files->visualItemRect(w.fileItem("/root/project")).center();
        for(bool escape:{true,false}){
            bool seen=false;QTimer closer;connect(&closer,&QTimer::timeout,&w,[&]{
                if(auto menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())){
                    seen=true;if(escape)QTest::keyClick(menu,Qt::Key_Escape);else menu->close();
                }
            });closer.start(5);w.fileMenu(point);closer.stop();QVERIFY(seen);settle();
            QVERIFY(w.transfers.isEmpty());QCOMPARE(w.downloadList->topLevelItemCount(),0);QVERIFY(w.transferQueue.isEmpty());
        }
    }
    void downloadDiagnosticsUseConstantOrigins(){
        const auto folder=data.filePath("activation-logs");QSettings().setValue("storage/logDirectory",folder);
        QSettings().setValue("downloads/path",data.path());
        auto &log=DiagnosticLog::instance();QString error;QVERIFY2(log.startLogging(&error),qPrintable(error));
        Workspace w(profile("download-diagnostic-regression"),{},true);enableFiles(w);w.sidebar->setCurrentIndex(1);
        deliver(w,"/root",{entry("/root/private-name",true)},true);takeJobs(w);
        const auto point=w.files->visualItemRect(w.fileItem("/root/private-name")).center();
        QTest::mouseClick(w.files->viewport(),Qt::LeftButton,{},point);QTest::mouseDClick(w.files->viewport(),Qt::LeftButton,{},point);settle();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
        w.transferSession=new Session(w.profile,{},&w);w.attachSession(w.transferSession,true);
        bool choose=false;QTimer answer;connect(&answer,&QTimer::timeout,&w,[&]{
            if(auto dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){
                if(choose){dialog->setDirectory(data.path());dialog->selectFile(data.path());QMetaObject::invokeMethod(dialog,"accept");}else dialog->reject();
            }else if(auto dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget()))dialog->accept();
        });answer.start(5);
        QVERIFY(!w.queueTransfers({"/root/private-name"},false));QVERIFY(w.transfers.isEmpty());
        choose=true;QVERIFY(w.queueTransfers({"/root/private-name"},false));answer.stop();
        w.transferReady=true;w.startTransfer();QVERIFY(w.activeTransfer);
        log.stopLogging();QFile file(folder+"/diagnostic.jsonl");QVERIFY(file.open(QIODevice::ReadOnly));const auto bytes=file.readAll();
        QVERIFY(!bytes.contains("private-name"));int activate=0,requested=0,queued=0,started=0;
        for(const auto &line:bytes.split('\n')){
            const auto row=QJsonDocument::fromJson(line).object();const auto event=row["event"].toString();
            if(event=="FileActivate"){++activate;QCOMPARE(row["origin"].toString(),QString("resource-double-click"));}
            if(event=="DownloadRequested"){++requested;QCOMPARE(row["origin"].toString(),QString("resource-menu"));}
            if(event=="DownloadQueued"){++queued;QCOMPARE(row["origin"].toString(),QString("resource-menu"));}
            if(event=="TransferStart"){++started;QCOMPARE(row["origin"].toString(),QString("resource-menu"));}
        }
        QVERIFY(activate>=1);QCOMPARE(requested,2);QCOMPARE(queued,1);QCOMPARE(started,1);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,false);
    }
    void reconnectRetainsTerminalsAndDirectory(){
        Workspace w(profile(),{},true);enableFiles(w);w.everConnected=true;w.directory="/srv/kept";
        QList<int> ids=w.terminals.keys();std::sort(ids.begin(),ids.end());QVERIFY(ids.size()>=2);
        auto old=w.session;old->terminalClosed(ids.last());QVERIFY(!w.terminals[ids.last()].running);
        w.addTerminal(w.terminals[ids.first()].group,"/srv/bookmark");const int extra=w.focusedTerm;
        w.renameTerminal(extra,"命名终端");auto view=w.terminals[extra].view;auto frame=w.terminals[extra].frame;const auto engine=view->term;view->feed("PRIVATE_HISTORY\r\n");
        const int count=w.terminals.size(),group=w.terminals[extra].group;const auto order=w.terminalGroups[group]->indexOf(frame);const auto focus=w.focusedTerm;takeJobs(w);
        w.bookmarkGroups[group]={"fixture-bookmark",w.profile.credentialKey(),"/srv/bookmark","收藏终端"};
        old->disconnected("fixture peer close");QVERIFY(!w.online);QVERIFY(w.terminals[extra].restorePending);QVERIFY(!w.terminals[ids.last()].restorePending);
        w.beginMainConnection();auto next=new Session(w.profile,{},&w);w.session=next;w.attachSession(next);next->connected();w.monitorTimer.stop();
        const auto jobs=takeJobs(w);QSet<int> restarted;bool kept=false;for(const auto &j:jobs){if(j.type=="open-terminal"){restarted.insert(j.id);if(j.id==extra)QCOMPARE(j.path,QString("/srv/bookmark"));}if(j.type=="list")kept|=j.path=="/srv/kept";}
        QVERIFY(kept);QVERIFY(restarted.contains(extra));QVERIFY(restarted.contains(ids.first()));QVERIFY(!restarted.contains(ids.last()));
        QCOMPARE(w.terminals.size(),count);QCOMPARE(w.terminals[extra].view,view);QCOMPARE(w.terminals[extra].frame,frame);QCOMPARE(view->term,engine);QVERIFY(view->text().contains("PRIVATE_HISTORY"));
        QCOMPARE(w.terminals[extra].name,QString("命名终端"));QCOMPARE(w.terminals[extra].group,group);QCOMPARE(w.terminalGroups[group]->indexOf(frame),order);QCOMPARE(w.focusedTerm,focus);
        const QByteArray stale="OLD_ATTEMPT_BYTES";old->queuedOutput=stale.size();old->terminalOutput(extra,stale);QVERIFY(!view->text().contains("OLD_ATTEMPT_BYTES"));QCOMPARE(old->queuedOutput.load(),qint64(0));
        const auto beforeText=w.connectionStatus->text(),beforeTip=w.connectionStatus->toolTip();next->responseWaiting(true);next->responseWaiting(true);QVERIFY(w.connectionStatus->styleSheet().contains(colors().orange.name()));next->responseWaiting(false);next->responseWaiting(false);QCOMPARE(w.connectionStatus->text(),beforeText);QCOMPARE(w.connectionStatus->toolTip(),beforeTip);
        QTimer::singleShot(0,[]{if(auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()))dialog->reject();});
        next->failed("open-terminal",extra,"fixture denied");
        QVERIFY(!w.terminals[extra].running);
        for(int cycle=0;cycle<2;++cycle){
            auto previous=w.session;previous->disconnected("fixture peer close");w.beginMainConnection();auto replacement=new Session(w.profile,{},&w);w.session=replacement;w.attachSession(replacement);replacement->connected();w.monitorTimer.stop();
            const auto queued=takeJobs(w);QSet<int> reopening;for(const auto &j:queued)if(j.type=="open-terminal")reopening.insert(j.id);
            QCOMPARE(reopening,QSet<int>{ids.first()});QCOMPARE(w.terminals.size(),count);QCOMPARE(w.terminals[extra].view,view);QCOMPARE(w.terminals[extra].frame,frame);QCOMPARE(view->term,engine);
            QCOMPARE(w.focusedTerm,focus);QCOMPARE(w.directory,QString("/srv/kept"));QVERIFY(w.bookmarkGroups.contains(group));QCOMPARE(w.bookmarkGroups[group].name,QString("收藏终端"));
        }
    }
    void mainConnectionNoticeLifecycle(){
        Workspace w(profile(),{},true);w.resize(1200,780);w.show();settle();
        verifyConnectionLogo(w.connectionLoading,64);verifyConnectionLogo(w.connectionBar,20);
        QVERIFY(w.connectionLoading->isHidden());QVERIFY(w.connectionBar->isHidden());
        auto editor=w.editors.begin().value();editor->code->setPlainText("existing draft");const auto doc=editor->code->document();
        auto term=w.terminals.begin().value().view;auto engine=term->term;const auto terminalCount=w.terminals.size();
        w.beginMainConnection();auto old=new Session(w.profile,{},&w);w.session=old;w.attachSession(old);
        old->phase("SSH 握手…");settle();QVERIFY(w.mainPending);QVERIFY(w.connectionLoading->isHidden());QVERIFY(!w.connectionBar->isHidden());QCOMPARE(w.connectionBar->progress->maximum(),0);
        w.setActive(false);QCOMPARE(w.connectionBar->progress->maximum(),1);w.setActive(true);QCOMPARE(w.connectionBar->progress->maximum(),0);
        w.hide();settle();QCOMPARE(w.connectionBar->progress->maximum(),1);w.show();settle();QCOMPARE(w.connectionBar->progress->maximum(),0);
        old->phase("等待确认主机指纹");QCOMPARE(w.mainPhase,QString("等待确认主机指纹"));QVERIFY(w.connectionStatus->styleSheet().contains(colors().orange.name()));
        const auto generation=w.credentialGeneration;w.cancelMainConnection();QVERIFY(!w.mainPending);QCOMPARE(w.credentialGeneration,generation);QCOMPARE(w.connectionBar->progress->maximum(),1);
        old->phase("late handshake");old->connected();old->disconnected("late failure");QCOMPARE(w.mainPhase,QString("已取消连接"));QVERIFY(!w.online);
        w.beginMainConnection();auto fresh=new Session(w.profile,{},&w);w.session=fresh;w.attachSession(fresh);
        old->phase("obsolete session");QCOMPARE(w.mainPhase,QString("正在连接…"));
        fresh->disconnected("synthetic failure");QVERIFY(!w.mainPending);QVERIFY(w.mainPhase.contains("synthetic failure"));QCOMPARE(w.connectionBar->progress->maximum(),1);
        w.beginMainConnection();auto success=new Session(w.profile,{},&w);w.session=success;w.attachSession(success);success->phase("正在认证…");
        success->connected();w.monitorTimer.stop();QVERIFY(w.online);QVERIFY(!w.mainPending);QVERIFY(w.connectionBar->isHidden());QVERIFY(w.connectionLoading->isHidden());QVERIFY(w.connectionStatus->styleSheet().contains(colors().green.name()));
        QCOMPARE(editor->code->document(),doc);QCOMPARE(editor->code->toPlainText(),QString("existing draft"));QCOMPARE(term->term,engine);QCOMPARE(w.terminals.size(),terminalCount+1);
        // File-only and command-only phases cannot affect the main connection.
        auto transfer=new Session(w.profile,{},&w);w.transferSession=transfer;w.attachSession(transfer,true);transfer->phase("independent transfer");QVERIFY(!w.mainPhase.contains("independent"));
    }
    void cancelledWorkerDisablesRetryUntilFinished(){
        QTcpServer fixture;QVERIFY(fixture.listen(QHostAddress::LocalHost,0));auto p=profile();p.host="127.0.0.1";p.port=fixture.serverPort();p.timeout=3;
        Workspace w(p,{},true);w.show();w.beginMainConnection();auto s=new Session(p,{},&w);w.session=s;w.attachSession(s);s->start();QTRY_VERIFY_WITH_TIMEOUT(s->isRunning(),1000);
        w.cancelMainConnection();if(s->isRunning())QVERIFY(!w.connectionBar->retry->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(!s->isRunning(),5000);QTRY_VERIFY_WITH_TIMEOUT(w.connectionBar->retry->isEnabled(),1000);
        QCOMPARE(w.mainPhase,QString("已取消连接"));QVERIFY(!w.mainPending);QVERIFY(!w.online);QCOMPARE(w.session,s);
    }
    void mainCredentialCancelStopsNotice(){
        Workspace w(profile(),{},true);w.demo=false;w.resize(1100,720);w.show();settle();
        int dialogs=0;QTimer answer;connect(&answer,&QTimer::timeout,&answer,[&]{auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(d&&d->findChild<QLineEdit *>("loginSecret")){++dialogs;QVERIFY(w.mainPending);QVERIFY(w.mainPhase.contains("等待输入"));d->reject();}});answer.start(5);
        w.reconnect();answer.stop();QCOMPARE(dialogs,1);QVERIFY(!w.mainPending);QVERIFY(!w.preparingCredentials);QVERIFY(!w.session);QCOMPARE(w.mainPhase,QString("已取消连接"));QCOMPARE(w.connectionBar->progress->maximum(),1);
    }
    void initialConnectionThemeAndGeometry_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void initialConnectionThemeAndGeometry(){
        QFETCH(bool,light);applyTheme(light);Workspace w(profile(),{},false);w.preparingCredentials=true;w.resize(1200,780);w.show();settle();
        w.preparingCredentials=false;w.beginMainConnection();settle();
        verifyConnectionLogo(w.connectionLoading,64);verifyConnectionLogo(w.connectionBar,20);
        QVERIFY(!w.connectionLoading->isHidden());QVERIFY(w.connectionBar->isHidden());QCOMPARE(w.connectionLoading->geometry(),w.connectionLoading->parentWidget()->rect());QCOMPARE(w.connectionLoading->progress->maximum(),0);
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path());
        QVERIFY(w.grab().save(report+QString("/connecting-%1.png").arg(light?"light":"dark")));
        w.mainConnectionState("等待确认主机指纹",true);QDialog authentication(&w);authentication.setWindowTitle("模拟认证窗口");authentication.show();authentication.raise();settle();QVERIFY(authentication.isVisible());authentication.hide();
        UiZoom::instance().setLevel(1);settle();verifyConnectionLogo(w.connectionLoading,64);verifyConnectionLogo(w.connectionBar,20);QCOMPARE(w.connectionLoading->geometry(),w.connectionLoading->parentWidget()->rect());QVERIFY(w.grab().save(report+QString("/connecting-waiting-120-%1.png").arg(light?"light":"dark")));
        w.mainConnectionState("连接失败：合成测试连接已取消",false);settle();QVERIFY(w.connectionLoading->isHidden());QVERIFY(!w.connectionBar->isHidden());QCOMPARE(w.connectionBar->progress->maximum(),1);
        QVERIFY(w.grab().save(report+QString("/connection-failed-%1.png").arg(light?"light":"dark")));
        w.beginMainConnection();settle();QCOMPARE(w.connectionLoading->geometry(),w.connectionLoading->parentWidget()->rect());w.setActive(false);QCOMPARE(w.connectionLoading->progress->maximum(),1);
        w.shutdown();QVERIFY(w.connectionLoading->isHidden());UiZoom::instance().setLevel(0);
    }
    void fileDragDispatchLeavesViewEventStack(){
#ifdef Q_OS_WIN
        const QVariantMap row{{"path","/queued.txt"},{"name","queued.txt"},{"size",4},{"modified",1},{"permissions",0100644}};
        auto make=[&]{auto w=new Workspace(profile(),{},true);w->demo=false;w->online=true;w->session=new Session(w->profile,{},w);w->authenticatedFingerprint="SHA256:synthetic";return w;};
        int calls=0;QPointer<Workspace> owner=make();bool onGui=false,metadata=false;
        owner->fileDragExecutor=[&](QWidget *,const auto &b,const QPixmap &badge){++calls;onGui=QThread::currentThread()==qApp->thread();metadata=b->files.first().remotePath=="/queued.txt"&&!badge.isNull();return true;};
        owner->files->remoteDragRequested({row});QCOMPARE(calls,0);settle();QCOMPARE(calls,1);QVERIFY(onGui);QVERIFY(metadata);
        owner->files->remoteDragRequested({row});delete owner.data();settle();QCOMPARE(calls,1);QVERIFY(owner.isNull());
        owner=make();std::weak_ptr<FileDragBatch> batch;
        owner->fileDragExecutor=[&](QWidget *,const auto &b,const QPixmap &){++calls;batch=b;delete owner.data();return false;};
        owner->files->remoteDragRequested({row});QCOMPARE(calls,1);settle();QCOMPARE(calls,2);QVERIFY(owner.isNull());settle();QVERIFY(batch.expired());
#endif
    }
    void fileDragSelectionUploadAndDirectoryBoundary(){
#ifndef Q_OS_WIN
        QSKIP("Native virtual-file export is Windows-only");
#else
        ExposedFileView view;view.resize(700,480);view.show();view.activateWindow();
        for(int i=0;i<5;++i){auto row=new QTreeWidgetItem(&view,{QString("file-%1.txt").arg(i)});auto info=entry("/root/"+row->text(0));info["permissions"]=0100644;row->setData(0,Qt::UserRole,info);}
        auto directory=new QTreeWidgetItem(&view,{"folder"});directory->setData(0,Qt::UserRole,entry("/root/folder",true));
        settle();QSignalSpy drags(&view,&FileView::remoteDragRequested),uploads(&view,&FileView::uploadDropped);
        auto click=[&](int row,Qt::KeyboardModifiers modifiers){QTest::mouseClick(view.viewport(),Qt::LeftButton,modifiers,view.visualItemRect(view.topLevelItem(row)).center());};
        click(0,{});click(2,Qt::ControlModifier);QCOMPARE(view.selectedItems().size(),2);view.startDrag(Qt::CopyAction);QCOMPARE(drags.size(),1);QCOMPARE(drags[0][0].toList().size(),2);
        click(4,Qt::ShiftModifier);QVERIFY(view.selectedItems().size()>=3);view.startDrag(Qt::CopyAction);QCOMPARE(drags.size(),2);
        view.clearSelection();directory->setSelected(true);view.startDrag(Qt::CopyAction);QCOMPARE(drags.size(),3);QVERIFY(drags.last()[0].toList().first().toMap()["directory"].toBool());
        // Selection rectangle starts only in blank space, not an entire-row click.
        const auto blank=QPoint(view.viewport()->width()-12,view.viewport()->height()-12);
        QTest::mousePress(view.viewport(),Qt::LeftButton,{},blank);QMouseEvent movement(QEvent::MouseMove,QPointF(5,5),QPointF(view.viewport()->mapToGlobal(QPoint(5,5))),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view.viewport(),&movement);QTest::mouseRelease(view.viewport(),Qt::LeftButton,{},QPoint(5,5));QVERIFY(view.selectedItems().size()>=5);
        QFile upload(data.filePath("upload.txt"));QVERIFY(upload.open(QIODevice::WriteOnly));upload.write("local");upload.close();
        QMimeData local;local.setUrls({QUrl::fromLocalFile(data.filePath("upload.txt"))});const auto point=view.visualItemRect(directory).center();
        QDragEnterEvent enter(point,Qt::CopyAction,&local,Qt::LeftButton,{});QApplication::sendEvent(view.viewport(),&enter);QVERIFY(enter.isAccepted());
        QDropEvent drop(point,Qt::CopyAction,&local,Qt::LeftButton,{});QApplication::sendEvent(view.viewport(),&drop);QVERIFY(drop.isAccepted());QCOMPARE(uploads.size(),1);QCOMPARE(uploads[0][1].toString(),QString("/root/folder"));
#endif
    }
    void setiMappingAndRendering() {
        QVERIFY(FileIconProvider::resourcesAvailable());
        const QList<QPair<QString,QString>> cases = {
            {"README.md","_info"}, {"CMakeLists.txt","_makefile_3"}, {"tsconfig.json","_tsconfig"},
            {"src/main.ts","_typescript"}, {"src/main.js","_javascript"}, {"main.spec.ts","_typescript_1"},
            {"界面.tsx","_react"}, {"app.jsx","_react"}, {"config.json","_json"}, {"settings.yaml","_yml"},
            {".bashrc","_shell"}, {"deploy.sh","_shell"}, {"Dockerfile","_docker"}, {"Dockerfile.dev","_docker"},
            {"main.py","_python"}, {"main.cpp","_cpp"}, {"style.css","_css"}, {"index.html","_html_3"},
            {"photo.png","_image"}, {"archive.zip","_zip_1"}, {"config.conf","_config"},
            {"config.ini","_default"}, {"未知文件","_default"}, {"unrecognized.rabbit","_default"}
        };
        for (bool light : {false,true}) {
            applyTheme(light);
            for (const auto &[path,expected] : cases) {
                QCOMPARE(FileIconProvider::definition("/root/"+path), expected+(light?"_light":""));
                const auto icon = FileIconProvider::file(path);
                for (qreal dpr : {1.0,1.5,2.0}) {
                    const auto pixmap = icon.pixmap(QSize(20,20),dpr);
                    QCOMPARE(pixmap.size(),QSize(qRound(20*dpr),qRound(20*dpr)));
                    QCOMPARE(pixmap.devicePixelRatio(),dpr);
                    const auto image = pixmap.toImage(); int visible = 0;
                    for (int y=0;y<image.height();++y) for(int x=0;x<image.width();++x)
                        if (qAlpha(image.pixel(x,y))) ++visible;
                    QVERIFY(visible>20); QVERIFY(visible<image.width()*image.height());
                    QCOMPARE(qAlpha(image.pixel(0,0)),0);
                    QCOMPARE(qAlpha(image.pixel(image.width()-1,image.height()-1)),0);
                }
            }
            const auto folder=FileIconProvider::directory();
            QVERIFY(folder.pixmap(QSize(20,20),QIcon::Normal,QIcon::On).toImage()!=
                    folder.pixmap(QSize(20,20),QIcon::Normal,QIcon::Off).toImage());
        }
        applyTheme(false);
    }
    void fileIconsFollowRenameMigrationAndThemes() {
        Workspace w(profile("file-icons"),{},true); enableFiles(w); w.resize(1440,1000);
        w.sidebar->setCurrentIndex(1);
        QVariantList rows{entry("/root/src",true)};
        for(const auto &name : QStringList{"main.ts","main.js","main.spec.ts","界面.tsx","app.jsx","tsconfig.json",
                "README.md","CMakeLists.txt","Dockerfile","Dockerfile.dev",".bashrc","deploy.sh","main.py",
                "config.json","config.conf","config.ini","settings.yaml","photo.png","archive.zip","未知文件","no-extension"})
            rows << entry("/root/"+name);
        deliver(w,"/root",rows,true);
        for(const auto &name : QStringList{"main.ts","main.js"}) {
            const auto path="/root/"+name;
            w.session->completed("read",w.nextJob++,{{"path",path},{"data",QByteArray("const value = 42;\nconsole.log(value);\n")},
                                                  {"hash",QByteArray("hash")},{"permissions",0644}});
        }
        auto editor=w.editors.value("/root/main.ts"); QVERIFY(editor);
        auto document=editor->code->document();
        editor->code->moveCursor(QTextCursor::End); editor->code->insertPlainText("// preserved draft\n");
        const auto content=editor->bytes();
        auto source=w.groupFor(editor); w.editArea->movePage(editor,source,DropZone::Right); settle();
        QCOMPARE(w.groupFor(editor)->tabIcon(w.groupFor(editor)->indexOf(editor)).cacheKey(),FileIconProvider::file(editor->path).cacheKey());
        const auto directory=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path()); QDir().mkpath(directory);
        for(bool light : {false,true}) {
            applyTheme(light); w.refreshTheme(); settle();
            const auto prefix=directory+(light?"/seti-light":"/seti-dark");
            QCOMPARE(w.fileItem("/root/main.js")->icon(0).cacheKey(),FileIconProvider::file("main.js").cacheKey());
            QVERIFY(w.grab().save(prefix+"-mixed-tree.png"));
            w.floatPage(editor); settle(); auto window=w.floating.value(editor); QVERIFY(window);
            QCOMPARE(window->windowIcon().cacheKey(),FileIconProvider::file(editor->path).cacheKey());
            QVERIFY(window->grab().save(prefix+"-editor-window.png"));
            // Theme switches update this existing floating window and document.
            applyTheme(!light); w.refreshTheme(); settle();
            QCOMPARE(window->windowIcon().cacheKey(),FileIconProvider::file(editor->path).cacheKey());
            QCOMPARE(editor->code->document(),document); QCOMPARE(editor->bytes(),content);
            applyTheme(light); w.refreshTheme(); w.movePage(editor); settle();
            const auto term=w.terminals.begin().value(); w.floatPage(term.frame); settle();
            auto terminalWindow=w.floating.value(term.frame); QVERIFY(terminalWindow);
            QVERIFY(terminalWindow->grab().save(prefix+"-terminal-window.png"));
            terminalWindow->close(); settle(); QVERIFY(!w.floating.contains(term.frame));
        }
        w.floatPage(editor); settle();
        auto window=w.floating.value(editor);
        w.session->completed("rename",w.nextJob++,{{"old","/root/main.ts"},{"path","/root/renamed.js"}}); settle();
        QCOMPARE(w.editors.value("/root/renamed.js"),editor);
        QCOMPARE(window->windowIcon().cacheKey(),FileIconProvider::file("renamed.js").cacheKey());
        QVERIFY(window->windowTitle().contains("/root/renamed.js"));
        QCOMPARE(editor->bytes(),content); QCOMPARE(editor->code->document(),document); QVERIFY(document->isUndoAvailable());
        w.movePage(editor); settle();
        QCOMPARE(w.groupFor(editor)->tabIcon(w.groupFor(editor)->indexOf(editor)).cacheKey(),FileIconProvider::file("renamed.js").cacheKey());
        w.session->completed("rename",w.nextJob++,{{"old","/root"},{"path","/renamed-root"}}); settle();
        QCOMPARE(editor->path,QString("/renamed-root/renamed.js"));
        QCOMPARE(w.groupFor(editor)->tabIcon(w.groupFor(editor)->indexOf(editor)).cacheKey(),FileIconProvider::file(editor->path).cacheKey());
        editor->discardDraft(); applyTheme(false);
    }
    void parentNavigationAndSort() {
        Workspace w(profile(), {}, true); enableFiles(w);
        auto a=entry("/root/alpha",true), z=entry("/root/zeta",true);
        a.remove("size"); z.remove("size"); a["modified"]=40; z["modified"]=10;
        auto small=entry("/root/small.txt"), large=entry("/root/large.txt"), hidden=entry("/root/.hidden");
        small["size"]=2; large["size"]=900; hidden["size"]=20;
        small["modified"]=30; large["modified"]=5; hidden["modified"]=20;
        QVariantList rows{small,z,large,hidden,a}; deliver(w,"/root",rows,true); takeJobs(w);
        auto nav=w.files->topLevelItem(0); QVERIFY(nav->data(0,ParentNavigationRole).toBool());
        QCOMPARE(nav->text(0),QString("..")); QVERIFY(!nav->icon(0).isNull());
        QVERIFY(nav->text(1).isEmpty() && nav->text(2).isEmpty());
        QVERIFY(!nav->data(0,Qt::UserRole).isValid()); QVERIFY(!(nav->flags() & Qt::ItemIsSelectable));
        QVERIFY(!w.fileItem("") && !w.fileItem("/"));
        auto names=[&] { QStringList out; for(int i=0;i<w.files->topLevelItemCount();++i)out<<w.files->topLevelItem(i)->text(0);return out; };
        for(int col=0;col<3;++col) for(auto order:{Qt::AscendingOrder,Qt::DescendingOrder}) {
            w.files->sortItems(col,order); settle(); const auto n=names();
            QCOMPARE(n[0],QString(".."));
            QSet<QString> dirs{n[1],n[2]}; QCOMPARE(dirs,(QSet<QString>{"alpha","zeta"}));
            if(col==1) {
                QCOMPARE(n[1],order==Qt::AscendingOrder?QString("alpha"):QString("zeta"));
                QCOMPARE(n[3],order==Qt::AscendingOrder?QString("small.txt"):QString("large.txt"));
                QCOMPARE(n[5],order==Qt::AscendingOrder?QString("large.txt"):QString("small.txt"));
            }
            if(col==2) {
                QCOMPARE(n[1],order==Qt::AscendingOrder?QString("zeta"):QString("alpha"));
                QCOMPARE(n[3],order==Qt::AscendingOrder?QString("large.txt"):QString("small.txt"));
            }
        }
        w.files->sortItems(0,Qt::AscendingOrder); w.createEntry(false); settle();
        QCOMPARE(w.files->topLevelItem(0),nav); QVERIFY(w.files->topLevelItem(1)->data(0,FileDraftRole).toBool());
        w.files->sortItems(1,Qt::DescendingOrder); settle(); QCOMPARE(w.files->topLevelItem(0),nav); QVERIFY(w.files->topLevelItem(1)->data(0,FileDraftRole).toBool());
        w.cancelEntryDraft();
        for(int n=0;n<3;++n) { deliver(w,"/root",rows); QCOMPARE(w.files->topLevelItemCount(),6); QCOMPARE(w.files->topLevelItem(0),nav); }
        auto folder=w.fileItem("/root/alpha"); folder->setExpanded(true); deliver(w,"/root/alpha",{entry("/root/alpha/child")});
        QCOMPARE(folder->childCount(),1); QVERIFY(!folder->child(0)->data(0,ParentNavigationRole).toBool());
        w.showHiddenFiles=false; w.fileFilter->setText("no match"); settle(); QVERIFY(!nav->isHidden()); QVERIFY(w.fileItem("/root/.hidden")->isHidden());
        w.fileFilter->clear(); w.showHiddenFiles=true; w.filterFiles();
        auto changed=entry("/root/small.txt",true); changed.remove("size"); rows[0]=changed;
        w.files->sortItems(1,Qt::AscendingOrder); deliver(w,"/root",rows); settle();
        QCOMPARE(w.files->topLevelItem(0),nav); QVERIFY(w.fileItem("/root/small.txt")->data(0,Qt::UserRole).toMap()["directory"].toBool());
        for(int i=1;i<=3;++i) QVERIFY(w.files->topLevelItem(i)->data(0,Qt::UserRole).toMap()["directory"].toBool());
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path()); QDir().mkpath(report);
        for(bool light:{false,true}) { applyTheme(light); w.refreshTheme(); settle(); QVERIFY(w.grab().save(report+(light?"/parent-navigation-light.png":"/parent-navigation-dark.png"))); }
        applyTheme(false);
    }
    void parentNavigationInputAndRequests() {
        Workspace w(profile(),{},true); enableFiles(w);
        deliver(w,"/root",{entry("/root/a"),entry("/root/b"),entry("/root/c")},true); takeJobs(w);
        auto nav=w.files->topLevelItem(0); auto first=w.fileItem("/root/a"); auto last=w.fileItem("/root/c");
        QVERIFY(nav->data(0,ParentNavigationRole).toBool());
        const auto point=w.files->visualItemRect(nav).center(); QVERIFY(w.files->rowAt(point)==nav);
        QSignalSpy drag(w.files,&FileView::remoteDragRequested), upload(w.files,&FileView::uploadDropped);
        for(auto button:{Qt::LeftButton,Qt::RightButton}) for(auto modifier:{Qt::NoModifier,Qt::ControlModifier,Qt::ShiftModifier}) {
            w.files->clearSelection(); w.files->setCurrentItem(first); first->setSelected(true); last->setSelected(true);
            QTest::mouseClick(w.files->viewport(),button,modifier,point); settle();
            QVERIFY2(w.files->selectedItems().isEmpty(),"navigation click retained or recreated a file selection");
            QCOMPARE(w.files->currentItem(),nav);
            w.files->setFocus(); QTest::keyClick(w.files,Qt::Key_F2); QTest::keyClick(w.files,Qt::Key_Delete); settle();
            QVERIFY(takeJobs(w).isEmpty()); QVERIFY(!w.entryDraft.editor);
        }
        bool menu=false; QTimer closer; connect(&closer,&QTimer::timeout,&w,[&]{if(auto popup=qobject_cast<QMenu*>(QApplication::activePopupWidget())){menu=true;popup->close();}});
        closer.start(5); w.fileMenu(point); closer.stop(); QVERIFY(!menu);
        QVERIFY(!w.files->uploadTarget(point).valid()); QVERIFY(w.files->uploadTarget(point).error.contains("导航"));
        QFile file(data.filePath("nav-upload.txt")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture"); file.close();
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(file.fileName())});
        QDragEnterEvent enter(point,Qt::CopyAction,&mime,Qt::LeftButton,{}); QApplication::sendEvent(w.files->viewport(),&enter); QVERIFY(!enter.isAccepted());
        QDropEvent drop(point,Qt::CopyAction,&mime,Qt::LeftButton,{}); QApplication::sendEvent(w.files->viewport(),&drop); QVERIFY(!drop.isAccepted()); QCOMPARE(upload.size(),0); QCOMPARE(drag.size(),0);
        w.files->setCurrentItem(first);first->setSelected(true);QTest::mousePress(w.files->viewport(),Qt::LeftButton,{},point);
        const auto end=point+QPoint(40,30);QMouseEvent movement(QEvent::MouseMove,QPointF(end),QPointF(w.files->viewport()->mapToGlobal(end)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(w.files->viewport(),&movement);QTest::mouseRelease(w.files->viewport(),Qt::LeftButton,{},end);settle();QVERIFY(w.files->selectedItems().isEmpty());QCOMPARE(drag.size(),0);
        QTest::keyClick(w.files,Qt::Key_A,Qt::ControlModifier); QVERIFY(!nav->isSelected());
        QTest::mouseClick(w.files->viewport(),Qt::LeftButton,{},point); QTest::mouseDClick(w.files->viewport(),Qt::LeftButton,{},point); settle();
        auto jobs=takeJobs(w); QCOMPARE(jobs.size(),1); QCOMPARE(jobs[0].type,QString("list")); QCOMPARE(jobs[0].path,QString("/")); QCOMPARE(w.directory,QString("/root"));
        QTimer dismiss; connect(&dismiss,&QTimer::timeout,&w,[]{if(auto dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))dialog->accept();}); dismiss.start(5);
        w.session->failed("list",jobs[0].id,"fixture failure"); dismiss.stop(); QCOMPARE(w.directory,QString("/root")); QCOMPARE(w.files->topLevelItem(0),nav);
        QTest::mouseClick(w.files->viewport(),Qt::LeftButton,{},point); QTest::mouseDClick(w.files->viewport(),Qt::LeftButton,{},point); jobs=takeJobs(w); QCOMPARE(jobs.size(),1);
        w.session->completed("list",jobs[0].id,{{"path","/"},{"entries",QVariantList{}}});settle();QCOMPARE(w.directory,QString("/"));QCOMPARE(w.files->topLevelItemCount(),0);
        deliver(w,"/root",{entry("/root/a"),entry("/root/b"),entry("/root/c")},true);takeJobs(w);nav=w.files->topLevelItem(0);
        QTest::mouseClick(w.files->viewport(),Qt::LeftButton,{},point); QTest::mouseDClick(w.files->viewport(),Qt::LeftButton,{},point); jobs=takeJobs(w); QCOMPARE(jobs.size(),1);
        const int stale=jobs[0].id; w.navigate("/new"); auto newer=takeJobs(w); QCOMPARE(newer.size(),1);
        w.session->completed("list",stale,{{"path","/"},{"entries",QVariantList{}}}); QCOMPARE(w.directory,QString("/root"));
        w.session->completed("list",newer[0].id,{{"path","/new"},{"entries",QVariantList{}}}); settle(); QCOMPARE(w.directory,QString("/new"));
        QCOMPARE(w.files->topLevelItemCount(),1); nav=w.files->topLevelItem(0); QVERIFY(nav->data(0,ParentNavigationRole).toBool());
        QTest::mouseClick(w.files->viewport(),Qt::LeftButton,{},w.files->visualItemRect(nav).center()); QTest::mouseDClick(w.files->viewport(),Qt::LeftButton,{},w.files->visualItemRect(nav).center());
        jobs=takeJobs(w); QCOMPARE(jobs.size(),1); QCOMPARE(jobs[0].path,QString("/"));
        w.session->completed("list",jobs[0].id,{{"path","/"},{"entries",QVariantList{}}}); settle(); QCOMPARE(w.directory,QString("/")); QCOMPARE(w.files->topLevelItemCount(),0);
        QCOMPARE(w.pathEdit->text(),QString("/")); QCOMPARE(w.history.last(),QString("/"));
        w.online=false; w.directory.clear(); const int before=w.nextJob;
        for(auto button:w.findChildren<QToolButton*>()) if(button->accessibleName()=="上一级")button->click();
        QCOMPARE(w.nextJob,before);
    }
    void demoParentNavigationHistory() {
        Workspace w(profile(),{},true); w.resize(1100,720); w.show(); settle();
        auto nav=w.files->topLevelItem(0); QVERIFY(nav->data(0,ParentNavigationRole).toBool());
        const auto click=[&] { const auto p=w.files->visualItemRect(w.files->topLevelItem(0)).center(); QTest::mouseClick(w.files->viewport(),Qt::LeftButton,{},p); QTest::mouseDClick(w.files->viewport(),Qt::LeftButton,{},p); settle(); };
        click(); QCOMPARE(w.directory,QString("/home/demo")); click(); QCOMPARE(w.directory,QString("/home")); click(); QCOMPARE(w.directory,QString("/"));
        for(int i=0;i<w.files->topLevelItemCount();++i) QVERIFY(!w.files->topLevelItem(i)->data(0,ParentNavigationRole).toBool());
        for(auto button:w.findChildren<QToolButton*>()) if(button->accessibleName()=="返回")button->click();
        QCOMPARE(w.directory,QString("/home")); QVERIFY(w.files->topLevelItem(0)->data(0,ParentNavigationRole).toBool());
        for(auto button:w.findChildren<QToolButton*>()) if(button->accessibleName()=="前进")button->click();
        QCOMPARE(w.directory,QString("/"));
    }
    void clipboardShortcutsAndBatchState() {
        Workspace w(profile(),{},true); enableFiles(w);
        w.transferSession=new Session(w.profile,{},&w);w.attachSession(w.transferSession,true);
        auto row=entry("/root/a.txt");row["permissions"]=0100644;
        deliver(w,"/root",{row,entry("/root/folder",true)},true);takeJobs(w);
        auto a=w.fileItem("/root/a.txt");w.files->setCurrentItem(a);a->setSelected(true);w.files->setFocus();
        QTest::keyClick(w.files,Qt::Key_C,Qt::ControlModifier);settle();
        auto batch=FileClipboard::instance().current();QVERIFY(batch);QVERIFY(!batch->ready);QCOMPARE(w.transferQueue.size(),1);
        QVERIFY(!QApplication::clipboard()->mimeData()->hasUrls());QVERIFY(!batch->spool.isEmpty());
        w.pasteFiles("/root");QVERIFY(w.taskSummary->text().contains("准备"));QCOMPARE(w.transferQueue.size(),1);
        const int id=w.transferQueue.dequeue();auto job=w.transfers[id];QFile staged(job.destination);QVERIFY(staged.open(QIODevice::WriteOnly));staged.write("staged bytes");staged.close();
        w.activeTransfer=id;w.transferSession->completed("download",id,{{"completed",1},{"failed",0},{"skipped",0},{"activeMs",1}});settle();
        QVERIFY(batch->ready && batch->published);QCOMPARE(QApplication::clipboard()->mimeData()->urls().size(),1);
        QCOMPARE(QApplication::clipboard()->mimeData()->urls().first().toLocalFile(),job.destination);
        QVERIFY(w.downloadList->topLevelItem(w.downloadList->topLevelItemCount()-1)->text(0).contains("就绪"));
        w.files->setCurrentItem(a);a->setSelected(true);QTest::keyClick(w.files,Qt::Key_C,Qt::ControlModifier);
        auto replaced=FileClipboard::instance().current();QVERIFY(replaced && replaced!=batch);QCOMPARE(w.transferQueue.size(),1);
        const int stale=w.transferQueue.dequeue();QApplication::clipboard()->setText("outside clipboard");w.activeTransfer=stale;
        w.transferSession->completed("download",stale,{{"completed",1},{"failed",0},{"skipped",0},{"activeMs",1}});settle();
        QCOMPARE(QApplication::clipboard()->text(),QString("outside clipboard"));QVERIFY(!replaced->published);
        QMimeData *fake=new QMimeData;fake->setData(FileClipboard::mimeType(),"forged");fake->setUrls({QUrl::fromLocalFile(staged.fileName())});QApplication::clipboard()->setMimeData(fake);
        w.pasteFiles("/root");QVERIFY(w.taskSummary->text().contains("无效"));QVERIFY(w.transferQueue.isEmpty());
        auto mime=new QMimeData;mime->setUrls({QUrl("https://example.invalid/file")});QApplication::clipboard()->setMimeData(mime);w.pasteFiles("/root");QVERIFY(w.transferQueue.isEmpty());
        QFile local(data.filePath("clipboard-local.txt"));QVERIFY(local.open(QIODevice::WriteOnly));local.write("local");local.close();
        mime=new QMimeData;mime->setUrls({QUrl::fromLocalFile(local.fileName())});QApplication::clipboard()->setMimeData(mime);
        w.files->setCurrentItem(a);a->setSelected(true);w.files->setFocus();QTest::keyClick(w.files,Qt::Key_V,Qt::ControlModifier);
        QCOMPARE(w.transferQueue.size(),1);auto upload=w.transfers[w.transferQueue.first()];QCOMPARE(upload.type,QString("upload"));QCOMPARE(upload.path,QString("/root/clipboard-local.txt"));QVERIFY(upload.options["clipboardPaste"].toBool());
        w.transferQueue.clear();
        w.createEntry(false);QVERIFY(w.entryDraft.editor);auto editor=w.entryDraft.editor.data();editor->setText("text");editor->selectAll();editor->setFocus();
        QApplication::clipboard()->setText("input text");QTest::keyClick(editor,Qt::Key_V,Qt::ControlModifier);QCOMPARE(editor->text(),QString("input text"));QVERIFY(w.transferQueue.isEmpty());w.cancelEntryDraft();
    }
    void clipboardMoveProtectsEditorsAndSiblingFiles() {
        Workspace w(profile(),{},true);enableFiles(w);w.transferSession=new Session(w.profile,{},&w);w.attachSession(w.transferSession,true);
        auto source=entry("/root/a.txt");source["permissions"]=0100644;deliver(w,"/root",{source,entry("/root/target",true)},true);takeJobs(w);
        auto read=[&](QString path){w.session->completed("read",w.nextJob++,{{"path",path},{"data",QByteArray("draft\n")},{"hash",QByteArray("hash")},{"permissions",0644}});};
        read("/root/a.txt");read("/root/target/unrelated.txt");auto e=w.editors.value("/root/a.txt");QVERIFY(e);e->code->insertPlainText("changed");auto doc=e->code->document();
        auto a=w.fileItem("/root/a.txt");w.files->setCurrentItem(a);a->setSelected(true);w.files->setFocus();QTest::keyClick(w.files,Qt::Key_X,Qt::ControlModifier);
        auto batch=FileClipboard::instance().current();QVERIFY(batch && batch->kind==FileClipboardBatch::Kind::Cut);
        w.pasteFiles("/root/target");QCOMPARE(w.transferQueue.size(),1);const int id=w.transferQueue.dequeue();QCOMPARE(w.transfers[id].type,QString("move"));
        QVERIFY(w.clipboardPathLocked("/root/a.txt"));QVERIFY(w.clipboardPathLocked("/root/target/a.txt"));QVERIFY(!w.clipboardPathLocked("/root/target/unrelated.txt"));
        const auto before=w.nextJob;w.saveEditor(e);QCOMPARE(w.nextJob,before);QVERIFY(e->dirty());
        w.activeTransfer=id;w.transferSession->completed("move",id,{{"old","/root/a.txt"},{"path","/root/target/a.txt"}});settle();
        QCOMPARE(w.editors.value("/root/target/a.txt"),e);QCOMPARE(e->code->document(),doc);QVERIFY(e->dirty());QVERIFY(!w.editors.contains("/root/a.txt"));QVERIFY(!w.clipboardPathLocked("/root/target/a.txt"));
        QVERIFY(batch->roots.isEmpty());QVERIFY(!batch->moving);
        read("/root/b.txt");read("/root/target/b.txt");auto b=entry("/root/b.txt");b["permissions"]=0100644;
        deliver(w,"/root",{b,entry("/root/target",true)},true);auto item=w.fileItem("/root/b.txt");w.files->setCurrentItem(item);item->setSelected(true);w.files->setFocus();QTest::keyClick(w.files,Qt::Key_X,Qt::ControlModifier);
        w.pasteFiles("/root/target");QVERIFY(w.transferQueue.isEmpty());QVERIFY(w.taskSummary->text().contains("编辑器"));QVERIFY(w.editors.contains("/root/target/b.txt"));
    }
    void clipboardRejectsIncompleteAndInvalidSelections() {
        Workspace w(profile(),{},true);enableFiles(w);w.transferSession=new Session(w.profile,{},&w);w.attachSession(w.transferSession,true);
        auto parent=entry("/root/folder",true), nested=entry("/root/folder/file.txt"), other=entry("/root/other",true);nested["permissions"]=0100644;
        deliver(w,"/root",{parent,other},true);deliver(w,"/root/folder",{nested});takeJobs(w);
        auto folder=w.fileItem("/root/folder");folder->setExpanded(true);auto child=w.fileItem("/root/folder/file.txt");
        w.files->clearSelection();w.files->setCurrentItem(folder);folder->setSelected(true);child->setSelected(true);w.copyFileSelection(false);
        auto batch=FileClipboard::instance().current();QVERIFY(batch);QCOMPARE(batch->roots,QStringList{"/root/folder"});QCOMPARE(w.transferQueue.size(),1);
        const int id=w.transferQueue.dequeue();w.activeTransfer=id;
        w.transferSession->completed("download",id,{{"completed",1},{"skipped",1},{"failed",0},{"activeMs",1}});settle();
        QVERIFY(batch->failed);QVERIFY(!batch->published);QVERIFY(!QApplication::clipboard()->mimeData()->hasUrls());
        QVERIFY(w.downloadList->topLevelItem(w.downloadList->topLevelItemCount()-1)->text(0).contains("失败"));
        const auto previous=QApplication::clipboard()->mimeData()->data(FileClipboard::mimeType());
        auto link=entry("/root/link",false,true);link["permissions"]=0120777;w.copyFileSelection(false,{link});
        QCOMPARE(QApplication::clipboard()->mimeData()->data(FileClipboard::mimeType()),previous);
        auto a=entry("/root/folder/a.txt"),b=entry("/root/other/a.txt");a["permissions"]=b["permissions"]=0100644;
        w.copyFileSelection(false,{a,b});auto copies=FileClipboard::instance().current();QVERIFY(copies);QCOMPARE(copies->roots.size(),2);QVERIFY(copies->staged[0]!=copies->staged[1]);QCOMPARE(QFileInfo(copies->staged[0]).fileName(),QFileInfo(copies->staged[1]).fileName());
        w.cancelClipboardJobs();QCOMPARE(copies->remaining,0);QVERIFY(copies->failed);QVERIFY(!copies->published);
        w.copyFileSelection(true,{parent});auto cut=FileClipboard::instance().current();QVERIFY(cut);w.pasteFiles("/root/folder/sub");QVERIFY(w.taskSummary->text().contains("自身"));
        Workspace different(profile("another-profile"),{},true);enableFiles(different);different.transferSession=new Session(different.profile,{},&different);different.attachSession(different.transferSession,true);
        different.pasteFiles("/root");QVERIFY(different.taskSummary->text().contains("另一服务器"));QVERIFY(different.transferQueue.isEmpty());
    }
    void localDeletePreservesOtherBranchesAndRejectsStaleLoads() {
        Workspace w(profile(), {}, true); enableFiles(w);
        QVariantList root{entry("/root/a", true), entry("/root/b", true)};
        for (int i = 0; i < 80; ++i) root.append(entry(QString("/root/z%1").arg(i)));
        deliver(w, "/root", root, true);
        auto a = w.fileItem("/root/a"), b = w.fileItem("/root/b");
        a->setExpanded(true); b->setExpanded(true);
        deliver(w, "/root/a", {entry("/root/a/removed"), entry("/root/a/kept")});
        deliver(w, "/root/b", {entry("/root/b/selected")});
        auto selected = w.fileItem("/root/b/selected");
        w.files->setCurrentItem(selected); selected->setSelected(true);
        w.files->setFocus(); w.files->verticalScrollBar()->setValue(15); settle();
        const int scroll = w.files->verticalScrollBar()->value();
        takeJobs(w);
        const int deletion = w.nextJob++;
        w.fileMutations[deletion] = "/root/a/removed";
        w.session->completed("delete", deletion, {{"path", "/root/a/removed"}});
        auto jobs = takeJobs(w);
        QCOMPARE(jobs.size(), 1); QCOMPARE(jobs[0].path, QString("/root/a"));
        w.session->completed("list", jobs[0].id, {{"path", "/root/a"}, {"entries", QVariantList{entry("/root/a/kept")}}});
        settle();
        QCOMPARE(w.fileItem("/root/b"), b); QCOMPARE(w.fileItem("/root/b/selected"), selected);
        QVERIFY(b->isExpanded() && a->isExpanded() && selected->isSelected());
        QCOMPARE(w.files->currentItem(), selected); QVERIFY(w.files->hasFocus());
        QCOMPARE(w.files->verticalScrollBar()->value(), scroll);
        w.requestDirectory("/root/a"); const int stale = w.nextJob - 1;
        w.syncFileMutation("/root/a", true);
        w.session->completed("list", stale, {{"path", "/root/a"}, {"entries", QVariantList{entry("/root/a/ghost")}}});
        QVERIFY(!w.fileItem("/root/a/ghost"));
        w.requestDirectory("/root/b"); const int oldGeneration = w.nextJob - 1;
        w.navigate("/elsewhere");
        w.session->completed("list", oldGeneration, {{"path", "/root/b"}, {"entries", QVariantList{}}});
        QCOMPARE(w.fileItem("/root/b/selected"), selected);
    }
    void recursivePartialFailureRefreshesOnlyLoadedAffectedDirectories() {
        Workspace w(profile(), {}, true); enableFiles(w);
        deliver(w, "/root", {entry("/root/a", true), entry("/root/b", true)}, true);
        deliver(w, "/root/a", {entry("/root/a/child", true)});
        deliver(w, "/root/a/child", {entry("/root/a/child/left")});
        deliver(w, "/root/b", {entry("/root/b/untouched")});
        takeJobs(w);
        w.syncFileMutation("/root/a", false);
        QSet<QString> requested;
        for (const auto &j : takeJobs(w)) requested.insert(j.path);
        QCOMPARE(requested, (QSet<QString>{"/root", "/root/a", "/root/a/child"}));
        QPersistentModelIndex oldChild = w.files->indexFromItem(w.fileItem("/root/a/child"));
        deliver(w, "/root", {entry("/root/a", true, true), entry("/root/b", true)}, true);
        QVERIFY(!oldChild.isValid());
        QCOMPARE(w.fileItem("/root/a")->childCount(), 0);
        QVERIFY(w.fileItem("/root/b/untouched"));
    }
    void inlineCreationTargetsAndFailureRecovery() {
        Workspace w(profile(), {}, true); enableFiles(w);
        deliver(w, "/root", {entry("/root/target", true), entry("/root/other", true)}, true);
        takeJobs(w);
        auto target = w.fileItem("/root/target");
        w.files->setCurrentItem(w.fileItem("/root/other"));
        w.fileItem("/root/other")->setSelected(true);
        bool chosen = false;
        QTimer chooseAction;
        connect(&chooseAction, &QTimer::timeout, &w, [&] {
            auto menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            for (auto action : menu->actions()) if (action->text() == "新建文件") {
                chooseAction.stop(); chosen = true;
                QTest::mouseClick(menu, Qt::LeftButton, {}, menu->actionGeometry(action).center());
                return;
            }
            menu->close(); chooseAction.stop();
        });
        chooseAction.start(10);
        const auto point = w.files->visualItemRect(target).center();
        QTest::mouseClick(w.files->viewport(), Qt::RightButton, {}, point);
        // Offscreen has no window-system context-menu synthesis after a mouse click.
        if (!chosen) {
            QContextMenuEvent context(QContextMenuEvent::Mouse, point,
                                      w.files->viewport()->mapToGlobal(point));
            QApplication::sendEvent(w.files->viewport(), &context);
        }
        chooseAction.stop(); QVERIFY(chosen);
        QCOMPARE(w.directory, QString("/root"));
        QCOMPARE(w.entryDraft.parentPath, QString("/root/target"));
        QVERIFY(w.entryDraft.waiting && !w.entryDraft.editor);
        auto load = takeJobs(w); QCOMPARE(load.size(), 1);
        w.session->completed("list", load[0].id, {{"path", "/root/target"},
                             {"entries", QVariantList{entry("/root/target/existing")}}});
        settle(); QVERIFY(w.entryDraft.editor);
        QCOMPARE(w.files->itemFromIndex(w.entryDraft.row)->parent(), target);
        auto edit = w.entryDraft.editor;
        edit->setText("existing"); QTest::keyClick(edit, Qt::Key_Return); settle();
        QCOMPARE(takeJobs(w).size(), 0); QCOMPARE(w.entryDraft.editor, edit);
        edit->setText("../invalid"); QTest::keyClick(edit, Qt::Key_Return); settle();
        QCOMPARE(takeJobs(w).size(), 0);
        edit->setText("中文 file");
        w.files->setCurrentItem(w.fileItem("/root/other"));
        QTest::keyClick(edit, Qt::Key_Return); settle();
        auto jobs = takeJobs(w); QCOMPARE(jobs.size(), 1);
        QCOMPARE(jobs[0].path, QString("/root/target/中文 file"));
        QCOMPARE(jobs[0].type, QString("touch"));
        w.session->failed("touch", jobs[0].id, "fixture permission denied"); settle();
        QCOMPARE(w.entryDraft.editor, edit); QCOMPARE(edit->text(), QString("中文 file")); QVERIFY(edit->isEnabled());
        takeJobs(w); edit->setText("corrected"); QTest::keyClick(edit, Qt::Key_Return); settle();
        jobs = takeJobs(w); QCOMPARE(jobs.size(), 1);
        w.session->completed("touch", jobs[0].id, {{"path", jobs[0].path}}); settle();
        QVERIFY(!w.entryDraft.editor);
        w.createEntry(true, "/root/target");
        QTest::keyClick(w.entryDraft.editor, Qt::Key_Escape); settle();
        QVERIFY(!w.entryDraft.editor);
        w.createEntry(false, "/root/target");
        w.pathEdit->setFocus(); // no text: cancel on loss
        QFocusEvent focus(QEvent::FocusOut); QApplication::sendEvent(w.entryDraft.editor, &focus); settle();
        QVERIFY(!w.entryDraft.editor);
    }
    void actualNavigationAndNewButtonFinishNamedDraftOnce() {
        Workspace w(profile(), {}, true); enableFiles(w);
        deliver(w, "/root", {entry("/root/target", true), entry("/root/other", true)}, true);
        deliver(w, "/root/target", {}); takeJobs(w);
        auto verifyCreate = [&](QString name) {
            int creates = 0;
            for (const auto &j : takeJobs(w)) if (j.type == "touch") {
                ++creates; QCOMPARE(j.path, "/root/target/" + name);
            }
            QCOMPARE(creates, 1);
        };
        w.createEntry(false, "/root/target"); w.entryDraft.editor->setText("breadcrumb");
        auto crumb = w.breadcrumbs->findChildren<QPushButton *>().last();
        QTest::mouseClick(crumb, Qt::LeftButton); settle(); verifyCreate("breadcrumb");
        w.createEntry(false, "/root/target"); w.entryDraft.editor->setText("doubleclick");
        const auto point = w.files->visualItemRect(w.fileItem("/root/other")).center();
        QTest::mouseDClick(w.files->viewport(), Qt::LeftButton, {}, point); settle(); verifyCreate("doubleclick");
        w.createEntry(false, "/root/target"); w.entryDraft.editor->setText("newbutton");
        QToolButton *newButton = nullptr;
        for (auto b : w.findChildren<QToolButton *>()) if (b->accessibleName() == "新建文件") newButton = b;
        QVERIFY(newButton); QTest::mouseClick(newButton, Qt::LeftButton); settle(); verifyCreate("newbutton");
        QVERIFY(w.entryDraft.editor); QCOMPARE(w.entryDraft.parentPath, QString("/root"));
        w.entryDraft.editor->setText("bad/name");
        QTest::mouseClick(newButton, Qt::LeftButton); settle();
        QCOMPARE(w.entryDraft.editor->text(), QString("bad/name"));
        QVERIFY(takeJobs(w).isEmpty());
        w.session->disconnected("fixture disconnect"); settle();
        QVERIFY(!w.entryDraft.editor);
    }
    void terminalPanelMaximizeRestoreRetainsObjects() {
        Workspace w(profile(), {}, true); w.resize(1100, 720); w.show(); settle();
        auto document = w.editors.cbegin().value()->code->document();
        auto original = w.terminals.begin().value();
        const int originalCount = w.terminals.size();
        const auto sizes = w.verticalSplit->sizes();
        w.maximizeTerminal->click(); settle();
        QVERIFY(w.terminalMaximized && w.editorPanel->isHidden());
        QVERIFY(w.sidebar->isVisible()); QVERIFY(w.connectionStatus->isVisible());
        QCOMPARE(w.terminals[original.id].view, original.view);
        QVERIFY(!w.isFullScreen());
        w.maximizeTerminal->click(); settle();
        QVERIFY(!w.terminalMaximized && w.editorPanel->isVisible());
        QCOMPARE(w.verticalSplit->sizes(), sizes);
        QCOMPARE(w.editors.cbegin().value()->code->document(), document);
        w.setTerminalMaximized(true); w.hideTerminalPanel(); settle();
        QVERIFY(w.editorPanel->isVisible() && w.terminalPanel->isHidden());
        QVERIFY(!w.maximizeTerminal->isChecked());
        w.addTerminal(original.group); settle();
        QCOMPARE(w.terminals.size(), originalCount + 1);
        QCOMPARE(w.terminalGroups[original.group]->orientation(), Qt::Horizontal);
        for (auto combo : w.findChildren<QComboBox *>()) QVERIFY(combo->accessibleName() != "终端拆分方向");
    }
    void networkPreferenceIsPerProfileAndOnlyUserActivationPersists() {
        QSettings().setValue("network/one/interface", "eth1");
        QSettings().setValue("network/two/interface", "eth0");
        Workspace one(profile("one"), {}, true), two(profile("two"), {}, true);
        one.monitor(network({"eth0", "eth1"}), 1); two.monitor(network({"eth1", "eth0"}), 1);
        one.resize(1100,720); one.show(); settle();
        auto popup=one.nic->view()->window(); WorkspacePopupEvents popupEvents;
        popup->installEventFilter(&popupEvents); one.nic->showPopup(); settle();
        QVERIFY(popup->isVisible()); QCOMPARE(popupEvents.shows,1); QCOMPARE(popupEvents.hides,0);
        QCOMPARE(one.nic->currentText(), QString("eth1")); QCOMPARE(two.nic->currentText(), QString("eth0"));
        QSignalSpy resets(one.nic->model(), &QAbstractItemModel::modelReset);
        QSignalSpy removed(one.nic->model(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(one.nic->model(), &QAbstractItemModel::rowsInserted);
        QPersistentModelIndex nicIdentity = one.nic->model()->index(1, 0);
        one.monitor(network({"eth0", "eth1"}), 1);
        QCOMPARE(resets.size(), 0); QCOMPARE(removed.size(), 0); QCOMPARE(inserted.size(), 0);
        QVERIFY(popup->isVisible()); QCOMPARE(popupEvents.shows,1); QCOMPARE(popupEvents.hides,0);
        QVERIFY(nicIdentity.isValid()); QCOMPARE(nicIdentity.data().toString(), QString("eth1"));
        one.monitor(network({"eth1", "eth0"}), 1); QCOMPARE(one.nic->currentText(), QString("eth1"));
        QVERIFY(!popup->isVisible()); QCOMPARE(popupEvents.shows,1); QCOMPARE(popupEvents.hides,1);
        one.monitor(network({"eth0"}), 1); QCOMPARE(one.nic->currentText(), QString("eth0"));
        QCOMPARE(QSettings().value("network/one/interface").toString(), QString("eth1"));
        one.monitor(network({"eth0", "eth1"}), 1); QCOMPARE(one.nic->currentText(), QString("eth1"));
        one.nic->setCurrentIndex(0); one.nic->activated(0);
        Workspace reopened(profile("one"), {}, true);
        reopened.monitor(network({"eth1", "eth0"}), 1); QCOMPARE(reopened.nic->currentText(), QString("eth0"));
        QCOMPARE(QSettings().value("network/two/interface").toString(), QString("eth0"));
        popup->removeEventFilter(&popupEvents);
    }
    void terminalSelectionBoundActionsRenameAndDirectClose() {
        Workspace w(profile(), {}, true); enableFiles(w);
        const int first = w.focusedTerm, group = w.terminals[first].group;
        for (int id : w.terminals.keys()) if (id != first) w.closeTerminal(id);
        settle(); takeJobs(w);
        w.addTerminal(group); const int second = w.focusedTerm;
        w.addTerminal(group); const int third = w.focusedTerm;
        settle(); takeJobs(w);
        auto row = [&](int id) -> QTreeWidgetItem * {
            for (QTreeWidgetItemIterator it(w.terminalManager); *it; ++it)
                if ((*it)->data(0, Qt::UserRole).toInt() == id) return *it;
            return nullptr;
        };
        auto stableRow = row(first);
        QSignalSpy resets(w.terminalManager->model(), &QAbstractItemModel::modelReset);
        w.activateTerminal(first, true); settle();
        QCOMPARE(w.terminalManager->currentItem(), stableRow);
        QCOMPARE(w.focusedTerm, first);
        QCOMPARE(w.terminalRowId, first);
        const auto point = w.terminalManager->visualItemRect(row(second)).center();
        auto viewport = w.terminalManager->viewport();
        QMouseEvent hover(QEvent::MouseMove, point, viewport->mapToGlobal(point),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &hover);
        QCOMPARE(w.terminalRowId, second);
        QCOMPARE(w.focusedTerm, first);
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(viewport, &leave);
        QCOMPARE(w.terminalRowId, first);
        emit w.terminalGroups[group]->splitterMoved(300,1);
        QVERIFY(w.terminals[first].view->gridSizeTimer.isActive());
        QVERIFY(w.terminals[second].view->gridSizeTimer.isActive());
        QTRY_VERIFY(!w.terminals[first].view->gridSizeTimer.isActive());
        w.fileFilter->setFocus(); settle();
        QVERIFY(stableRow->isSelected());
        w.maximizeTerminal->click(); settle();
        QCOMPARE(w.terminalManager->currentItem(), stableRow);
        QVERIFY(stableRow->isSelected());
        w.maximizeTerminal->click(); settle();
        w.terminalManager->setFocus();
        QTest::keyClick(w.terminalManager, Qt::Key_Down); settle();
        QCOMPARE(w.focusedTerm, second); QVERIFY(w.terminalManager->hasFocus());
        QTest::keyClick(w.terminalManager, Qt::Key_Down); settle();
        QCOMPARE(w.focusedTerm, third); QVERIFY(w.terminalManager->hasFocus());
        QVERIFY(!row(first)->parent()); QVERIFY(w.currentTerminal->isEnabled());
        w.activateTerminal(first);
        w.renameTerminal(first, "开发终端 / a very long terminal name for tooltip");
        QVERIFY(w.currentTerminal->toolTip().contains("a very long terminal name"));
        QCOMPARE(row(first), stableRow); QCOMPARE(resets.size(), 0);
        w.floatPage(w.terminals[first].frame); settle();
        w.renameTerminal(first, "浮窗重命名");
        QVERIFY(w.floating.value(w.terminals[first].frame)->windowTitle().endsWith("浮窗重命名"));
        w.closePage(w.terminals[first].frame); settle();
        QCOMPARE(w.terminals[first].frame->parentWidget(), w.terminalGroups[group]);
        auto close = w.terminalRowActions->findChild<QToolButton *>("closeSelectedTerminal");
        QVERIFY(close); w.activateTerminal(second); settle();
        QTest::mousePress(close, Qt::LeftButton);
        w.activateTerminal(third); // A focus notification between press/release cannot retarget the action.
        QTest::mouseRelease(close, Qt::LeftButton); settle();
        QVERIFY(!w.terminals.contains(second)); QCOMPARE(w.focusedTerm, third);
        takeJobs(w);
        w.closeTerminal(first); w.closeTerminal(first); settle();
        QCOMPARE(w.focusedTerm, third); QVERIFY(w.terminals.contains(third));
        QVERIFY(w.terminalManager->isHidden()); QVERIFY(w.currentTerminal->isVisible());
        int closes = 0;
        for (const auto &job : takeJobs(w)) {
            QVERIFY(job.type != "terminal-status");
            if (job.type == "close-terminal") { ++closes; QCOMPARE(job.id, first); }
        }
        QCOMPARE(closes, 1); QVERIFY(!QApplication::activeModalWidget());
        w.addTerminal(group); const int last = w.focusedTerm; takeJobs(w);
        w.closeTerminal(last); QCOMPARE(w.focusedTerm, third);
        w.closeTerminal(third); settle();
        QVERIFY(!w.focusedTerm); QVERIFY(w.currentTerminal->text().isEmpty());
        QVERIFY(!w.closeCurrentTerminal->isEnabled() && !w.splitTerminal->isEnabled());
    }
    void fileThreeColumnSelectionAndTargets() {
        Workspace w(profile("three-columns"),{},true);enableFiles(w);
        w.sidebar->setCurrentIndex(1);
        deliver(w,"/root",{entry("/root/A",true),entry("/root/a.txt"),entry("/root/b.txt")},true);
        auto view=w.files;view->setTreeMotionEnabled(false);view->setColumnHidden(2,false);
        view->header()->setSectionResizeMode(0,QHeaderView::Fixed);
        view->header()->setSectionResizeMode(1,QHeaderView::Fixed);
        view->header()->setSectionResizeMode(2,QHeaderView::Fixed);
        view->setColumnWidth(0,160);view->setColumnWidth(1,75);view->setColumnWidth(2,140);
        w.resize(1450,800);
        auto split=w.findChild<QSplitter *>("workspaceSplit");QVERIFY(split);split->setSizes({520,930});settle();
        auto a=w.fileItem("/root/A"),one=w.fileItem("/root/a.txt"),two=w.fileItem("/root/b.txt");
        QVERIFY(a&&one&&two);
        auto point=[&](QTreeWidgetItem *item,int column){
            return QPoint(view->columnViewportPosition(column)+view->columnWidth(column)/2,
                          view->visualItemRect(item).center().y());
        };
        for(int column=0;column<3;++column){
            const auto p=point(one,column);QVERIFY(view->viewport()->rect().contains(p));
            QTest::mouseClick(view->viewport(),Qt::LeftButton,{},p);
            QCOMPARE(view->currentItem(),one);QVERIFY(one->isSelected());
            QCOMPARE(view->rowAt(p),one);QCOMPARE(view->uploadTarget(p).path,QString("/root"));
            QCOMPARE(view->uploadTarget(point(a,column)).path,QString("/root/A"));
        }
        QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::ControlModifier,point(two,2));
        QVERIFY(one->isSelected());QVERIFY(two->isSelected());
        QCOMPARE(view->selectedItems().size(),2);
    }
    void terminalPeerReformationPreservesSessions() {
        Workspace w(profile(), {}, true); enableFiles(w);
        const int first=w.focusedTerm, group=w.terminals[first].group;
        for(int id:w.terminals.keys())if(id!=first)w.closeTerminal(id);
        w.addTerminal(group);const int second=w.focusedTerm;
        w.addTerminal(group);const int third=w.focusedTerm;
        w.addTerminal(group);const int fourth=w.focusedTerm;
        w.addTerminal();const int independent=w.focusedTerm;
        takeJobs(w);settle();
        QHash<int,Terminal *> views;
        for(int id:w.terminals.keys())views[id]=w.terminals[id].view;
        auto verify=[&](const QList<int> &ids,const QList<int> &positions){
            QCOMPARE(w.terminalManager->topLevelItemCount(),ids.size());
            for(int i=0;i<ids.size();++i){
                auto row=w.terminalManager->topLevelItem(i);QVERIFY(row);
                QCOMPARE(row->childCount(),0);QCOMPARE(row->data(0,Qt::UserRole).toInt(),ids[i]);
                QCOMPARE(row->data(0,TerminalPeerPositionRole).toInt(),positions[i]);
                QCOMPARE(w.terminals[ids[i]].view,views[ids[i]]);
            }
        };
        verify({first,second,third,fourth,independent},{1,2,2,3,0});
        w.closeTerminal(first);verify({second,third,fourth,independent},{1,2,3,0});
        w.closeTerminal(third);verify({second,fourth,independent},{1,3,0});
        w.closeTerminal(fourth);verify({second,independent},{0,0});
        const auto jobs=takeJobs(w);QSet<int> closed;
        for(const auto &job:jobs){QVERIFY(job.type!="open-terminal");if(job.type=="close-terminal")closed.insert(job.id);}
        QCOMPARE(closed,QSet<int>({first,third,fourth}));
        w.closeTerminal(independent);QVERIFY(w.terminalManager->isHidden());
        QCOMPARE(w.terminals[second].view,views[second]);
    }
    void deleteKeyFocusConfirmationAndSessionIdentity() {
        Workspace w(profile(), {}, true); enableFiles(w);
        deliver(w, "/root", {entry("/root/a", true), entry("/root/b")}, true);
        deliver(w, "/root/a", {entry("/root/a/child")}); takeJobs(w);
        w.files->setCurrentItem(w.fileItem("/root/a"));
        w.fileItem("/root/a")->setSelected(true); w.fileItem("/root/a/child")->setSelected(true);
        int prompts = 0; bool accept = false, disconnect = false, replaceSession = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &w, [&] {
            auto box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) return;
            ++prompts; QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel));
            if (disconnect) w.session->disconnected("during delete confirmation");
            if (replaceSession) w.session = new Session(w.profile, {}, &w);
            box->button(accept ? QMessageBox::Yes : QMessageBox::Cancel)->click();
        });
        responder.start(5); w.files->setFocus();
        QTest::keyClick(w.files, Qt::Key_Delete); QCOMPARE(prompts, 1); QVERIFY(takeJobs(w).isEmpty());
        accept = true; w.activateWindow(); w.files->setFocus(); settle(); QVERIFY(w.files->hasFocus()); QTest::keyClick(w.files, Qt::Key_Delete);
        QCOMPARE(prompts, 2);
        auto jobs = takeJobs(w); QCOMPARE(jobs.size(), 2); QCOMPARE(jobs[1].type,QString("journalSeal")); QCOMPARE(jobs[0].path, QString("/root/a"));
        QCOMPARE(jobs[0].type, QString("delete")); QVERIFY(jobs[0].recursive);
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Delete, {}, {}, true);
        QApplication::sendEvent(w.files, &repeat); QCOMPARE(prompts, 2);
        for (auto edit : {w.fileFilter, w.pathEdit}) {
            if (edit == w.pathEdit) w.editDirectoryPath();
            edit->setFocus(); edit->setText("abc"); edit->setCursorPosition(0);
            QTest::keyClick(edit, Qt::Key_Delete); QCOMPARE(edit->text(), QString("bc"));
        }
        w.createEntry(false); QVERIFY(w.entryDraft.editor);
        w.entryDraft.editor->setText("abc"); w.entryDraft.editor->setCursorPosition(0);
        QTest::keyClick(w.entryDraft.editor, Qt::Key_Delete); QCOMPARE(w.entryDraft.editor->text(), QString("bc"));
        w.cancelEntryDraft();
        auto editor = w.editors.cbegin().value(); editor->code->setFocus(); QTest::keyClick(editor->code, Qt::Key_Delete);
        w.terminals[w.focusedTerm].view->setFocus(); QTest::keyClick(w.terminals[w.focusedTerm].view, Qt::Key_Delete);
        QCOMPARE(prompts, 2); takeJobs(w);
        w.files->clearSelection(); w.activateWindow(); w.files->setFocus(); settle(); QTest::keyClick(w.files, Qt::Key_Delete); QCOMPARE(prompts, 2);
        w.fileFilter->clear(); settle();
        w.fileItem("/root/b")->setSelected(true);
        disconnect = true; w.activateWindow(); w.files->setFocus(); settle(); QTest::keyClick(w.files, Qt::Key_Delete);
        QCOMPARE(prompts, 3); QVERIFY(takeJobs(w).isEmpty());
        w.online = true; disconnect = false;
        auto origin = w.session;
        replaceSession = true;
        w.activateWindow(); w.files->setFocus(); settle(); QTest::keyClick(w.files, Qt::Key_Delete);
        QCOMPARE(prompts, 4); QVERIFY(takeJobs(w).isEmpty()); QVERIFY(origin->jobs.empty());
        responder.stop();
    }
    void pathBlankClickNavigationCancelAndTerminalControlL() {
        Workspace w(profile(), {}, true); enableFiles(w); takeJobs(w);
        const auto blank = QPoint(w.breadcrumbs->width()-8, w.breadcrumbs->height()/2);
        const int height = w.addressStack->height();
        QTest::mouseClick(w.breadcrumbs, Qt::RightButton, {}, blank);
        QCOMPARE(w.addressStack->currentWidget(), w.breadcrumbs);
        QTest::mouseClick(w.breadcrumbs, Qt::LeftButton, {}, blank);
        QCOMPARE(w.addressStack->currentWidget(), w.pathEdit);
        QCOMPARE(w.pathEdit->selectedText(), w.directory); QCOMPARE(w.addressStack->height(), height);
        w.pathEdit->setText("/uncommitted"); QTest::keyClick(w.pathEdit, Qt::Key_Escape);
        QCOMPARE(w.pathEdit->text(), w.directory); QVERIFY(takeJobs(w).isEmpty());
        QTest::mouseClick(w.breadcrumbs, Qt::LeftButton, {}, blank);
        w.pathEdit->setText("/uncommitted"); w.fileFilter->setFocus(); settle();
        QCOMPARE(w.addressStack->currentWidget(), w.breadcrumbs); QCOMPARE(w.pathEdit->text(), w.directory);
        QVERIFY(takeJobs(w).isEmpty());
        auto root = w.breadcrumbs->findChildren<QPushButton *>().first();
        QTest::mouseClick(root, Qt::LeftButton); auto jobs = takeJobs(w);
        QCOMPARE(jobs.size(), 1); QCOMPARE(jobs[0].path, QString("/"));
        QCOMPARE(w.addressStack->currentWidget(), w.breadcrumbs);
        w.editDirectoryPath(); w.pathEdit->setText("/committed"); QTest::keyClick(w.pathEdit, Qt::Key_Return);
        jobs = takeJobs(w); QCOMPARE(jobs.size(), 1); QCOMPARE(jobs[0].path, QString("/committed"));
        auto terminal = w.terminals[w.focusedTerm].view; terminal->setFocus();
        QTest::keyClick(terminal, Qt::Key_L, Qt::ControlModifier);
        jobs = takeJobs(w); QVERIFY(std::any_of(jobs.cbegin(), jobs.cend(), [](const Job &job) {
            return job.type == "input" && job.data == QByteArray(1, char(12));
        }));
    }
    void transferMeterPauseResetsSamplingAndAttempt() {
        Session session(profile(), {}); auto &meter = session.transferMeter;
        meter.begin(1); meter.bytes = 1024;
        QTest::qWait(130); session.reportTransfer();
        const auto before = meter.activeMs();
        session.pauseTransfer(true);
        std::thread resume([&] { std::this_thread::sleep_for(std::chrono::milliseconds(240)); session.pauseTransfer(false); });
        session.checkTransfer(); resume.join();
        QVERIFY(meter.activeMs() - before < 80);
        QCOMPARE(meter.sampledBytes, quint64(1024));
        QSignalSpy stats(&session, &Session::transferStatistics);
        meter.bytes += 512; QTest::qWait(130); session.reportTransfer();
        QCOMPARE(stats.size(), 1); QVERIFY(stats.last()[3].toDouble() > 0);
        meter.begin(2); QCOMPARE(meter.bytes, quint64(0)); QCOMPARE(meter.sampledBytes, quint64(0));
        QCOMPARE(meter.excludedMs, qint64(0)); session.reportTransfer(true);
        QCOMPARE(stats.last()[0].toInt(), 2); QCOMPARE(stats.last()[1].toULongLong(), quint64(0));
        QCOMPARE(stats.last()[3].toDouble(), 0.0);
    }
    void workspaceTreeAnimationEvidence() {
        const auto output=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path());
        QVERIFY(QDir().mkpath(output));
        Workspace w(profile("tree-evidence"),{},true);enableFiles(w);w.resize(1280,800);
        w.sidebar->setCurrentIndex(1);
        deliver(w,"/root",{entry("/root/project",true),entry("/root/assets",true),entry("/root/README.md")},true);
        deliver(w,"/root/project",{entry("/root/project/src",true),entry("/root/project/main.cpp"),entry("/root/project/settings.json")});
        auto folder=w.fileItem("/root/project");QVERIFY(folder);folder->setExpanded(false);
        w.addTerminal(w.terminals[w.focusedTerm].group);w.addTerminal();settle();
        for(bool light:{false,true}){
            applyTheme(light);w.refreshTheme();settle();
            folder->setExpanded(false);QTest::qWait(400);
            const QString prefix=output+(light?"/actual-workspace-light":"/actual-workspace-dark");
            QVERIFY(w.grab().save(prefix+"-closed.png"));
            folder->setExpanded(true);
            for(int n=0;n<8;++n){QTest::qWait(35);QVERIFY(w.grab().save(prefix+QString("-frame-%1.png").arg(n,2,10,QChar('0'))));}
            QTest::qWait(350);QVERIFY(w.grab().save(prefix+"-open.png"));
        }
    }
    void themeEvidence_data() {
        QTest::addColumn<bool>("light");
        QTest::newRow("dracula") << false;
        QTest::newRow("github-light") << true;
    }
    void themeEvidence() {
        QFETCH(bool, light); applyTheme(light);
        Workspace w(profile(), {}, true); enableFiles(w); w.resize(1280, 800);
        deliver(w, "/root", {entry("/root/project", true), entry("/root/assets", true), entry("/root/README.md")}, true);
        deliver(w, "/root/project", {entry("/root/project/main.cpp"), entry("/root/project/settings.json")});
        w.createEntry(false, "/root/project"); w.entryDraft.editor->setText("新建配置.json"); settle();
        QVERIFY(w.entryDraft.editor->height() >= w.entryDraft.editor->sizeHint().height());
        QVERIFY(w.entryDraft.editor->width() > 160);
        const auto directory = qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR", data.path());
        QDir().mkpath(directory);
        const auto prefix = directory + (light ? "/workspace-light" : "/workspace-dark");
        QVERIFY(w.grab().save(prefix + "-inline.png"));
        w.cancelEntryDraft();
        w.addTerminal(w.terminals[w.focusedTerm].group);
        w.renameTerminal(w.focusedTerm, "构建会话 build / Qt 6.8.3");
        w.activateTerminal(w.focusedTerm, true); settle();
        QVERIFY(w.grab().save(prefix + "-terminal.png"));
        w.editDirectoryPath(); settle();
        QVERIFY(w.grab().save(prefix + "-path-edit.png"));
        QTest::keyClick(w.pathEdit, Qt::Key_Escape);
        w.transferSession = new Session(w.profile, {}, &w); w.attachSession(w.transferSession, true);
        auto batch = new QTreeWidgetItem(w.uploadList, {"上传 · 2 项", "传输中"}); batch->setExpanded(true);
        Job job; job.type = "upload"; job.id = 900; job.path = "/root/project/a-very-long-build-archive.tar.gz";
        w.transfers[900] = job; w.activeTransfer = 900;
        w.transferItems[900] = new QTreeWidgetItem(batch, {"a-very-long-build-archive.tar.gz", "传输中"});
        w.transferSession->transferProgress(900, job.path, 65536, 1048576);
        w.transferSession->transferStatistics(900, 65536, 250, 262144, false);
        Job settings; settings.id = 901; settings.type = "upload"; settings.path = "/root/settings.json";
        w.transfers[901] = settings;
        w.transferItems[901] = new QTreeWidgetItem(batch, {"settings.json", "等待"});
        w.transferItems[900]->setData(0, Qt::UserRole, 900);
        w.transferItems[901]->setData(0, Qt::UserRole, 901);
        // Run the same completed handler for a previous task before showing the current one.
        w.activeTransfer = 901;
        w.transferSession->completed("upload", 901, {{"completed", 1}, {"payloadBytes", 12800}, {"activeMs", 1000}});
        w.activeTransfer = 900;
        w.transferSession->transferProgress(900, job.path, 65536, 1048576);
        w.transferSession->transferStatistics(900, 65536, 250, 262144, false);
        w.updateTransferBatches();
        w.transferPanel->show(); w.transferPanel->raise(); settle();
        QVERIFY(w.grab().save(prefix + "-transfer.png"));
        QVERIFY(w.transferItems[900]->text(1).contains("当前文件"));
        w.transferSession->completed("upload", 900, {{"completed", 1}, {"payloadBytes", 1048576}, {"activeMs", 2000}});
        QVERIFY(w.transferItems[900]->text(1).contains("平均"));
        QVERIFY(!w.taskSummary->text().contains("/s"));
        QCOMPARE(batch->text(1), QString("2 结束 · 0 等待/传输 · 0 失败"));
        QVERIFY(w.grab().save(prefix + "-transfer-complete.png"));
        w.transferPanel->hide(); w.maximizeTerminal->click(); settle();
        QVERIFY(w.terminalManager->currentItem()->isSelected());
        QVERIFY(w.grab().save(prefix + "-maximized.png"));
        w.maximizeTerminal->click(); settle();
        QVERIFY(w.grab().save(prefix + "-restored.png"));
    }
};
}
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv); rs::WorkspaceTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "test_workspace.moc"
