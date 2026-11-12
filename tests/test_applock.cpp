#include "../src/ui/applock.h"
#include "../src/ui/sessionlockevents.h"
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wtsapi32.h>
#endif
#include "../src/ui/mainwindow.h"
#include "../src/ui/updatecontroller.h"
#include "../src/ui/theme.h"
#include "../src/ui/sshprompts.h"
#include "../src/ui/uipreferences.h"
#include "../src/core/singleinstance.h"
#include <QtTest>
namespace rs {
class LockSecrets final:public SecretBackend {
  public:std::atomic_int reads=0;
    SecretResult read(const QString &)override{++reads;return {SecretResult::Found,"fixture-secret",{}};}
    QString write(const QString &,const QByteArray &)override{return {};}
    QString remove(const QString &)override{return {};}
};
class AppLockTests:public QObject {
    Q_OBJECT
    QTemporaryDir root;
    std::unique_ptr<QTemporaryDir> data;
    void settle(){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
    bool perform(SecurityStore &s,SecurityStore::Action action,QByteArray old={},QByteArray next={}){
        QSignalSpy spy(&s,&SecurityStore::completed);s.request(action,std::move(old),std::move(next));
        if(spy.isEmpty()&&!spy.wait(10000))return false;
        if(!spy.isEmpty()&&!spy.last()[1].toBool())qInfo()<<"security result"<<int(action)<<spy.last()[2].toString()<<"retry-ms"<<s.retryAfterMs();
        return !spy.isEmpty()&&spy.last()[1].toBool();
    }
    void enable(SecurityStore &s){QVERIFY(perform(s,SecurityStore::Enable,{},"fixture-pass"));}
    void unlock(AppLock &lock){lock.unlock("fixture-pass");QTRY_VERIFY_WITH_TIMEOUT(!lock.locked(),10000);settle();}
    QString report(){auto path=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data->path());QDir().mkpath(path);return path;}
  private slots:
    void initTestCase(){QStandardPaths::setTestModeEnabled(true);QCoreApplication::setOrganizationName("RabbitShellLockTests");QCoreApplication::setApplicationName("Isolated");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication::setStyle("Fusion");qApp->setQuitOnLastWindowClosed(false);
        for(auto font:{"C:/Windows/Fonts/msyh.ttc","C:/Windows/Fonts/consola.ttf"})if(QFileInfo::exists(font))QFontDatabase::addApplicationFont(font);QApplication::setFont(QFont("Microsoft YaHei UI"));}
    void init(){data=std::make_unique<QTemporaryDir>(root.filePath("case-XXXXXX"));qputenv("RABBITSHELL_DATA_DIR",data->path().toUtf8());QSettings().clear();OperationGate::setLocked(false);UiZoom::instance().setLevel(0);applyTheme(false);}
    void cleanup(){OperationGate::setLocked(false);settle();}
    void verifierAndChangeDisable(){
        SecurityStore store(data->path());QVERIFY(!store.enabled());enable(store);
        QFile file(data->filePath("application-lock.json"));QVERIFY(file.open(QIODevice::ReadOnly));auto bytes=file.readAll();file.close();QVERIFY(!bytes.contains("fixture-pass"));const auto object=QJsonDocument::fromJson(bytes).object();QCOMPARE(object["iterations"].toInt(),600000);QCOMPARE(QByteArray::fromBase64(object["salt"].toString().toLatin1()).size(),32);
        QVERIFY(!perform(store,SecurityStore::Verify,"wrong"));QVERIFY(store.retryAfterMs()>0);QVERIFY(!perform(store,SecurityStore::Disable,"fixture-pass"));QVERIFY(store.enabled());QTest::qWait(store.retryAfterMs()+20);
        QVERIFY(!perform(store,SecurityStore::Change,"wrong","another-pass"));QTest::qWait(store.retryAfterMs()+20);
        QVERIFY(perform(store,SecurityStore::Change,"fixture-pass","another-pass"));QVERIFY(!perform(store,SecurityStore::Verify,"fixture-pass"));QTest::qWait(store.retryAfterMs()+20);QVERIFY(perform(store,SecurityStore::Verify,"another-pass"));
        QVERIFY(perform(store,SecurityStore::Disable,"another-pass"));QVERIFY(!store.enabled());SecurityStore loaded(data->path());QVERIFY(!loaded.enabled());QVERIFY(!loaded.damaged());QVERIFY(QFileInfo::exists(data->filePath("application-lock.required")));
    }
    void corruptMissingAndReset(){
        SecurityStore store(data->path());enable(store);QFile file(data->filePath("application-lock.json"));QVERIFY(file.open(QIODevice::ReadOnly));const auto original=file.readAll();file.close();
        UiPreferences::instance().reset();SecurityStore afterReset(data->path());QVERIFY(afterReset.enabled());QVERIFY(perform(afterReset,SecurityStore::Verify,"fixture-pass"));
        QVERIFY(file.remove());SecurityStore missing(data->path());QVERIFY(missing.enabled());QVERIFY(missing.damaged());QVERIFY(!perform(missing,SecurityStore::Enable,{},"bypass-pass"));
        for(auto bytes:{QByteArray("not-json"),QByteArray("{\"version\":1,\"enabled\":true}"),QByteArray("{\"version\":1,\"enabled\":false,\"verifier\":\"missing-salt\"}")}){QVERIFY(file.open(QIODevice::WriteOnly));file.write(bytes);file.close();SecurityStore bad(data->path());QVERIFY(bad.enabled());QVERIFY(bad.damaged());}
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(original);file.close();SecurityStore valid(data->path());QVERIFY(!valid.damaged());
    }
    void atomicFailureAndMarkerFailure(){
        SecurityStore blocked(data->path());QDir().mkdir(data->filePath("application-lock.json"));QVERIFY(!perform(blocked,SecurityStore::Enable,{},"fixture-pass"));QVERIFY(!blocked.enabled());QVERIFY(!QFileInfo::exists(data->filePath("application-lock.required")));
        QDir record(data->filePath("application-lock.json"));QVERIFY(record.rmdir(record.absolutePath()));
        SecurityStore normal(data->path());QDir().mkdir(data->filePath("application-lock.required"));
        // An existing non-file marker is damaged, never treated as a successful protection marker.
        QVERIFY(!perform(normal,SecurityStore::Enable,{},"fixture-pass"));QVERIFY(normal.enabled());QVERIFY(!normal.damaged());QVERIFY(perform(normal,SecurityStore::Verify,"fixture-pass"));
    }
    void cancelledKdf_data(){QTest::addColumn<int>("action");QTest::newRow("enable")<<int(SecurityStore::Enable);QTest::newRow("change")<<int(SecurityStore::Change);QTest::newRow("disable")<<int(SecurityStore::Disable);}
    void cancelledKdf(){
        QFETCH(int,action);SecurityStore store(data->path());if(action!=SecurityStore::Enable)enable(store);
        QSignalSpy spy(&store,&SecurityStore::completed);store.request(SecurityStore::Action(action),action==SecurityStore::Enable?QByteArray():QByteArray("fixture-pass"),"replacement-pass");store.cancel(store.requestId());QVERIFY(spy.wait(10000));QVERIFY(!spy.last()[1].toBool());QCOMPARE(store.enabled(),action!=SecurityStore::Enable);if(store.enabled())QVERIFY(perform(store,SecurityStore::Verify,"fixture-pass"));else QVERIFY(!QFileInfo::exists(data->filePath("application-lock.required")));
    }
    void cancelledSettingsUi_data(){cancelledKdf_data();}
    void cancelledSettingsUi(){
        QFETCH(int,action);AppLock lock(data->path());if(action!=SecurityStore::Enable)enable(lock.security());
        QWidget parent;parent.show();QTimer driver;driver.setInterval(10);bool entered=false,cancelled=false;
        connect(&driver,&QTimer::timeout,&parent,[&]{for(auto w:QApplication::topLevelWidgets())if(auto dialog=qobject_cast<QDialog *>(w);dialog&&dialog->isVisible()){
            if(!entered){for(auto name:{"lockCurrentPassword","lockNewPassword","lockConfirmPassword"})if(auto field=dialog->findChild<QLineEdit *>(name))field->setText(QString(name).contains("Current")?"fixture-pass":"replacement-pass");auto buttons=dialog->findChild<QDialogButtonBox *>();if(buttons){entered=true;buttons->button(QDialogButtonBox::Ok)->click();}}
            else if(lock.security().busy()){cancelled=true;dialog->reject();driver.stop();}
        }});driver.start();lock.passwordDialog(SecurityStore::Action(action),&parent);driver.stop();QVERIFY(entered&&cancelled);QTRY_VERIFY_WITH_TIMEOUT(!lock.security().busy(),10000);QVERIFY(!lock.locked());QCOMPARE(lock.security().enabled(),action!=SecurityStore::Enable);
    }
    void lockDuringKdfCannotCommitOrUnlock(){
        AppLock lock(data->path());enable(lock.security());
        lock.security().request(SecurityStore::Disable,"fixture-pass");lock.simulateSystemLock();QTRY_VERIFY_WITH_TIMEOUT(!lock.security().busy(),10000);QVERIFY(lock.security().enabled());
        lock.unlock("fixture-pass");lock.simulateSystemLock();lock.unlock("wrong-while-busy");QTRY_VERIFY_WITH_TIMEOUT(!lock.security().busy(),10000);QVERIFY(lock.locked());unlock(lock);
    }
    void trayMenuRemainsRestrictedWhenLocked(){
        AppLock lock(data->path());enable(lock.security());MainWindow main(true,std::make_shared<LockSecrets>());main.show();settle();lock.lock();
        auto menu=main.findChild<QMenu *>("applicationTrayMenu");QVERIFY(menu);
        menu->popup(main.mapToGlobal(QPoint(10,10)));settle();QVERIFY(menu->isVisible());
        QList<QAction *> visible;for(auto action:menu->actions())if(action->isVisible())visible.append(action);
        QCOMPARE(visible.size(),4);QCOMPARE(visible[0]->text(),QString("显示面板"));
        auto trayLock=menu->findChild<QAction *>("trayLock");QVERIFY(trayLock);QCOMPARE(visible[1],trayLock);QVERIFY(!trayLock->isEnabled());
        QVERIFY(visible[2]->isSeparator());QCOMPARE(visible[3]->text(),QString("退出"));
        auto update=menu->findChild<QAction *>("trayUpdate");auto cancel=menu->findChild<QAction *>("trayCancelUpdate");
        QVERIFY(update&&cancel);QVERIFY(!update->isVisible());QVERIFY(!cancel->isVisible());QVERIFY(!update->isEnabled());QVERIFY(!cancel->isEnabled());
        QSignalSpy updateTriggered(update,&QAction::triggered),cancelTriggered(cancel,&QAction::triggered),restarting(main.updateController(),&UpdateController::restartRequested);
        const auto state=main.updateController()->state();
        update->trigger();cancel->trigger();main.updateController()->requestUpdate();
        QCOMPARE(updateTriggered.size(),0);QCOMPARE(cancelTriggered.size(),0);QCOMPARE(restarting.size(),0);
        QCOMPARE(main.updateController()->state(),state);QVERIFY(!main.updateController()->restartPending());
        visible[0]->trigger();settle();QVERIFY(lock.locked());QVERIFY(main.isVisible());QVERIFY(main.findChild<QLineEdit *>("applicationUnlockPassword"));
        visible[3]->trigger();QVERIFY(lock.locked());QVERIFY(!main.exitWasConfirmed());menu->hide();unlock(lock);
    }
    void trayLockActionAndWindowRestoration(){
        AppLock lock(data->path());MainWindow main(true,std::make_shared<LockSecrets>());main.show();settle();
        auto menu=main.findChild<QMenu *>("applicationTrayMenu");QVERIFY(menu);auto action=menu->findChild<QAction *>("trayLock");QVERIFY(action);QVERIFY(!action->isEnabled());QVERIFY(action->toolTip().contains("启用"));
        enable(lock.security());QTRY_VERIFY(action->isEnabled());
        QDialog floating(&main,Qt::Window),hidden(&main,Qt::Window),modal(&main);floating.show();modal.setWindowModality(Qt::WindowModal);modal.open();settle();QVERIFY(floating.isVisible());QVERIFY(modal.isVisible());QVERIFY(!hidden.isVisible());
        main.hideToTray();settle();QVERIFY(!main.isVisible());QVERIFY(!floating.isVisible());QVERIFY(!modal.isVisible());
        main.showFromTray();settle();QVERIFY(main.isVisible());QVERIFY(floating.isVisible());QVERIFY(modal.isVisible());QVERIFY(!hidden.isVisible());modal.reject();settle();
        action->trigger();settle();QVERIFY(lock.locked());QVERIFY(!action->isEnabled());main.hideToTray();main.showFromTray();settle();QVERIFY(lock.locked());QVERIFY(main.findChild<QWidget *>("applicationLockSurface"));unlock(lock);
    }
    void explicitExitProtectsDrafts_data(){QTest::addColumn<int>("mode");QTest::newRow("cancel")<<0;QTest::newRow("changed-during-confirm")<<1;QTest::newRow("save-fails")<<2;QTest::newRow("save-completes")<<3;}
    void explicitExitProtectsDrafts(){
        QFETCH(int,mode);MainWindow main(true,std::make_shared<LockSecrets>());main.show();settle();auto w=main.workspaces.begin().value();auto editor=w->editors.begin().value();QVERIFY(editor);editor->code->appendPlainText("exit draft");QVERIFY(editor->dirty());
        if(mode==3){Profile p=w->profile;p.id="second-test-workspace";auto other=new Workspace(p,{},true,nullptr,&main.repository,&main.bookmarks);main.workspaces[p.id]=other;main.tabs->addTab(other,"second");connect(other,&Workspace::readyToClose,&main,[&main,other]{main.workspaceSaveFinished(other,true);});connect(other,&Workspace::closeCancelled,&main,[&main,other]{main.workspaceSaveFinished(other,false);});auto second=other->editors.begin().value();QVERIFY(second);second->code->appendPlainText("second draft");}
        if(mode==2)editor->code->setReadOnly(true);
        int confirmations=0,changedWarnings=0;QTimer driver;driver.setInterval(10);connect(&driver,&QTimer::timeout,&main,[&]{auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(!box)return;if(box->windowTitle()=="退出 FrogShell"){++confirmations;if(mode==1)editor->code->appendPlainText("changed while confirming");auto choice=mode==0?QMessageBox::Cancel:mode==1?QMessageBox::Discard:QMessageBox::Save;box->button(choice)->click();}else if(box->windowTitle()=="状态已变化"){++changedWarnings;box->accept();}});driver.start();main.requestExit();driver.stop();QCOMPARE(confirmations,1);
        if(mode==3){QCOMPARE(main.exitSavePending.size(),2);QVERIFY(!main.exiting);QTRY_VERIFY_WITH_TIMEOUT(main.finalExit,5000);QVERIFY(main.workspaces.isEmpty());QCOMPARE(confirmations,1);}
        else {QTRY_VERIFY_WITH_TIMEOUT(main.exitSavePending.isEmpty(),5000);QVERIFY(!main.exiting);QCOMPARE(main.workspaces.size(),1);QVERIFY(editor->dirty());QCOMPARE(changedWarnings,mode==1?1:0);editor->code->setReadOnly(false);editor->discardDraft();}
        qApp->setQuitOnLastWindowClosed(false);
    }
    void windowsInputAccessibilityAndState(){
        AppLock lock(data->path());enable(lock.security());MainWindow main(true,std::make_shared<LockSecrets>());main.resize(1200,780);main.show();settle();
        auto workspace=main.workspaces.isEmpty()?nullptr:main.workspaces.begin().value();QVERIFY(workspace);auto count=workspace->terminalCount();const auto credentialGeneration=workspace->credentialGeneration,servicesGeneration=workspace->servicesGeneration;
        auto edit=workspace->editors.isEmpty()?nullptr:workspace->editors.begin().value().data();QVERIFY(edit);edit->code->appendPlainText("unsaved lock fixture");const auto document=edit->code->document();const auto text=edit->code->toPlainText();
        QDialog floating(nullptr,Qt::Window);floating.setWindowTitle("sensitive file.txt");auto fl=new QVBoxLayout(&floating);auto secret=new QLabel("sensitive-content");fl->addWidget(secret);floating.resize(640,420);floating.show();
        QDialog modal(&main);modal.setWindowTitle("pending action");auto ml=new QVBoxLayout(&modal);ml->addWidget(new QLineEdit("private input"));modal.setWindowModality(Qt::WindowModal);modal.open();settle();
        lock.lock();settle();QVERIFY(!modal.isVisible());QVERIFY(floating.isVisible());QVERIFY(secret->isHidden());QVERIFY(!secret->isEnabled());QVERIFY(main.centralWidget()->isHidden());QCOMPARE(main.windowTitle(),QString("FrogShell · 已锁定"));
        QVERIFY(main.findChild<QWidget *>("applicationLockSurface"));QVERIFY(floating.findChild<QWidget *>("applicationLockSurface"));
        QWidget later;auto laterLayout=new QVBoxLayout(&later);auto laterText=new QLabel("new secret");laterLayout->addWidget(laterText);later.resize(500,340);later.show();settle();QVERIFY(later.findChild<QWidget *>("applicationLockSurface"));QVERIFY(laterText->isHidden());
        auto iface=QAccessible::queryAccessibleInterface(secret);QVERIFY(iface);QVERIFY(iface->state().invisible);QVERIFY(iface->state().disabled);
        const auto zoom=UiZoom::instance().level();QTest::keyClick(main.windowHandle(),Qt::Key_Plus,Qt::ControlModifier);QCOMPARE(UiZoom::instance().level(),zoom);
        workspace->addTerminal();workspace->openFile("/should-not-open");workspace->reconnect();QCOMPARE(workspace->terminalCount(),count);QCOMPARE(workspace->credentialGeneration,credentialGeneration);QCOMPARE(workspace->servicesGeneration,servicesGeneration);
        QMimeData mime;mime.setUrls({QUrl::fromLocalFile(data->filePath("not-uploaded"))});QDragEnterEvent drag(QPoint(4,4),Qt::CopyAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(workspace->files->viewport(),&drag);QDropEvent drop(QPointF(4,4),Qt::CopyAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(workspace->files->viewport(),&drop);QCOMPARE(workspace->pendingTransfers(),0);
        main.close();QVERIFY(lock.locked());QVERIFY(!main.isVisible());lock.activate();QVERIFY(main.isVisible());
        auto field=main.findChild<QLineEdit *>("applicationUnlockPassword");QVERIFY(field);main.windowHandle()->requestActivate();QTRY_COMPARE(qApp->focusWindow(),main.windowHandle());field->setFocus();QTRY_COMPARE(qApp->focusWidget(),static_cast<QWidget *>(field));
        // Events enter the native QWindow wrapper and must reach the lock field.
        for(char key:QByteArray("fixture-pass"))QTest::keyClick(main.windowHandle(),Qt::Key(key>='a'&&key<='z'?key-'a'+'A':key));
        QCOMPARE(field->text(),QString("fixture-pass"));
        auto unlockButton=main.findChild<QPushButton *>("applicationUnlock");QVERIFY(unlockButton);QTest::mouseClick(main.windowHandle(),Qt::LeftButton,{},unlockButton->mapTo(&main,unlockButton->rect().center()));
        QTRY_VERIFY_WITH_TIMEOUT(!lock.locked(),10000);settle();QVERIFY(!main.centralWidget()->isHidden());QVERIFY(secret->isVisible());QVERIFY(secret->isEnabled());QCOMPARE(floating.windowTitle(),QString("sensitive file.txt"));QCOMPARE(edit->code->document(),document);QCOMPARE(edit->code->toPlainText(),text);QCOMPARE(workspace->terminalCount(),count);
        edit->discardDraft();edit->code->document()->setModified(false);
    }
    void newModalAndDirtyCloseAreCancelled(){
        AppLock lock(data->path());enable(lock.security());MainWindow main(true,std::make_shared<LockSecrets>());main.show();settle();
        auto w=main.workspaces.begin().value();auto editor=w->editors.begin().value();QVERIFY(editor);editor->code->appendPlainText("keep dirty draft");QVERIFY(editor->dirty());
        QTimer::singleShot(0,&lock,&AppLock::lock);w->closePage(editor);QVERIFY(lock.locked());QVERIFY(editor&&editor->dirty());QVERIFY(w->editors.values().contains(editor));
        QDialog createdLocked(&main);auto layout=new QVBoxLayout(&createdLocked);layout->addWidget(new QLabel("pending private text"));
        QTimer::singleShot(1000,&createdLocked,&QDialog::accept);QCOMPARE(createdLocked.exec(),int(QDialog::Rejected));unlock(lock);
        editor->discardDraft();editor->code->document()->setModified(false);
    }
    void realFloatingEditorRoute(){
        AppLock lock(data->path());enable(lock.security());MainWindow main(true,std::make_shared<LockSecrets>());main.resize(1440,900);main.show();settle();
        auto workspace=main.workspaces.begin().value();auto editor=workspace->editors.begin().value();QVERIFY(editor);editor->code->appendPlainText("floating draft");auto document=editor->code->document();auto text=editor->code->toPlainText();
        workspace->floatPage(editor);settle();auto floating=workspace->floating.value(editor);QVERIFY(floating);QCOMPARE(floating->parentWidget(),workspace);QVERIFY(floating->isVisible());const auto parent=editor->parentWidget();
        lock.lock();settle();QVERIFY(floating->isVisible());QVERIFY(floating->isEnabled());auto surface=floating->findChild<QWidget *>("applicationLockSurface");QVERIFY(surface);auto password=surface->findChild<QLineEdit *>();QVERIFY(password);QVERIFY(password->isEnabled());
        floating->windowHandle()->requestActivate();QTRY_COMPARE(qApp->focusWindow(),floating->windowHandle());password->setFocus();QTRY_COMPARE(qApp->focusWidget(),static_cast<QWidget *>(password));for(char key:QByteArray("fixture-pass"))QTest::keyClick(floating->windowHandle(),Qt::Key(key>='a'&&key<='z'?key-'a'+'A':key));QCOMPARE(password->text(),QString("fixture-pass"));QTest::keyClick(floating->windowHandle(),Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!lock.locked(),10000);settle();QVERIFY(floating->isVisible());QVERIFY(editor->isEnabled());QCOMPARE(editor->parentWidget(),parent);QCOMPARE(editor->code->document(),document);QCOMPARE(editor->code->toPlainText(),text);editor->discardDraft();document->setModified(false);
    }
    void startupNoSecretsAndStaleTrust(){
        {SecurityStore initial(data->path());enable(initial);}
        auto backend=std::make_shared<LockSecrets>();int constructed=0;AppLock lock(data->path());QVERIFY(lock.locked());
        connect(&lock,&AppLock::unlocked,&lock,[&]{++constructed;});lock.showStartup();settle();QCOMPARE(constructed,0);QCOMPARE(backend->reads.load(),0);QVERIFY(!lock.startup.isNull());
        ConnectionRepository repository(nullptr,backend);Profile p;p.id="private";p.host="127.0.0.1";p.user="fixture";repository.profiles={p};bool readDone=false;repository.read(p,&lock,[&](SecretResult result,quint64){readDone=true;QVERIFY(result.status!=SecretResult::Found);});QTRY_VERIFY(readDone);QCOMPARE(backend->reads.load(),0);
        Session session(p,"fixture-only");QSignalSpy phase(&session,&Session::phase);session.start();QVERIFY(session.wait(1000));QCOMPARE(phase.size(),0);
        bool trusted=false;QWidget owner;requestHostTrust(&owner,p,"fixture-fingerprint",false,[]{return true;},[&](bool accepted){trusted=accepted;});settle();QVERIFY(!trusted);
        unlock(lock);QCOMPARE(constructed,1);settle();QVERIFY(!trusted);
    }
    void screenshots_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void screenshots(){
        QFETCH(bool,light);applyTheme(light);AppLock lock(data->path());enable(lock.security());MainWindow main(true,std::make_shared<LockSecrets>());main.resize(1440,900);main.show();settle();
        auto workspace=main.workspaces.begin().value();auto editor=workspace->editors.begin().value();QVERIFY(editor);workspace->floatPage(editor);auto floating=workspace->floating.value(editor);QVERIFY(floating);floating->resize(720,480);lock.lock();settle();
        const auto suffix=light?QString("light"):QString("dark");QVERIFY(main.grab().save(report()+"/lock-workspace-"+suffix+".png"));QVERIFY(floating->grab().save(report()+"/lock-floating-"+suffix+".png"));
        UiZoom::instance().setLevel(2);main.resize(800,480);settle();auto overlay=main.findChild<QWidget *>("applicationLockSurface");QVERIFY(overlay);QCOMPARE(overlay->geometry(),main.rect());auto input=overlay->findChild<QLineEdit *>();QVERIFY(input);QVERIFY(overlay->rect().contains(input->mapTo(overlay,input->rect().bottomRight())));QVERIFY(main.grab().save(report()+"/lock-small-zoom-"+suffix+".png"));UiZoom::instance().setLevel(0);unlock(lock);
        lock.lock();lock.showStartup();settle();QVERIFY(lock.startup->grab().save(report()+"/lock-startup-"+suffix+".png"));unlock(lock);
        for(auto workspace:main.workspaces)for(auto editor:workspace->editors)if(editor){editor->discardDraft();editor->code->document()->setModified(false);}
    }
    void existingServiceAndTransferLoopback(){
        const int servicePort=qEnvironmentVariableIntValue("RABBITSHELL_SERVICE_PORT"),uploadPort=qEnvironmentVariableIntValue("RABBITSHELL_UPLOAD_PORT");
        if(!servicePort||!uploadPort)QSKIP("Run private lock integration fixture (loopback only)");
        AppLock lock(data->path());enable(lock.security());Profile profile;profile.id="lock-service";profile.host="127.0.0.1";profile.user="fixture";profile.port=servicePort;profile.timeout=3;
        Workspace w(profile,QByteArray("fixture-only"),true);w.demo=false;w.online=true;w.show();auto panel=w.servicesPanel;panel->demo=false;panel->setActivity(false);
        auto service=w.serviceSession(&w,Session::Mode::Commands);QVERIFY(service);QVERIFY(panel->mutationGuard);QCOMPARE(panel->mutationGuard(),QString());
        disconnect(service,&Session::hostKey,&w,nullptr);connect(service,&Session::hostKey,&w,[service](const QString &,bool changed){service->trust(!changed);});panel->setSession(service);service->start();
        QTRY_VERIFY_WITH_TIMEOUT(panel->ready&&!panel->busy,10000);auto originalWorker=panel->worker;QSignalSpy serviceCompleted(service,&Session::completed);
        auto transferProfile=profile;transferProfile.id="lock-transfer";transferProfile.port=uploadPort;
        auto transfer=new Session(transferProfile,"fixture-only",&w);QSignalSpy connected(transfer,&Session::connected);connect(transfer,&Session::hostKey,transfer,[transfer](const QString &,bool changed){transfer->trust(!changed);});transfer->start();QTRY_COMPARE_WITH_TIMEOUT(connected.size(),1,10000);
        w.transferSession=transfer;w.transferReady=true;w.attachSession(transfer,true);
        QFile input(data->filePath("meter-slow-lock.bin"));QVERIFY(input.open(QIODevice::WriteOnly));QByteArray bytes(1024*1024,'L');QCOMPARE(input.write(bytes),qint64(bytes.size()));input.close();
        QSignalSpy progress(transfer,&Session::transferProgress),completed(transfer,&Session::completed);QVERIFY(w.queueTransfers({input.fileName()},true,"/root"));QTRY_VERIFY_WITH_TIMEOUT(progress.size()>0,5000);
        const int progressBefore=progress.size();const auto firstBytes=progress.last()[2].toULongLong();lock.lock();QVERIFY(!panel->mutationGuard().isEmpty());QVERIFY(!panel->submit("restart",{{"unit","installed.service"}}));
        QTRY_VERIFY_WITH_TIMEOUT(progress.size()>progressBefore&&progress.last()[2].toULongLong()>firstBytes,5000);QVERIFY(lock.locked());QVERIFY(transfer->isRunning());
        unlock(lock);QCOMPARE(panel->worker,originalWorker);QCOMPARE(panel->mutationGuard(),QString());QTRY_VERIFY_WITH_TIMEOUT(!panel->busy,5000);QVERIFY(panel->submit("restart",{{"unit","installed.service"}}));QTRY_VERIFY_WITH_TIMEOUT(serviceCompleted.size()>0&&!panel->busy,10000);
        QTRY_VERIFY_WITH_TIMEOUT(w.activeTransfer==0,15000);QVERIFY(!completed.isEmpty());QFile destination(qEnvironmentVariable("RABBITSHELL_UPLOAD_ROOT")+"/root/meter-slow-lock.bin");QVERIFY(destination.open(QIODevice::ReadOnly));QCOMPARE(destination.readAll(),bytes);
        transfer->stop();QVERIFY(transfer->wait(5000));service->stop();QVERIFY(service->wait(5000));w.demo=true;
    }
    void windowsNativeNotifications(){
#ifdef Q_OS_WIN
        if(QGuiApplication::platformName()!="windows")QSKIP("Run with windows plugin; all test windows stay hidden");
        AppLock lock(data->path());enable(lock.security());QVERIFY(lock.platformSupport().startsWith("已启用 Windows"));
        const auto receiver=reinterpret_cast<HWND>(lock.systemEvents->nativeWindow());QVERIFY(IsWindow(receiver));QVERIFY(!IsWindowVisible(receiver));
        for(auto event:{WTS_SESSION_LOCK,WTS_CONSOLE_DISCONNECT,WTS_REMOTE_DISCONNECT}){SendMessageW(receiver,WM_WTSSESSION_CHANGE,event,0);QVERIFY(lock.locked());unlock(lock);}
        SendMessageW(receiver,WM_POWERBROADCAST,PBT_APMSUSPEND,0);QVERIFY(lock.locked());unlock(lock);QVERIFY(!IsWindowVisible(receiver));
#else
        QSKIP("Windows native message route only");
#endif
    }
    void securitySettingsScreens_data(){screenshots_data();}
    void lockMenuEntry(){
        AppLock lock(data->path());enable(lock.security());MainWindow main(true,std::make_shared<LockSecrets>());main.show();main.activateWindow();settle();
        QVERIFY(!main.tabs->cornerWidget(Qt::TopRightCorner));auto bar=main.menuBar();auto preferences=bar->findChild<QMenu *>("applicationMenu-设置");QVERIFY(preferences);
        QTest::mouseClick(bar,Qt::LeftButton,{},bar->actionGeometry(preferences->menuAction()).center());settle();auto action=preferences->findChild<QAction *>("applicationLock");QVERIFY(action&&action->isVisible());
        QTest::mouseClick(preferences,Qt::LeftButton,{},preferences->actionGeometry(action).center());settle();QVERIFY(lock.locked());QVERIFY(main.findChild<QWidget *>("applicationLockSurface"));unlock(lock);
        for(auto w:main.workspaces)for(auto editor:w->editors)editor->discardDraft();
    }
    void securitySettingsScreens(){
        QFETCH(bool,light);applyTheme(light);AppLock lock(data->path());enable(lock.security());MainWindow main(false,std::make_shared<LockSecrets>());main.resize(1440,900);main.show();settle();
        bool captured=false;QTimer timer;timer.setInterval(20);connect(&timer,&QTimer::timeout,&main,[&]{for(auto widget:QApplication::topLevelWidgets())if(auto dialog=qobject_cast<QDialog *>(widget);dialog&&dialog->isVisible()&&dialog->windowTitle()=="设置"){
            if(auto toggle=dialog->findChild<Toggle *>("applicationLockToggle")){if(auto scroll=dialog->findChild<QScrollArea *>())scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());settle();captured=dialog->grab().save(report()+QString("/lock-settings-%1.png").arg(light?"light":"dark"));dialog->accept();timer.stop();return;}
        }});timer.start();main.settings();timer.stop();QVERIFY(captured);
    }
    void singleInstanceWakeOnly(){
        SingleInstance primary(data->path());QCOMPARE(primary.acquire(),SingleInstance::Primary);QSignalSpy wake(&primary,&SingleInstance::activationRequested);SingleInstance second(data->path()+"/./");QCOMPARE(second.acquire(),SingleInstance::Activated);QTRY_COMPARE(wake.size(),1);
#ifdef Q_OS_WIN
        SingleInstance third(data->path().toUpper());QCOMPARE(third.acquire(),SingleInstance::Activated);QTRY_COMPARE(wake.size(),2);
#endif
    }
};
}
QTEST_MAIN(rs::AppLockTests)
#include "test_applock.moc"
