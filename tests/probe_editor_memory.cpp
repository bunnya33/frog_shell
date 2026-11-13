#include "../src/ui/mainwindow.h"
#include "../src/ui/theme.h"
#include <QtTest>
#include "../src/ui/editorlexer.h"
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif
namespace rs {
class ProbeBackend final : public SecretBackend {
  public:
    SecretResult read(const QString &) override { return {}; }
    QString write(const QString &, const QByteArray &) override { return "disabled in memory probe"; }
    QString remove(const QString &) override { return {}; }
};
class EditorMemoryTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    QString report;
    QJsonArray samples;
    QElapsedTimer clock;
    void settle() {
        for (int i = 0; i < 3; ++i) {
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        QTest::qWait(10);
    }
    QByteArray content(int bytes) {
        QByteArray line = "const value = 42; // controlled editor lifecycle fixture ";
        line += QByteArray(255 - line.size(), 'x');
        line += '\n';
        return line.repeated(bytes / line.size());
    }
    int buttons(QTabBar *bar) {
        return int(bar->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly).size());
    }
    void sample(MainWindow &window, Workspace &workspace, int round, const QString &phase, int bytes,
                int retained = 0) {
        QJsonObject result{{"round", round}, {"phase", phase}, {"largeBytes", bytes},
                           {"elapsedMs", clock.elapsed()}, {"widgets", QApplication::allWidgets().size()},
                           {"editors", workspace.findChildren<Editor *>().size()},
                           {"documents", workspace.findChildren<EditorDocument *>().size()},
                           {"qtDocuments", workspace.findChildren<QTextDocument *>().size()},
                           {"highlighters", workspace.findChildren<EditorLexer *>().size()},
                           {"mainTabButtons", buttons(window.tabs->tabBar())},
                           {"editorTabButtons", buttons(workspace.editArea->activeGroup()->tabBar())},
                           {"trackedAlive", retained}};
#ifdef Q_OS_WIN
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                                     sizeof(memory)));
        DWORD handles = 0;
        QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &handles));
        result["privateBytes"] = qint64(memory.PrivateUsage);
        result["workingSet"] = qint64(memory.WorkingSetSize);
        result["handles"] = int(handles);
#endif
        samples.append(result);
        QFile file(report + "/memory-" + qEnvironmentVariable("RABBITSHELL_MEMORY_LABEL", "probe") + ".json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"rounds", 25}, {"warmupRounds", 3},
                                            {"platform", QGuiApplication::platformName()}, {"samples", samples}}).toJson());
    }
  private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QString("offscreen"));
        QVERIFY(data.isValid());
        qputenv("RABBITSHELL_DATA_DIR", data.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellMemoryProbe");
        QCoreApplication::setApplicationName("RabbitShellMemoryProbe");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data.path());
        QApplication::setStyle("Fusion");
        for (auto font : {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/consola.ttf"})
            if (QFileInfo::exists(font)) QVERIFY(QFontDatabase::addApplicationFont(font) >= 0);
        QApplication::setFont(QFont("Microsoft YaHei UI", 10));
        QFont::insertSubstitution("monospace", "Consolas");
        applyTheme(false);
        report = qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR", data.path());
        QVERIFY(QDir().mkpath(report));
        clock.start();
    }
    void lifecycle() {
        // Allocate identical source buffers before all samples; no forced heap/working-set trimming.
        const auto smallText = content(128 * 1024), mediumText = content(1024 * 1024),
                   boundaryText = content(19 * 256 * 1024);
        MainWindow window(false, std::make_shared<ProbeBackend>());
        Profile profile;
        profile.id = "isolated-memory-probe";
        auto workspace = new Workspace(profile, {}, true);
        window.workspaces[profile.id] = workspace;
        window.tabs->addTab(workspace, "Memory fixture");
        window.configureWorkspaceTab(workspace);
        window.tabs->setCurrentWidget(workspace);
        window.resize(1200, 800); window.show();
        workspace->editArea->mergeAll();
        for (const auto editor : workspace->editors.values()) workspace->closePage(editor);
        settle();
        QVERIFY(!workspace->session);
        sample(window, *workspace, -4, "initial", 0);
        const auto persistentDocuments=workspace->findChildren<QTextDocument *>().size(); // Rich network legend owns one document.
        for (int round = -3; round < 25; ++round) {
            const auto &largeText = (round == -1 || round == 9 || round == 19) ? boundaryText : mediumText;
            QList<QPointer<QObject>> tracked;
            QList<Editor *> opened;
            for (int index = 0; index < 2; ++index) {
                auto editor = new Editor(profile.id, QString("/probe-%1-%2.txt").arg(round).arg(index),
                                         index ? largeText : smallText, "fixture-hash");
                workspace->editors[editor->path] = editor;
                workspace->editArea->addPage(editor, QString("probe %1").arg(index));
                opened << editor;
                QVERIFY(editor->findChild<QLabel *>("filePathIcon"));
                tracked << editor->findChild<QLabel *>("filePathIcon") << editor << editor->code << editor->code->document()
                        << editor->code->findChild<EditorLexer *>();
                auto group = workspace->editArea->groupFor(editor);
                tracked << group->tabBar()->tabButton(group->indexOf(editor), QTabBar::RightSide);
            }
            if (round % 3 == 0) {
                workspace->editArea->movePage(opened[0], workspace->editArea->groupFor(opened[0]), DropZone::Right);
                workspace->editArea->activatePage(opened[1]);
                workspace->editArea->mergeAll();
            } else if (round % 3 == 1) {
                workspace->floatPage(opened[0]);
                tracked << workspace->floating.value(opened[0]);
                settle();
                workspace->movePage(opened[0], workspace->editArea->activeGroup());
            }
            if (round % 4 == 0) {
                opened[0]->code->insertPlainText("edit and undo");
                QVERIFY(opened[0]->code->document()->isUndoAvailable());
                opened[0]->code->undo();
                QVERIFY(!opened[0]->dirty());
                opened[0]->code->showFind(true);
                settle();
                QTest::keyClick(opened[0]->code, Qt::Key_Escape);
            }
            // Exercise the real main-tab replacement path while keeping one controlled workspace.
            window.configureWorkspaceTab(workspace);
            settle();
            sample(window, *workspace, round, "open", int(largeText.size()));
            if (round == 12) {
                opened[0]->code->insertPlainText("dirty close fixture");
                QTimer::singleShot(0, [] {
                    if (auto box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                        box->button(QMessageBox::Discard)->click();
                });
            }
            for (auto editor : opened) workspace->closePage(editor);
            settle();
            int alive = 0;
            for (const auto &object : tracked) alive += !object.isNull();
            QCOMPARE(alive, 0);
            QCOMPARE(workspace->editors.size(), 0);
            QCOMPARE(workspace->floating.size(), 0);
            QCOMPARE(workspace->findChildren<QTextDocument *>().size(), persistentDocuments);
            QCOMPARE(workspace->findChildren<EditorDocument *>().size(), 0);
            QCOMPARE(workspace->findChildren<EditorLexer *>().size(), 0);
            sample(window, *workspace, round, round == -1 ? "warm" : "close", int(largeText.size()), alive);
        }
        QCOMPARE(workspace->session, nullptr);
    }
};
} // namespace rs
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    rs::EditorMemoryTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "probe_editor_memory.moc"
