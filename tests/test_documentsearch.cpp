#include "../src/ui/documentsearch.h"
#include "../src/ui/mainwindow.h"
#include "../src/ui/applock.h"
#include "../src/ui/windowstyle.h"
#include "../src/core/operationgate.h"
#include <QtTest>
namespace rs {
class DocumentSearchTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary;
    QString report;
    Profile profile(const QString &id="search-a"){Profile p;p.id=id;p.name="staging-api";p.user="demo";p.host="192.0.2.21";return p;}
    QString sample() const {return "get gateway error 中文\nrelay internal 已连接\nGet Gateway Error 重试\nget gateway error; get gateway error\n";}
    Editor *add(Workspace &workspace,const QString &path,const QString &text){
        auto editor=new Editor(workspace.profile.id,path,text.toUtf8(),{});workspace.editors[path]=editor;workspace.editArea->addPage(editor,QFileInfo(path).fileName());return editor;
    }
    void query(DocumentSearchController &controller,const QString &text){controller.parameters()->findChild<QLineEdit *>("advancedSearchQuery")->setText(text);controller.executeSearch();}
    void settle(){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
  private slots:
    void initTestCase(){
        QVERIFY(temporary.isValid());qputenv("RABBITSHELL_DATA_DIR",temporary.path().toUtf8());QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellSearchTests");QCoreApplication::setApplicationName("RabbitShellSearchTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
        QApplication::setStyle("Fusion");for(auto font:{"C:/Windows/Fonts/msyh.ttc","C:/Windows/Fonts/consola.ttf"})if(QFileInfo::exists(font))QFontDatabase::addApplicationFont(font);
        QApplication::setFont(QFont("Microsoft YaHei UI",10));QApplication::setWindowIcon(applicationIcon());applyTheme(false);
        report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",temporary.path());QDir().mkpath(report);
    }
    void cleanup(){OperationGate::setLocked(false);settle();}
    void coreCountsUnicodeZeroWidthAndLimits(){
        auto found=searchDocument(sample(),{"get gateway error"});QCOMPARE(found.matchCount,4);QCOMPARE(found.lines.size(),3);QCOMPARE(found.lines.last().spans.size(),2);
        QCOMPARE(searchDocument(sample(),{"get gateway error",true}).matchCount,3);
        QCOMPARE(searchDocument("中文 中文名 中文",{"中文",false,true}).matchCount,2);
        QCOMPARE(searchDocument("猫😀猫",{"(?=猫)",false,false,true}).matchCount,2);
        QVERIFY(!searchDocument(sample(),{"(",false,false,true}).error.isEmpty());
        QCOMPARE(searchDocument(sample(),{"not present"}).matchCount,0);
        const auto capped=searchDocument(QString(15000,'x'),{"x"});QCOMPARE(capped.matchCount,searchMatchLimit);QVERIFY(!capped.notice.isEmpty());
        QVERIFY(!searchDocument(QString(searchRegexTextLimit+1,'x'),{"x",false,false,true}).error.isEmpty());
    }
    void sourceStableAndSingleContainer(){
        QMainWindow main;auto workspace=new Workspace(profile(),{},true);main.setCentralWidget(workspace);auto a=add(*workspace,"/test/warning.log",sample()),b=add(*workspace,"/test/server.js","get gateway error B");
        DocumentSearchController controller(&main);controller.registerWorkspace(workspace);main.resize(1000,700);main.show();
        QVERIFY(controller.openFor(workspace,a));QSignalSpy finished(&controller,&DocumentSearchController::searchFinished);query(controller,"get gateway error");QTRY_COMPARE(finished.count(),1);QVERIFY(finished.last().first().toBool());
        QCOMPARE(controller.results()->outcome().matchCount,4);auto panel=controller.results();
        workspace->editArea->activatePage(b);b->code->setFocus();QCOMPARE(controller.resultSource().path,a->path);QCOMPARE(controller.source().path,a->path);
        controller.setDetached(true);QVERIFY(controller.isDetached());QCOMPARE(controller.results(),panel);QVERIFY(panel->window()->objectName()=="documentSearchResultsWindow");
        controller.setDetached(false);QVERIFY(!controller.isDetached());QCOMPARE(controller.results(),panel);QCOMPARE(workspace->editArea->groupFor(panel)!=nullptr,true);
        QSignalSpy activated(&controller,&DocumentSearchController::resultActivated);controller.activateResult(2);QCOMPARE(activated.count(),1);QCOMPARE(workspace->editArea->activeGroup()->currentWidget(),a);QCOMPARE(a->code->textCursor().blockNumber(),3);
        QVERIFY(controller.openFor(workspace,b));query(controller,"get gateway error");QTRY_COMPARE(finished.count(),2);QCOMPARE(controller.resultSource().path,b->path);QCOMPARE(controller.results(),panel);QCOMPARE(panel->outcome().matchCount,1);
    }
    void anchorsStaleAndReopenDoNotRestoreDiscardedDraft(){
        QMainWindow main;auto workspace=new Workspace(profile("anchors"),{},true);main.setCentralWidget(workspace);auto a=add(*workspace,"/test/a.txt",sample());DocumentSearchController controller(&main);controller.registerWorkspace(workspace);main.show();
        QVERIFY(controller.openFor(workspace,a));QSignalSpy done(&controller,&DocumentSearchController::searchFinished);query(controller,"get gateway error");QTRY_COMPARE(done.count(),1);
        QTextCursor prefix(a->code->document());prefix.insertText("前置行\n");QSignalSpy activated(&controller,&DocumentSearchController::resultActivated);controller.activateResult(0);QCOMPARE(activated.count(),1);QCOMPARE(a->code->textCursor().blockNumber(),1);
        QTextCursor edit(a->code->document()->findBlockByNumber(1));edit.select(QTextCursor::BlockUnderCursor);edit.insertText("changed");controller.activateResult(0);QCOMPARE(activated.count(),1);QVERIFY(controller.results()->findChild<QLabel *>("documentSearchResultFeedback")->text().contains("过期"));
        workspace->editors.remove(a->path);workspace->editArea->removePage(a);delete a;settle();QVERIFY(controller.resultSource().editor.isNull());QVERIFY(controller.resultSource().document.isNull());
        auto reopened=add(*workspace,"/test/a.txt","different remote content");controller.activateResult(1);QCOMPARE(activated.count(),1);QCOMPARE(reopened->code->toPlainText(),QString("different remote content"));
        reopened->code->setPlainText(sample());controller.activateResult(1);QCOMPARE(activated.count(),2);QCOMPARE(reopened->code->textCursor().blockNumber(),2);
    }
    void replacementCancellationAndRegexFailure(){
        QMainWindow main;auto workspace=new Workspace(profile("jobs"),{},true);main.setCentralWidget(workspace);auto a=add(*workspace,"/test/a.txt",sample());DocumentSearchController controller(&main);controller.registerWorkspace(workspace);main.show();controller.openFor(workspace,a);QSignalSpy done(&controller,&DocumentSearchController::searchFinished);
        query(controller,"get gateway error");query(controller,"relay internal");QTRY_COMPARE(done.count(),1);QVERIFY(done.last().first().toBool());QCOMPARE(controller.results()->outcome().matchCount,1);QCOMPARE(controller.results()->findChild<QLabel *>("documentSearchResultQuery")->text(),QString("relay internal"));
        controller.parameters()->findChild<QCheckBox *>("advancedSearchRegex")->setChecked(true);query(controller,"(");QTRY_COMPARE(done.count(),2);QVERIFY(!done.last().first().toBool());QVERIFY(controller.parameters()->findChild<QLabel *>("advancedSearchFeedback")->text().contains("无效"));
        a->code->setPlainText(QString(60000,'a')+'!');query(controller,"(a|aa)+$");QTRY_COMPARE_WITH_TIMEOUT(done.count(),3,5000);QVERIFY(!done.last().first().toBool());QVERIFY(!controller.searching());
        query(controller,"a");OperationGate::lockAgain();QTest::qWait(150);QVERIFY(!controller.openFor(workspace,a));controller.activateResult(0);OperationGate::setLocked(false);QTest::qWait(300);QCOMPARE(controller.results()->outcome().matchCount,1);
    }
    void floatingFocusAndWorkspaceClose(){
        MainWindow main(true);auto controller=main.documentSearch();auto workspace=main.findChild<Workspace *>();QVERIFY(workspace);auto a=add(*workspace,"/test/float.txt",sample());main.resize(1000,700);main.show();workspace->floatPage(a);a->code->setFocus();settle();QVERIFY(controller->canOpen(a->code));QVERIFY(controller->openFor(a->code));
        QSignalSpy done(controller,&DocumentSearchController::searchFinished);query(*controller,"relay internal");QTRY_COMPARE(done.count(),1);auto result=controller->results();workspace->shutdown();QVERIFY(result);QVERIFY(result->parentWidget()==&main);QVERIFY(result->isHidden());
    }
    void hardDeadlineKeepsUiResponsive(){
        QMainWindow main;auto workspace=new Workspace(profile("deadline"),{},true);main.setCentralWidget(workspace);auto editor=add(*workspace,"/test/deadline.txt",sample());DocumentSearchController controller(&main);controller.registerWorkspace(workspace);main.show();controller.openFor(workspace,editor);
        qputenv("RABBITSHELL_TEST_STALL_SEARCH","1");auto reset=qScopeGuard([]{qunsetenv("RABBITSHELL_TEST_STALL_SEARCH");});
        int ticks=0;QTimer heartbeat;connect(&heartbeat,&QTimer::timeout,this,[&]{++ticks;});heartbeat.start(20);QSignalSpy done(&controller,&DocumentSearchController::searchFinished);QElapsedTimer elapsed;elapsed.start();query(controller,"gateway");
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,5000);QVERIFY(!done.first().first().toBool());QVERIFY(elapsed.elapsed()<4500);QVERIFY(ticks>20);QVERIFY(!controller.searching());QVERIFY(controller.parameters()->findChild<QLabel *>("advancedSearchFeedback")->text().contains("耗时过长"));
    }
    void lockedWindowsAndNavigation(){
        QTemporaryDir lockDirectory;AppLock lock(lockDirectory.path());lock.security().request(SecurityStore::Enable,{},QByteArray("fixture-password"));QTRY_VERIFY_WITH_TIMEOUT(!lock.security().busy(),15000);QVERIFY(lock.security().enabled());
        QMainWindow main;auto workspace=new Workspace(profile("locked-search"),{},true);main.setCentralWidget(workspace);auto a=add(*workspace,"/test/a.txt",sample());DocumentSearchController controller(&main);controller.registerWorkspace(workspace);main.show();controller.openFor(workspace,a);QSignalSpy done(&controller,&DocumentSearchController::searchFinished);query(controller,"relay internal");QTRY_COMPARE(done.count(),1);controller.setDetached(true);lock.lock();
        QVERIFY(lock.locked());QVERIFY(controller.parameters()->findChild<QWidget *>("applicationLockSurface"));QVERIFY(controller.results()->window()->findChild<QWidget *>("applicationLockSurface"));QSignalSpy activated(&controller,&DocumentSearchController::resultActivated);controller.activateResult(0);QCOMPARE(activated.count(),0);
        lock.unlock(QByteArray("fixture-password"));QTRY_VERIFY_WITH_TIMEOUT(!lock.locked(),15000);controller.activateResult(0);QCOMPARE(activated.count(),1);
    }
    void productionVisuals_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void productionVisuals(){
        QFETCH(bool,light);applyTheme(light);QMainWindow main;auto workspace=new Workspace(profile("visual-search"),{},true);main.setCentralWidget(workspace);workspace->editArea->mergeAll();auto a=add(*workspace,"/home/demo/relay/logs/warning.log",sample());a->code->insertPlainText("草稿\n");DocumentSearchController controller(&main);controller.registerWorkspace(workspace);main.resize(1100,740);main.show();controller.openFor(workspace,a);QSignalSpy done(&controller,&DocumentSearchController::searchFinished);query(controller,"get gateway error");QTRY_COMPARE(done.count(),1);settle();
        const auto suffix=light?"light":"dark";QVERIFY(controller.parameters()->grab().save(report+"/search-dialog-"+suffix+".png"));QVERIFY(controller.results()->grab().save(report+"/search-results-"+suffix+".png"));
        controller.setDetached(true);settle();QVERIFY(controller.results()->window()->grab().save(report+"/search-floating-"+suffix+".png"));controller.parameters()->resize(uiPx(320),controller.parameters()->height());settle();
        auto dialog=controller.parameters();auto sensitive=dialog->findChild<QCheckBox *>("advancedSearchCase"),regex=dialog->findChild<QCheckBox *>("advancedSearchRegex");
        for(const auto name:{"advancedSearchCase","advancedSearchWholeWord","advancedSearchRegex"}){
            auto option=dialog->findChild<QCheckBox *>(name);QVERIFY(option);QVERIFY2(option->width()>=option->sizeHint().width(),name);
            QVERIFY(dialog->rect().contains(QRect(option->mapTo(dialog,QPoint()),option->size())));
        }
        QVERIFY(regex->mapTo(dialog,QPoint()).y()>sensitive->mapTo(dialog,QPoint()).y());
        auto footer=dialog->findChild<QWidget *>("advancedSearchFooter");QVERIFY(footer);QCOMPARE(footer->width(),dialog->width());
        auto close=dialog->findChild<QPushButton *>("advancedSearchClose");QVERIFY(close->property("promptButton").toBool());QVERIFY(close->width()>=uiPx(82));QVERIFY(close->height()>=uiPx(31));
        QVERIFY(dialog->grab().save(report+"/search-narrow-"+suffix+".png"));
    }
};
}
int main(int argc,char **argv){
    if(argc==2&&QByteArray(argv[1])=="--internal-document-search-worker"){QCoreApplication worker(argc,argv);if(qEnvironmentVariableIsSet("RABBITSHELL_TEST_STALL_SEARCH"))QThread::sleep(6);return rs::documentSearchWorker();}
    QApplication app(argc,argv);rs::DocumentSearchTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "test_documentsearch.moc"
