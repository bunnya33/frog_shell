#include "../src/core/profile.h"
#include "../src/core/filename.h"
#include "../src/core/fileoperationjournal.h"
#include "../src/ui/editor.h"
#include "../src/ui/terminal.h"
#include "../src/ui/theme.h"
#include "../src/ui/windowstyle.h"
#include "../src/ui/workspace.h"
#include <QtTest>
class CoreTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellTest");
        QCoreApplication::setApplicationName("RabbitShellTest");
        rs::applyTheme(false);
    }
    void shellEscaping() {
        QCOMPARE(rs::shellQuote("a'b\n$(id)"), QString("'a'\"'\"'b\n$(id)'"));
        QCOMPARE(rs::remoteJoin("/home/user", "../other"), QString("/home/other"));
    }
    void numberedNames_data() {
        QTest::addColumn<QString>("name");QTest::addColumn<bool>("directory");QTest::addColumn<QString>("expected");
        QTest::newRow("extension")<<QString("install.sh")<<false<<QString("install (2).sh");
        QTest::newRow("compound")<<QString("archive.tar.gz")<<false<<QString("archive (2).tar.gz");
        QTest::newRow("compound-case")<<QString("archive.TAR.XZ")<<false<<QString("archive (2).TAR.XZ");
        QTest::newRow("directory-dot")<<QString("folder.name")<<true<<QString("folder.name (2)");
        QTest::newRow("hidden")<<QString(".env")<<false<<QString(".env (2)");
        QTest::newRow("no-extension")<<QString("file")<<false<<QString("file (2)");
        QTest::newRow("hidden-extension")<<QString(".settings.json")<<false<<QString(".settings (2).json");
    }
    void numberedNames() {
        QFETCH(QString,name);QFETCH(bool,directory);QFETCH(QString,expected);
        QCOMPARE(rs::numberedFileName(name,2,directory),expected);
        QVERIFY(rs::isNumberedFileName(name,expected,directory));
        QVERIFY(!rs::isNumberedFileName(name,name,directory));
        QVERIFY(!rs::isNumberedFileName(name,expected+"-unrelated",directory));
        QVERIFY(!rs::isNumberedFileName(name,expected.replace("(2)","(0)"),directory));
    }
    void sessionHistoryIsolationAndBarriers() {
        QTemporaryDir temp; QVERIFY(temp.isValid());qputenv("RABBITSHELL_DATA_DIR",temp.path().toUtf8());
        rs::Profile p;p.id="private-journal";p.host="fixture";p.user="test";
        rs::FileOperationJournal invalid(p,{});QVERIFY(!invalid.valid());
        rs::FileOperationJournal a(p,"SHA256:fixture-A"),b(p,"SHA256:fixture-B");QVERIFY(a.valid()&&b.valid());QVERIFY(a.mutationLockPath()!=b.mutationLockPath());
        QString guardError;auto guard=a.acquireMutationGuard(&guardError);QVERIFY2(guard,qPrintable(guardError));
        rs::FileOperationJournal same(p,"SHA256:fixture-A");
        QVERIFY(!same.acquireMutationGuard(&guardError));
        auto otherGuard=b.acquireMutationGuard(&guardError);QVERIFY(otherGuard);
        guard.reset();QVERIFY(same.acquireMutationGuard(&guardError));otherGuard.reset();
        QString error,effect;const auto batch=rs::FileOperationJournal::newId();
        QVERIFY(a.prepare(batch,"create",{},"/root/file",{{"size",42}},&effect,&error));
        QJsonObject root;QVERIFY(a.snapshot(&root,&error));QCOMPARE(root["batches"].toArray().size(),1);
        auto saved=root["batches"].toArray().first().toObject()["effects"].toArray().first().toObject();
        QCOMPARE(saved["state"].toString(),QString("prepared"));
        rs::FileOperationJournal shared(p,"SHA256:fixture-A");QVERIFY(shared.snapshot(&root,&error));QCOMPARE(root["batches"].toArray().size(),1);
        QVERIFY(b.snapshot(&root,&error));QVERIFY(root["batches"].toArray().isEmpty());
        QString duplicate;QVERIFY(!a.prepare(rs::FileOperationJournal::newId(),"move","/root/file/child","/elsewhere",{},&duplicate,&error));
        QVERIFY2(a.transition(effect,"prepared","unknown",{},&error),qPrintable(error));QVERIFY(!a.prepare(rs::FileOperationJournal::newId(),"move","/root/file","/other",{},&duplicate,&error));
        QVERIFY(!a.transition(effect,"prepared","committed",{},&error));QVERIFY(a.transition(effect,"unknown","committed",{},&error));
        QVERIFY(a.prepare(batch,"move","/root/file","/other",{},&duplicate,&error));
        QVERIFY(!a.prepare(batch,"delete","/",{},{},nullptr,&error));
        QVERIFY(a.transition(duplicate,"prepared","committed",{},&error));
        QVariantMap undo;QVERIFY(a.beginUndo({},&undo,&error));QCOMPARE(undo["id"].toString(),duplicate);
        QCOMPARE(undo["undoSource"].toString(),QString("/other"));QCOMPARE(undo["undoTarget"].toString(),QString("/root/file"));
        QVERIFY(!a.beginUndo({},&undo,&error));
        QVERIFY(a.transition(duplicate,"undo_prepared","undone",{},&error));
        const auto openBatch=rs::FileOperationJournal::newId();QString openEffect;
        QVERIFY(a.prepare(openBatch,"create",{},"/root/new",{{"directory",false}},&openEffect,&error,true));
        QVERIFY(a.transition(openEffect,"prepared","committed",{},&error));QVERIFY(!a.beginUndo({},&undo,&error));
        QVERIFY(a.sealBatch(openBatch,&error));QVERIFY(a.beginUndo({},&undo,&error));
        QCOMPARE(undo["id"].toString(),openEffect);QVERIFY(undo["undoTarget"].toString().isEmpty());
        QVERIFY(a.transition(openEffect,"undo_prepared","undone",{},&error));
        const auto barrier=rs::FileOperationJournal::newId();QString newer,overwritten;
        QVERIFY(a.prepare(barrier,"create",{},"/root/newer",{},&newer,&error,true));QVERIFY(a.transition(newer,"prepared","committed",{},&error));
        QVERIFY(a.prepare(barrier,"overwrite",{},"/root/old",{},&overwritten,&error,true));QVERIFY(a.transition(overwritten,"prepared","committed",{},&error));
        QVERIFY(a.sealBatch(barrier,&error));QVERIFY(!a.beginUndo({},&undo,&error));QVERIFY(error.contains("不能撤销"));
        rs::Profile ephemeral=p;ephemeral.id="ephemeral";ephemeral.host="other-fixture";
        {rs::FileOperationJournal first(ephemeral,"SHA256:fixture-A");QString id;
         QVERIFY(first.prepare(rs::FileOperationJournal::newId(),"create",{},"/root/temp",{},&id,&error));}
        {rs::FileOperationJournal next(ephemeral,"SHA256:fixture-A");QVERIFY(next.snapshot(&root,&error));QVERIFY(root["batches"].toArray().isEmpty());}
        qunsetenv("RABBITSHELL_DATA_DIR");
    }
    void profilePersistence() {
        QTemporaryDir d;
        qputenv("RABBITSHELL_DATA_DIR", d.path().toUtf8());
        rs::Profile p;
        p.name = "测试";
        p.host = "localhost";
        p.user = "tester";
        p.port = 2222;
        p.authentication = "key";
        p.keyPath = "/local/key";
        QVERIFY(rs::saveProfiles({p}));
        auto profiles = rs::loadProfiles();
        QCOMPARE(profiles.size(), 1);
        QCOMPARE(profiles[0].json(), p.json());
        QVERIFY(!p.json().contains("password"));
        qunsetenv("RABBITSHELL_DATA_DIR");
    }
    void terminalProtocol() {
        rs::Terminal terminal;
        terminal.resize(720, 400);
        terminal.show();
        QTest::qWait(20);
        terminal.feed("hello\r\n\x1b[31mred\x1b[0m\r\n");
        QVERIFY(terminal.text().contains("hello\nred"));
        terminal.feed("\x1b[2J\x1b[H");
        auto utf8 = QString("中文").toUtf8();
        terminal.feed(utf8.left(2));
        terminal.feed(utf8.mid(2));
        QVERIFY(terminal.text().contains("中文"));
        QSignalSpy input(&terminal, &rs::Terminal::input);
#ifdef Q_OS_MACOS
        QTest::keyClick(&terminal, Qt::Key_C, Qt::MetaModifier);
#else
        QTest::keyClick(&terminal, Qt::Key_C, Qt::ControlModifier);
#endif
        QVERIFY(!input.isEmpty());
        QCOMPARE(input.last()[0].toByteArray(), QByteArray(1, 3));
    }
    void terminalHistoryBounded() {
        rs::Terminal t;
        QByteArray line(100, 'x');
        line += "\r\n";
        for (int i = 0; i < 4000; ++i)
            t.feed(line);
        QVERIFY(t.historyBytes() <= 2 * 1024 * 1024);
        QVERIFY(t.text().contains(QString(50, 'x')));
    }
    void editorSaveRaceAndTheme() {
        QTemporaryDir d;
        qputenv("RABBITSHELL_DATA_DIR", d.path().toUtf8());
        QByteArray original = "{\r\n  \"a\": true\r\n}\r\n";
        rs::Editor e("test", "/config.json", original, "hash");
        QCOMPARE(e.bytes(), original);
        e.code->moveCursor(QTextCursor::End);
        e.code->insertPlainText("new");
        auto sent = e.bytes();
        e.code->insertPlainText("er");
        e.saved(sent, "newhash");
        QVERIFY(e.dirty());
        rs::applyTheme(true);
        e.code->refreshTheme();
        QVERIFY(e.dirty());
        QVERIFY(e.bytes().endsWith("newer"));
        e.saved(e.bytes(), "finalhash");
        QVERIFY(!e.dirty());
        e.code->insertPlainText("unsaved");
        e.reload("\xef\xbb\xbfremote\r\n", "remotehash");
        QCOMPARE(e.bytes(), QByteArray("\xef\xbb\xbfremote\r\n"));
        QCOMPARE(e.remoteHash, QByteArray("remotehash"));
        QVERIFY(!e.dirty());
        for (int attempt = 0; attempt < 32; ++attempt) {
            const QByteArray incoming =
                QByteArray("\xef\xbb\xbfvalue=") + QByteArray::number(attempt) + "\r\n";
            e.reload(incoming, "reloaded");
            QCOMPARE(e.bytes(), incoming);
            QVERIFY(!e.dirty());
        }
        qunsetenv("RABBITSHELL_DATA_DIR");
    }
    void detachedEditorLifecycle() {
        QTemporaryDir directory;
        qputenv("RABBITSHELL_DATA_DIR", directory.path().toUtf8());
        rs::Profile profile;
        profile.id = "window-test";
        profile.name = "test";
        rs::Workspace workspace(profile, {}, true);
        workspace.resize(1280, 850);
        workspace.show();
        QTest::qWait(10);
        auto editors = workspace.findChildren<rs::Editor *>();
        QCOMPARE(editors.size(), 4);
        QPointer<rs::Editor> editor = editors[0];
        QPointer<rs::EditorTabs> source;
        for (auto tabs : workspace.findChildren<rs::EditorTabs *>())
            if (tabs->indexOf(editor) >= 0)
                source = tabs;
        QVERIFY(source);
        source->floatEditor(editor);
        QTest::qWait(10);
        QVERIFY(editor);
        QVERIFY(editor->isVisible());
        QTRY_VERIFY(source.isNull());
        QVERIFY(qobject_cast<QDialog *>(editor->window()));
        QVERIFY(editor->window()->windowFlags() & Qt::WindowMaximizeButtonHint);
        QVERIFY(!(editor->window()->windowFlags() & Qt::FramelessWindowHint));
        auto content = editor->bytes();
        workspace.setActive(false);
        QVERIFY(!editor->isVisible());
        workspace.setActive(true);
        QVERIFY(editor->isVisible());
        QCOMPARE(editor->bytes(), content);
        auto area = workspace.findChild<rs::EditorArea *>();
        QVERIFY(area);
        area->movePageRequested(editor);
        QVERIFY(editor->isVisible());
        QCOMPARE(editor->bytes(), content);
        QVERIFY(area->groupFor(editor));
        area->closePageRequested(editor);
        QTRY_VERIFY(editor.isNull());
        qunsetenv("RABBITSHELL_DATA_DIR");
    }
    void windowScreenRecovery() {
        const QList<QRect> screens = {QRect(0, 0, 1920, 1040), QRect(-1600, 0, 1600, 900)};
        const QRect left(-1500, 50, 1000, 700);
        QCOMPARE(rs::fitWindowToScreens(left, screens), left);
        auto disconnected = rs::fitWindowToScreens(QRect(4000, 4000, 1200, 800), screens);
        QVERIFY(screens[0].contains(disconnected));
        auto scaled = rs::fitWindowToScreens(QRect(100, 100, 1280, 850), {QRect(0, 0, 960, 520)});
        QCOMPARE(scaled, QRect(0, 0, 960, 520));
    }
};
QTEST_MAIN(CoreTests)
#include "test_core.moc"
