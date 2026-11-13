#include "../src/ui/updatecontroller.h"
#include "../src/ui/mainwindow.h"
#include "../src/ui/applock.h"
#include "../src/ui/theme.h"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
namespace rs {
// A genuine synthetic parent supplies the same observed identity as the native
// launcher. The child exercises production prepareInstall, never starts helper.
int updateUiParent(const QStringList &arguments) {
    const auto root=arguments[2],runtime=arguments[3],mode=arguments[4];
    selfupdate::PortableOrigin origin;origin.singleExecutable=true;
    if(!selfupdate::currentProcessIdentity(&origin.launcher).ok()||
       !selfupdate::snapshotFile(origin.launcher.imagePath,&origin.originalFile).ok())return 91;
    auto environment=QProcessEnvironment::systemEnvironment();
    const auto prefix=QStringLiteral("RABBITSHELL_PORTABLE_ORIGIN_");
    const auto &file=origin.originalFile;
    for(const auto &key:environment.keys())if(key.startsWith("RABBITSHELL_PORTABLE_",Qt::CaseInsensitive))environment.remove(key);
    const QMap<QString,QString> fields{{"KIND","single-exe-v1"},{"PATH",origin.launcher.imagePath},
        {"PID",QString::number(origin.launcher.pid)},{"CREATED",QString::number(origin.launcher.created)},
        {"VOLUME",QString::number(file.volume)},{"INDEX",QString::number(file.index)},
        {"BYTES",QString::number(file.bytes)},{"MODIFIED",QString::number(file.modified)},
        {"SHA256",QString::fromLatin1(file.sha256.toHex())}};
    for(auto i=fields.cbegin();i!=fields.cend();++i)environment.insert(prefix+i.key(),i.value());
    environment.insert("FROGSHELL_UPDATE_UI_CHILD","1");
    QProcess child;child.setProcessEnvironment(environment);
    child.start(runtime,{"preparedExitProtection:"+mode,"-o",root+"/child-tests.txt,txt"});
    if(!child.waitForStarted(5000)||!child.waitForFinished(45000))return 92;
    return child.exitStatus()==QProcess::NormalExit?child.exitCode():93;
}
class UpdateSecrets final:public SecretBackend {
    SecretResult read(const QString &)override{return {};}
    QString write(const QString &,const QByteArray &)override{return {};}
    QString remove(const QString &)override{return {};}
};
class UpdateHttp final:public QObject {
  public:
    QTcpServer server;QByteArray package=QByteArray(512*1024,'P');int requests=0,status=200;bool slow=false,corrupt=false,noUpdate=false,failPayloadOnce=false;
    UpdateHttp(){
        if(!server.listen(QHostAddress::LocalHost,0))qFatal("loopback fixture failed");
        connect(&server,&QTcpServer::newConnection,this,[this]{while(server.hasPendingConnections()){
            auto socket=server.nextPendingConnection();socket->setParent(this);auto request=std::make_shared<QByteArray>();
            connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            connect(socket,&QTcpSocket::readyRead,this,[this,socket,request]{
                *request+=socket->readAll();if(socket->property("handled").toBool()||!request->contains("\r\n\r\n"))return;
                socket->setProperty("handled",true);++requests;
                if(status!=200){socket->write("HTTP/1.1 "+QByteArray::number(status)+" Limited\r\nRetry-After: 120\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");socket->disconnectFromHost();return;}
                const auto path=request->split(' ').value(1);QByteArray body;bool metadata=path.endsWith("/latest");
                if(!metadata&&failPayloadOnce){failPayloadOnce=false;socket->write("HTTP/1.1 503 Temporary\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");socket->disconnectFromHost();return;}
                if(metadata){
                    QJsonObject asset{{"name","FrogShell-0.1.43-portable.exe"},{"size",package.size()},
                        {"browser_download_url","https://github.com/bunnya33/frog_shell/releases/download/v0.1.43/FrogShell-0.1.43-portable.exe"},
                        {"digest","sha256:"+QString::fromLatin1(QCryptographicHash::hash(package,QCryptographicHash::Sha256).toHex())}};
                    body=QJsonDocument(QJsonObject{{"tag_name",noUpdate?"v0.1.42":"v0.1.43"},{"draft",false},{"prerelease",false},
                        {"html_url","https://github.com/bunnya33/frog_shell/releases/tag/v0.1.43"},
                        {"body","修复连接恢复\n<script>此处是纯文本</script>\n新增更新校验"},{"assets",QJsonArray{asset}}}).toJson();
                }else {body=package;if(corrupt)body[0]='X';}
                socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n");
                auto offset=std::make_shared<qint64>(0);auto timer=new QTimer(socket);timer->setInterval(slow&&!metadata?15:1);
                connect(timer,&QTimer::timeout,socket,[socket,timer,body,offset]{if(socket->state()!=QAbstractSocket::ConnectedState){timer->stop();return;}const auto size=qMin<qint64>(8192,body.size()-*offset);if(size>0){socket->write(body.constData()+*offset,size);*offset+=size;}if(*offset==body.size()){timer->stop();socket->disconnectFromHost();}});timer->start();
            });
        }});
    }
};
class UpdateTransport final:public QNetworkAccessManager {
    UpdateHttp &fixture;
  public:
    int logicalRequests=0;QList<QUrl> logicalUrls;
    explicit UpdateTransport(UpdateHttp &fixture):fixture(fixture){setProxy(QNetworkProxy::NoProxy);}
  protected:
    QNetworkReply *createRequest(Operation op,const QNetworkRequest &request,QIODevice *body)override{
        ++logicalRequests;logicalUrls.append(request.url());auto local=request;auto url=request.url();url.setScheme("http");url.setHost("127.0.0.1");url.setPort(fixture.server.serverPort());local.setUrl(url);return QNetworkAccessManager::createRequest(op,local,body);
    }
};
class MenuTests final:public QObject {
    Q_OBJECT
    QTemporaryDir root;std::unique_ptr<QTemporaryDir> data;QString report;
    releaseupdate::Config runtime(){return {{},{},"0.1.42","windows","x86_64"};}
    QDialog *about(UpdateController &controller,QWidget &owner){controller.showAbout(&owner);QCoreApplication::processEvents();return owner.findChild<QDialog *>("aboutFrogShellDialog");}
  private slots:
    void initTestCase(){QVERIFY(root.isValid());QStandardPaths::setTestModeEnabled(true);QCoreApplication::setOrganizationName("FrogShellUpdateTests");QCoreApplication::setApplicationName("Isolated");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,root.path());QApplication::setStyle("Fusion");QApplication::setWindowIcon(applicationIcon());qApp->setQuitOnLastWindowClosed(false);if(QFileInfo::exists("C:/Windows/Fonts/msyh.ttc"))QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QApplication::setFont(QFont("Microsoft YaHei UI"));report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",root.path());QVERIFY(QDir().mkpath(report));}
    void init(){data=std::make_unique<QTemporaryDir>(root.filePath("case-XXXXXX"));qputenv("RABBITSHELL_DATA_DIR",data->path().toUtf8());QSettings().clear();QSettings().setValue("update/automaticCheckEnabled",false);OperationGate::setLocked(false);applyTheme(false);}
    void cleanup(){OperationGate::setLocked(false);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
    void fixedSourceAndLegacySettings(){
        QSettings().remove("update/automaticCheckEnabled");
        QSettings().setValue("update/githubOwner","attacker");QSettings().setValue("update/githubRepo","other");
        QSettings().setValue("update/lastAutomaticAttemptUtc",QDateTime::currentDateTimeUtc());
        auto config=runtime();config.owner="attacker";config.repository="other";
        UpdateHttp fixture;UpdateTransport network(fixture);UpdateController controller(config,{},data->filePath("updates"),nullptr,&network);
        QCOMPARE(controller.configuration().owner,QString("bunnya33"));QCOMPARE(controller.configuration().repository,QString("frog_shell"));
        QVERIFY(!QSettings().contains("update/githubOwner"));QVERIFY(!QSettings().contains("update/githubRepo"));
        std::unique_ptr<QWidget> settings(controller.createSettingsPage());
        QVERIFY(!settings->findChild<QLineEdit *>("updateGitHubOwner"));QVERIFY(!settings->findChild<QLineEdit *>("updateGitHubRepo"));
        auto automatic=settings->findChild<QCheckBox *>("updateAutomaticCheck");QVERIFY(automatic&&automatic->isChecked());
        controller.check(true);QTRY_COMPARE_WITH_TIMEOUT(controller.state(),releaseupdate::State::Available,5000);
        QCOMPARE(network.logicalUrls.first(),QUrl("https://api.github.com/repos/bunnya33/frog_shell/releases/latest"));
        QVERIFY(controller.setAutomaticCheckEnabled(false));QVERIFY(!QSettings().value("update/automaticCheckEnabled").toBool());
    }
    void startupAndHalfHourChecks() {
        QSettings().setValue("update/automaticCheckEnabled",true);
        QSettings().setValue("update/lastAutomaticAttemptUtc",QDateTime::currentDateTimeUtc());
        UpdateHttp fixture;UpdateTransport network(fixture);UpdateController controller(runtime(),{},data->filePath("updates"),nullptr,&network);
        QTimer *periodic=nullptr;for(auto timer:controller.findChildren<QTimer *>())if(timer->interval()==30*60*1000)periodic=timer;
        QVERIFY(periodic);QVERIFY(periodic->isActive());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(),releaseupdate::State::Available,5000);
        QCOMPARE(network.logicalRequests,1);QVERIFY(controller.hasUpdate());
        QVERIFY(QMetaObject::invokeMethod(periodic,"timeout"));QCOMPARE(network.logicalRequests,1);QCOMPARE(controller.state(),releaseupdate::State::Available);
        QSettings().setValue("update/lastAutomaticAttemptUtc",QDateTime::currentDateTimeUtc().addSecs(-1801));
        UpdateController fresh(runtime(),{},data->filePath("next"),nullptr,&network);
        QTimer *next=nullptr;for(auto timer:fresh.findChildren<QTimer *>())if(timer->interval()==30*60*1000)next=timer;
        QVERIFY(next);QVERIFY(QMetaObject::invokeMethod(next,"timeout"));
        QTRY_COMPARE_WITH_TIMEOUT(fresh.state(),releaseupdate::State::Available,5000);QCOMPARE(network.logicalRequests,2);
        QTest::qWait(1700);QCOMPARE(network.logicalRequests,2);
        QVERIFY(!QApplication::activeModalWidget());
    }
    void disabledStartupAndTimer() {
        UpdateHttp fixture;UpdateTransport network(fixture);UpdateController controller(runtime(),{},data->filePath("disabled"),nullptr,&network);
        QTimer *periodic=nullptr;for(auto timer:controller.findChildren<QTimer *>())if(timer->interval()==30*60*1000)periodic=timer;
        QVERIFY(periodic);QTest::qWait(1700);QVERIFY(QMetaObject::invokeMethod(periodic,"timeout"));QCOMPARE(network.logicalRequests,0);
        QVERIFY(!QSettings().value("update/automaticCheckEnabled").toBool());
    }
    void automaticAttemptResetsPeriodicTimer() {
        QSettings().setValue("update/automaticCheckEnabled",true);
        UpdateHttp fixture;UpdateTransport network(fixture);UpdateController controller(runtime(),{},data->filePath("reset-timer"),nullptr,&network);
        QTimer *periodic=nullptr;for(auto timer:controller.findChildren<QTimer *>())if(timer->interval()==30*60*1000)periodic=timer;
        QVERIFY(periodic);QCOMPARE(periodic->timerType(),Qt::PreciseTimer);
        // Simulate a tick due soon, then start a real automatic fixture request.
        periodic->start(10000);controller.check(true);
        QCOMPARE(network.logicalRequests,1);QCOMPARE(periodic->interval(),30*60*1000);
        QVERIFY(periodic->remainingTime()>30*60*1000-1000);
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(),releaseupdate::State::Available,5000);
    }
    void lockedStartupIsDeferredOnce() {
        QSettings().setValue("update/automaticCheckEnabled",true);AppLock lock(data->path());
        QSignalSpy enabled(&lock.security(),&SecurityStore::completed);lock.security().request(SecurityStore::Enable,{},"fixture-pass");
        QTRY_VERIFY_WITH_TIMEOUT(!enabled.isEmpty(),10000);QVERIFY(enabled.last()[1].toBool());lock.lock();QVERIFY(OperationGate::locked());
        UpdateHttp fixture;UpdateTransport network(fixture);UpdateController controller(runtime(),{},data->filePath("locked"),nullptr,&network);
        QTest::qWait(1700);QCOMPARE(network.logicalRequests,0);
        lock.unlock("fixture-pass");QTRY_VERIFY_WITH_TIMEOUT(!OperationGate::locked(),10000);
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(),releaseupdate::State::Available,5000);QCOMPARE(network.logicalRequests,1);
        lock.lock();lock.unlock("fixture-pass");QTRY_VERIFY_WITH_TIMEOUT(!OperationGate::locked(),10000);
        QTest::qWait(100);QCOMPARE(network.logicalRequests,1);
    }
    void startupRespectsBusyMigrationAndCooldown() {
        QSettings().setValue("update/automaticCheckEnabled",true);
        QSettings().setValue("update/rateLimitUntilUtc",QDateTime::currentDateTimeUtc().addSecs(120));
        UpdateHttp fixture;UpdateTransport network(fixture);UpdateController controller(runtime(),{},data->filePath("cooldown"),nullptr,&network);
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(),releaseupdate::State::Throttled,5000);QCOMPARE(network.logicalRequests,0);
        controller.check();QCOMPARE(network.logicalRequests,0);
        QSettings().remove("update/rateLimitUntilUtc");
        UpdateController migrating(runtime(),{},data->filePath("migrating"),nullptr,&network);migrating.setMigrationPending(true);
        QTest::qWait(1700);migrating.check(true);QCOMPARE(network.logicalRequests,0);
        migrating.setMigrationPending(false);QTRY_COMPARE_WITH_TIMEOUT(migrating.state(),releaseupdate::State::Available,5000);QCOMPARE(network.logicalRequests,1);
        fixture.slow=true;migrating.download();QTRY_COMPARE_WITH_TIMEOUT(migrating.state(),releaseupdate::State::Downloading,3000);
        migrating.check(true);QCOMPARE(network.logicalRequests,2);
        QTRY_VERIFY_WITH_TIMEOUT(migrating.ready(),10000);migrating.check(true);QVERIFY(migrating.ready());QCOMPARE(network.logicalRequests,2);
    }
    void rateLimitSurvivesControllerRestart() {
        QSettings().setValue("update/automaticCheckEnabled",true);
        UpdateHttp fixture;fixture.status=429;UpdateTransport network(fixture);
        {
            UpdateController first(runtime(),{},data->filePath("first"),nullptr,&network);
            QTRY_COMPARE_WITH_TIMEOUT(first.state(),releaseupdate::State::Failed,5000);QCOMPARE(network.logicalRequests,1);
            QVERIFY(QSettings().value("update/rateLimitUntilUtc").toDateTime()>QDateTime::currentDateTimeUtc());
        }
        fixture.status=200;UpdateController restarted(runtime(),{},data->filePath("restarted"),nullptr,&network);
        QTRY_COMPARE_WITH_TIMEOUT(restarted.state(),releaseupdate::State::Throttled,5000);QCOMPARE(network.logicalRequests,1);
    }
    void trayUpdateIconAndManualFallback(){
        for(bool light:{false,true}){
            applyTheme(light);const auto base=applicationIcon();const auto update=MainWindow::trayUpdateIcon();QVERIFY(!update.isNull());
            for(int size:{16,20,24,32,48,64,128}){
                const auto original=base.pixmap(size,size).toImage().convertToFormat(QImage::Format_ARGB32);
                const auto image=update.pixmap(size,size).toImage().convertToFormat(QImage::Format_ARGB32);
                QCOMPARE(image.size(),QSize(size,size));QVERIFY(image!=original);
                bool green=false,mouth=false,lightRegion=false;
                for(int y=0;y<size;++y)for(int x=0;x<size;++x){const QColor c=image.pixelColor(x,y);
                    if(c.alpha()>180&&c.green()>c.red()*1.1&&c.green()>c.blue()*1.2&&c.green()>90)green=true;
                    if(c.alpha()>180&&c.red()>c.green()*1.25&&c.red()>c.blue()*1.15&&c.red()>130)mouth=true;
                    if(c.alpha()>230&&c.red()>190&&c.green()>190&&c.blue()>190)lightRegion=true;
                }
                QVERIFY(green);QVERIFY(mouth);QVERIFY(lightRegion);
                // Resampling may spread a faint antialiased fringe into an edge pixel.
                for(int i=0;i<size;++i){QVERIFY(image.pixelColor(i,0).alpha()<=32);QVERIFY(image.pixelColor(i,size-1).alpha()<=32);QVERIFY(image.pixelColor(0,i).alpha()<=32);QVERIFY(image.pixelColor(size-1,i).alpha()<=32);}
            }
            for(qreal dpr:{1.0,1.25,1.5,2.0,3.0}){
                const auto pixmap=update.pixmap(QSize(16,16),dpr);QVERIFY(!pixmap.isNull());
                const auto image=pixmap.toImage();QVERIFY(image.hasAlphaChannel());QCOMPARE(image.pixelColor(0,0).alpha(),0);
            }
        }
        UpdateHttp fixture;UpdateTransport network(fixture);UpdateController controller(runtime(),{},data->filePath("updates"),nullptr,&network);
        MainWindow window(false,std::make_shared<UpdateSecrets>(),&controller);window.show();
        auto action=window.findChild<QAction *>("trayUpdate");auto tray=window.findChild<QSystemTrayIcon *>("applicationTrayIcon");QVERIFY(action&&tray);QVERIFY(!action->isVisible());const auto baseKey=tray->icon().cacheKey();
        controller.check();QTRY_VERIFY_WITH_TIMEOUT(controller.hasUpdate(),5000);QVERIFY(action->isVisible());QVERIFY(action->text().contains("手动"));
        QVERIFY(tray->toolTip().contains("0.1.43"));QVERIFY(tray->icon().cacheKey()!=baseKey);QSignalSpy manual(&controller,&UpdateController::manualUpdateRequested);
        action->trigger();QCOMPARE(manual.size(),1);auto dialog=window.findChild<QDialog *>("aboutFrogShellDialog");QVERIFY(dialog);QCOMPARE(network.logicalRequests,1);
        dialog->close();fixture.noUpdate=true;controller.check();QTRY_VERIFY_WITH_TIMEOUT(!controller.hasUpdate(),5000);QVERIFY(!action->isVisible());QCOMPARE(tray->icon().cacheKey(),baseKey);
    }
    void downloadRetryStatusAndCancellation(){
        UpdateHttp fixture;UpdateTransport network(fixture);UpdateController controller(runtime(),{},data->filePath("updates"),nullptr,&network);
        controller.check();QTRY_COMPARE_WITH_TIMEOUT(controller.state(),releaseupdate::State::Available,5000);
        fixture.failPayloadOnce=true;controller.download();
        QTRY_VERIFY_WITH_TIMEOUT(controller.statusText().contains("重试"),3000);
        QVERIFY(controller.progressText().contains("1/3"));QVERIFY(controller.updateBusy());
        QTRY_VERIFY_WITH_TIMEOUT(controller.ready(),5000);QCOMPARE(network.logicalRequests,3);
        // A new operation can also be canceled while waiting for backoff.
        controller.check();QTRY_COMPARE_WITH_TIMEOUT(controller.state(),releaseupdate::State::Available,5000);
        fixture.failPayloadOnce=true;controller.download();QTRY_VERIFY_WITH_TIMEOUT(controller.statusText().contains("重试"),3000);
        const auto requests=network.logicalRequests;controller.cancel();QTest::qWait(1100);
        QCOMPARE(network.logicalRequests,requests);QVERIFY(!controller.ready());QVERIFY(!controller.updateBusy());
    }
    void downloadOutlivesViewAndManualReady(){
        UpdateHttp fixture;UpdateTransport network(fixture);UpdateController controller(runtime(),{},data->filePath("updates"),nullptr,&network);QWidget owner;owner.show();
        controller.check();QTRY_COMPARE_WITH_TIMEOUT(controller.state(),releaseupdate::State::Available,5000);fixture.slow=true;controller.download();QTRY_COMPARE_WITH_TIMEOUT(controller.state(),releaseupdate::State::Downloading,3000);
        auto dialog=about(controller,owner);QVERIFY(dialog);dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!owner.findChild<QDialog *>("aboutFrogShellDialog"));
        QTRY_VERIFY_WITH_TIMEOUT(controller.ready(),6000);dialog=about(controller,owner);QVERIFY(dialog);QVERIFY(dialog->findChild<QPlainTextEdit *>("aboutReleaseNotes")->toPlainText().contains("<script>"));QVERIFY(dialog->findChild<QPushButton *>("aboutRestartUpdate")->isHidden());QVERIFY(!dialog->findChild<QPushButton *>("aboutDownloadLocation")->isHidden());
        for(bool light:{false,true}){applyTheme(light);QCoreApplication::processEvents();QVERIFY(dialog->grab().save(report+QString("/about-ready-%1.png").arg(light?"light":"dark")));}
        controller.requestRestart();QVERIFY(!controller.restartPending());controller.cancel();QVERIFY(!controller.ready());QDirIterator files(data->filePath("updates/downloads"),QDir::Files,QDirIterator::Subdirectories);QVERIFY(!files.hasNext());
    }
    void lockedCancelsAndBlocks(){
        AppLock lock(data->path());QSignalSpy enabled(&lock.security(),&SecurityStore::completed);lock.security().request(SecurityStore::Enable,{},"fixture-pass");QTRY_VERIFY_WITH_TIMEOUT(!enabled.isEmpty(),10000);QVERIFY(enabled.last()[1].toBool());
        UpdateHttp fixture;fixture.slow=true;UpdateTransport network(fixture);UpdateController controller(runtime(),{},data->filePath("updates"),nullptr,&network);QWidget owner;owner.show();about(controller,owner);
        controller.check();QTRY_COMPARE_WITH_TIMEOUT(controller.state(),releaseupdate::State::Available,5000);controller.download();QTRY_COMPARE(controller.state(),releaseupdate::State::Downloading);lock.lock();QVERIFY(OperationGate::locked());QVERIFY(!controller.ready());QVERIFY(controller.hasUpdate());const auto requests=network.logicalRequests;controller.check();controller.download();controller.requestRestart();controller.requestUpdate();QCOMPARE(network.logicalRequests,requests);QVERIFY(!controller.restartPending());
        lock.unlock("fixture-pass");QTRY_VERIFY_WITH_TIMEOUT(!OperationGate::locked(),10000);controller.check();QTRY_COMPARE_WITH_TIMEOUT(controller.state(),releaseupdate::State::Available,5000);QVERIFY(controller.hasUpdate());
    }
    void menuAndExitCancellation(){
        UpdateHttp fixture;UpdateTransport network(fixture);UpdateController controller(runtime(),{},data->filePath("updates"),nullptr,&network);MainWindow window(false,std::make_shared<UpdateSecrets>(),&controller);window.show();
        auto action=window.findChild<QAction *>("aboutFrogShellAction");QVERIFY(action);auto menu=window.findChild<QMenu *>("applicationMenu-帮助");QVERIFY(menu);QMetaObject::invokeMethod(menu,"aboutToShow");action->trigger();QCoreApplication::processEvents();auto dialog=window.findChild<QDialog *>("aboutFrogShellDialog");QVERIFY(dialog);dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QSignalSpy aborted(&window,&MainWindow::exitAborted);QTimer::singleShot(0,&window,[]{auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(box)box->button(QMessageBox::Cancel)->click();});window.requestExit();QCOMPARE(aborted.size(),1);QVERIFY(!window.exitWasConfirmed());QVERIFY(!controller.restartPending());QCOMPARE(network.logicalRequests,0);
    }
    void preparedExitProtection_data(){
        QTest::addColumn<QString>("mode");
        for(const auto mode:{"cancel","changed","save-fails","save-completes","prepare-cancel","migration","download-error","download-cancel","intent-cancel"})QTest::newRow(mode)<<QString(mode);
    }
    void preparedExitProtection(){
        QFETCH(QString,mode);
        if(!qEnvironmentVariableIsSet("FROGSHELL_UPDATE_UI_CHILD")){
            QTemporaryDir sandbox;QVERIFY(sandbox.isValid());const auto launcher=sandbox.filePath("synthetic 外层.exe");
            QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),launcher));
            selfupdate::FileIdentity before,after;QVERIFY(selfupdate::snapshotFile(launcher,&before).ok());
            QProcess parent;parent.start(launcher,{"--update-ui-parent",sandbox.path(),QCoreApplication::applicationFilePath(),mode});
            QVERIFY(parent.waitForStarted(5000));QVERIFY(parent.waitForFinished(55000));
            const auto childReport=sandbox.filePath("child-tests.txt");
            if(QFileInfo::exists(childReport)){QFile::remove(report+"/exit-"+mode+".txt");QVERIFY(QFile::copy(childReport,report+"/exit-"+mode+".txt"));}
            QFile result(childReport);result.open(QIODevice::ReadOnly);const auto detail=result.readAll();
            QVERIFY2(parent.exitStatus()==QProcess::NormalExit&&parent.exitCode()==0,detail.constData());
            QVERIFY(selfupdate::snapshotFile(launcher,&after).ok());QCOMPARE(before.sha256,after.sha256);return;
        }
        selfupdate::PortableOrigin origin;QVERIFY(selfupdate::originFromEnvironment(QProcessEnvironment::systemEnvironment(),&origin).ok());
        UpdateHttp fixture;UpdateTransport network(fixture);const auto base=data->filePath("updates");
        UpdateController controller(runtime(),origin,base,nullptr,&network);
        controller.check();QTRY_COMPARE_WITH_TIMEOUT(controller.state(),releaseupdate::State::Available,5000);
        QSignalSpy restarting(&controller,&UpdateController::restartRequested);
        if(mode=="migration"||mode=="prepare-cancel"){controller.download();QTRY_VERIFY_WITH_TIMEOUT(controller.ready(),5000);}
        if(mode=="migration"){
            controller.setMigrationPending(true);controller.requestRestart();QVERIFY(!controller.restartPending());QCOMPARE(restarting.size(),0);QVERIFY(controller.statusText().contains("迁移"));return;
        }
        if(mode=="prepare-cancel"){
            controller.requestRestart();QVERIFY(controller.preparing());controller.cancel();
            QTRY_VERIFY_WITH_TIMEOUT(QThreadPool::globalInstance()->activeThreadCount()==0,5000);QTest::qWait(30);
            QVERIFY(!controller.restartPending());QCOMPARE(restarting.size(),0);QVERIFY(!controller.takePreparedInstall().valid());
            QDirIterator files(base,QDir::Files,QDirIterator::Subdirectories);QVERIFY(!files.hasNext());return;
        }
        QSettings().setValue("update/installReceipt","previous-receipt");QSettings().setValue("update/installToken","previous-token");
        QString receipt;connect(&controller,&UpdateController::restartRequested,this,[&]{receipt=QSettings().value("update/installReceipt").toString();});
        auto window=std::make_unique<MainWindow>(true,std::make_shared<UpdateSecrets>(),&controller);window->show();
        auto updateAction=window->findChild<QAction *>("trayUpdate");auto cancelAction=window->findChild<QAction *>("trayCancelUpdate");QVERIFY(updateAction&&cancelAction);QVERIFY(updateAction->isVisible());
        auto workspace=window->workspaces.cbegin().value();auto editor=workspace->editors.cbegin().value();QVERIFY(editor);editor->code->moveCursor(QTextCursor::End);editor->code->insertPlainText("\npending update draft");
        if(mode=="save-fails")editor->code->setReadOnly(true);
        QSignalSpy aborted(window.get(),&MainWindow::exitAborted),completed(window.get(),&MainWindow::exitCompleted);
        bool saveSeen=false,approvedBeforeSave=false,completedBeforeSave=false;
        connect(workspace,&Workspace::readyToClose,this,[&]{saveSeen=true;approvedBeforeSave=controller.takePreparedInstall().valid();});
        connect(window.get(),&MainWindow::exitCompleted,this,[&]{completedBeforeSave=!saveSeen;});
        int prompts=0;QTimer driver;driver.setInterval(5);connect(&driver,&QTimer::timeout,this,[&]{
            if(auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())){
                if(box->windowTitle()=="退出 FrogShell"){
                    ++prompts;if(mode=="changed"){editor->code->moveCursor(QTextCursor::End);editor->code->insertPlainText("\nchanged during confirmation");}
                    if(mode=="intent-cancel")controller.cancelExitIntent();
                    const auto choice=(mode=="cancel"||mode=="download-error"||mode=="download-cancel")?QMessageBox::Cancel:(mode=="changed"||mode=="intent-cancel")?QMessageBox::Discard:QMessageBox::Save;box->button(choice)->click();
                }else box->accept();
            }
        });
        if(mode=="download-error"||mode=="download-cancel"){
            fixture.corrupt=mode=="download-error";fixture.slow=true;updateAction->trigger();
            QCOMPARE(controller.state(),releaseupdate::State::Downloading);QVERIFY(!updateAction->isEnabled());
            const int requests=network.logicalRequests;updateAction->trigger();controller.requestUpdate();QCOMPARE(network.logicalRequests,requests);
            if(mode=="download-cancel"){QVERIFY(cancelAction->isVisible());cancelAction->trigger();QCOMPARE(controller.state(),releaseupdate::State::Cancelled);}
            else {QTRY_COMPARE_WITH_TIMEOUT(controller.state(),releaseupdate::State::Failed,6000);QVERIFY(controller.statusText().contains("SHA-256"));}
            QVERIFY(!controller.restartPending());QVERIFY(!controller.ready());QVERIFY(updateAction->isEnabled());
            fixture.corrupt=false;fixture.slow=false;
        }
        driver.start();updateAction->trigger();controller.requestUpdate();QTRY_COMPARE_WITH_TIMEOUT(restarting.size(),1,10000);
        QCOMPARE(prompts,1);QVERIFY(!receipt.isEmpty());
        if(mode=="save-completes"){
            // Approval and transfer of ownership must wait for async save/shutdown.
            QTRY_COMPARE_WITH_TIMEOUT(completed.size(),1,5000);QVERIFY(saveSeen);QVERIFY(!approvedBeforeSave);QVERIFY(!completedBeforeSave);QVERIFY(window->exitWasConfirmed());QVERIFY(window->workspaces.isEmpty());
            window.reset();auto install=controller.takePreparedInstall();QVERIFY(install.valid());QVERIFY(QFileInfo::exists(install.planPath()));
            selfupdate::InstallStatus state;QVERIFY(selfupdate::readInstallStatus(receipt,&state).ok());QVERIFY(state.phase!="complete");
            QVERIFY(selfupdate::discardPreparedInstall(&install).ok());
        }else{
            QTRY_VERIFY_WITH_TIMEOUT(!aborted.isEmpty(),5000);QVERIFY(!window->exitWasConfirmed());QCOMPARE(window->workspaces.size(),1);QVERIFY(editor->dirty());
            QVERIFY(!controller.restartPending());QVERIFY(!controller.takePreparedInstall().valid());QVERIFY(!QFileInfo::exists(receipt));
            QCOMPARE(QSettings().value("update/installReceipt").toString(),QString("previous-receipt"));
            QCOMPARE(QSettings().value("update/installToken").toString(),QString("previous-token"));editor->code->setReadOnly(false);editor->discardDraft();
        }
        driver.stop();qApp->setQuitOnLastWindowClosed(false);
    }
};
}
int main(int argc,char **argv){
    if(argc==5&&QByteArray(argv[1])=="--update-ui-parent"){QCoreApplication app(argc,argv);return rs::updateUiParent(app.arguments());}
    QApplication app(argc,argv);rs::MenuTests test;return QTest::qExec(&test,argc,argv);
}
#include "test_updateui.moc"
