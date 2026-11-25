#include "../src/ui/workspace.h"
#include "../src/ui/mainwindow.h"
#include "../src/ui/sshprompts.h"
#include "../src/core/operationgate.h"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
namespace rs {
class PortsMemoryBackend : public SecretBackend {
  public:
    QByteArray value="fixture-only";
    SecretResult read(const QString &) override {return {SecretResult::Found,value,{}};}
    QString write(const QString &,const QByteArray &secret) override {value=secret;return {};}
    QString remove(const QString &) override {value.clear();return {};}
};
class PortsTests : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    QJsonObject fixture;
    Profile profile() {
        Profile p;p.id=QUuid::createUuid().toString(QUuid::WithoutBraces);p.host="127.0.0.1";p.user="fixture";
        p.port=fixture.value("port").toInt();p.name="Loopback ports";p.timeout=5;return p;
    }
    TunnelRule rule(const QString &profileId,quint16 local=18080,quint16 remote=8080) {
        TunnelRule r;r.profileId=profileId;r.localPort=local;r.remotePort=remote;return r;
    }
    void settle() {QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
    void type(QLineEdit *field,const QString &value) {
        field->setFocus();field->selectAll();QTest::keyClicks(field,value);if(value.isEmpty())QTest::keyClick(field,Qt::Key_Backspace);
    }
    void show(PortsPanel &panel) {panel.resize(640,350);panel.show();panel.activateWindow();settle();}
    void verifyColumns(PortsPanel &panel) {
        const int width=panel.table->viewport()->width();int total=0;
        QCOMPARE(panel.table->model()->columnCount(),5);
        for(int column=0;column<5;++column) {
            const int left=panel.table->columnViewportPosition(column),size=panel.table->columnWidth(column);
            QVERIFY(size>=uiPx(60));if(column==0)QVERIFY(left<=0);total+=size;
        }
        QVERIFY(total>=width);
        if(total>width)QVERIFY(panel.table->horizontalScrollBar()->maximum()>0);
    }
    quint16 unusedPort() {QTcpServer s;if(!s.listen(QHostAddress::LocalHost,0))return 0;return s.serverPort();}
    QJsonObject control(const QByteArray &command) {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, quint16(fixture["control"].toInt()));
        if (!socket.waitForConnected(2000)) return {};
        socket.write(command + '\n');
        QByteArray bytes;
        while (!bytes.endsWith('\n') && socket.waitForReadyRead(2000)) bytes += socket.readAll();
        return QJsonDocument::fromJson(bytes).object();
    }
    bool exchange(quint16 port) {
        QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,port);
        if(!QTest::qWaitFor([&]{return socket.state()==QAbstractSocket::ConnectedState;},8000))return false;
        const QByteArray bytes=QByteArray("binary\0echo",11)+QByteArray(40000,'x');socket.write(bytes);
        if(!QTest::qWaitFor([&]{return socket.bytesAvailable()>=bytes.size();},10000))return false;
        return socket.readAll()==bytes;
    }
    void answerTrust(QTimer &timer) {
        connect(&timer,&QTimer::timeout,&timer,[]{
            if(auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if(box->windowTitle().startsWith("首次连接"))box->button(QMessageBox::Yes)->click();
            }
        });timer.start(10);
    }
  private slots:
    void initTestCase() {
        QVERIFY(directory.isValid());QCOMPARE(QGuiApplication::platformName(),QString("offscreen"));
        qputenv("RABBITSHELL_DATA_DIR",directory.path().toUtf8());QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellPortsTests");QCoreApplication::setApplicationName("RabbitShellPortsTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
        QApplication::setStyle("Fusion");
        for(auto font:{"C:/Windows/Fonts/msyh.ttc","C:/Windows/Fonts/consola.ttf"})if(QFileInfo::exists(font))QFontDatabase::addApplicationFont(font);
        QApplication::setFont(QFont("Microsoft YaHei UI",10));applyTheme(false);
        const auto path=qEnvironmentVariable("RABBITSHELL_TUNNEL_FIXTURE");
        if(!path.isEmpty()){QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));fixture=QJsonDocument::fromJson(file.readAll()).object();}
    }
    void mapListeningAndTrafficDisplay(){
        const auto p=profile();PortsPanel panel(p.id);show(panel);
        QVERIFY(panel.ensureMapping("0.0.0.0",8080));QCOMPARE(panel.rules().size(),1);auto first=panel.rules().first();
        QCOMPARE(first.remoteAddress,QString("127.0.0.1"));QCOMPARE(first.remotePort,quint16(8080));QCOMPARE(panel.state(first.id),QString("stopped"));
        QVERIFY(panel.ensureMapping("127.0.0.1",8080));QCOMPARE(panel.rules().size(),1);
        QVERIFY(panel.ensureMapping("::",8080));QCOMPARE(panel.rules().size(),2);QCOMPARE(panel.rules().last().remoteAddress,QString("::1"));
        panel.setRuleState(first.id,"running");panel.setRuleTraffic(first.id,2048,4096);const int row=panel.rowFor(first.id);
        QCOMPARE(panel.table->model()->index(row,2).data().toString(),QString("2.0 KiB/s"));QCOMPARE(panel.table->model()->index(row,3).data().toString(),QString("4.0 KiB/s"));
        panel.setRuleState(first.id,"stopped");QCOMPARE(panel.table->model()->index(row,2).data().toString(),QString("0 B/s"));
        panel.beginEdit();QVERIFY(panel.hasDraft());QVERIFY(!panel.ensureMapping("127.0.0.1",9000));QVERIFY(panel.hasDraft());panel.cancelDraft();
    }
    void compactColumnsAndEmptyCard(){
        const auto p=profile();PortsPanel panel(p.id);show(panel);
        QVERIFY(panel.tableCard->isHidden());QVERIFY(panel.empty->isVisible());
        const QStringList columns{"本地端口","远端端口","上传速率","下载速率","状态"};
        for(int col=0;col<5;++col)QCOMPARE(panel.table->model()->headerData(col,Qt::Horizontal).toString(),columns[col]);
        QVERIFY(panel.ensureMapping("127.0.0.1",8080));QVERIFY(panel.tableCard->isVisible());const auto r=panel.rules().first();panel.setRuleState(r.id,"running");
        QSignalSpy stop(&panel,&PortsPanel::stopRequested);const auto index=panel.table->model()->index(0,4);const auto rect=panel.table->visualRect(index);
        QTest::mouseClick(panel.table->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(rect.left()+uiPx(26),rect.center().y()));QCOMPARE(stop.count(),1);
        panel.setRuleState(r.id,"stopped");QVERIFY(panel.table->rowHeight(0)<uiPx(55));panel.remove(r.id);QVERIFY(panel.tableCard->isHidden());
        const auto output=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR");if(!output.isEmpty())for(bool light:{false,true}){applyTheme(light);panel.refreshTheme();settle();QVERIFY(panel.grab().save(output+QString("/ports-empty-%1.png").arg(light?"light":"dark")));}
    }
    void portCellsRequireDoubleClick(){
        const auto p=profile();auto r=rule(p.id);QVERIFY(saveTunnelRules(p.id,{r}));PortsPanel panel(p.id);show(panel);
        for(int column:{0,1}){
            const auto cell=panel.table->visualRect(panel.table->model()->index(0,column)).center();
            QTest::mouseClick(panel.table->viewport(),Qt::LeftButton,Qt::NoModifier,cell);settle();QVERIFY(!panel.hasDraft());QCOMPARE(panel.table->currentIndex().row(),0);
            QTest::mouseDClick(panel.table->viewport(),Qt::LeftButton,Qt::NoModifier,cell);settle();QVERIFY(panel.hasDraft());
            QCOMPARE(QApplication::focusWidget(),static_cast<QWidget *>(column==0?panel.localInput:panel.remoteInput));panel.cancelDraft();settle();
        }
        panel.setRuleState(r.id,"running");QSignalSpy stop(&panel,&PortsPanel::stopRequested);const auto rect=panel.table->visualRect(panel.table->model()->index(0,4));
        QTest::mouseClick(panel.table->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(rect.left()+uiPx(26),rect.center().y()));QCOMPARE(stop.count(),1);QVERIFY(!panel.hasDraft());
    }
    void draftKeyboardBlurAndPersistence() {
        const auto p=profile();QWidget root;auto layout=new QVBoxLayout(&root);auto panel=new PortsPanel(p.id);
        auto outside=new QLineEdit;layout->addWidget(panel);layout->addWidget(outside);root.resize(660,400);root.show();root.activateWindow();settle();
        QVERIFY(panel->empty->isVisible());QCOMPARE(panel->table->model()->columnCount(),5);
        QTest::mouseClick(panel->emptyAdd,Qt::LeftButton);type(panel->remoteInput,"8080");QCOMPARE(panel->localInput->text(),QString("8080"));
        type(panel->localInput,"18080");type(panel->remoteInput,"8081");QCOMPARE(panel->localInput->text(),QString("18080"));
        QTest::keyClick(panel->localInput,Qt::Key_Tab);settle();QVERIFY(panel->hasDraft());QCOMPARE(QApplication::focusWidget(),panel->remoteInput);
        QTest::keyClick(panel->remoteInput,Qt::Key_Return);QVERIFY(!panel->hasDraft());QCOMPARE(panel->rules().size(),1);
        const auto saved=panel->rules().first();QCOMPARE(saved.localPort,quint16(18080));QCOMPARE(panel->state(saved.id),QString("stopped"));
        auto persisted=loadTunnelRules(p.id);QCOMPARE(persisted.size(),1);QVERIFY(!persisted.first().autoStart);
        panel->beginEdit();QTest::mouseClick(outside,Qt::LeftButton);settle();QVERIFY(!panel->hasDraft());
        panel->beginEdit();type(panel->remoteInput,"3000");QTest::mouseClick(outside,Qt::LeftButton);settle();QVERIFY(!panel->hasDraft());QCOMPARE(panel->rules().size(),2);
        panel->beginEdit();
        for(auto bad:{"0","1.2","letters","65536","-1"}) {
            type(panel->remoteInput,bad);QTest::keyClick(panel->remoteInput,Qt::Key_Return);QVERIFY(panel->hasDraft());QVERIFY(!panel->remoteError->text().isEmpty());
        }
        type(panel->remoteInput,"");type(panel->localInput,"44");QTest::mouseClick(outside,Qt::LeftButton);settle();QVERIFY(panel->hasDraft());
        QTest::keyClick(panel->localInput,Qt::Key_Escape);QVERIFY(!panel->hasDraft());
        PortsPanel reopened(p.id,true);QCOMPARE(reopened.rules().size(),2);QCOMPARE(reopened.state(saved.id),QString("stopped"));
    }
    void runningDraftOtherRuleAndDeletionFailure() {
        const auto p=profile();auto a=rule(p.id),b=rule(p.id,15432,5432);QVERIFY(saveTunnelRules(p.id,{a,b}));
        PortsPanel panel(p.id);show(panel);panel.setRuleState(a.id,"running");panel.setRuleState(b.id,"running");
        QSignalSpy restart(&panel,&PortsPanel::restartRequested),stop(&panel,&PortsPanel::stopRequested);
        panel.beginEdit(a.id);type(panel.remoteInput,"8181");QVERIFY(panel.commitDraft());QVERIFY(panel.hasDraft());QCOMPARE(panel.rule(a.id)->remotePort,quint16(8080));
        QTimer answer;connect(&answer,&QTimer::timeout,&answer,[&]{
            if(auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());dialog && dialog->objectName()=="portAdvanced") {
                dialog->findChild<QLineEdit *>("remoteAddress")->setText("10.0.0.12");
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            }
        });answer.start(10);panel.advanced(b.id);answer.stop();QCOMPARE(restart.count(),1);QVERIFY(panel.hasDraft());QCOMPARE(panel.remoteInput->text(),QString("8181"));
        QCOMPARE(panel.rule(b.id)->remoteAddress,QString("10.0.0.12"));
        QTimer confirm;connect(&confirm,&QTimer::timeout,&confirm,[]{if(auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))box->button(QMessageBox::Yes)->click();});
        confirm.start(10);panel.remove(a.id);confirm.stop();QCOMPARE(stop.count(),1);QVERIFY(panel.hasDraft());
        panel.setRuleState(a.id,"stopped");QVERIFY(!panel.hasDraft());QVERIFY(!panel.rule(a.id));QCOMPARE(panel.rules().size(),1);
        panel.beginEdit(b.id);type(panel.remoteInput,"6000");panel.pendingDelete.insert(b.id);panel.loaded=false;
        QSignalSpy changed(panel.table->model(),&QAbstractItemModel::dataChanged);
        panel.setRuleState(b.id,"stopped");QVERIFY(panel.hasDraft());QVERIFY(panel.rule(b.id));QCOMPARE(panel.state(b.id),QString("stopped"));QVERIFY(!panel.draftToggle->isChecked());
        QVERIFY(!changed.isEmpty());QCOMPARE(qvariant_cast<QModelIndex>(changed.last().at(1)).column(),4);
        QVERIFY(!panel.storageError->text().isEmpty());panel.cancelDraft();
    }
    void advancedRechecksBusy() {
        const auto p=profile();auto r=rule(p.id);QVERIFY(saveTunnelRules(p.id,{r}));PortsPanel panel(p.id);show(panel);
        QString message;QTimer answer;connect(&answer,&QTimer::timeout,&answer,[&]{
            if(auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());dialog && dialog->objectName()=="portAdvanced") {
                panel.setRuleState(r.id,"starting");dialog->findChild<QLineEdit *>("remoteAddress")->setText("db.internal");
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
                message=dialog->findChild<QLabel *>("portDialogError")->text();dialog->reject();
            }
        });answer.start(10);panel.advanced(r.id);answer.stop();QVERIFY(!message.isEmpty());QCOMPARE(panel.rule(r.id)->remoteAddress,QString("127.0.0.1"));
    }
    void workspaceTabHeightAndDraft() {
        Workspace w(profile(),{},true);w.resize(1050,700);w.show();settle();
        auto terminal=w.terminals.begin()->view;QPointer<Terminal> keep=terminal;const auto bytes=terminal->findChildren<QObject *>().size();
        w.showPorts();QVERIFY(w.terminalActions->isHidden());w.portsPanel->beginEdit();type(w.portsPanel->remoteInput,"half");
        const auto height=w.verticalSplit->sizes().last();w.setTerminalMaximized(true);w.bottomTabs->setCurrentIndex(0);settle();
        QVERIFY(w.terminalMaximized);QVERIFY(w.portsPanel->hasDraft());QVERIFY(!keep.isNull());
        w.bottomTabs->setCurrentIndex(1);QCOMPARE(w.portsPanel->remoteInput->text(),QString("half"));
        w.hideTerminalPanel();QVERIFY(!w.terminalMaximized);QVERIFY(w.editorPanel->isVisible());w.terminalPanel->show();settle();
        QCOMPARE(w.bottomTabs->currentIndex(),1);QVERIFY(w.portsPanel->hasDraft());QVERIFY(qAbs(w.verticalSplit->sizes().last()-height)<15);
        QCOMPARE(terminal->findChildren<QObject *>().size(),bytes);
        applyTheme(true);w.refreshTheme();QCOMPARE(w.portsPanel->remoteInput->text(),QString("half"));applyTheme(false);
    }
    void trustQueueBatchesAndDialogGeneration() {
        QWidget owner;owner.show();const auto p=profile();int first=0,second=0;
        requestHostTrust(&owner,p,"SHA256:isolated-trust",false,[]{return true;},[&](bool ok){if(ok)++first;});
        QTRY_VERIFY(qobject_cast<QMessageBox *>(QApplication::activeModalWidget()));
        QPointer<QMessageBox> old=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());old->setAttribute(Qt::WA_DeleteOnClose,false);
        requestHostTrust(&owner,p,"SHA256:isolated-trust",false,[]{return true;},[&](bool ok){if(ok)++first;});
        old->button(QMessageBox::Yes)->click();QCOMPARE(first,2);
        QFile::remove(dataPath()+"/known_hosts");
        requestHostTrust(&owner,p,"SHA256:isolated-trust",false,[]{return true;},[&](bool ok){if(ok)++second;});
        QTRY_VERIFY(QApplication::activeModalWidget() && QApplication::activeModalWidget()!=old);
        QPointer<QMessageBox> current=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());QVERIFY(current);
        delete old;settle();QVERIFY(current && current->isVisible());QCOMPARE(second,0);
        current->button(QMessageBox::Yes)->click();QCOMPARE(second,1);settle();
    }
    void loopbackSingleRuleRestartAndStop() {
        if(!fixture.value("port").toInt())QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE using run_ports_integration.py");
        const auto p=profile();auto r=rule(p.id,unusedPort(),quint16(fixture["targets"].toObject()["echo"].toInt()));QVERIFY(r.localPort);QVERIFY(saveTunnelRules(p.id,{r}));
        Workspace w(p,QByteArray("fixture-only"),true);w.demo=false;w.resize(1100,720);w.show();QTimer trust;answerTrust(trust);
        w.startPortRule(r.id);QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(r.id),QString("running"),20000);QVERIFY(exchange(r.localPort));
        QPointer<TunnelService> previous=w.tunnelService;
        w.portsPanel->beginEdit(r.id);const auto oldPort=r.localPort;const auto newPort=unusedPort();QVERIFY(newPort);
        type(w.portsPanel->localInput,QString::number(newPort));QTest::mouseClick(w.portsPanel->draftRow->findChild<QPushButton *>("applyPortDraft"),Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(previous.isNull(),15000);QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(r.id),QString("running"),20000);
        QVERIFY(w.tunnelService);QVERIFY(exchange(newPort));QTcpServer released;QVERIFY(released.listen(QHostAddress::LocalHost,oldPort));released.close();
        auto next=*w.portsPanel->rule(r.id);next.localPort=unusedPort();w.restartTunnel(next);w.stopTunnel(r.id);
        QTRY_VERIFY_WITH_TIMEOUT(!w.tunnelService,15000);QVERIFY(w.queuedTunnelStarts.isEmpty());QCOMPARE(w.tunnelCount(),0);
        w.startPortRule(r.id);QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(r.id),QString("running"),20000);QVERIFY(exchange(newPort));
        QSignalSpy stopped(&w,&Workspace::shutdownComplete);w.shutdown();QTRY_COMPARE(stopped.count(),1);
        QVERIFY(!w.tunnelService);QVERIFY(released.listen(QHostAddress::LocalHost,newPort));
    }
    void loopbackSuccessRemembersIntent() {
        QVERIFY(!fixture.isEmpty());
        const auto p=profile();auto r=rule(p.id,unusedPort(),quint16(fixture["targets"].toObject()["echo"].toInt()));
        QVERIFY(saveTunnelRules(p.id,{r}));
        Workspace w(p,QByteArray("fixture-only"),true);w.demo=false;QTimer trust;answerTrust(trust);
        w.startPortRule(r.id);QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(r.id),QString("running"),20000);
        QVERIFY2(loadTunnelRules(p.id).first().autoStart,"Successful activation must persist the existing autoStart intent");
        QVERIFY(w.portsPanel->rule(r.id)->autoStart);
        w.stopTunnel(r.id);QTRY_VERIFY_WITH_TIMEOUT(!w.tunnelService,15000);
        QVERIFY(!loadTunnelRules(p.id).first().autoStart);
    }
    void loopbackRestoreLifecycleAndFailureIsolation() {
        QVERIFY(!fixture.isEmpty());const auto p=profile();
        auto a=rule(p.id,unusedPort(),quint16(fixture["targets"].toObject()["echo"].toInt()));
        auto b=rule(p.id,unusedPort(),a.remotePort),c=rule(p.id,unusedPort(),a.remotePort);
        a.autoStart=c.autoStart=true;QVERIFY(saveTunnelRules(p.id,{a,b,c}));
        QTcpServer blocker;QVERIFY(blocker.listen(QHostAddress::AnyIPv4,c.localPort));
        QTimer trust;answerTrust(trust);
        {
            Workspace w(p,QByteArray("fixture-only"),true);w.demo=false;w.bottomTabs->setCurrentIndex(0);
            w.restorePortRules();QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(a.id),QString("running"),20000);
            QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(c.id),QString("failed"),10000);
            QCOMPARE(w.portsPanel->state(b.id),QString("stopped"));QCOMPARE(w.bottomTabs->currentIndex(),0);
            QVERIFY(!w.findChild<QMessageBox*>("tunnelStartFailure"));QVERIFY(loadTunnelRules(p.id).last().autoStart);QVERIFY(exchange(a.localPort));
            QTRY_VERIFY(w.findChild<QMessageBox*>("tunnelRestoreFailure"));
            w.findChild<QMessageBox*>("tunnelRestoreFailure")->button(QMessageBox::Ok)->click();settle();
            auto service=w.tunnelService;w.restorePortRules();QTest::qWait(70);QCOMPARE(w.tunnelService,service);
            QCOMPARE(w.portsPanel->state(c.id),QString("failed")); // No retry loop in the same connection.
            blocker.close();w.connectionAutoStarted=false;w.restorePortRules();
            QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(c.id),QString("running"),10000);QCOMPARE(w.tunnelService,service);
            w.stopTunnel(c.id);QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(c.id),QString("stopped"),10000);
            QVERIFY(!loadTunnelRules(p.id).last().autoStart);
            // Reconnect arriving while the old facade retires queues restoration until idle.
            w.stopTunnels();w.connectionAutoStarted=false;w.restorePortRules();
            QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(a.id),QString("running"),20000);
            QTRY_VERIFY_WITH_TIMEOUT(w.tunnelService!=service,10000);QVERIFY(exchange(a.localPort));
            QVERIFY(loadTunnelRules(p.id).first().autoStart);w.stopTunnels();QTRY_VERIFY_WITH_TIMEOUT(!w.tunnelService,10000);
        }
        {
            Workspace w(p,QByteArray("fixture-only"),true);w.demo=false;w.restorePortRules();
            QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(a.id),QString("running"),20000);
            QCOMPARE(w.portsPanel->state(b.id),QString("stopped"));QCOMPARE(w.portsPanel->state(c.id),QString("stopped"));
            w.stopTunnel(a.id);QTRY_VERIFY_WITH_TIMEOUT(!w.tunnelService,10000);QVERIFY(!loadTunnelRules(p.id).first().autoStart);
        }
        auto unrelated=profile();PortsPanel different(unrelated.id);QVERIFY(different.rules().isEmpty());
    }
    void loopbackPersistenceFailuresAndLateRunning() {
        QVERIFY(!fixture.isEmpty());const auto p=profile();
        auto r=rule(p.id,unusedPort(),quint16(fixture["targets"].toObject()["echo"].toInt()));QVERIFY(saveTunnelRules(p.id,{r}));
        QTimer trust;answerTrust(trust);Workspace w(p,QByteArray("fixture-only"),true);w.demo=false;
        const auto path=dataPath()+"/tunnels.json",backup=path+".test-backup";
        QVERIFY(QFile::rename(path,backup));QVERIFY(QDir().mkdir(path));
        w.startPortRule(r.id);QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(r.id),QString("running"),20000);
        QVERIFY(!w.portsPanel->rule(r.id)->autoStart);QVERIFY(!w.portsPanel->storageError->isHidden());
        QVERIFY(w.portsPanel->runtime[r.id].intentError.contains("未保存"));QVERIFY(exchange(r.localPort));
        QVERIFY(QDir().rmdir(path));QVERIFY(QFile::rename(backup,path));
        w.stopTunnel(r.id);QTRY_VERIFY_WITH_TIMEOUT(!w.tunnelService,10000);
        w.startPortRule(r.id);QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(r.id),QString("running"),20000);
        QVERIFY(loadTunnelRules(p.id).first().autoStart);
        QVERIFY(QFile::rename(path,backup));QVERIFY(QDir().mkdir(path));
        w.stopTunnel(r.id);QTRY_VERIFY_WITH_TIMEOUT(!w.tunnelService,10000);
        QVERIFY(w.portsPanel->rule(r.id)->autoStart);QVERIFY(w.portsPanel->runtime[r.id].intentError.contains("仍会"));
        QVERIFY(QDir().rmdir(path));QVERIFY(QFile::rename(backup,path));
        w.stopTunnel(r.id);QVERIFY(!loadTunnelRules(p.id).first().autoStart);
        w.requestTunnelStart(*w.portsPanel->rule(r.id));QPointer<TunnelService> old=w.tunnelService;QVERIFY(old);
        w.stopTunnel(r.id);old->ruleStateChanged(r.id,"running",{});
        QVERIFY(!loadTunnelRules(p.id).first().autoStart);QTRY_VERIFY_WITH_TIMEOUT(!w.tunnelService,10000);
        OperationGate::setLocked(true);w.requestTunnelStart(r);QVERIFY(!w.tunnelService);QVERIFY(!loadTunnelRules(p.id).first().autoStart);OperationGate::setLocked(false);
    }
    void occupiedFailurePrompt_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void occupiedFailurePrompt() {
        QFETCH(bool,light);applyTheme(light);QVERIFY(!fixture.isEmpty());const auto p=profile();
        auto r=rule(p.id,unusedPort(),quint16(fixture["targets"].toObject()["echo"].toInt()));QVERIFY(saveTunnelRules(p.id,{r}));
        QTcpServer blocker;QVERIFY(blocker.listen(QHostAddress::AnyIPv4,r.localPort));
        Workspace w(p,QByteArray("fixture-only"),true);w.demo=false;w.resize(1000,700);w.show();QTimer trust;answerTrust(trust);
        w.startPortRule(r.id);QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(r.id),QString("failed"),20000);
        auto box=w.findChild<QMessageBox*>("tunnelStartFailure");QVERIFY(box);QTRY_VERIFY(box->isVisible());
        QVERIFY(box->text().contains(tunnelEndpoint(r.localAddress,r.localPort)));QVERIFY(box->text().contains("占用"));
        QVERIFY(!loadTunnelRules(p.id).first().autoStart);QCOMPARE(w.portsPanel->table->model()->index(0,4).data(Qt::CheckStateRole).toInt(),int(Qt::Unchecked));
        const auto output=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR");if(!output.isEmpty()){settle();QVERIFY(box->grab().save(output+QString("/port-occupied-%1.png").arg(light?"light":"dark")));}
        box->button(QMessageBox::Ok)->click();QTRY_VERIFY_WITH_TIMEOUT(!w.tunnelService,10000);
        blocker.close();w.startPortRule(r.id);QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(r.id),QString("running"),20000);
        QVERIFY(loadTunnelRules(p.id).first().autoStart);w.stopTunnel(r.id);QTRY_VERIFY_WITH_TIMEOUT(!w.tunnelService,10000);
        applyTheme(false);
    }
    void loopbackReconnectsOnSamePortAfterOutage() {
        if (!fixture.value("port").toInt()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE using run_ports_integration.py");
        const auto p = profile();
        const auto r = rule(p.id, unusedPort(), quint16(fixture["targets"].toObject()["echo"].toInt()));
        QVERIFY(r.localPort);
        QVERIFY(saveTunnelRules(p.id, {r}));
        Workspace w(p, QByteArray("fixture-only"), true);
        w.demo = false;
        QTimer trust;
        answerTrust(trust);
        w.startPortRule(r.id);
        QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(r.id), QString("running"), 10000);
        QVERIFY(exchange(r.localPort));
        const auto before = control("outage");
        QVERIFY(!before.isEmpty());
        const auto resume = qScopeGuard([&] { control("resume"); });
        QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(r.id), QString("reconnecting"), 3000);
        QCOMPARE(w.tunnelCounts()->running, 0);
        QTRY_VERIFY_WITH_TIMEOUT(control("stats")["refused_during_outage"].toInt() >=
                                 before["refused_during_outage"].toInt() + 2, 8500);
        QVERIFY(!control("resume").isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(r.id), QString("running"), 10000);
        QVERIFY(exchange(r.localPort));
        QVERIFY(!control("outage").isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(r.id), QString("reconnecting"), 3000);
        w.portsPanel->toggleRule(r.id);
        QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(r.id), QString("stopped"), 5000);
        QVERIFY(!control("resume").isEmpty());
        QTest::qWait(1200);
        QCOMPARE(w.portsPanel->state(r.id), QString("stopped"));
        QVERIFY(!w.portsPanel->rule(r.id)->autoStart);
        QVERIFY(!w.tunnelService);
    }
    void loopbackSftpFailureCredentialsAndIndependentLifecycle() {
        if(!fixture.value("port").toInt())QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE using run_ports_integration.py");
        auto p=profile();p.rememberSecret=true;auto backend=std::make_shared<PortsMemoryBackend>();ConnectionRepository repository(nullptr,backend);repository.profiles={p};
        auto a=rule(p.id,unusedPort(),quint16(fixture["targets"].toObject()["echo"].toInt())),b=rule(p.id,unusedPort(),a.remotePort);QVERIFY(saveTunnelRules(p.id,{a,b}));
        Workspace w(p,QByteArray("fixture-only"),true,nullptr,&repository);w.demo=false;
        connect(&repository,&ConnectionRepository::changed,&w,[&](const Profile &p,const std::optional<QByteArray> &secret){w.updateCredentials(p,secret);});
        QSignalSpy persisted(&repository,&ConnectionRepository::changed);
        const auto generation=w.credentialGeneration;QTimer trust;answerTrust(trust);w.persistCredential=true;
        w.startSession();QSignalSpy authenticated(w.session,&Session::authenticated),disconnected(w.session,&Session::disconnected);
        w.startPortRule(a.id);
        QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(a.id),QString("running"),20000);
        QTRY_VERIFY_WITH_TIMEOUT(w.session && !w.session->isRunning(),20000);QVERIFY(!w.online);
        QCOMPARE(authenticated.count(),1);QTRY_COMPARE(disconnected.count(),1);
        QVERIFY2(disconnected.first().first().toString().contains("SFTP"),qPrintable(disconnected.first().first().toString()));
        QTRY_VERIFY_WITH_TIMEOUT(persisted.count()>0,10000);QTRY_VERIFY_WITH_TIMEOUT(!repository.pending(),10000);QCOMPARE(w.credentialGeneration,generation);QVERIFY(exchange(a.localPort));
        for(auto id:w.terminals.keys())w.closeTerminal(id);QCOMPARE(w.terminalCount(),0);QVERIFY(exchange(a.localPort));
        // A new manual start works after the SFTP failure, with no terminal or facade left.
        w.stopTunnel(a.id);QTRY_VERIFY_WITH_TIMEOUT(!w.tunnelService,15000);
        w.startPortRule(a.id);QTRY_COMPARE_WITH_TIMEOUT(w.portsPanel->state(a.id),QString("running"),20000);QVERIFY(exchange(a.localPort));
        // A UI-only queued B is cancelled without retiring or stopping healthy A.
        w.preparingCredentials=true;w.requestTunnelStart(b);QVERIFY(w.queuedTunnelStarts.contains(b.id));w.stopTunnel(b.id);w.preparingCredentials=false;
        QVERIFY(!w.tunnelRetiring);QCOMPARE(w.portsPanel->state(b.id),QString("stopped"));QVERIFY(exchange(a.localPort));
        auto changed=p;changed.host="192.0.2.99";repository.profiles[0]=changed;repository.invalidate(p.id);w.updateCredentials(changed,{});
        w.requestTunnelStart(b);QCOMPARE(w.portsPanel->state(b.id),QString("failed"));QVERIFY(exchange(a.localPort));
        bool confirmed=false;QTimer confirm;connect(&confirm,&QTimer::timeout,&confirm,[&]{if(auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());box&&box->windowTitle()=="断开 SSH"){confirmed=true;box->button(QMessageBox::Yes)->click();}});
        confirm.start(10);w.disconnectHost();confirm.stop();QVERIFY(confirmed);QTRY_VERIFY_WITH_TIMEOUT(!w.tunnelService,15000);QCOMPARE(w.tunnelCount(),0);
        QTcpServer released;QVERIFY(released.listen(QHostAddress::LocalHost,a.localPort));
    }
    void themeEvidence_data() {QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void themeEvidence() {
        QFETCH(bool,light);applyTheme(light);const auto p=profile();auto a=rule(p.id),b=rule(p.id,15432,5432),c=rule(p.id,18081,8081);
        b.remoteAddress="db.internal";b.localAddress="::1";QVERIFY(saveTunnelRules(p.id,{a,b,c}));
        Workspace workspace(p,{},true);workspace.resize(1120,760);workspace.show();workspace.showPorts();workspace.portsPanel->setRuleState(a.id,"running");
        workspace.portsPanel->setRuleState(c.id,"failed","本地端口 18081 已被占用");settle();
        const auto output=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR");if(output.isEmpty())return;QDir().mkpath(output);
        verifyColumns(*workspace.portsPanel);
        const auto prefix=output+"/ports-"+(light?"light":"dark");QVERIFY(workspace.grab().save(prefix+"-workspace.png"));
        workspace.setTerminalMaximized(true);settle();QVERIFY(workspace.grab().save(prefix+"-maximized.png"));
        PortsPanel narrow(p.id);narrow.resize(320,400);narrow.show();narrow.beginEdit();type(narrow.remoteInput,"invalid");narrow.commitDraft();settle();
        verifyColumns(narrow);QVERIFY(narrow.draftRow->width()>=narrow.table->viewport()->width());
        const auto rowRect=QRect(QPoint(),narrow.draftRow->size());
        auto rect=[&](QWidget *widget){return QRect(widget->mapTo(narrow.draftRow,QPoint()),widget->size());};
        const auto save=narrow.draftRow->findChild<QPushButton *>("applyPortDraft"),cancel=narrow.draftRow->findChild<QPushButton *>("cancelPortDraft");
        QVERIFY(rowRect.contains(rect(save)));QVERIFY(rowRect.contains(rect(cancel)));QVERIFY(!rect(save).intersects(rect(cancel)));
        for(auto error:{narrow.localError,narrow.remoteError}) {
            QVERIFY(error->isVisible());QVERIFY(rowRect.contains(rect(error)));QVERIFY(!rect(error).intersects(rect(save)));QVERIFY(!rect(error).intersects(rect(cancel)));
            QVERIFY(error->height()>=error->heightForWidth(error->width()));
        }
        QVERIFY(narrow.grab().save(prefix+"-320-draft.png"));
        applyTheme(false);
    }
};
}
QTEST_MAIN(rs::PortsTests)
#include "test_ports.moc"
