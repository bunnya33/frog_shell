#include "../src/core/credentials.h"
#include "../src/ui/mainwindow.h"
#include "../src/ui/theme.h"
#include <QTcpServer>
#include <QtTest>
#include <atomic>

namespace rs {
class MemoryBackend : public SecretBackend {
    QMutex mutex;
    QHash<QString, QByteArray> values;

  public:
    std::atomic_bool failRead = false, failWrite = false, failRemove = false;
    std::atomic_int writes = 0, reads = 0, deletes = 0, delay = 0, readDelay = 0;
    SecretResult read(const QString &key) override {
        ++reads;QThread::msleep(readDelay);
        if (failRead)
            return SecretResult::failure("synthetic locked backend");
        QMutexLocker lock(&mutex);
        return values.contains(key) ? SecretResult{SecretResult::Found, values[key], {}} : SecretResult{};
    }
    QString write(const QString &key, const QByteArray &value) override {
        ++writes;
        QThread::msleep(delay);
        if (failWrite)
            return "synthetic write failure";
        QMutexLocker lock(&mutex);
        values[key] = value;
        return {};
    }
    QString remove(const QString &key) override {
        ++deletes;
        if (failRemove)
            return "synthetic delete failure";
        QMutexLocker lock(&mutex);
        values.remove(key);
        return {};
    }
};
class CredentialTests : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    std::unique_ptr<QTemporaryDir> directory;
    static Profile profile() {
        Profile p;
        p.name = "Synthetic only";
        p.host = "127.0.0.1";
        p.user = "fixture";
        p.port = qEnvironmentVariableIntValue("RABBITSHELL_TEST_PORT");
        if (!p.port)
            p.port = 22;
        return p;
    }
    static bool update(ConnectionRepository &repo, Profile p, std::optional<QByteArray> secret,
                       QString *error = nullptr) {
        QEventLoop loop;
        bool called = false, ok = false;
        repo.update(p, secret, &loop, [&](bool result, const QString &message) {
            called = true;
            ok = result;
            if (error)
                *error = message;
            loop.quit();
        });
        QTimer::singleShot(45000, &loop, &QEventLoop::quit);
        loop.exec();
        return called && ok;
    }
    static bool remove(ConnectionRepository &repo, const QString &id) {
        QEventLoop loop;
        bool ok = false;
        repo.remove(id, &loop, [&](bool result, const QString &) {
            ok = result;
            loop.quit();
        });
        QTimer::singleShot(7000, &loop, &QEventLoop::quit);
        loop.exec();
        return ok;
    }
    // Only touches test-process widgets and the loopback fixture. Never native user windows.
    static void answerDialogs(QTimer &timer, int *logins = nullptr, int *warnings = nullptr,
                              bool remember = true, QString secret = "fixture-only") {
        QObject::connect(&timer, &QTimer::timeout, &timer, [=] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            if (auto field = dialog->findChild<QLineEdit *>("loginSecret")) {
                if (logins)
                    ++*logins;
                field->setText(secret);
                dialog->findChild<QCheckBox *>("loginRememberSecret")->setChecked(remember);
                dialog->accept();
            } else if (auto box = qobject_cast<QMessageBox *>(dialog)) {
                if (box->windowTitle().contains("首次连接") || box->windowTitle() == "断开 SSH")
                    box->button(QMessageBox::Yes)->click();
                else {
                    if (warnings)
                        ++*warnings;
                    if (!box->buttons().isEmpty())
                        box->buttons().first()->click();
                    else
                        box->reject();
                }
            }
        });
        timer.start(10);
    }
  private slots:
    void initTestCase() {
        QVERIFY(root.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellCredentialTest");
        QCoreApplication::setApplicationName("RabbitShellCredentialTest");
        QApplication::setQuitOnLastWindowClosed(false);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
        applyTheme(false);
    }
    void init() {
        directory = std::make_unique<QTemporaryDir>(root.path() + "/case-XXXXXX");
        QVERIFY(directory->isValid());
        qputenv("RABBITSHELL_DATA_DIR", directory->path().toUtf8());
    }
    void cleanup() {
        directory.reset();
    }
    void cancelledMainSecretReadCannotRestart(){
        auto backend=std::make_shared<MemoryBackend>();backend->readDelay=40;
        ConnectionRepository repo(nullptr,backend);auto p=profile();p.rememberSecret=true;repo.profiles={p};
        Workspace w(p,{},true,nullptr,&repo);w.demo=false;
        w.reconnect();QVERIFY(w.mainPending);QVERIFY(w.preparingCredentials);const auto generation=w.credentialGeneration;
        w.cancelMainConnection();QVERIFY(!w.mainPending);QCOMPARE(w.credentialGeneration,generation);
        QTRY_VERIFY_WITH_TIMEOUT(!repo.pending(),3000);QTest::qWait(60);
        QVERIFY(!w.session);QVERIFY(!w.preparingCredentials);QVERIFY(!w.mainPending);QCOMPARE(w.mainPhase,QString("已取消连接"));
        QCOMPARE(backend->reads.load(),1);QCOMPARE(backend->writes.load()+backend->deletes.load(),0);
    }
    void staleMainSecretReadDoesNotRetry(){
        auto backend=std::make_shared<MemoryBackend>();backend->readDelay=40;
        ConnectionRepository repo(nullptr,backend);auto p=profile();p.rememberSecret=true;repo.profiles={p};
        Workspace w(p,{},true,nullptr,&repo);w.demo=false;w.reconnect();repo.invalidate(p.id);
        QTRY_VERIFY_WITH_TIMEOUT(!repo.pending(),3000);QTest::qWait(60);
        QVERIFY(!w.session);QVERIFY(!w.mainPending);QVERIFY(!w.preparingCredentials);QVERIFY(w.mainPhase.contains("配置已更改"));QCOMPARE(backend->reads.load(),1);
    }
    void defaultIdentityExportAndCopy() {
        auto backend = std::make_shared<MemoryBackend>();
        ConnectionRepository repo(nullptr, backend);
        auto p = profile();
        QVERIFY(!p.rememberSecret);
        QVERIFY(update(repo, p, "synthetic-password"));
        QCOMPARE(backend->writes.load(), 0);
        QCOMPARE(backend->reads.load(), 0);
        QCOMPARE(loadProfiles()[0].rememberSecret, false);
        auto original = p.credentialKey();
        for (int part = 0; part < 6; ++part) {
            auto changed = p;
            if (part == 0)
                changed.id += "other";
            if (part == 1)
                changed.host = "localhost";
            if (part == 2)
                ++changed.port;
            if (part == 3)
                changed.user += "other";
            if (part == 4)
                changed.authentication = "key";
            if (part == 5) {
                changed.authentication = "key";
                changed.keyPath = "synthetic-key";
            }
            QVERIFY(changed.credentialKey() != original);
        }
        p.name = "Renamed";
        p.directory = "/home";
        p.group = "group";
        QCOMPARE(p.credentialKey(), original);
        p.rememberSecret = true;
        auto copy = p.duplicate();
        QVERIFY(!copy.rememberSecret);
        QVERIFY(copy.credentialKey() != original);
        auto exported = p.exportedJson();
        QVERIFY(!exported.contains("rememberSecret"));
        QVERIFY(!QJsonDocument(exported).toJson().contains("synthetic-password"));
        QFile config(directory->filePath("connections.json"));
        QVERIFY(config.open(QIODevice::ReadOnly));
        QVERIFY(!config.readAll().contains("synthetic-password"));
    }
    void retainRenameForgetChangeAndDelete() {
        auto backend = std::make_shared<MemoryBackend>();
        ConnectionRepository repo(nullptr, backend);
        auto p = profile();
        p.rememberSecret = true;
        QVERIFY(update(repo, p, "synthetic-one"));
        p.name = "Renamed";
        p.directory = "/home";
        QVERIFY(update(repo, p, std::nullopt));
        QCOMPARE(backend->read(p.credentialKey()).value, QByteArray("synthetic-one"));
        auto changed = p;
        changed.user = "different";
        QVERIFY(!update(repo, changed, std::nullopt));
        QCOMPARE(repo.find(p.id)->user, p.user);
        QVERIFY(update(repo, changed, "synthetic-two"));
        QCOMPARE(backend->read(p.credentialKey()).status, SecretResult::Missing);
        QCOMPARE(backend->read(changed.credentialKey()).value, QByteArray("synthetic-two"));
        changed.rememberSecret = false;
        QVERIFY(update(repo, changed, std::nullopt));
        QCOMPARE(backend->read(changed.credentialKey()).status, SecretResult::Missing);
        changed.rememberSecret = true;
        QVERIFY(update(repo, changed, "synthetic-three"));
        QVERIFY(remove(repo, changed.id));
        QVERIFY(!repo.find(changed.id));
        QCOMPARE(backend->read(changed.credentialKey()).status, SecretResult::Missing);
    }
    void emptyKeyPassphraseIsFound() {
        auto backend = std::make_shared<MemoryBackend>();
        ConnectionRepository repo(nullptr, backend);
        auto p = profile();
        p.authentication = "key";
        p.keyPath = "synthetic-key";
        p.rememberSecret = true;
        QVERIFY(update(repo, p, QByteArray()));
        auto result = backend->read(p.credentialKey());
        QCOMPARE(result.status, SecretResult::Found);
        QVERIFY(result.value.isEmpty());
        QVERIFY(update(repo, p, std::nullopt));
    }
    void backendFailureAndConfigRollback() {
        auto backend = std::make_shared<MemoryBackend>();
        ConnectionRepository repo(nullptr, backend);
        auto p = profile();
        QVERIFY(update(repo, p, std::nullopt));
        p.rememberSecret = true;
        backend->failWrite = true;
        QString error;
        QVERIFY(!update(repo, p, "synthetic", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!repo.find(p.id)->rememberSecret);
        QVERIFY(!loadProfiles()[0].rememberSecret);
        backend->failWrite = false;
        QVERIFY(update(repo, p, "original"));
        backend->failRead = true;
        QVERIFY(!update(repo, p, "replacement"));
        backend->failRead = false;
        backend->failRemove = true;
        auto forget = p;
        forget.rememberSecret = false;
        QVERIFY(!update(repo, forget, std::nullopt));
        QVERIFY(repo.find(p.id)->rememberSecret);
        backend->failRemove = false;
        QVERIFY(QFile::remove(directory->filePath("connections.json")));
        QVERIFY(QDir().mkdir(directory->filePath("connections.json")));
        QVERIFY(!update(repo, p, "replacement", &error));
        QCOMPARE(backend->read(p.credentialKey()).value, QByteArray("original"));
        QVERIFY(QDir().rmdir(directory->filePath("connections.json")));
        QVERIFY(saveProfiles(repo.profiles));
    }
    void delayedAuthenticationCannotRestoreRevokedPolicy() {
        auto backend = std::make_shared<MemoryBackend>();
        ConnectionRepository repo(nullptr, backend);
        auto p = profile();
        QVERIFY(update(repo, p, std::nullopt));
        backend->delay = 150;
        const auto token = repo.revision(p.id);
        int completed = 0;
        repo.authenticated(p, "synthetic", true, token, this, [&](bool, const QString &) { ++completed; });
        QTRY_VERIFY(backend->writes.load() > 0);
        repo.update(p, std::nullopt, this, [&](bool, const QString &) { ++completed; });
        QTRY_COMPARE(completed, 2);
        QVERIFY(!repo.find(p.id)->rememberSecret);
        QCOMPARE(backend->read(p.credentialKey()).status, SecretResult::Missing);
        repo.authenticated(p, "late", true, token, this, [&](bool, const QString &) { ++completed; });
        QTRY_COMPARE(completed, 3);
        QVERIFY(!repo.find(p.id)->rememberSecret);
        QVERIFY(remove(repo, p.id));
        repo.authenticated(p, "late-after-delete", true, repo.revision(p.id), this,
                           [&](bool, const QString &) { ++completed; });
        QTRY_COMPARE(completed, 4);
        QVERIFY(!repo.find(p.id));
        QCOMPARE(backend->read(p.credentialKey()).status, SecretResult::Missing);
    }
    void formBusyRejectAndAsynchronousExit() {
        auto backend = std::make_shared<MemoryBackend>();
        backend->delay = 200;
        MainWindow window(false, backend);
        window.show();
        bool rejectedWhileBusy = false;
        QTimer::singleShot(0, &window, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->findChild<QLineEdit *>("profileName")->setText("Synthetic");
            dialog->findChild<QLineEdit *>("profileHost")->setText("127.0.0.1");
            dialog->findChild<QLineEdit *>("profileUser")->setText("fixture");
            dialog->findChild<QLineEdit *>("profileSecret")->setText("synthetic");
            dialog->findChild<QCheckBox *>("profileRememberSecret")->setChecked(true);
            auto buttons = dialog->findChild<QDialogButtonBox *>();
            QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton);
            QTest::keyClick(dialog, Qt::Key_Escape);
            dialog->close();
            rejectedWhileBusy = dialog->isVisible();
        });
        window.editProfile();
        QVERIFY(rejectedWhileBusy);
        QCOMPARE(window.profiles.size(), 1);
        auto p = window.profiles[0];
        bool done = false;
        window.repository.update(p, QByteArray("replacement"), &window,
                                 [&](bool ok, const QString &) { done = ok; });
        QVERIFY(!window.close());
        QVERIFY(window.isVisible());
        bool ticked = false;
        QTimer::singleShot(10, this, [&] { ticked = true; });
        QTRY_VERIFY(ticked);
        QTRY_VERIFY(done);
        QTRY_VERIFY(!window.isVisible());
        QCOMPARE(backend->read(p.credentialKey()).value, QByteArray("replacement"));
        QVERIFY(loadProfiles()[0].rememberSecret);
    }
    void nativeStoreAcrossProcess() {
        if (!qEnvironmentVariableIntValue("RABBITSHELL_TEST_NATIVE_CREDENTIALS"))
            QSKIP("Set RABBITSHELL_TEST_NATIVE_CREDENTIALS=1 to explicitly test the system credential store");
        auto p = profile();
        p.rememberSecret = true;
        auto backend = systemSecretBackend();
        struct Cleanup {
            std::shared_ptr<SecretBackend> backend;
            QString key;
            ~Cleanup() {
                backend->remove(key);
            }
        } cleanup{backend, p.credentialKey()};
        const auto initial = backend->read(p.credentialKey());
#if defined(Q_OS_LINUX)
        if (initial.status == SecretResult::Error)
            QSKIP(qPrintable("Native Secret Service unavailable: " + initial.error));
#endif
        QCOMPARE(initial.status, SecretResult::Missing);
        ConnectionRepository repo(nullptr, backend);
        QString error;
        const bool saved = update(repo, p, QByteArray("native-synthetic"), &error);
#if defined(Q_OS_LINUX)
        if (!saved && !repo.pending())
            QSKIP(qPrintable("Native Secret Service cannot store: " + error));
#endif
        QVERIFY2(saved, qPrintable(error));
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--native-probe"});
        QVERIFY(child.waitForFinished(10000));
        QCOMPARE(child.exitCode(), 0);
        if (qEnvironmentVariableIntValue("RABBITSHELL_TEST_PORT")) {
            QVERIFY(update(repo, p, QByteArray("fixture-only")));
            child.start(QCoreApplication::applicationFilePath(), {"--native-login"});
            QVERIFY(child.waitForFinished(15000));
            QCOMPARE(child.exitCode(), 0);
        }
        QVERIFY(update(repo, p, QByteArray("native-updated")));
        QCOMPARE(backend->read(p.credentialKey()).value, QByteArray("native-updated"));
        QVERIFY(update(repo, p, QByteArray()));
        QCOMPARE(backend->read(p.credentialKey()).status, SecretResult::Found);
        QVERIFY(backend->read(p.credentialKey()).value.isEmpty());
        QVERIFY(remove(repo, p.id));
        QCOMPARE(backend->read(p.credentialKey()).status, SecretResult::Missing);
    }
    void loopbackWorkspaceLoginReconnectAndTransfer() {
        if (!qEnvironmentVariableIntValue("RABBITSHELL_TEST_PORT"))
            QSKIP("Set RABBITSHELL_TEST_PORT to run loopback workspace credential tests");
        auto backend = std::make_shared<MemoryBackend>();
        MainWindow window(false, backend);
        auto p = profile();
        p.rememberSecret = true;
        QVERIFY(update(window.repository, p, QByteArray("wrong-synthetic")));
        int logins = 0, warnings = 0;
        QTimer responder;
        answerDialogs(responder, &logins, &warnings);
        window.open(p);
        auto workspace = window.workspaces.value(p.id);
        QVERIFY(workspace);
        QTRY_VERIFY2_WITH_TIMEOUT(workspace->isConnected(),
                                  qPrintable(QString("%1; logins=%2 warnings=%3 rejected=%4 preparing=%5")
                                                 .arg(workspace->connectionStatus->text())
                                                 .arg(logins)
                                                 .arg(warnings)
                                                 .arg(workspace->passwordWasRejected)
                                                 .arg(workspace->preparingCredentials)),
                                  15000);
        QTRY_VERIFY(!window.repository.pending());
        QCOMPARE(logins, 1);
        QCOMPARE(backend->read(p.credentialKey()).value, QByteArray("fixture-only"));
        auto editor = new Editor(p.id, "/draft.txt", "original", "synthetic-hash");
        workspace->editors[editor->path] = editor;
        workspace->editArea->addPage(editor, "draft.txt");
        editor->code->insertPlainText("draft");
        workspace->disconnectHost();
        QTRY_VERIFY_WITH_TIMEOUT(!workspace->isConnected() && !workspace->session->isRunning(), 7000);
        QVERIFY(!workspace->credential);
        window.open(*window.find(p.id), QByteArray("fixture-only"));
        QTRY_VERIFY_WITH_TIMEOUT(workspace->isConnected(), 10000);
        QCOMPARE(window.workspaces.size(), 1);
        QCOMPARE(window.workspaces.value(p.id), workspace);
        QVERIFY(editor->dirty());
        QVERIFY(editor->bytes().contains("draft"));
        QCOMPARE(logins, 1);
        // No supplied password: the existing workspace must reload its native-store abstraction.
        workspace->disconnectHost();
        QTRY_VERIFY_WITH_TIMEOUT(!workspace->isConnected() && !workspace->session->isRunning(), 7000);
        const auto readsBeforeReconnect = backend->reads.load();
        window.open(*window.find(p.id));
        QTRY_VERIFY_WITH_TIMEOUT(workspace->isConnected(), 10000);
        QVERIFY(backend->reads.load() > readsBeforeReconnect);
        QCOMPARE(logins, 1);
        QCOMPARE(window.workspaces.size(), 1);
        QVERIFY(editor->dirty());
        const auto writesBeforeTransfer = backend->writes.load();
        Job job;
        job.type = "download";
        job.id = 900;
        job.path = "/config.json";
        job.destination = directory->filePath("download.json");
        workspace->transfers[job.id] = job;
        workspace->transferQueue.enqueue(job.id);
        workspace->transferItems[job.id] =
            new QTreeWidgetItem(workspace->downloadList, {"config.json", "等待"});
        workspace->ensureTransferSession();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(job.destination), 10000);
        QTRY_VERIFY(workspace->transferReady && !workspace->activeTransfer);
        QCOMPARE(backend->writes.load(), writesBeforeTransfer);
        QCOMPARE(warnings, 0);
        editor->saved(editor->bytes(), "synthetic-hash");
        window.closeWorkspace(workspace, true);
        QTRY_VERIFY_WITH_TIMEOUT(!window.workspaces.contains(p.id), 10000);
        const auto readsBeforeOpen = backend->reads.load();
        window.open(*window.find(p.id));
        workspace = window.workspaces.value(p.id);
        QTRY_VERIFY_WITH_TIMEOUT(workspace->isConnected(), 10000);
        QVERIFY(backend->reads.load() > readsBeforeOpen);
        QCOMPARE(logins, 1);
        editor = new Editor(p.id, "/second-draft.txt", "original", "synthetic-hash");
        workspace->editors[editor->path] = editor;
        workspace->editArea->addPage(editor, "second-draft.txt");
        editor->code->insertPlainText("draft");
        // Changed identity must leave editor, queued job identity, and workspace unchanged.
        auto changed = *window.find(p.id);
        changed.user = "other";
        changed.rememberSecret = false;
        QVERIFY(update(window.repository, changed, std::nullopt));
        window.open(changed);
        QCOMPARE(window.workspaces.size(), 1);
        QCOMPARE(workspace->profile.user, QString("fixture"));
        QVERIFY(editor->dirty());
        QVERIFY(remove(window.repository, p.id));
        QVERIFY(!workspace->rememberCredential);
        workspace->shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(
            !workspace->session->isRunning() &&
                (!workspace->transferSession || !workspace->transferSession->isRunning()),
            10000);
        window.workspaces.clear();
    }
    void loopbackTerminalLayoutDoesNotRestart() {
        if (!qEnvironmentVariableIntValue("RABBITSHELL_TEST_PORT"))
            QSKIP("Set RABBITSHELL_TEST_PORT to run loopback terminal layout tests");
        auto backend=std::make_shared<MemoryBackend>();
        MainWindow window(false,backend);
        window.resize(1280,850); window.show();
        auto p=profile(); p.rememberSecret=true;
        QVERIFY(update(window.repository,p,QByteArray("fixture-only")));
        QTimer responder; answerDialogs(responder);
        window.open(p);
        auto workspace=window.workspaces.value(p.id);
        QTRY_VERIFY_WITH_TIMEOUT(workspace->isConnected(),10000);
        QTRY_VERIFY(!workspace->terminals.isEmpty());
        const auto original=workspace->terminals.begin().value();
        QTRY_VERIFY_WITH_TIMEOUT(original.view->text().contains("fixture$"),5000);
        auto session=workspace->session;
        responder.stop();
        const int nextId=workspace->nextTerm;
        QSignalSpy opened(session,&Session::completed), closed(session,&Session::terminalClosed);
        auto editor=new Editor(p.id,"/layout.txt","layout test","hash");
        workspace->editors[editor->path]=editor;
        workspace->editArea->addPage(editor,"layout.txt");
        workspace->movePage(original.frame,workspace->editArea->activeGroup());
        QVERIFY(!workspace->groupFor(original.frame));
        QVERIFY(!workspace->editArea->movePage(original.frame,workspace->editArea->activeGroup(),DropZone::Bottom));
        QCoreApplication::processEvents();
        workspace->floatPage(original.frame);
        QCoreApplication::processEvents();
        workspace->movePage(original.frame);
        auto back=workspace->floating.value(original.frame)->findChild<QAction *>("returnToWorkspace");
        QVERIFY(back); back->trigger();
        QCoreApplication::processEvents();
        QVERIFY(!workspace->floating.contains(original.frame));
        workspace->setTerminalMaximized(true);
        QCoreApplication::processEvents();
        QVERIFY(workspace->editorPanel->isHidden());
        workspace->setTerminalMaximized(false);
        QCoreApplication::processEvents();
        QVERIFY(workspace->editorPanel->isVisible());
        QCOMPARE(workspace->session,session);
        QCOMPARE(workspace->nextTerm,nextId);
        QCOMPARE(workspace->terminals[original.id].view,original.view);
        QVERIFY(workspace->terminals[original.id].running);
        original.view->input("layout-continuity\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(original.view->text().contains("layout-continuity"),5000);
        QCOMPARE(original.view->text().count("fixture$"),1);
        QCOMPARE(closed.size(),0);
        for (const auto &call : opened) QVERIFY(call[0].toString()!="open-terminal");
        workspace->closeTerminal(original.id);
        workspace->closeTerminal(original.id);
        QVERIFY(!workspace->terminals.contains(original.id));
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 5000);
        window.closeWorkspace(workspace,true);
        QTRY_VERIFY_WITH_TIMEOUT(!window.workspaces.contains(p.id),10000);
    }
    void loopbackDirectClosePreservesOtherPty() {
        if (!qEnvironmentVariableIntValue("RABBITSHELL_TEST_PORT"))
            QSKIP("Set RABBITSHELL_TEST_PORT for the isolated live PTY fixture");
        QTimer responder; answerDialogs(responder);
        Workspace workspace(profile(), QByteArray("fixture-only"));
        workspace.resize(1100, 740); workspace.show();
        QTRY_VERIFY_WITH_TIMEOUT(workspace.isConnected(), 10000);
        QTRY_VERIFY(!workspace.terminals.isEmpty());
        const auto original = workspace.terminals.begin().value();
        QTRY_VERIFY_WITH_TIMEOUT(original.view->text().contains("fixture$"), 6000);
        workspace.monitorTimer.stop(); responder.stop();
        auto session = workspace.session;
        QSignalSpy disconnected(session, &Session::disconnected), closed(session, &Session::terminalClosed);
        original.view->input("before-second-open\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(original.view->text().contains("before-second-open"), 5000);
        workspace.addTerminal(original.group);
        const int second = workspace.focusedTerm;
        QVERIFY(second != original.id);
        QTRY_VERIFY_WITH_TIMEOUT(workspace.terminals[second].view->text().contains("fixture$"), 6000);
        original.view->input("after-second-open\r\n");
        workspace.terminals[second].view->input("second-alive\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(original.view->text().contains("after-second-open"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(workspace.terminals[second].view->text().contains("second-alive"), 5000);
        workspace.closeTerminal(original.id); workspace.closeTerminal(original.id);
        QVERIFY(!workspace.terminals.contains(original.id));
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 5000);
        workspace.terminals[second].view->input("after-neighbor-close\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(workspace.terminals[second].view->text().contains("after-neighbor-close"), 5000);
        QCOMPARE(disconnected.size(), 0);
        QCOMPARE(workspace.session, session);
        workspace.shutdown(); QTRY_VERIFY_WITH_TIMEOUT(!session->isRunning(), 10000);
    }
    void loopbackUnsavedReopenAndKeyCorrection() {
        if (!qEnvironmentVariableIntValue("RABBITSHELL_TEST_PORT"))
            QSKIP("Set RABBITSHELL_TEST_PORT to run loopback workspace credential tests");
        auto backend = std::make_shared<MemoryBackend>();
        MainWindow window(false, backend);
        auto p = profile();
        QVERIFY(update(window.repository, p, std::nullopt));
        int logins = 0, warnings = 0;
        QTimer responder;
        answerDialogs(responder, &logins, &warnings, false);
        window.open(p);
        auto workspace = window.workspaces[p.id];
        QTRY_VERIFY_WITH_TIMEOUT(workspace->isConnected(), 15000);
        QCOMPARE(logins, 1);
        QCOMPARE(backend->writes.load(), 0);
        window.closeWorkspace(workspace, true);
        QTRY_VERIFY_WITH_TIMEOUT(!window.workspaces.contains(p.id), 10000);
        window.open(*window.find(p.id));
        workspace = window.workspaces[p.id];
        QTRY_VERIFY_WITH_TIMEOUT(workspace->isConnected(), 10000);
        QCOMPARE(logins, 2);
        QCOMPARE(backend->writes.load(), 0);
        window.closeWorkspace(workspace, true);
        QTRY_VERIFY_WITH_TIMEOUT(!window.workspaces.contains(p.id), 10000);
        responder.stop();
        auto keyProfile = profile();
        keyProfile.authentication = "key";
        keyProfile.keyPath = qEnvironmentVariable("RABBITSHELL_TEST_KEY");
        keyProfile.rememberSecret = true;
        QVERIFY(update(window.repository, keyProfile, QByteArray("synthetic-wrong-passphrase")));
        QTimer keyResponder;
        answerDialogs(keyResponder, &logins, &warnings, true, "fixture-passphrase");
        window.open(keyProfile);
        workspace = window.workspaces[keyProfile.id];
        QTRY_VERIFY_WITH_TIMEOUT(workspace->isConnected(), 10000);
        QTRY_VERIFY(!window.repository.pending());
        QCOMPARE(logins, 3);
        QCOMPARE(backend->read(keyProfile.credentialKey()).value, QByteArray("fixture-passphrase"));
        QCOMPARE(warnings, 0);
        window.closeWorkspace(workspace, true);
        QTRY_VERIFY_WITH_TIMEOUT(!window.workspaces.contains(keyProfile.id), 10000);
    }
    void handshakeFailurePreservesSavedSecret() {
        auto backend = std::make_shared<MemoryBackend>();
        ConnectionRepository repo(nullptr, backend);
        QTcpServer badServer;
        QVERIFY(badServer.listen(QHostAddress::LocalHost));
        connect(&badServer, &QTcpServer::newConnection, this, [&] {
            auto socket = badServer.nextPendingConnection();
            socket->disconnectFromHost();
            socket->deleteLater();
        });
        auto p = profile();
        p.port = badServer.serverPort();
        p.rememberSecret = true;
        QVERIFY(update(repo, p, QByteArray("synthetic-preserve")));
        int logins = 0, warnings = 0;
        QTimer responder;
        answerDialogs(responder, &logins, &warnings);
        Workspace workspace(p, std::nullopt, false, nullptr, &repo);
        QTRY_VERIFY(workspace.session);
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.session->isRunning(), 7000);
        QVERIFY(!workspace.passwordWasRejected);
        QVERIFY(workspace.credential.has_value());
        QCOMPARE(backend->read(p.credentialKey()).value, QByteArray("synthetic-preserve"));
        QCOMPARE(logins, 0);
        QCOMPARE(warnings, 0);
        // The same failed transport in the independent transfer connection must preserve the candidate too.
        workspace.online = true;
        workspace.startSession(true);
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.transferSession || !workspace.transferSession->isRunning(), 7000);
        QVERIFY(!workspace.passwordWasRejected);
        QVERIFY(workspace.credential.has_value());
        QCOMPARE(backend->read(p.credentialKey()).value, QByteArray("synthetic-preserve"));
        QCOMPARE(logins, 0);
        workspace.online = false;
    }
    void loopbackSaveFailureDoesNotRepeatOnTransfer() {
        if (!qEnvironmentVariableIntValue("RABBITSHELL_TEST_PORT"))
            QSKIP("Set RABBITSHELL_TEST_PORT to run loopback workspace credential tests");
        auto backend = std::make_shared<MemoryBackend>();
        MainWindow window(false, backend);
        auto p = profile();
        QVERIFY(update(window.repository, p, std::nullopt));
        backend->failWrite = true;
        int logins = 0, warnings = 0;
        QTimer responder;
        answerDialogs(responder, &logins, &warnings);
        window.open(p);
        auto workspace = window.workspaces[p.id];
        QTRY_VERIFY_WITH_TIMEOUT(workspace->isConnected(), 15000);
        QTRY_VERIFY(!window.repository.pending());
        QCOMPARE(logins, 1);
        QCOMPARE(warnings, 1);
        QVERIFY(!window.find(p.id)->rememberSecret);
        const auto writes = backend->writes.load();
        workspace->startSession(true);
        QTRY_VERIFY_WITH_TIMEOUT(workspace->transferReady, 10000);
        QCOMPARE(backend->writes.load(), writes);
        QCOMPARE(warnings, 1);
        workspace->shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(
            !workspace->session->isRunning() &&
                (!workspace->transferSession || !workspace->transferSession->isRunning()),
            10000);
        window.workspaces.clear();
    }
};
} // namespace rs

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (app.arguments().contains("--native-probe") || app.arguments().contains("--native-login")) {
        // This mode is only called by the test using its inherited isolated data directory.
        if (qEnvironmentVariable("RABBITSHELL_DATA_DIR").isEmpty())
            return 10;
        const auto profiles = rs::loadProfiles();
        if (profiles.size() != 1 || !profiles[0].rememberSecret)
            return 11;
        const auto result = rs::systemSecretBackend()->read(profiles[0].credentialKey());
        if (app.arguments().contains("--native-probe"))
            return result.status == rs::SecretResult::Found && result.value == "native-synthetic" ? 0 : 12;
        if (result.status != rs::SecretResult::Found || result.value != "fixture-only" ||
            profiles[0].host != "127.0.0.1" ||
            profiles[0].port != qEnvironmentVariableIntValue("RABBITSHELL_TEST_PORT"))
            return 13;
        rs::Session session(profiles[0], result.value);
        bool connected = false;
        QObject::connect(&session, &rs::Session::hostKey, &app,
                         [&](const QString &, bool changed) { session.trust(!changed); });
        QObject::connect(&session, &rs::Session::connected, &app, [&] {
            connected = true;
            session.stop();
        });
        QObject::connect(&session, &QThread::finished, &app, &QCoreApplication::quit);
        QTimer::singleShot(10000, &app, [&] { session.stop(); });
        session.start();
        app.exec();
        session.wait();
        return connected ? 0 : 14;
    }
    rs::CredentialTests test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_credentials.moc"
