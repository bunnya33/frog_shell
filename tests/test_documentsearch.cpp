#include "../src/ui/documentsearch.h"
#include "../src/ui/mainwindow.h"
#include "../src/ui/applock.h"
#include "../src/ui/windowstyle.h"
#include "../src/ui/uizoom.h"
#include "../src/core/operationgate.h"
#include <QtTest>
namespace rs {
class DocumentSearchTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary;
    std::unique_ptr<QTemporaryDir> caseData;
    QString report;
    Profile profile(const QString &id="search-a"){Profile p;p.id=id;p.name="staging-api";p.user="demo";p.host="192.0.2.21";return p;}
    QString sample() const {return "get gateway error 中文\nrelay internal 已连接\nGet Gateway Error 重试\nget gateway error; get gateway error\n";}
    Editor *add(Workspace &workspace,const QString &path,const QString &text){
        auto editor=new Editor(workspace.profile.id,path,text.toUtf8(),{});workspace.editors[path]=editor;workspace.editArea->addPage(editor,QFileInfo(path).fileName());return editor;
    }
    void query(DocumentSearchController &controller,const QString &text){controller.parameters()->findChild<QLineEdit *>("advancedSearchQuery")->setText(text);controller.executeSearch();}
    void settle(){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
    QList<EditorExtraSelection> tokens(CodeEdit *code,const QColor &color=QColor("#f6d75d")){
        QList<EditorExtraSelection> found;for(const auto &s:code->extraSelections())if(s.format.background().color()==color)found.append(s);return found;
    }
  private slots:
    void initTestCase(){
        QVERIFY(temporary.isValid());qputenv("RABBITSHELL_DATA_DIR",temporary.path().toUtf8());QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellSearchTests");QCoreApplication::setApplicationName("RabbitShellSearchTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
        QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,temporary.path());QSettings().setValue("update/automaticCheckEnabled",false);
        QApplication::setStyle("Fusion");for(auto font:{"C:/Windows/Fonts/msyh.ttc","C:/Windows/Fonts/consola.ttf"})if(QFileInfo::exists(font))QFontDatabase::addApplicationFont(font);
        QApplication::setFont(QFont("Microsoft YaHei UI",10));QApplication::setWindowIcon(applicationIcon());applyTheme(false);
        report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",temporary.path());QDir().mkpath(report);
    }
    void init(){
        // Theme/data rows must never recover an earlier fixture's autosaved draft.
        caseData=std::make_unique<QTemporaryDir>(temporary.filePath("case-XXXXXX"));QVERIFY(caseData->isValid());
        qputenv("RABBITSHELL_DATA_DIR",caseData->path().toUtf8());
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
    void editorHighlightsFollowResultsAndEdits(){
        QMainWindow main;auto workspace=new Workspace(profile("highlight"),{},true);main.setCentralWidget(workspace);
        const QString text="修复 修复\nFix fix FIX\n修复完成 修复\n";
        auto a=add(*workspace,"/test/a.js",text),b=add(*workspace,"/test/b.js","修复 B 修复\n");
        DocumentSearchController controller(&main);controller.registerWorkspace(workspace);main.resize(1000,700);main.show();
        QVERIFY(controller.openFor(workspace,a));QSignalSpy done(&controller,&DocumentSearchController::searchFinished);
        const bool dirty=a->dirty(), undo=a->code->document()->isUndoAvailable();
        query(controller,"修复");QTRY_COMPARE(done.count(),1);QVERIFY(done.last().first().toBool());
        QCOMPARE(tokens(a->code).size(),4);QVERIFY(tokens(b->code).isEmpty());
        QCOMPARE(a->code->toPlainText(),text);QCOMPARE(a->dirty(),dirty);QCOMPARE(a->code->document()->isUndoAvailable(),undo);
        controller.activateResult(0);QCOMPARE(tokens(a->code,QColor("#eaa13b")).size(),1);QCOMPARE(tokens(a->code).size(),3);
        QVERIFY(a->code->selectionBackground() == QColor("#eaa13b"));controller.setDetached(true);controller.results()->view()->setFocus();settle();
        QCOMPARE(tokens(a->code,QColor("#eaa13b")).size(),1);
        workspace->editArea->activatePage(b);QVERIFY(tokens(b->code).isEmpty());QCOMPARE(tokens(a->code).size(),3);
        EditorCursor prefix(a->code->document());prefix.insertText("前置\n");
        QCOMPARE(tokens(a->code).size(),3);QCOMPARE(tokens(a->code,QColor("#eaa13b")).size(),1);
        QSignalSpy activated(&controller,&DocumentSearchController::resultActivated);controller.activateResult(0);QCOMPARE(activated.count(),1);QCOMPARE(a->code->textCursor().blockNumber(),1);
        EditorCursor hit(a->code->document());hit.setPosition(a->code->document()->findBlockByNumber(1).position());hit.setPosition(hit.position()+2,QTextCursor::KeepAnchor);hit.insertText("改变");
        QCOMPARE(tokens(a->code,QColor("#eaa13b")).size(),0);QCOMPARE(tokens(a->code).size(),3);controller.activateResult(0);QCOMPARE(activated.count(),1);
        QVERIFY(controller.openFor(workspace,b));query(controller,"修复");QTRY_COMPARE(done.count(),2);QVERIFY(done.last().first().toBool());
        QVERIFY(tokens(a->code).isEmpty());QCOMPARE(tokens(b->code).size(),2);
        auto regex=controller.parameters()->findChild<QCheckBox *>("advancedSearchRegex");regex->setChecked(true);query(controller,"(");QTRY_COMPARE(done.count(),3);QVERIFY(!done.last().first().toBool());QCOMPARE(tokens(b->code).size(),2);
        regex->setChecked(false);query(controller,"不存在");QTRY_COMPARE(done.count(),4);QVERIFY(done.last().first().toBool());QVERIFY(tokens(b->code).isEmpty());
    }
    void editorSearchOptionsAndReopen(){
        QMainWindow main;auto workspace=new Workspace(profile("highlight-options"),{},true);main.setCentralWidget(workspace);
        const QString text="Fix fix FIX fixed\n修复 修复完成 修复\n";auto a=add(*workspace,"/test/a.txt",text);
        DocumentSearchController controller(&main);controller.registerWorkspace(workspace);main.show();QVERIFY(controller.openFor(workspace,a));QSignalSpy done(&controller,&DocumentSearchController::searchFinished);
        auto dialog=controller.parameters();auto sensitive=dialog->findChild<QCheckBox *>("advancedSearchCase"),whole=dialog->findChild<QCheckBox *>("advancedSearchWholeWord"),regex=dialog->findChild<QCheckBox *>("advancedSearchRegex");
        sensitive->setChecked(true);whole->setChecked(true);query(controller,"fix");QTRY_COMPARE(done.count(),1);QCOMPARE(tokens(a->code).size(),1);QCOMPARE(tokens(a->code).first().cursor.selectedText(),QString("fix"));
        sensitive->setChecked(false);query(controller,"fix");QTRY_COMPARE(done.count(),2);QCOMPARE(tokens(a->code).size(),3);
        whole->setChecked(false);regex->setChecked(true);query(controller,"(?=修复)|FIX");QTRY_COMPARE(done.count(),3);QCOMPARE(controller.results()->outcome().matchCount,7);QCOMPARE(tokens(a->code).size(),4);
        query(controller,"修复");QTRY_COMPARE(done.count(),4);QCOMPARE(tokens(a->code).size(),3);
        workspace->editors.remove(a->path);workspace->editArea->removePage(a);delete a;settle();
        auto reopened=add(*workspace,"/test/a.txt","changed");controller.activateResult(0);QVERIFY(tokens(reopened->code).isEmpty());
        reopened->code->setPlainText(text);controller.activateResult(0);QCOMPARE(tokens(reopened->code).size(),2);QCOMPARE(tokens(reopened->code,QColor("#eaa13b")).size(),1);
        EditorCursor prefix(reopened->code->document());prefix.insertText("prefix\n");controller.activateResult(0);QCOMPARE(reopened->code->textCursor().blockNumber(),2);
    }
    void navigationCentersAndCoexistsWithFindAndDiagnostics(){
        QMainWindow main;auto workspace=new Workspace(profile("center"),{},true);main.setCentralWidget(workspace);
        QString text="{\"broken\":\n";for(int i=1;i<120;++i)text+=i==55?QString("unique 修复 token\n"):QString("line %1\n").arg(i);
        auto a=add(*workspace,"/test/center.json",text);DocumentSearchController controller(&main);controller.registerWorkspace(workspace);main.resize(900,650);main.show();
        QVERIFY(controller.openFor(workspace,a));QSignalSpy done(&controller,&DocumentSearchController::searchFinished);query(controller,"修复");QTRY_COMPARE(done.count(),1);
        QTRY_VERIFY_WITH_TIMEOUT(a->findChild<QLabel *>("syntaxStatus")->text().contains("语法错误"),5000);
        controller.activateResult(0);auto centered=[&]{return qAbs(a->code->cursorRect().center().y()-a->code->viewport()->rect().center().y())<=2*a->code->fontMetrics().lineSpacing();};
        QVERIFY(centered());bool underline=false;for(const auto &s:a->code->extraSelections())underline|=s.format.underlineStyle()==QTextCharFormat::WaveUnderline;QVERIFY(underline);
        workspace->floatPage(a);UiZoom::instance().setLevel(1);auto reset=qScopeGuard([]{UiZoom::instance().setLevel(0);});controller.activateResult(0);settle();QVERIFY(centered());
        a->code->showFind();auto find=a->code->findChild<QLineEdit *>("findQuery");auto status=a->code->findChild<QLabel *>("findStatus");QVERIFY(find&&status);find->setText("unique");
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(" / 1"),5000);QVERIFY(centered());
        bool hasAdvanced=false,hasFind=false;underline=false;for(const auto &s:a->code->extraSelections()){
            hasAdvanced|=s.cursor.selectedText()==QString("修复");hasFind|=s.cursor.selectedText()==QString("unique");underline|=s.format.underlineStyle()==QTextCharFormat::WaveUnderline;
        }QVERIFY(hasAdvanced&&hasFind&&underline);QCOMPARE(a->code->toPlainText(),text);
        controller.activateResult(0);QCOMPARE(a->code->textCursor().selectedText(),QString("修复"));
        find->clear();QCOMPARE(a->code->textCursor().selectedText(),QString("修复"));underline=false;for(const auto &s:a->code->extraSelections())underline|=s.format.underlineStyle()==QTextCharFormat::WaveUnderline;QVERIFY(underline);
        workspace->closePage(controller.results());underline=false;for(const auto &s:a->code->extraSelections())underline|=s.format.underlineStyle()==QTextCharFormat::WaveUnderline;QVERIFY(underline);
    }
    void searchDialogSeedsLiteralEditorSelection() {
        QMainWindow main;
        auto workspace = new Workspace(profile("seed-selection"),{},true);
        main.setCentralWidget(workspace);
        const QString selected = "a+b\n 中文";
        auto editor = add(*workspace,"/test/seed.txt",selected+"\nseparator\n"+selected);
        DocumentSearchController controller(&main);
        controller.registerWorkspace(workspace);
        main.show();
        EditorCursor span(editor->code->document());
        span.setPosition(selected.size(),QTextCursor::KeepAnchor);
        editor->code->setTextCursor(span);
        auto dialog = controller.parameters();
        auto regex = dialog->findChild<QCheckBox *>("advancedSearchRegex");
        regex->setChecked(true);
        QVERIFY(controller.openFor(editor->code));
        QCOMPARE(dialog->options().query,selected);
        QVERIFY(!regex->isChecked());
        QSignalSpy done(&controller,&DocumentSearchController::searchFinished);
        controller.executeSearch();
        QTRY_COMPARE(done.count(),1);
        QCOMPARE(controller.results()->outcome().matchCount,2);
        span.clearSelection();
        editor->code->setTextCursor(span);
        QVERIFY(controller.openFor(editor->code));
        QCOMPARE(dialog->options().query,selected);
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
    void lastSearchResultControlsBottomPanel(){
        QMainWindow main; auto workspace=new Workspace(profile("search-last-result"),{},false);
        workspace->demo=true; main.setCentralWidget(workspace); main.resize(1000,700); main.show(); settle();
        auto editor=add(*workspace,"/test/last.txt",sample());
        DocumentSearchController controller(&main); controller.registerWorkspace(workspace);
        QVERIFY(controller.openFor(workspace,editor)); QSignalSpy finished(&controller,&DocumentSearchController::searchFinished);
        query(controller,"relay internal"); QTRY_COMPARE(finished.count(),1); QVERIFY(finished.last().first().toBool());
        auto result=controller.results(); QVERIFY(workspace->groupFor(result));
        workspace->closePage(editor); settle(); QVERIFY(!workspace->terminalMaximized);
        controller.setDetached(true); settle(); QVERIFY(workspace->terminalMaximized);
        controller.setDetached(false); QVERIFY(!workspace->terminalMaximized);
        workspace->closePage(result); settle(); QVERIFY(workspace->terminalMaximized);
        controller.showResults(); QVERIFY(!workspace->terminalMaximized); QVERIFY(workspace->groupFor(result));
    }
    void searchHighlightLifecycleKeepsResultsAndManualSelection(){
        QMainWindow main;auto workspace=new Workspace(profile("search-lifecycle"),{},true);main.setCentralWidget(workspace);
        auto editor=add(*workspace,"/test/lifecycle.txt",sample());DocumentSearchController controller(&main);controller.registerWorkspace(workspace);main.resize(1000,700);main.show();
        QVERIFY(controller.openFor(workspace,editor));QSignalSpy done(&controller,&DocumentSearchController::searchFinished);
        query(controller,"get gateway error");QTRY_COMPARE(done.count(),1);QVERIFY(done.last().first().toBool());QVERIFY(!tokens(editor->code).isEmpty());
        controller.parameters()->hide();QVERIFY(!tokens(editor->code).isEmpty()); // Automatic hiding is not cancellation.
        controller.activateResult(0);QVERIFY(editor->code->textCursor().hasSelection());
        workspace->closePage(controller.results());QVERIFY(tokens(editor->code).isEmpty());QVERIFY(!editor->code->textCursor().hasSelection());
        QCOMPARE(controller.results()->outcome().matchCount,4);QCOMPARE(controller.resultSource().path,editor->path);
        controller.showResults();controller.activateResult(0);QVERIFY(editor->code->textCursor().hasSelection());QVERIFY(!tokens(editor->code).isEmpty());
        EditorCursor manual(editor->code->document());manual.setPosition(0);manual.setPosition(3,QTextCursor::KeepAnchor);editor->code->setTextCursor(manual);
        controller.setDetached(true);controller.results()->window()->close();QCOMPARE(editor->code->textCursor(),manual);QVERIFY(tokens(editor->code).isEmpty());
        controller.showResults();controller.activateResult(0);QVERIFY(controller.openFor(workspace,editor));
        auto input=controller.parameters()->findChild<QLineEdit *>("advancedSearchQuery");input->clear();QVERIFY(tokens(editor->code).isEmpty());QVERIFY(!editor->code->textCursor().hasSelection());
        query(controller,"relay internal");QTRY_COMPARE(done.count(),2);controller.activateResult(0);QVERIFY(editor->code->textCursor().hasSelection());
        controller.parameters()->reject();QVERIFY(tokens(editor->code).isEmpty());QVERIFY(!editor->code->textCursor().hasSelection());QCOMPARE(controller.results()->outcome().matchCount,1);
        qputenv("RABBITSHELL_TEST_STALL_SEARCH","1");auto reset=qScopeGuard([]{qunsetenv("RABBITSHELL_TEST_STALL_SEARCH");});
        QVERIFY(controller.openFor(workspace,editor));query(controller,"gateway");QVERIFY(controller.searching());input->clear();QVERIFY(!controller.searching());QTest::qWait(200);QCOMPARE(done.count(),2);QCOMPARE(controller.results()->outcome().matchCount,1);
    }
    void pendingActivationCancelled_data(){QTest::addColumn<bool>("clearQuery");QTest::newRow("clear-query")<<true;QTest::newRow("reject-dialog")<<false;}
    void pendingActivationCancelled(){
        QFETCH(bool,clearQuery);QMainWindow main;auto workspace=new Workspace(profile("pending-cancel"),{},true);main.setCentralWidget(workspace);
        auto original=add(*workspace,"/test/pending.txt",sample());DocumentSearchController controller(&main);controller.registerWorkspace(workspace);main.show();
        QVERIFY(controller.openFor(workspace,original));QSignalSpy done(&controller,&DocumentSearchController::searchFinished);query(controller,"get gateway error");QTRY_COMPARE(done.count(),1);
        workspace->editors.remove(original->path);workspace->editArea->removePage(original);delete original;settle();
        // Offline reopen stays pending until the fixture source is delivered.
        controller.activateResult(0);
        if(clearQuery)controller.parameters()->findChild<QLineEdit *>("advancedSearchQuery")->clear();else controller.parameters()->reject();
        QCOMPARE(controller.results()->outcome().matchCount,4);
        auto reopened=add(*workspace,"/test/pending.txt",sample());QSignalSpy activated(&controller,&DocumentSearchController::resultActivated);
        emit workspace->stateChanged();QCOMPARE(activated.count(),0);QVERIFY(tokens(reopened->code).isEmpty());
        controller.activateResult(0);QCOMPARE(activated.count(),1);QVERIFY(reopened->code->textCursor().hasSelection());QCOMPARE(controller.resultSource().path,reopened->path);
    }
    void anchorsStaleAndReopenDoNotRestoreDiscardedDraft(){
        QMainWindow main;auto workspace=new Workspace(profile("anchors"),{},true);main.setCentralWidget(workspace);auto a=add(*workspace,"/test/a.txt",sample());DocumentSearchController controller(&main);controller.registerWorkspace(workspace);main.show();
        QVERIFY(controller.openFor(workspace,a));QSignalSpy done(&controller,&DocumentSearchController::searchFinished);query(controller,"get gateway error");QTRY_COMPARE(done.count(),1);
        EditorCursor prefix(a->code->document());prefix.insertText("前置行\n");QSignalSpy activated(&controller,&DocumentSearchController::resultActivated);controller.activateResult(0);QCOMPARE(activated.count(),1);QCOMPARE(a->code->textCursor().blockNumber(),1);
        EditorCursor edit(a->code->document()->findBlockByNumber(1));edit.select(QTextCursor::BlockUnderCursor);edit.insertText("changed");controller.activateResult(0);QCOMPARE(activated.count(),1);QVERIFY(controller.results()->findChild<QLabel *>("documentSearchResultFeedback")->text().contains("过期"));
        a->discardDraft();workspace->editors.remove(a->path);workspace->editArea->removePage(a);delete a;settle();QVERIFY(controller.resultSource().editor.isNull());QVERIFY(controller.resultSource().document.isNull());
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
    void advancedEditorVisuals_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void advancedEditorVisuals(){
        QFETCH(bool,light);applyTheme(light);QMainWindow main;auto workspace=new Workspace(profile("visual-editor"),{},true);main.setCentralWidget(workspace);workspace->editArea->mergeAll();
        const QString wrapped="const message = \"修复："+QString("中文配置与状态检查 ").repeated(30)+"修复完成\";";
        auto a=add(*workspace,"/home/demo/config.js","// 搜索结果 修复\n"+wrapped+"\nconst 修复状态 = true; // 修复\n");
        a->code->setLineWrapMode(EditorSurface::WidgetWidth);DocumentSearchController controller(&main);controller.registerWorkspace(workspace);main.resize(900,680);main.show();
        QVERIFY(controller.openFor(workspace,a));QSignalSpy done(&controller,&DocumentSearchController::searchFinished);query(controller,"修复");QTRY_COMPARE(done.count(),1);
        controller.parameters()->hide();controller.setDetached(true);controller.activateResult(1);controller.results()->view()->setFocus();settle();
        const auto block=a->code->document()->findBlockByNumber(1);QVERIFY(int(a->code->send(SCI_WRAPCOUNT, block.blockNumber()))>=3);
        EditorCursor start(block);const int top=a->code->cursorRect(start).top();const int height=a->code->lineHeight();
        const auto shot=a->code->viewport()->grab().toImage();const QColor row(light?"#f0edf8":"#363447");
        for(int i=0;i<3;++i){const int y=top+i*height+height/2;QVERIFY(y>=0&&y<a->code->viewport()->height());const qreal dpr=shot.devicePixelRatio();QCOMPARE(shot.pixelColor(qRound((a->code->viewport()->width()-8)*dpr),qRound(y*dpr)),row);}
        QCOMPARE(tokens(a->code,QColor("#eaa13b")).size(),1);QVERIFY(tokens(a->code).size()>=3);
        const auto suffix=light?"light":"dark";QVERIFY(main.grab().save(report+"/advanced-editor-"+suffix+".png"));QVERIFY(controller.results()->window()->grab().save(report+"/advanced-detached-"+suffix+".png"));
    }
};
}
int main(int argc,char **argv){
    if(argc==2&&QByteArray(argv[1])=="--internal-document-search-worker"){QCoreApplication worker(argc,argv);if(qEnvironmentVariableIsSet("RABBITSHELL_TEST_STALL_SEARCH"))QThread::sleep(6);return rs::documentSearchWorker();}
    QApplication app(argc,argv);rs::DocumentSearchTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "test_documentsearch.moc"
