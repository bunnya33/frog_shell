#include "../src/ui/contentfont.h"
#include "../src/ui/uipreferences.h"
#include "../src/ui/editor.h"
#include "../src/ui/terminal.h"
#include "../src/ui/theme.h"
#include "../src/ui/fontsettings.h"
#include "../src/ui/mainwindow.h"
#include "../src/ui/applock.h"
#include "../src/ui/services.h"
#include "../src/ui/icons.h"
#include "../src/ui/fileicons.h"
#include "../src/ui/styledcombobox.h"
#include "../src/ui/messagebox.h"
#include "promptchecks.h"
#include "../src/core/diagnostics.h"
#include <QtTest>
#include <QtTest/qtestwheel.h>

namespace rs {
class FontMemorySecrets final : public SecretBackend {
  public:
    int reads=0,writes=0,removes=0;
    SecretResult read(const QString &) override { ++reads;return {SecretResult::Missing,{},{}}; }
    QString write(const QString &,const QByteArray &) override { ++writes;return {}; }
    QString remove(const QString &) override { ++removes;return {}; }
};
class AppearanceTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    QString report;
    void wheel(QWidget *target, int y, Qt::KeyboardModifiers modifiers = contentZoomModifier,
               QPoint pixels = {}, int x = 0) {
        const auto point = target->rect().center();
        QWheelEvent e(point,target->mapToGlobal(point),pixels,QPoint(x,y),Qt::NoButton,modifiers,Qt::NoScrollPhase,false);
        QApplication::sendEvent(target,&e); settle();
    }
    void settle() { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents(); }
    void show(Terminal &t) {
        t.resize(1000, 320); t.show(); t.activateWindow(); t.setFocus(); settle();
    }
    QVector<VTermScreenCell> row(const Terminal &t, int r) {
        QVector<VTermScreenCell> cells(t.cols);
        for (int col = 0; col < t.cols; ++col) t.cell(r, col, &cells[col]);
        return cells;
    }
    QString logicalText(const Terminal &t) {
        QString out;
        for(int r=0;r<(t.alternate?0:int(t.history.size()))+t.rows;++r)
            for(int c=0;c<t.usedColumns(r);++c) {
                VTermScreenCell value{};t.cell(r,c,&value);
                if(value.chars[0]==0xffffffffU)continue;
                if(!value.chars[0])out+=' ';
                for(auto ch:value.chars){if(!ch)break;out+=QString::fromUcs4(reinterpret_cast<const char32_t *>(&ch),1);}
            }
        return out;
    }
    void verifyWideCells(const Terminal &t) {
        for(int r=0;r<(t.alternate?0:int(t.history.size()))+t.rows;++r)
            for(int c=0;c<t.usedColumns(r);++c) {
                VTermScreenCell value{};t.cell(r,c,&value);
                if(value.chars[0]==0x4e2d){QCOMPARE(value.width,char(2));QVERIFY(c+1<t.cols);VTermScreenCell next{};QVERIFY(t.cell(r,c+1,&next));QCOMPARE(next.chars[0],uint32_t(0xffffffffU));}
                if(!c)QVERIFY(value.chars[0]!=0xffffffffU);
            }
    }
    QColor ink(const Terminal &t, int r, int col = 0, bool background = false) {
        VTermScreenCell sc{};
        if (!t.cell(r, col, &sc)) return {};
        return t.displayColor(background ? sc.bg : sc.fg, colors().text, colors().background);
    }
    int pixels(const QImage &image, QRect rect, QColor color) {
        int n = 0;
        rect = rect.intersected(image.rect());
        for (int y = rect.top(); y <= rect.bottom(); ++y)
            for (int x = rect.left(); x <= rect.right(); ++x)
                n += image.pixelColor(x, y) == color;
        return n;
    }
    QColor syntax(CodeEdit &code, int block, const QString &needle) {
        const auto b = code.document()->findBlockByNumber(block);
        const int position = int(b.text().indexOf(needle));
        if (position < 0) return {};
        for (const auto &range : b.layout()->formats())
            if (position >= range.start && position < range.start + range.length)
                return range.format.foreground().color();
        return {};
    }
    static QString sampleCode() {
        return QString::fromUtf8(
            "const url = \"https://host/a // still # string\"; // return 42\n"
            "const quote = \"escaped \\\" # literal\"; # tail\n"
            "/* const \"not string\"\n"
            "call() # comment */ const n = 42;\n"
            "\"\"\" triple // #\n"
            "still # // \"\"\"\n"
            "function greet(name) { return true; }\n"
            "{ \"prop\": \"value // #\", \"n\": 42 }\n"
            "// 中文注释 · Cascadia Code NF · 编程字形\n"
            "const status = greet(\"你好 é  \");\n");
    }
  private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QString("offscreen"));
        QVERIFY(data.isValid());
        qputenv("RABBITSHELL_DATA_DIR", data.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellAppearanceTests");
        QCoreApplication::setApplicationName("RabbitShellAppearanceTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data.path());
        QApplication::setStyle("Fusion");
        // Only the CJK UI/fallback font is loaded from the OS in this harness.
        // Cascadia is always read from the executable's Qt resource.
        if (QFileInfo::exists("C:/Windows/Fonts/msyh.ttc")) {
            QVERIFY(QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc") >= 0);
            QApplication::setFont(QFont("Microsoft YaHei UI"));
        }
        report = qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR", data.path());
        QVERIFY(QDir().mkpath(report));
        QGuiApplication::styleHints()->setCursorFlashTime(0);
    }
    void init() { UiZoom::instance().setLevel(0); ContentFonts::instance().resetDefaults(); applyTheme(false); }
    void cleanup(){UiZoom::instance().setLevel(0);}
  private:
    // Real production widgets; all fixture data and state live in the temporary test directory.
    QByteArray redesignSample(bool second) {
        QByteArray b=second?"__CPU__\ncpu 346 0 100 1754 0 0 0 0\n":"__CPU__\ncpu 100 0 100 1000 0 0 0 0\n";
        b+="__OS__\nPRETTY_NAME=\"Ubuntu 24.04 LTS\"\n__ARCH__\nx86_64\n__MEM__\nMemTotal: 8388608 kB\nMemAvailable: 4404019 kB\n__UP__\n302400 0\n__DISK__\nFilesystem 1024-blocks Used Available Capacity Mounted\n/dev/sda1 83886080 44669338 39216742 53% /\n__NET__\n";
        b+=second?"eth0: 2616582 0 0 0 0 0 0 0 739680 0\n":"eth0: 100000 0 0 0 0 0 0 0 100000 0\n";
        b+="eth1: 10000 0 0 0 0 0 0 0 10000 0\nlo: 1000 0 0 0 0 0 0 0 1000 0\n__PROC__\nPID USER %CPU RSS STAT COMMAND\n"
           "18917 deploy 12.4 1074790 R python\n21348 deploy 4.8 301977 S node\n21483 www-data 2.6 67072 S nginx\n1567 postgres 2.1 525312 S postgres\n18923 redis 1.2 212992 S redis-server\n2281 root 0.3 12800 S sshd\n1 root 0.1 13312 S systemd\n482 root 0.1 45056 S systemd-journald\n953 root 0.0 23040 S containerd\n1750 root 0.0 15360 S cron\n2290 deploy 0.0 9216 S bash\n3001 root 0.0 4096 S kworker\n";
        return b;
    }
    void prepareRedesign(Workspace *w) {
        w->editArea->mergeAll();
        for(auto e:w->editors) { w->editArea->removePage(e);delete e; }
        w->editors.clear();settle();
        const QByteArray cpp=QString::fromUtf8("#include <QApplication>\n#include <QMainWindow>\n\n// 初始化服务器工作区，保留原生窗口行为\nint main(int argc, char *argv[]) {\n    QApplication app(argc, argv);\n    QMainWindow window;\n    window.setWindowTitle(\"RabbitShell\");\n    window.resize(1440, 900);\n    window.show();\n\n    // 连接状态与资源监控由会话更新\n    return app.exec();\n}\n").toUtf8();
        auto a=new Editor(w->profile.id,w->directory+"/src/main.cpp",cpp,"fixture");
        auto b=new Editor(w->profile.id,w->directory+"/config.json",QString::fromUtf8("{\n  \"name\": \"staging-api\",\n  \"host\": \"127.0.0.1\",\n  \"port\": 8080,\n  \"environment\": \"staging\",\n  \"logging\": {\n    \"level\": \"info\",\n    \"directory\": \"/var/log/app\"\n  },\n  \"description\": \"测试环境服务配置\"\n}\n").toUtf8(),"fixture");
        w->editors[a->path]=a;w->editors[b->path]=b;w->editArea->addPage(a,"main.cpp");w->editArea->addPage(b,"config.json");w->editArea->movePage(b,w->editArea->groupFor(a),DropZone::Right);w->editArea->activatePage(a);
        a->code->moveCursor(QTextCursor::End);a->code->insertPlainText("// 未保存的本地草稿\n");w->updateEditorTitle(a);
        for(auto t:w->terminals){t.view->feed("\x1b[2J\x1b[H\x1b[32mdeploy@staging-api\x1b[0m:~/project$ npm run dev\r\n\r\n  \x1b[35mVITE\x1b[0m v6.0.0  ready in 318 ms\r\n\r\n  Local:   http://localhost:8080/\r\n  Network: http://192.0.2.21:8080/\r\n\r\n[INFO] 服务已启动，等待连接\r\n");}
        for(int i=0;i<w->files->topLevelItemCount();++i){auto item=w->files->topLevelItem(i);if(item->text(0)=="src"){
            auto file=new QTreeWidgetItem(item,{"main.cpp","2.4 KiB"});file->setIcon(0,FileIconProvider::file("main.cpp"));file->setData(0,Qt::UserRole,QVariantMap{{"path",w->directory+"/src/main.cpp"},{"directory",false}});
            auto sub=new QTreeWidgetItem(item,{"utils",{}});sub->setIcon(0,FileIconProvider::directory());sub->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);item->setExpanded(true);w->files->setCurrentItem(file);
        }}
        w->bookmarkStore->add(w->profile.id,"/home/demo/project");w->bookmarkStore->add(w->profile.id,"/var/log/app");w->bookmarkStore->add(w->profile.id,"/etc/nginx");
        auto split=w->findChild<QSplitter *>("workspaceSplit");if(split)split->setSizes({345,1094});
        w->verticalSplit->setSizes({559,280});settle();
    }
  private slots:
    void findFeatureEvidence_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void findFeatureEvidence(){
        QFETCH(bool,light);applyTheme(light);CodeEdit code;code.resize(820,400);code.setPlainText("foo FOO foobar\nfoo12 ABC34\n中文 你好\n");code.show();code.showFind(true);
        code.query->setText("foo");QTRY_COMPARE_WITH_TIMEOUT(code.matches.size(),4,5000);
        QVERIFY(code.query->width()>=uiPx(130));for(auto b:{code.findCase,code.findWord,code.findRegex}){QVERIFY(b->isVisible());QVERIFY(!b->text().isEmpty());QVERIFY(b->geometry().left()>=code.query->geometry().right());}QVERIFY(code.findPanel->grab().save(report+QString("/find-%1.png").arg(light?"light":"dark")));
        code.findWord->setChecked(true);QTRY_COMPARE_WITH_TIMEOUT(code.matches.size(),2,5000);
        code.findWord->setChecked(false);code.findRegex->setChecked(true);code.query->setText("([A-Za-z]+)([0-9]+)");
        QTRY_COMPARE_WITH_TIMEOUT(code.matches.size(),2,5000);code.replacement->setText("$2-$1");code.replaceMatches(true);
        QVERIFY(code.toPlainText().contains("12-foo 34-ABC"));code.undo();QVERIFY(code.toPlainText().contains("foo12 ABC34"));
        code.query->setText("(");QTRY_COMPARE_WITH_TIMEOUT(code.findStatus->text(),QString("正则表达式无效"),5000);
        code.query->setText("(?=foo)");QTRY_COMPARE_WITH_TIMEOUT(code.matches.size(),4,5000);
        applyTheme(false);
    }
    void settingsCategoryEvidence_data(){findFeatureEvidence_data();}
    void settingsCategoryEvidence(){
        QFETCH(bool,light);applyTheme(light);AppLock lock(data.path());MainWindow window(true,std::make_shared<FontMemorySecrets>());window.show();settle();
        bool captured=false;QTimer::singleShot(0,&window,[&]{auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!dialog)return;auto list=dialog->findChild<QListWidget *>("settingsCategories");if(list){captured=true;for(int i=0;i<list->count();++i){list->setCurrentRow(i);settle();captured=dialog->grab().save(report+QString("/settings-%1-%2.png").arg(light?"light":"dark").arg(i))&&captured;}}dialog->accept();});
        window.settings();QVERIFY(captured);applyTheme(false);
    }
    void promptAndListeningEvidence(){
        for(bool light:{false,true})for(int level:{0,2}){
            UiZoom::instance().setLevel(level);applyTheme(light);
            MainWindow window(true,std::make_shared<FontMemorySecrets>());window.resize(1440,900);window.show();settle();
            auto w=qobject_cast<Workspace *>(window.tabs->currentWidget());QVERIFY(w);w->showListeningPage();w->sidebar->setCurrentIndex(0);settle();
            QVERIFY(!window.tabs->cornerWidget(Qt::TopRightCorner));
            for(auto table:{w->miniListening,w->listeningTable}){
                auto h=table->horizontalHeader();QStyleOptionHeader option;option.initFrom(h);option.state|=QStyle::State_Horizontal;option.orientation=Qt::Horizontal;option.rect=QRect(0,0,h->sectionSize(2),h->height());option.section=2;option.text="端口";option.sortIndicator=QStyleOptionHeader::SortDown;
                const auto text=h->style()->subElementRect(QStyle::SE_HeaderLabel,&option,h);
                QVERIFY2(text.height()>=h->fontMetrics().height(),qPrintable(QString("%1 header height %2, label height %3 < font %4").arg(table->objectName()).arg(h->height()).arg(text.height()).arg(h->fontMetrics().height())));
                QVERIFY(text.top()>=0&&text.bottom()<h->height());QVERIFY(text.width()>=h->fontMetrics().horizontalAdvance(option.text));
                QVERIFY(table->rowCount()>0);
            }
            auto mini=w->miniListening;QScrollArea *scroll=nullptr;for(auto p=mini->parentWidget();p;p=p->parentWidget())if((scroll=qobject_cast<QScrollArea *>(p)))break;
            QVERIFY(scroll);scroll->ensureWidgetVisible(mini,0,0);settle();
            const auto headerRect=QRect(mini->horizontalHeader()->mapTo(scroll->viewport(),QPoint()),mini->horizontalHeader()->size());
            const auto rowRect=mini->visualItemRect(mini->item(0,0)).translated(mini->viewport()->mapTo(scroll->viewport(),QPoint()));
            QVERIFY(scroll->viewport()->rect().contains(headerRect));QVERIFY(scroll->viewport()->rect().contains(rowRect));
            const auto tag=QString("%1-z%2").arg(light?"light":"dark").arg(qRound(UiZoom::instance().factor()*100));
            QVERIFY(window.grab().save(report+"/listening-sidebar-"+tag+".png"));
            if(level==0)QVERIFY(window.grab().save(report+"/main-top-"+tag+".png"));
            for(auto e:w->editors)e->discardDraft();
        }
    }
    void redesignProductionEvidence_data(){ansiAndDefaultEnhancement_data();}
    void redesignProductionEvidence(){
        QFETCH(bool,light);applyTheme(light);QSettings().remove("process-table");
        MainWindow window(true,std::make_shared<FontMemorySecrets>());window.resize(1440,900);window.show();settle();
        auto w=qobject_cast<Workspace *>(window.tabs->currentWidget());QVERIFY(w);QVERIFY(!w->session);
        window.profiles={w->profile};window.profiles[0].directory="/home/demo/project";window.profiles[0].group="测试环境";
        for(int i=0;i<4;++i){Profile p;p.name=QStringList{"production-web","database","dev-tools","backup"}[i];p.host=QString("192.0.2.%1").arg(31+i);p.user="deploy";p.group=i<2?"生产环境":"开发环境";window.profiles<<p;}window.refresh();
        prepareRedesign(w);settle();
        const auto suffix=light?"light":"dark";
        qInfo()<<"terminal columns"<<w->terminalManager->columnWidth(0)<<w->terminalManager->columnWidth(1)<<w->terminalManager->indentation()<<w->terminalManager->visualItemRect(w->terminalManager->currentItem())<<w->terminalManager->font();
        QVERIFY(window.grab().save(report+"/workspace-"+suffix+".png"));
        window.tabs->setCurrentIndex(0);window.connections->setCurrentItem(window.connections->topLevelItem(0));window.connections->topLevelItem(0)->setExpanded(true);settle();
        QVERIFY(window.grab().save(report+"/manager-"+suffix+".png"));
        window.tabs->setCurrentWidget(w);w->showProcessPage();w->editArea->mergeAll();w->sidebar->setCurrentIndex(0);
        w->previousTotal=0;w->lastNetwork.clear();w->networkClock.invalidate();w->monitor(redesignSample(false),32);
        QVERIFY(!w->resourceSummary->cpuUsage().has_value());QVERIFY(w->networkRates.isEmpty());
        QTest::qWait(1000);w->monitor(redesignSample(true),32);
        QVERIFY(w->resourceSummary->cpuUsage().has_value());QVERIFY(qAbs(*w->resourceSummary->cpuUsage()-24.6)<0.01);
        QVERIFY(w->networkRates.contains("eth0"));QVERIFY(qAbs(w->networkRates["eth0"].first/1024/1024-2.4)<0.2);
        auto &series=w->networkPlot->series["eth0"];series.clear();const auto now=QDateTime::currentMSecsSinceEpoch();
        for(int i=0;i<=30;++i)series.append({now-60000+i*2000,{(1.2+0.04*i+0.24*qSin(i*.7))*1024*1024,(360+8.5*i)*1024}});
        series.last().second={2.4*1024*1024,620*1024};
        auto table=w->processes;for(int r=0;r<table->rowCount();++r)if(table->item(r,0)->data(ProcessIdRole).toInt()==18917)table->selectRow(r);
        window.tabs->setFocus();settle();
        QVERIFY(window.grab().save(report+"/system-info-"+suffix+".png"));
        w->showListeningPage();w->sidebar->setCurrentIndex(0);settle();
        QVERIFY(window.grab().save(report+"/listening-ports-"+suffix+".png"));
        QVERIFY(w->resourceSummary->accessibleDescription().contains("24.6%"));QVERIFY(w->networkRate->accessibleDescription().contains("RX"));
        // A partial monitoring payload must still update latency/processes without inventing resource values.
        w->monitor("__NET__\neth0: 2716582 0 0 0 0 0 0 0 839680 0\n__PROC__\nPID USER %CPU RSS STAT COMMAND\n55 root 1.0 512 R worker\n",45);
        QVERIFY(!w->resourceSummary->cpuUsage());QCOMPARE(w->processes->rowCount(),1);QCOMPARE(w->connectionStatus->property("latency").toInt(),45);
        for(auto e:w->editors)e->discardDraft();
    }

    void redesignGeometryAndState_data(){ansiAndDefaultEnhancement_data();}
    void standardMessageBoxes_data(){ansiAndDefaultEnhancement_data();}
    void standardMessageBoxes(){
        QFETCH(bool,light);applyTheme(light);
        for(int level:{0,2}) {
            UiZoom::instance().setLevel(level);QWidget parent;parent.show();
            AppMessageBox box(QMessageBox::Warning,"确认删除","将删除此文件，操作无法恢复。\n/home/deploy/项目说明.md",QMessageBox::Yes|QMessageBox::Cancel,&parent);
            auto cancel=box.button(QMessageBox::Cancel);QSignalSpy finished(&box,&QDialog::finished);box.open();settle();QVERIFY(qobject_cast<QMessageBox *>(&box));QCOMPARE(box.defaultButton(),cancel);
            auto grid=qobject_cast<QGridLayout *>(box.layout());QVERIFY(grid);auto nativeLabel=box.findChild<QLabel *>("qt_msgbox_label");QVERIFY(nativeLabel);
            // First display, before any theme cycle: ordinary buttons must
            // already have their visible frame, not only the default Cancel.
            auto yes=qobject_cast<QPushButton *>(box.button(QMessageBox::Yes));QVERIFY(yes);auto edgeError=test::promptBorderError(yes,light);QVERIFY2(edgeError.isEmpty(),qPrintable(edgeError));
            nativeLabel->grab();auto lineError=test::promptLineError(nativeLabel->findChild<QTextDocument *>());QVERIFY2(lineError.isEmpty(),qPrintable(lineError));QCOMPARE(nativeLabel->font().pixelSize(),uiPx(14));
            box.setText(QString("长路径：/home/")+QString(300,'x')+"/说明.md");box.setInformativeText("保留按钮角色、默认动作和取消行为。");settle();QVERIFY(box.layout());QVERIFY(box.findChild<QLabel *>("qt_msgbox_label")==nativeLabel);
            auto check=new QCheckBox("不再询问");box.setCheckBox(check);box.setDetailedText("诊断详情\n第二行");settle();QCOMPARE(box.checkBox(),check);
            auto buttonBox=box.findChild<QDialogButtonBox *>();QVERIFY(buttonBox);for(auto b:box.buttons())QVERIFY(box.buttonRole(b)!=QMessageBox::InvalidRole);
            auto detail=box.findChild<QWidget *>("qt_msgboxex_detailedtext");QVERIFY(detail);QVERIFY(!detail->isVisible());for(auto b:box.buttons())if(box.buttonRole(b)==QMessageBox::ActionRole)b->click();settle();
            applyTheme(!light);settle();applyTheme(light);settle();QVERIFY(box.findChild<QLabel *>("qt_msgbox_label")==nativeLabel);QCOMPARE(box.checkBox(),check);
            box.hide();box.show();settle();QVERIFY(box.width()<=box.screen()->availableGeometry().width());
            auto symbol=box.findChild<QWidget *>("messageSymbol");QVERIFY(symbol);QCOMPARE(symbol->size(),uiSize(40,40));
            const auto name=QString("/message-%1-z%2.png").arg(light?"light":"dark").arg(qRound(UiZoom::instance().factor()*100));QVERIFY(box.grab().save(report+name));
            auto renderedText=nativeLabel->findChild<QTextDocument *>();QVERIFY(renderedText);int wrappedLines=0;for(auto block=renderedText->begin();block.isValid();block=block.next())for(int n=0;n<block.layout()->lineCount();++n){auto line=block.layout()->lineAt(n);QVERIFY(line.naturalTextWidth()<=nativeLabel->width()+1);++wrappedLines;}QVERIFY(wrappedLines>3);
            lineError=test::promptLineError(renderedText);QVERIFY2(lineError.isEmpty(),qPrintable(lineError));QVERIFY(renderedText->size().height()<=nativeLabel->height()+1);
            box.setText(QString("长说明\n").repeated(100));settle();auto scroll=box.findChild<QScrollArea *>("messageScroll");QVERIFY(scroll);QVERIFY(scroll->verticalScrollBar()->maximum()>0);
            QTest::keyClick(&box,Qt::Key_Escape);QCOMPARE(box.result(),int(QMessageBox::Cancel));QVERIFY(finished.count()>0);
            auto owned=new AppMessageBox(QMessageBox::Information,"完成","已完成",QMessageBox::Ok,&parent);QPointer<QMessageBox> safe=owned;owned->setAttribute(Qt::WA_DeleteOnClose);owned->open();settle();QCOMPARE(owned->defaultButton(),owned->button(QMessageBox::Ok));QVERIFY(owned->defaultButton()->isDefault());QVERIFY(owned->defaultButton()->isVisible());QVERIFY(owned->defaultButton()->isEnabled());QSignalSpy accepted(owned,&QDialog::finished);QTest::keyClick(owned,Qt::Key_Return);QCOMPARE(accepted.count(),1);QCOMPARE(accepted.first().first().toInt(),int(QMessageBox::Ok));QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(safe.isNull());
            AppMessageBox shortBox(QMessageBox::Warning,"删除文件","确定删除“部署配置.json”？\n此操作无法撤销。",QMessageBox::Yes|QMessageBox::Cancel,&parent);shortBox.button(QMessageBox::Yes)->setText("删除");shortBox.open();settle();QCOMPARE(shortBox.button(QMessageBox::Yes)->text(),QString("删除"));QCOMPARE(shortBox.button(QMessageBox::Cancel)->text(),QString("取消"));QCOMPARE(shortBox.standardButton(shortBox.button(QMessageBox::Yes)),QMessageBox::Yes);QVERIFY(shortBox.grab().save(report+QString("/message-delete-%1-z%2.png").arg(light?"light":"dark").arg(qRound(100*UiZoom::instance().factor()))));shortBox.reject();
            AppMessageBox unsaved(QMessageBox::Question,"未保存的更改","“README.md”有未保存的更改。\n关闭前是否保存？",QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,&parent);unsaved.open();settle();QCOMPARE(unsaved.button(QMessageBox::Save)->text(),QString("保存"));QCOMPARE(unsaved.button(QMessageBox::Discard)->text(),QString("不保存"));QVERIFY(unsaved.grab().save(report+QString("/message-unsaved-%1-z%2.png").arg(light?"light":"dark").arg(qRound(100*UiZoom::instance().factor()))));unsaved.reject();
            AppMessageBox explicitDefault(QMessageBox::Question,"确认","自定义默认按钮保持不变",QMessageBox::Ok|QMessageBox::Cancel,&parent);explicitDefault.button(QMessageBox::Ok)->setText("执行本次操作");explicitDefault.setDefaultButton(QMessageBox::Ok);explicitDefault.open();settle();QCOMPARE(explicitDefault.defaultButton(),explicitDefault.button(QMessageBox::Ok));QCOMPARE(explicitDefault.defaultButton()->text(),QString("执行本次操作"));QMessageBox *base=&explicitDefault;base->setText("通过基类更新正文");base->setInformativeText("保留原生网格和按钮");settle();QCOMPARE(explicitDefault.text(),QString("通过基类更新正文"));QSignalSpy explicitFinished(&explicitDefault,&QDialog::finished);QTest::keyClick(&explicitDefault,Qt::Key_Return);QCOMPARE(explicitFinished.count(),1);QCOMPARE(explicitDefault.result(),int(QMessageBox::Ok));
            auto host=new QWidget;auto child=new AppMessageBox(QMessageBox::Question,"确认","保留草稿？",QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,host);QPointer<QMessageBox> childSafe=child;child->open();settle();delete host;QVERIFY(childSafe.isNull());
        }
    }
    void panelHeaderSeam_data(){ansiAndDefaultEnhancement_data();}
    void panelHeaderSeam(){
        QFETCH(bool,light);applyTheme(light);QJsonArray evidence;
        for(int level:{0,1,2}) {
            UiZoom::instance().setLevel(level);settle();
            for(const auto &state:QStringList{"empty","single","multiple","overflow","split"}) {
                EditorArea area;area.resize(1100,430);area.show();settle();
                const int count=state=="empty"?0:state=="single"?1:state=="overflow"?24:3;
                for(int i=0;i<count;++i){auto page=new QPlainTextEdit;page->setPlainText("const value = 42;\n// 页签和操作共享同一条底边\n");area.addPage(page,QString("document-%1.cpp").arg(i));}
                if(state=="split"){auto split=area.activeGroup()->findChild<QToolButton *>("splitEditorGroup");QVERIFY(split&&split->isEnabled());QTest::mouseClick(split,Qt::LeftButton,{},QPoint(uiPx(10),split->height()/2));settle();QCOMPARE(area.groups().size(),2);}
                settle();auto pix=area.grab();const auto image=pix.toImage();const qreal dpr=pix.devicePixelRatio();
                for(auto group:area.groups()) {
                    auto bar=group->tabBar();auto actions=group->cornerWidget(Qt::TopRightCorner);QVERIFY(actions);
                    auto stack=group->findChild<QStackedWidget *>(QString(),Qt::FindDirectChildrenOnly);QVERIFY(stack);
                    const int edge=group->headerHeight()-uiPx(1);
                    QCOMPARE(bar->y(),0);QCOMPARE(actions->y(),0);QCOMPARE(bar->height(),edge);QCOMPARE(actions->height(),edge);
                    QCOMPARE(bar->geometry().bottom(),actions->geometry().bottom());QCOMPARE(stack->y(),group->headerHeight());
                    QVERIFY(bar->geometry().right()<actions->geometry().left());
                    const auto origin=group->mapTo(&area,QPoint());const int y=qFloor((origin.y()+edge+.5)*dpr);
                    for(int x=1;x<group->width()-1;++x)QCOMPARE(image.pixelColor(qFloor((origin.x()+x+.5)*dpr),y),colors().divider);
                    // The row above and page immediately below cannot contain a second divider.
                    for(int x:{group->width()/2,actions->x()+actions->width()/2}) {
                        QVERIFY(image.pixelColor(qFloor((origin.x()+x+.5)*dpr),qFloor((origin.y()+edge-1.5)*dpr))!=colors().divider);
                        QVERIFY(image.pixelColor(qFloor((origin.x()+x+.5)*dpr),qFloor((origin.y()+group->headerHeight()+1.5)*dpr))!=colors().divider);
                    }
                    if(group->count()>1&&state!="overflow") {const int target=group->currentIndex()==0?1:0;QTest::mouseClick(bar,Qt::LeftButton,{},bar->tabRect(target).center());QCOMPARE(group->currentIndex(),target);}
                    if(state=="overflow") {
                        bool scrolled=false;for(auto b:bar->findChildren<QToolButton *>()){if(b->isVisible()&&b->isEnabled()){QVERIFY(!b->geometry().intersects(actions->geometry()));QTest::mouseClick(b,Qt::LeftButton);scrolled=true;break;}}QVERIFY(scrolled);
                    }
                    evidence.append(QJsonObject{{"state",state},{"zoom",qRound(100*UiZoom::instance().factor())},{"dpr",dpr},{"header",group->headerHeight()},{"tabHeight",bar->height()},{"cornerHeight",actions->height()},{"pageTop",stack->y()},{"edgeY",edge}});
                }
                const auto name=QString("/header-%1-%2-z%3-dpr%4").arg(light?"light":"dark",state).arg(qRound(100*UiZoom::instance().factor())).arg(dpr);
                QVERIFY(pix.save(report+name+".png"));QVERIFY(pix.copy(0,0,pix.width(),qCeil((uiPx(36)+uiPx(12))*dpr)).save(report+name+"-crop.png"));
                if(state=="split"){auto merge=area.activeGroup()->findChild<QToolButton *>("mergeEditorGroups");QVERIFY(merge&&merge->isEnabled());QTest::mouseClick(merge,Qt::LeftButton);settle();QCOMPARE(area.groups().size(),1);QCOMPARE(area.activeGroup()->count(),count);}
            }
        }
        QFile file(report+QString("/header-%1-dpr%2.json").arg(light?"light":"dark").arg(qApp->devicePixelRatio()));QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(evidence).toJson());
    }
    void redesignGeometryAndState(){
        QFETCH(bool,light);applyTheme(light);QSettings().remove("process-table");
        MainWindow window(true,std::make_shared<FontMemorySecrets>());window.resize(1440,900);window.show();settle();
        auto w=qobject_cast<Workspace *>(window.tabs->currentWidget());QVERIFY(w);window.profiles={w->profile};window.refresh();prepareRedesign(w);settle();
        auto editor=w->editors.value(w->directory+"/src/main.cpp");QVERIFY(editor);auto document=editor->code->document();auto text=editor->bytes();
        QJsonArray metrics;
        auto split=w->findChild<QSplitter *>("workspaceSplit");QVERIFY(split);auto handle=split->handle(1);
        // Qt expands a 1px visual splitter into neighbouring panes for pointer input.
        const auto before=split->sizes();const QPoint hit=handle->rect().center()+QPoint(-1,0);
        QVERIFY(handle->rect().contains(hit));QVERIFY(handle->width()>=5);
        QTest::mousePress(handle,Qt::LeftButton,{},hit);QTest::mouseMove(handle,hit+QPoint(25,0));QTest::mouseRelease(handle,Qt::LeftButton,{},hit+QPoint(25,0));settle();
        QVERIFY(split->sizes()[0]>before[0]);split->setSizes(before);settle();
        for(int level:{0,1,2}){
            UiZoom::instance().setLevel(level);settle();
            const auto rail=w->findChild<QFrame *>("rail");QVERIFY(rail);QCOMPARE(rail->width(),uiPx(44));QCOMPARE(w->terminalManager->width(),uiPx(170));
            QCOMPARE(w->terminalManager->columnWidth(1),uiPx(60));
            QVERIFY(w->terminalManager->columnWidth(0)-2*w->terminalManager->indentation()-w->terminalManager->iconSize().width()-uiPx(8)>=w->terminalManager->fontMetrics().horizontalAdvance("shell"));
            QCOMPARE(window.tabs->tabBar()->height()+uiPx(1),uiPx(36));QCOMPARE(w->findChild<QWidget *>("resourceHeader")->height(),uiPx(36));
            QCOMPARE(w->connectionStatus->parentWidget()->height(),uiPx(24));
            for(auto group:w->editArea->groups()){
                QCOMPARE(group->headerHeight(),uiPx(36));QCOMPARE(group->tabBar()->iconSize(),uiSize(18,18));
                QVERIFY(group->tabBar()->tabRect(0).width()<group->width()-uiPx(80));
            }
            QCOMPARE(editor->code->document(),document);QCOMPARE(editor->bytes(),text);
            w->setTerminalMaximized(true);settle();QVERIFY(!w->editorPanel->isVisible());
            w->setTerminalMaximized(false);settle();QVERIFY(w->editorPanel->isVisible());
            QJsonObject entry{{"zoom",qRound(UiZoom::instance().factor()*100)},{"dpr",window.devicePixelRatioF()},
                              {"topHeight",window.tabs->tabBar()->height()},{"rail",rail->width()},
                              {"footer",w->connectionStatus->parentWidget()->height()},{"dock",w->terminalPanel->height()},
                              {"sidebar",w->sidebar->width()},{"manager",w->terminalManager->width()}};
            metrics.append(entry);
            const auto pix=window.centralWidget()->grab();const auto image=pix.toImage();
            const QPoint seam=handle->mapTo(window.centralWidget(),handle->contentsRect().center());
            const qreal dpr=image.devicePixelRatio();
            
            // Away from the header intersections the entire shared edge remains painted.
            for(int y=handle->mapTo(window.centralWidget(),handle->contentsRect().topLeft()).y()+uiPx(20);y<handle->mapTo(window.centralWidget(),handle->contentsRect().bottomRight()).y()-uiPx(20);y+=uiPx(20))
                QCOMPARE(image.pixelColor(qRound(seam.x()*dpr),qRound(y*dpr)),colors().divider);
            QVERIFY(pix.save(report+QString("/geometry-%1-z%2-dpr%3.png").arg(light?"light":"dark").arg(entry["zoom"].toInt()).arg(window.devicePixelRatioF())));
        }
        w->showProcessPage();w->sidebar->setCurrentIndex(0);window.resize(800,480);settle();
        auto scroll=w->sidebar->currentWidget()->findChild<QScrollArea *>();QVERIFY(scroll);QVERIFY(scroll->verticalScrollBar()->maximum()>0);
        QCOMPARE(w->processes->columnCount(),6);QVERIFY(w->processes->horizontalScrollBar()->maximum()>0);
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());settle();
        QVERIFY(w->miniProcesses->mapTo(scroll->viewport(),QPoint(0,w->miniProcesses->height())).y()<=scroll->viewport()->height()+uiPx(15));
        QVERIFY(window.centralWidget()->grab().save(report+QString("/small-screen-%1-dpr%2.png").arg(light?"light":"dark").arg(window.devicePixelRatioF())));
        for(auto e:w->editors)e->discardDraft();
        QFile file(report+QString("/geometry-%1-dpr%2.json").arg(light?"light":"dark").arg(window.devicePixelRatioF()));QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(metrics).toJson());
    }
    void buttonFeedback_data() {
        QTest::addColumn<bool>("light");
        QTest::newRow("dracula") << false;
        QTest::newRow("github-light") << true;
    }
    void buttonFeedback() {
        QFETCH(bool, light);
        applyTheme(light);
        const QColor hover(light ? "#e2e5e9" : "#4b4f60");
        const QColor pressed(light ? "#cbd1d8" : "#606579");
        const QColor primaryHover(light ? "#005cc5" : "#cba8fa");
        const QColor primaryPressed(light ? "#044a9b" : "#a77ae7");
        const QStringList columns{"normal", "hover", "pressed", "released", "checked", "checked hover", "Tab focus", "Space", "mouse from key", "disabled"};
        const QStringList rows{"toolbar icon", "tool text", "text", "primary", "activity"};
        QImage evidence(1340, 380, QImage::Format_ARGB32_Premultiplied);
        evidence.fill(colors().background);
        QPainter painter(&evidence);
        painter.setPen(colors().text);
        for (int col = 0; col < columns.size(); ++col)
            painter.drawText(QRect(135 + col * 120, 5, 118, 30), Qt::AlignCenter, columns[col]);

        for (int row = 0; row < rows.size(); ++row) {
            QWidget host;
            host.resize(250, 100);
            host.setAutoFillBackground(true);
            auto palette = host.palette(); palette.setColor(QPalette::Window, colors().side); host.setPalette(palette);
            QLineEdit outside(&host); outside.setGeometry(140, 60, 90, 25);
            QAbstractButton *control = nullptr;
            if (row == 0 || row == 4)
                control = iconButton(Icon::NewFile, "New file", &host,
                                     row == 4 ? IconSize::Activity : IconSize::Toolbar);
            else if (row == 1) {
                auto tool = new QToolButton(&host); tool->setText("Tool"); control = tool;
            } else {
                auto push = button("Action", &host);
                if (row == 3) push->setProperty("primary", true);
                control = push;
            }
            control->move(20, 15);
            if (row >= 1 && row <= 3) control->resize(90, 32);
            control->setCheckable(true);
            host.show(); host.activateWindow(); outside.setFocus(); settle();
            const QRect geometry = control->geometry();
            QSignalSpy clicks(control, &QAbstractButton::clicked);
            painter.setPen(colors().text);
            painter.drawText(QRect(5, 42 + row * 65, 125, 50), Qt::AlignVCenter, rows[row]);
            auto capture = [&](int column) {
                settle();
                const auto image = control->grab().toImage();
                const QRect cell(135 + column * 120, 42 + row * 65, 118, 50);
                painter.drawImage(QPoint(cell.center().x() - image.width() / 2,
                                         cell.center().y() - image.height() / 2), image);
                return image;
            };
            auto moveOutside = [&] { QTest::mouseMove(host.windowHandle(), QPoint(235, 10)); settle(); };
            auto moveInside = [&] { QTest::mouseMove(host.windowHandle(), control->geometry().center()); settle(); };
            auto outlinePixels = [&](const QImage &image) {
                const int edge=qMax(1,qRound(image.devicePixelRatio()));
                const int inset=4*edge;
                const auto ink=row==3?colors().text:colors().accent;
                return pixels(image,QRect(inset,0,image.width()-2*inset,edge),ink)
                     + pixels(image,QRect(inset,image.height()-edge,image.width()-2*inset,edge),ink)
                     + pixels(image,QRect(image.width()-edge,inset,edge,image.height()-2*inset),ink);
            };
            moveOutside();
            const auto normal = capture(0);
            moveInside();
            const auto hovered = capture(1);
            QTest::mousePress(control, Qt::LeftButton, Qt::NoModifier, control->rect().center());
            const auto down = capture(2);
            QTest::mouseRelease(control, Qt::LeftButton, Qt::NoModifier, control->rect().center());
            QCOMPARE(clicks.size(), 1);
            QVERIFY(!control->property("keyboardFocus").toBool());
            if(control->focusPolicy() & Qt::ClickFocus) QVERIFY(control->hasFocus());
            QCOMPARE(outlinePixels(control->grab().toImage()),0);
            control->setChecked(false); settle(); // Keep the real mouse-acquired focus.
            const auto released = capture(3);
            QCOMPARE(control->geometry(), geometry);
            QVERIFY(pixels(hovered, hovered.rect(), row == 3 ? primaryHover : hover) > 10);
            QVERIFY(pixels(down, down.rect(), row == 3 ? primaryPressed : pressed) > 10);
            QVERIFY(normal != hovered);
            QVERIFY(hovered != down);
            QCOMPARE(released, hovered);
            moveOutside(); control->setChecked(true);
            const auto checked = capture(4);
            if(row==0 || row==3 || row==4) QVERIFY(pixels(checked, checked.rect(), colors().accent) > 0);
            QCOMPARE(outlinePixels(checked),0);
            if (row != 3)
                QVERIFY(pixels(checked, checked.rect(), colors().selection) > 10);
            moveInside();
            const auto checkedHover = capture(5);
            QVERIFY(pixels(checkedHover, checkedHover.rect(), row == 3 ? primaryHover : hover) > 10);
            if (row == 4) QVERIFY(pixels(checkedHover, checkedHover.rect(), colors().accent) > 0);
            QTest::mousePress(control, Qt::LeftButton, Qt::NoModifier, control->rect().center());
            const auto checkedDown = control->grab().toImage();
            QVERIFY(pixels(checkedDown, checkedDown.rect(), row == 3 ? primaryPressed : pressed) > 10);
            if (row == 4) QVERIFY(pixels(checkedDown, checkedDown.rect(), colors().accent) > 0);
            QTest::mouseRelease(control, Qt::LeftButton, Qt::NoModifier, control->rect().center());
            QCOMPARE(clicks.size(), 2);
            moveOutside(); control->setChecked(false); outside.setFocus(Qt::OtherFocusReason);
            QTest::keyClick(&outside,Qt::Key_Tab);settle();
            QVERIFY(control->hasFocus());
            QVERIFY(control->property("keyboardFocus").toBool());
            const auto focused = capture(6);
            QVERIFY(outlinePixels(focused)>0);
            QTest::keyClick(control, Qt::Key_Space);
            QCOMPARE(clicks.size(), 3);
            QVERIFY(control->property("keyboardFocus").toBool());capture(7);
            // A pointer click on a button already focused by Tab must remove
            // the outline while keeping its actual focus and click behavior.
            moveInside();QTest::mouseClick(control,Qt::LeftButton);QCOMPARE(clicks.size(),4);
            QVERIFY(control->hasFocus());QVERIFY(!control->property("keyboardFocus").toBool());
            const auto mouseFromKey=capture(8);QCOMPARE(outlinePixels(mouseFromKey),0);QCOMPARE(mouseFromKey,hovered);
            outside.setFocus(Qt::OtherFocusReason);QTest::keyClick(&outside,Qt::Key_Backtab);settle();
            QVERIFY(control->hasFocus());QVERIFY(control->property("keyboardFocus").toBool());
            QTest::mouseClick(control,Qt::LeftButton);QCOMPARE(clicks.size(),5);
            QVERIFY(!control->property("keyboardFocus").toBool());
            control->setShortcut(QKeySequence(Qt::ALT | Qt::Key_J));
            QTest::keyClick(&host,Qt::Key_J,Qt::AltModifier);
            QTRY_COMPARE(clicks.size(),6);QVERIFY(control->property("keyboardFocus").toBool());
            QVERIFY(control->hasFocus());
            control->setEnabled(false);
            const auto disabled = capture(9);
            QVERIFY(pixels(disabled, disabled.rect(), colors().side) > 10);
            moveInside();
            const auto disabledHover = control->grab().toImage();
            QVERIFY(pixels(disabledHover, disabledHover.rect(), colors().side) > 10);
            QTest::mouseClick(control, Qt::LeftButton);
            QCOMPARE(clicks.size(), 6);
            QCOMPARE(control->geometry(), geometry);
            if (row == 0) {
                QCOMPARE(control->toolTip(), QString("New file"));
                QCOMPARE(control->accessibleName(), QString("New file"));
                QCOMPARE(control->focusPolicy(), Qt::StrongFocus);
                QCOMPARE(control->size(), uiSize(28, 28));
                control->setEnabled(true);
                QHelpEvent help(QEvent::ToolTip, control->rect().center(),
                                control->mapToGlobal(control->rect().center()));
                QApplication::sendEvent(control, &help);
                QCOMPARE(QToolTip::text(), QString("New file"));
                QToolTip::hideText();
            }
        }
        painter.end();
        QVERIFY(evidence.save(report + (light ? "/button-feedback-light.png" : "/button-feedback-dark.png")));
    }
    void diagnosticSettingsPath(){
        auto backend=std::make_shared<FontMemorySecrets>();MainWindow window(true,backend);
        bool visited=false;QTimer driver;
        connect(&driver,&QTimer::timeout,&window,[&]{
            auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!dialog)return;
            auto path=dialog->findChild<QLabel *>("diagnosticLogPath");auto copy=dialog->findChild<QPushButton *>("copyDiagnosticLogPath");
            QVERIFY(path && copy);QCOMPARE(path->text(),QDir::toNativeSeparators(DiagnosticLog::directory()));
            QVERIFY(dialog->findChild<QPushButton *>("openDiagnosticLogs"));copy->click();
            QCOMPARE(QApplication::clipboard()->text(),path->text());visited=true;dialog->accept();
        });driver.start(10);
        QTimer::singleShot(3000,&window,[]{if(auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()))dialog->reject();});
        window.settings();driver.stop();QVERIFY(visited);QVERIFY(DiagnosticLog::directory().startsWith(data.path()));
        QCOMPARE(backend->reads,0);QCOMPARE(backend->writes,0);
    }
    void resetAllSettings_data(){QTest::addColumn<bool>("confirm");QTest::newRow("escape-cancel")<<false;QTest::newRow("confirmed")<<true;}
    void resetAllSettings(){
        QFETCH(bool,confirm);
        auto backend=std::make_shared<FontMemorySecrets>();MainWindow window(true,backend);
        window.resize(1200,780);window.show();settle();
        auto w=window.workspaces.begin().value();auto ports=w->portsPanel;
        TunnelRule rule;rule.profileId=w->profile.id;rule.localPort=18080;rule.remotePort=8080;
        QVERIFY(saveTunnelRules(rule.profileId,{rule}));
        // Separate panel loads this private persisted rule; no worker is started.
        auto panel=new PortsPanel(rule.profileId,false);window.tabs->addTab(panel,"端口草稿");window.tabs->setCurrentWidget(panel);
        panel->setRuleState(rule.id,"running");panel->beginEdit(rule.id);panel->remoteInput->setText("8081");panel->remoteInput->setSelection(1,2);
        auto draft=panel->draftRow;auto model=panel->table->model();auto current=panel->table->currentIndex();
        QSignalSpy config(panel,&PortsPanel::configurationChanged),start(panel,&PortsPanel::startRequested),stop(panel,&PortsPanel::stopRequested),restart(panel,&PortsPanel::restartRequested),modelResets(model,&QAbstractItemModel::modelReset);
        auto editor=w->editors.begin().value();editor->code->setPlainText("retained draft 中文");auto cursor=editor->code->textCursor();cursor.setPosition(2);cursor.setPosition(9,QTextCursor::KeepAnchor);editor->code->setTextCursor(cursor);
        auto document=editor->code->document();const auto selection=cursor.selectedText();
        auto term=w->terminals.begin().value().view;term->feed("reset retains PTY buffer");auto vterm=term->term;const auto text=logicalText(*term);
        const auto termCount=w->terminals.size();const auto docCount=w->editors.size();
        Job transfer;transfer.type="download";transfer.id=871;w->transfers[transfer.id]=transfer;w->activeTransfer=transfer.id;w->pausedTransfers.insert(transfer.id);
        w->preferredNic="eth1";w->nic->setCurrentIndex(w->nic->findText("eth1"));w->showHiddenFiles=false;
        auto folder=new QTreeWidgetItem(w->files,{"folder"});folder->setData(0,Qt::UserRole,QVariantMap{{"path","/folder"},{"directory",true}});folder->setExpanded(true);
        auto hidden=new QTreeWidgetItem(folder,{".hidden"});hidden->setData(0,Qt::UserRole,QVariantMap{{"name",".hidden"},{"path","/folder/.hidden"},{"directory",false}});w->filterFiles();QVERIFY(hidden->isHidden());
        ContentFonts::instance().setPreferences({contentFontFamily(),700,19,18,22});UiZoom::instance().setLevel(2);applyTheme(true);w->refreshTheme();settle();
        w->miniProcesses->widthSaveTimer.setInterval(10000);w->miniProcesses->setColumnWidth(0,w->miniProcesses->columnWidth(0)+uiPx(37));QVERIFY(w->miniProcesses->widthSaveTimer.isActive());
        w->servicesPanel->table->setColumnWidth(0,uiPx(350));
        QSettings settings;settings.setValue("connections/groups",QStringList{"protected-group"});settings.setValue("network/"+w->profile.id+"/interface","eth1");settings.setValue("files/show-hidden",false);settings.setValue("downloads/path","private-download-dir");settings.setValue("window/geometry",QByteArray("prior geometry"));
        settings.setValue("future/unrelated","retained");
        Profile p;p.id="protected-profile";p.group="protected-group";p.rememberSecret=true;window.repository.profiles={p};QVERIFY(saveProfiles({p}));
        QFile trust(dataPath()+"/known_hosts");QVERIFY(trust.open(QIODevice::WriteOnly));trust.write("private synthetic trust bytes\n");trust.close();
        QMap<QString,QByteArray> protectedFiles;
        for(auto path:{"connections.json","tunnels.json","known_hosts"}){QFile f(dataPath()+"/"+path);QVERIFY(f.open(QIODevice::ReadOnly));protectedFiles[path]=f.readAll();}
        QMap<QString,QVariant> before;for(auto key:settings.allKeys())before[key]=settings.value(key);
        bool visited=false,defaultCancel=false;QSignalSpy resets(&UiPreferences::instance(),&UiPreferences::defaultsRestored);
        QTimer answer;connect(&answer,&QTimer::timeout,&answer,[&]{
            auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!d||d->windowTitle()!="设置")return;answer.stop();visited=true;
            QTimer::singleShot(0,&window,[&]{auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(!box)return;defaultCancel=box->defaultButton()==box->button(QMessageBox::Cancel)&&box->escapeButton()==box->button(QMessageBox::Cancel);if(confirm)box->button(QMessageBox::Ok)->click();else QTest::keyClick(box,Qt::Key_Escape);});
            d->findChild<QPushButton *>("resetAllSettings")->click();settle();
            auto scroll=d->findChild<QScrollArea *>();scroll->verticalScrollBar()->setValue(0);settle();
            d->grab().save(report+(confirm?"/reset-settings-dark.png":"/reset-settings-light-cancelled.png"));
            d->accept();
        });answer.start(5);window.settings();settle();QVERIFY(visited);QVERIFY(defaultCancel);QCOMPARE(resets.size(),confirm?1:0);
        if(confirm){
            QCOMPARE(ContentFonts::instance().preferences(),ContentFontPreferences{contentFontFamily()});ContentFonts recreated;QCOMPARE(recreated.preferences(),ContentFonts::instance().preferences());
            QCOMPARE(UiZoom::instance().level(),0);QVERIFY(!lightTheme());const auto decorations=window.frameGeometry().size()-window.size();const auto expected=QSize(1440,900).boundedTo(window.screen()->availableGeometry().size()-decorations).expandedTo(window.minimumSize());QCOMPARE(window.size(),expected);
            QCOMPARE(editor->code->font().pixelSize(),14);QCOMPARE(term->font().pixelSize(),14);
            QVERIFY(w->showHiddenFiles);QVERIFY(!hidden->isHidden());QVERIFY(w->preferredNic.isEmpty());QCOMPARE(w->nic->currentIndex(),0);
            QVERIFY(!w->miniProcesses->widthSaveTimer.isActive());QCOMPARE(w->miniProcesses->baseWidths[0],102.);QVERIFY(!w->miniProcesses->customWidths);
            QCOMPARE(w->servicesPanel->table->columnWidth(0),230);
            QTest::qWait(300);for(auto group:{"process-table","services","network","files","downloads","window"}){settings.beginGroup(group);QVERIFY(settings.allKeys().isEmpty());settings.endGroup();}
            ProcessTable later(true);QVERIFY(!later.customWidths);QCOMPARE(later.baseWidths[0],102.);
        }else{QMap<QString,QVariant> after;for(auto key:settings.allKeys())after[key]=settings.value(key);QCOMPARE(after,before);QCOMPARE(UiZoom::instance().level(),2);QVERIFY(lightTheme());}
        QCOMPARE(editor->code->document(),document);QCOMPARE(editor->code->toPlainText(),QString("retained draft 中文"));QCOMPARE(editor->code->textCursor().selectedText(),selection);
        QCOMPARE(term->term,vterm);QCOMPARE(logicalText(*term),text);QCOMPARE(w->terminals.size(),termCount);QCOMPARE(w->editors.size(),docCount);
        QCOMPARE(w->activeTransfer,871);QVERIFY(w->transfers.contains(871));QVERIFY(w->pausedTransfers.contains(871));QVERIFY(folder->isExpanded());
        QVERIFY(panel->hasDraft());QCOMPARE(panel->draftRow,draft);QCOMPARE(panel->remoteInput->text(),QString("8081"));QCOMPARE(panel->remoteInput->selectedText(),QString("08"));QCOMPARE(panel->state(rule.id),QString("running"));QCOMPARE(panel->table->currentIndex(),current);
        QCOMPARE(config.size()+start.size()+stop.size()+restart.size()+modelResets.size(),0);
        QCOMPARE(settings.value("connections/groups").toStringList(),QStringList{"protected-group"});QCOMPARE(settings.value("future/unrelated").toString(),QString("retained"));QCOMPARE(window.repository.profiles[0].credentialKey(),p.credentialKey());
        QCOMPARE(backend->reads+backend->writes+backend->removes,0);
        for(auto it=protectedFiles.cbegin();it!=protectedFiles.cend();++it){QFile f(dataPath()+"/"+it.key());QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),it.value());}
        w->activeTransfer=0;w->transfers.clear();w->pausedTransfers.clear();Q_UNUSED(ports);
    }
    void fontPreferencesSettingsAndPersistence() {
        auto &fonts = ContentFonts::instance();
        QCOMPARE(fonts.preferences().weight,500);
        QCOMPARE(fonts.preferences().editorPx,14); QCOMPARE(fonts.preferences().terminalPx,14); QCOMPARE(fonts.preferences().listPx,14);
        FontSettings settings; settings.resize(680,700); settings.show(); settle();
        auto family=settings.findChild<QComboBox *>("contentFontFamily"); auto weight=settings.findChild<QComboBox *>("contentFontWeight");
        auto editor=settings.findChild<QSpinBox *>("editorFontPx"),terminal=settings.findChild<QSpinBox *>("terminalFontPx"),list=settings.findChild<QSpinBox *>("listFontPx");
        QVERIFY(family && weight && editor && terminal && list);
        for(int i=0;i<family->count();++i) QVERIFY(family->itemData(i).toString()==contentFontFamily() || QFontDatabase::isFixedPitch(family->itemData(i).toString()));
        QCOMPARE(editor->suffix(),QString(" px")); QCOMPARE(editor->minimum(),8); QCOMPARE(editor->maximum(),40);
        CodeEdit a,b; Terminal t; QDialog floating; auto layout=new QVBoxLayout(&floating); auto detached=new CodeEdit;layout->addWidget(detached);
        b.hide();floating.show();auto document=detached->document();detached->setPlainText("draft intact");
        if(family->count()>1)family->setCurrentIndex(1);
        weight->setCurrentIndex(weight->findData(600));editor->setValue(19);terminal->setValue(17);list->setValue(21);settle();
        QCOMPARE(a.font().pixelSize(),19);QCOMPARE(b.font().pixelSize(),19);QCOMPARE(detached->font().pixelSize(),19);QCOMPARE(t.font().pixelSize(),17);
        QCOMPARE(a.font().weight(),QFont::DemiBold);QCOMPARE(a.font().family(),family->currentData().toString());
        ContentFonts reconstructed;QCOMPARE(reconstructed.preferences(),fonts.preferences());
        QSettings disk;QCOMPARE(disk.value("appearance/font/editorPx").toInt(),19);
        CodeEdit later;QCOMPARE(later.font(),a.font());
        applyTheme(true);settle();QCOMPARE(detached->document(),document);QCOMPARE(detached->toPlainText(),QString("draft intact"));QCOMPARE(a.font().pixelSize(),19);
        QSignalSpy changes(&fonts,&ContentFonts::changed);fonts.adjustSize(ContentFontRole::Editor,1);
        QCOMPARE(changes.size(),1);QCOMPARE(qvariant_cast<ContentFontRole>(changes[0][0]),ContentFontRole::Editor);
        QCOMPARE(t.font().pixelSize(),17);QCOMPARE(list->value(),21);
        settings.findChild<QPushButton *>("resetContentFonts")->click();settle();
        QCOMPARE(fonts.preferences(),ContentFontPreferences{contentFontFamily()});QCOMPARE(a.font().weight(),QFont::Medium);QCOMPARE(a.font().pixelSize(),14);
        disk.setValue("appearance/font/family","not-an-installed-font");disk.setValue("appearance/font/weight",999);
        disk.setValue("appearance/font/editorPx","invalid");disk.setValue("appearance/font/terminalPx",400);disk.setValue("appearance/font/listPx",0);
        ContentFonts invalid;QCOMPARE(invalid.preferences().family,contentFontFamily());QCOMPARE(invalid.preferences().weight,500);
        QCOMPARE(invalid.preferences().editorPx,14);QCOMPARE(invalid.preferences().terminalPx,40);QCOMPARE(invalid.preferences().listPx,8);
        fonts.resetDefaults();
        QPointer<CodeEdit> removed=new CodeEdit;delete removed;QVERIFY(!removed);fonts.adjustSize(ContentFontRole::Editor,1);
    }
    void globalZoomAndOriginalInput() {
#ifdef Q_OS_MACOS
        QCOMPARE(physicalControlModifier,Qt::MetaModifier);
#else
        QCOMPARE(physicalControlModifier,Qt::ControlModifier);
#endif
        auto &fonts=ContentFonts::instance();auto pref=fonts.preferences();pref.editorPx=15;fonts.setPreferences(pref);
        auto &zoom=UiZoom::instance();
        QWidget host;host.setProperty("rabbitUi",true);auto layout=new QVBoxLayout(&host);CodeEdit code,other;Terminal terminal;QLineEdit outside;
        layout->addWidget(&code);layout->addWidget(&terminal);layout->addWidget(&outside);host.resize(900,650);host.show();host.activateWindow();settle();
        code.setPlainText(QString("line\n").repeated(150));auto document=code.document();code.setFocus();settle();
        QKeyEvent override(QEvent::ShortcutOverride,Qt::Key_Equal,physicalControlModifier);override.ignore();QApplication::sendEvent(&code,&override);QVERIFY(override.isAccepted());
        QTest::keyClick(&code,Qt::Key_Equal,physicalControlModifier);settle();QCOMPARE(zoom.level(),1);QCOMPARE(code.font().pixelSize(),18);QCOMPARE(other.font().pixelSize(),18);QCOMPARE(terminal.font().pixelSize(),17);
        QCOMPARE(fonts.preferences(),pref);QCOMPARE(fonts.size(ContentFontRole::Editor),15);QCOMPARE(QSettings().value("appearance/font/editorPx").toInt(),15);
        terminal.setFocus();settle();terminal.feed("remote unchanged");auto state=terminal.term;auto text=logicalText(terminal);QSignalSpy input(&terminal,&Terminal::input);
        QTest::keyClick(&terminal,Qt::Key_Plus,physicalControlModifier|Qt::ShiftModifier);settle();QCOMPARE(zoom.level(),2);QCOMPARE(input.size(),0);
        QTest::keyClick(&terminal,Qt::Key_Minus,physicalControlModifier|Qt::KeypadModifier);settle();QCOMPARE(zoom.level(),1);QCOMPARE(input.size(),0);
        QTest::keyClick(&terminal,Qt::Key_0,physicalControlModifier);settle();QCOMPARE(zoom.level(),0);QCOMPARE(input.size(),0);QCOMPARE(terminal.term,state);QCOMPARE(logicalText(terminal),text);
        QTest::keyClick(&terminal,Qt::Key_Up,physicalControlModifier);QTest::keyClick(&terminal,Qt::Key_Down,physicalControlModifier);
        QCOMPARE(input.size(),2);QCOMPARE(input[0][0].toByteArray(),QByteArray("\x1b[1;5A"));QCOMPARE(input[1][0].toByteArray(),QByteArray("\x1b[1;5B"));
        QTest::keyClick(&terminal,Qt::Key_Up);QTest::keyClick(&terminal,Qt::Key_Down);QCOMPARE(input[2][0].toByteArray(),QByteArray("\x1b[A"));QCOMPARE(input[3][0].toByteArray(),QByteArray("\x1b[B"));
        QTest::keyClick(&terminal,Qt::Key_Plus);QTest::keyClick(&terminal,Qt::Key_Minus);QCOMPARE(input[4][0].toByteArray(),QByteArray("+"));QCOMPARE(input[5][0].toByteArray(),QByteArray("-"));
        for(auto extra:{Qt::AltModifier,Qt::ShiftModifier}){QTest::keyClick(&terminal,Qt::Key_Minus,physicalControlModifier|extra);QCOMPARE(zoom.level(),0);}
        QKeyEvent unrelated(QEvent::ShortcutOverride,Qt::Key_Equal,physicalControlModifier|Qt::AltModifier);unrelated.ignore();QApplication::sendEvent(&terminal,&unrelated);QVERIFY(!unrelated.isAccepted());
        code.setFocus();settle();QTest::keyClick(&code,Qt::Key_Up,physicalControlModifier);QCOMPARE(fonts.preferences(),pref);
        wheel(code.viewport(),120);QCOMPARE(code.font().pixelSize(),15);QCOMPARE(zoom.level(),0);
        outside.setFocus();wheel(code.viewport(),120);QCOMPARE(code.font().pixelSize(),15);
        code.showFind();settle();QVERIFY(code.query->hasFocus());wheel(code.viewport(),120);QCOMPARE(code.font().pixelSize(),15);QCOMPARE(fonts.preferences(),pref);
        QTest::keyClick(code.query,Qt::Key_Equal,physicalControlModifier|Qt::ShiftModifier);settle();QCOMPARE(zoom.level(),1);
        QTest::keyClick(&outside,Qt::Key_0,physicalControlModifier);settle();QCOMPARE(zoom.level(),0);
        code.setFocus();code.verticalScrollBar()->setValue(code.verticalScrollBar()->maximum());const int scroll=code.verticalScrollBar()->value();wheel(code.viewport(),120,Qt::NoModifier);QVERIFY(code.verticalScrollBar()->value()<scroll);QCOMPARE(code.document(),document);
        QMenu menu;menu.addAction("Parentless menu");QTest::keyClick(&menu,Qt::Key_Plus,physicalControlModifier|Qt::KeypadModifier);QCOMPARE(zoom.level(),1);
        QWidget unrelatedWindow;QTest::keyClick(&unrelatedWindow,Qt::Key_Plus,physicalControlModifier);QCOMPARE(zoom.level(),1);
        QCOMPARE(fonts.preferences(),pref);
    }
    void zoomMetricsStyleIdentityAndPersistence() {
        auto &z=UiZoom::instance();auto original=z.installedStyle;QVERIFY(original);
        StyledComboBox combo;QWidget host;auto layout=new QVBoxLayout(&host);uiMargins(layout,12,8,12,8);uiSpacing(layout,6);
        auto icon=iconButton(Icon::Add,"zoom metric");layout->addWidget(icon);auto label=new QLabel("UI chrome");layout->addWidget(label);host.setProperty("rabbitUi",true);host.show();settle();
        UiZoomStyle raw;QMap<QStyle::PixelMetric,int> metrics;
        for(auto m:{QStyle::PM_SmallIconSize,QStyle::PM_ButtonIconSize,QStyle::PM_ExclusiveIndicatorWidth,QStyle::PM_ScrollBarExtent})metrics[m]=raw.pixelMetric(m);
        const auto hit=icon->size(),image=icon->iconSize();auto base=ContentFonts::instance().preferences();base.editorPx=15;ContentFonts::instance().setPreferences(base);
        CodeEdit editor;Terminal terminal;QDialog floating;auto fl=new QVBoxLayout(&floating);auto detached=new CodeEdit;fl->addWidget(detached);floating.show();editor.hide();settle();
        for(int level:{1,2,-4,4,0,1,2,0}){
            z.setLevel(level);settle();QCOMPARE(z.installedStyle,original);QCOMPARE(icon->size(),uiSize(hit.width(),hit.height()));QCOMPARE(icon->iconSize(),uiSize(image.width(),image.height()));QCOMPARE(layout->contentsMargins().left(),uiPx(12));QCOMPARE(layout->spacing(),uiPx(6));QCOMPARE(label->font().pixelSize(),uiPx(13));QCOMPARE(QToolTip::font().pixelSize(),uiPx(13));
            QCOMPARE(editor.font().pixelSize(),uiPx(15));QCOMPARE(detached->font().pixelSize(),uiPx(15));QCOMPARE(terminal.font().pixelSize(),uiPx(14));QCOMPARE(ContentFonts::instance().preferences(),base);
            for(auto m:metrics.keys()){QCOMPARE(raw.pixelMetric(m),uiPx(metrics[m]));QCOMPARE(combo.style()->pixelMetric(m),uiPx(metrics[m]));}
            QCOMPARE(uiPx(1),qMax(1,qRound(z.factor())));QCOMPARE(uiPx(0),0);
        }
        z.setLevel(2);UiZoom reconstructed;QCOMPARE(reconstructed.level(),2);QCOMPARE(QSettings().value("appearance/uiZoomLevel").toInt(),2);
        CodeEdit later;QCOMPARE(later.font().pixelSize(),uiPx(15));
        applyTheme(true);settle();QCOMPARE(z.installedStyle,original);QCOMPARE(editor.font().pixelSize(),uiPx(15));QCOMPARE(detached->font().pixelSize(),uiPx(15));
        z.setLevel(100);QCOMPARE(z.level(),4);z.setLevel(-100);QCOMPARE(z.level(),-4);z.setLevel(0);
        // Screen DPR never feeds back into saved base sizes or the application factor.
        QVERIFY(host.devicePixelRatioF()>=1.0);QCOMPARE(ContentFonts::instance().size(ContentFontRole::Editor),15);QCOMPARE(z.factor(),1.);
        if(qEnvironmentVariable("QT_SCALE_FACTOR")=="1.5")QCOMPARE(host.devicePixelRatioF(),1.5);
        QPointer<QWidget> removed=new QWidget;uiFixedWidth(removed,55);delete removed;z.setLevel(1);QVERIFY(!removed);
        QApplication::setStyle("Fusion");settle();QVERIFY(!original);applyTheme(false);QVERIFY(z.installedStyle);QVERIFY(z.installedStyle!=original);
    }
    void settingsWheelRequiresFocus() {
        QWidget window;auto layout=new QVBoxLayout(&window);auto outside=new QLineEdit;layout->addWidget(outside);
        auto scroll=new QScrollArea;scroll->setWidgetResizable(true);auto content=new QWidget;auto page=new QVBoxLayout(content);
        auto label=new QLabel("outside controls");page->addWidget(label);auto panel=new FontSettings;page->addWidget(panel);
        auto blank=new QWidget;blank->setMinimumHeight(300);page->addWidget(blank);scroll->setWidget(content);layout->addWidget(scroll);
        window.resize(700,370);window.show();window.activateWindow();outside->setFocus();settle();QVERIFY(scroll->verticalScrollBar()->maximum()>0);
        for(auto spin:panel->findChildren<QSpinBox *>()) {
            outside->setFocus();scroll->ensureWidgetVisible(spin);settle();
            const auto value=spin->value();const int before=scroll->verticalScrollBar()->value();
            wheel(spin,before>0?120:-120,Qt::NoModifier);QCOMPARE(spin->value(),value);QVERIFY(!spin->hasFocus());
            QVERIFY(scroll->verticalScrollBar()->value()!=before);
            // Target the spin shell as well as the real line editor below: an
            // ignored shell press must not bubble into a false outside click.
            scroll->ensureWidgetVisible(spin);QTest::mouseClick(spin,Qt::LeftButton,{},QPoint(12,spin->height()/2));settle();QVERIFY(spin->hasFocus());
            wheel(spin,-120,Qt::NoModifier);QCOMPARE(spin->value(),value-1);
            QTest::mouseClick(label,Qt::LeftButton);settle();QVERIFY(!spin->hasFocus());
            wheel(spin,120,Qt::NoModifier);QCOMPARE(spin->value(),value-1);
            spin->setFocus();settle();QTest::keyClick(spin,Qt::Key_Up);QCOMPARE(spin->value(),value);
            wheel(spin,120,Qt::NoModifier);QCOMPARE(spin->value(),value); // Keyboard focus alone is not wheel opt-in.
            QStyleOptionSpinBox option;option.initFrom(spin);option.rect=spin->rect();option.subControls=QStyle::SC_All;
            const auto up=spin->style()->subControlRect(QStyle::CC_SpinBox,&option,QStyle::SC_SpinBoxUp,spin);
            QTest::mouseClick(spin,Qt::LeftButton,{},up.center());QCOMPARE(spin->value(),value+1);
            QTest::mouseClick(blank,Qt::LeftButton);settle();QVERIFY(!spin->hasFocus());
            wheel(spin,-120,Qt::NoModifier);QCOMPARE(spin->value(),value+1);
        }
        // A real wheel delivered through the window can target the spin box's
        // internal editor; losing that editor's focus must also disarm it.
        auto spin=panel->findChild<QSpinBox *>("editorFontPx");auto field=spin->findChild<QLineEdit *>();QVERIFY(field);
        scroll->ensureWidgetVisible(spin);settle();QTest::mouseClick(field,Qt::LeftButton);settle();QVERIFY(spin->hasFocus());
        const int fieldValue=spin->value();
        auto fieldWheel=[&] { QTest::wheelEvent(window.windowHandle(),window.mapFromGlobal(field->mapToGlobal(field->rect().center())),QPoint(0,-120));settle(); };
        fieldWheel();QCOMPARE(spin->value(),fieldValue-1);
        outside->setFocus();settle();QVERIFY(!spin->hasFocus());spin->setFocus();settle();QVERIFY(spin->hasFocus());
        fieldWheel();QCOMPARE(spin->value(),fieldValue-1);
        auto zoom=panel->findChild<QComboBox *>("uiZoomLevel");QVERIFY(zoom);scroll->ensureWidgetVisible(zoom);settle();
        outside->setFocus();const int old=zoom->currentIndex();const int before=scroll->verticalScrollBar()->value();
        wheel(zoom,before>0?120:-120,Qt::NoModifier);QCOMPARE(zoom->currentIndex(),old);QVERIFY(!zoom->hasFocus());QVERIFY(scroll->verticalScrollBar()->value()!=before);
        zoom->setMaxVisibleItems(3);scroll->ensureWidgetVisible(zoom);QTest::mouseClick(zoom,Qt::LeftButton);settle();QVERIFY(zoom->view()->isVisible());
        auto bar=zoom->view()->verticalScrollBar();QVERIFY(bar->maximum()>0);bar->setValue(0);wheel(zoom->view()->viewport(),-120,Qt::NoModifier);QVERIFY(bar->value()>0);QVERIFY(zoom->view()->isVisible());zoom->hidePopup();
        settle();QVERIFY(zoom->hasFocus());wheel(zoom,-120,Qt::NoModifier);QVERIFY(zoom->currentIndex()!=old);
        QTest::mouseClick(label,Qt::LeftButton);settle();QVERIFY(!zoom->hasFocus());const int changed=zoom->currentIndex();wheel(zoom,120,Qt::NoModifier);QCOMPARE(zoom->currentIndex(),changed);
        zoom->setFocus();settle();QTest::keyClick(zoom,Qt::Key_Up);QVERIFY(zoom->currentIndex()!=changed);
        const int keyboard=zoom->currentIndex();wheel(zoom,120,Qt::NoModifier);QCOMPARE(zoom->currentIndex(),keyboard);
        for (auto name : {"contentFontFamily","contentFontWeight"}) {
            auto combo=panel->findChild<QComboBox *>(name);QVERIFY(combo);outside->setFocus();settle();
            const int index=combo->currentIndex();wheel(combo,-120,Qt::NoModifier);QCOMPARE(combo->currentIndex(),index);
        }
    }
    void settingsZoomControlsAndFloatingState(){
        auto &fonts=ContentFonts::instance();auto pref=fonts.preferences();pref.editorPx=15;fonts.setPreferences(pref);
        FontSettings settings;settings.resize(700,850);settings.show();settle();
        auto levels=settings.findChild<QComboBox *>("uiZoomLevel");auto plus=settings.findChild<QPushButton *>("uiZoomIn"),minus=settings.findChild<QPushButton *>("uiZoomOut"),reset=settings.findChild<QPushButton *>("uiZoomReset");
        QVERIFY(levels&&plus&&minus&&reset);QCOMPARE(levels->currentData().toInt(),0);
        QDialog floating;auto layout=new QVBoxLayout(&floating);auto code=new CodeEdit;layout->addWidget(code);code->setPlainText("selected draft intact");auto cursor=code->textCursor();cursor.setPosition(2);cursor.setPosition(10,QTextCursor::KeepAnchor);code->setTextCursor(cursor);auto document=code->document();floating.show();settle();
        plus->click();settle();QCOMPARE(UiZoom::instance().level(),1);QCOMPARE(code->font().pixelSize(),18);QCOMPARE(levels->currentText(),QString("120%"));
        levels->setCurrentIndex(levels->findData(2));settle();QCOMPARE(code->font().pixelSize(),22);QCOMPARE(code->textCursor().selectedText(),QString("lected d"));QCOMPARE(code->document(),document);QCOMPARE(fonts.preferences(),pref);
        // Changing/resetting app zoom never resets base font settings.
        reset->click();settle();QCOMPARE(UiZoom::instance().level(),0);QCOMPARE(code->font().pixelSize(),15);QCOMPARE(fonts.preferences(),pref);
        minus->click();QCOMPARE(UiZoom::instance().level(),-1);QCOMPARE(QToolTip::font().pixelSize(),uiPx(13));
        settings.findChild<QPushButton *>("resetContentFonts")->click();QCOMPARE(UiZoom::instance().level(),-1);
        reset->click();
        QVERIFY(settings.grab().save(report+"/zoom-settings-controls.png"));
    }
    void globalZoomListsAndBaseColumnWidths() {
        ServicesPanel services("offline",true);services.resize(880,400);services.show();settle();auto item=services.table->topLevelItem(0);services.table->setCurrentItem(item);
        services.table->setColumnWidth(0,271);settle();const auto before=QSettings().value("services/columns-v1");
        QSignalSpy resets(services.table->model(),&QAbstractItemModel::modelReset),requests(&services,&ServicesPanel::connectionRequested);
        ProcessTable process(false);process.resize(900,300);process.show();process.setColumnWidth(0,111);settle();
        auto &z=UiZoom::instance();for(int level:{1,2,0,2,0}){z.setLevel(level);settle();QCOMPARE(services.table->columnWidth(0),uiPx(271));QCOMPARE(process.columnWidth(0),uiPx(111));QCOMPARE(services.table->currentItem(),item);QCOMPARE(services.table->font().pixelSize(),uiPx(14));QVERIFY(services.table->visualItemRect(item).height()>=services.table->fontMetrics().height());QCOMPARE(process.horizontalHeader()->height(),uiPx(32));}
        QCOMPARE(QSettings().value("services/columns-v1"),before);QCOMPARE(resets.size(),0);QCOMPARE(requests.size(),0);QVERIFY(!services.timer.isActive());
        QTest::qWait(280);QCOMPARE(QSettings().value("process-table/full/widths-v1").toList()[0].toDouble(),111.);
        z.setLevel(2);ServicesPanel later("offline",true);QCOMPARE(later.table->columnWidth(0),uiPx(271));
    }
    void globalZoomEvidence_data(){ansiAndDefaultEnhancement_data();}
    void globalZoomEvidence(){
        QFETCH(bool,light);applyTheme(light);auto pref=ContentFonts::instance().preferences();pref.editorPx=15;ContentFonts::instance().setPreferences(pref);
        MainWindow window(true,std::make_shared<FontMemorySecrets>());window.resize(1500,1050);window.show();window.activateWindow();settle();
        auto workspace=qobject_cast<Workspace *>(window.tabs->currentWidget());QVERIFY(workspace);
        auto editor=workspace->findChild<Editor *>();QVERIFY(editor);auto document=editor->code->document();auto text=editor->bytes();
        auto rail=workspace->findChild<QFrame *>("rail");QVERIFY(rail);auto footer=workspace->connectionStatus->parentWidget();const QSize outer=window.size();
        QSignalSpy queries(workspace->servicesPanel,&ServicesPanel::connectionRequested);QSignalSpy changes(workspace->portsPanel,&PortsPanel::configurationChanged);
        for(int level:{0,1,2}){
            UiZoom::instance().setLevel(level);settle();QCOMPARE(ContentFonts::instance().size(ContentFontRole::Editor),15);QCOMPARE(ContentFonts::instance().size(ContentFontRole::Terminal),14);QCOMPARE(editor->code->font().pixelSize(),uiPx(15));QCOMPARE(rail->width(),uiPx(44));QCOMPARE(window.size(),outer);QVERIFY(footer->height()>=uiPx(24));
            const QString tag=QString("%1-%2").arg(light?"light":"dark").arg(qRound(UiZoom::instance().factor()*100));
            window.setWindowTitle("RabbitShell 0.1.18 · "+tag);QVERIFY(window.grab().save(report+"/zoom-workspace-"+tag+".png"));
            window.tabs->setCurrentIndex(0);settle();QVERIFY(window.grab().save(report+"/zoom-manager-"+tag+".png"));window.tabs->setCurrentWidget(workspace);settle();
        }
        QCOMPARE(editor->code->document(),document);QCOMPARE(editor->bytes(),text);QCOMPARE(queries.size(),0);QCOMPARE(changes.size(),0);
        UiZoom::instance().setLevel(0);settle();QCOMPARE(window.size(),outer);
    }
    void mediumWeightActuallyRendersHeavier() {
        auto &fonts=ContentFonts::instance();Terminal t;show(t);t.feed("Hamburgefontsiv 0123456789\x1b[?25l");
        auto pref=fonts.preferences();pref.weight=400;fonts.setPreferences(pref);settle();const auto regular=t.viewport()->grab().toImage();
        pref.weight=500;fonts.setPreferences(pref);settle();const auto medium=t.viewport()->grab().toImage();
        QVERIFY(medium!=regular);
        auto ink=[&](const QImage &im){qint64 amount=0;const int base=qGray(colors().background.rgb());for(int y=0;y<im.height();++y)for(int x=0;x<im.width();++x)amount+=qAbs(qGray(im.pixel(x,y))-base);return amount;};
        QVERIFY(ink(medium)>ink(regular));
        QVERIFY(regular.save(report+"/weight-400.png"));QVERIFY(medium.save(report+"/weight-500.png"));
        t.feed("\r\x1b[1mHamburgefontsiv 0123456789");settle();const auto bold=t.viewport()->grab().toImage();
        QVERIFY(bold!=medium);QVERIFY(ink(bold)>ink(medium));QVERIFY(bold.save(report+"/weight-sgr-700.png"));
        VTermScreenCellAttrs attributes{};QCOMPARE(t.cellFont(attributes).weight(),QFont::Medium);attributes.bold=1;QVERIFY(t.cellFont(attributes).weight()>=QFont::Bold);
    }
    void listFontsAndSettingsPreserveDraft_data() { ansiAndDefaultEnhancement_data(); }
    void listFontsAndSettingsPreserveDraft() {
        QFETCH(bool,light);applyTheme(light);
        const QString profileId=QUuid::createUuid().toString(QUuid::WithoutBraces);
        TunnelRule edited,running;edited.profileId=running.profileId=profileId;
        edited.localPort=18080;edited.remotePort=8080;running.localPort=15432;running.remotePort=5432;
        QVERIFY(saveTunnelRules(profileId,{edited,running}));
        MainWindow window(false,std::make_shared<FontMemorySecrets>());
        auto panel=new PortsPanel(profileId);window.tabs->addTab(panel,"字体隔离验证 · 端口");window.tabs->setCurrentWidget(panel);
        window.resize(1020,720);window.show();window.activateWindow();settle();
        panel->setRuleState(running.id,"running");panel->beginEdit(edited.id);
        panel->remoteInput->selectAll();QTest::keyClicks(panel->remoteInput,"8081");panel->remoteInput->setSelection(1,2);settle();
        const auto index=panel->table->model()->index(0,0);panel->table->setCurrentIndex(index);
        panel->remoteInput->setFocus();panel->remoteInput->setSelection(1,2);settle();QCOMPARE(panel->remoteInput->selectedText(),QString("08"));
        const auto current=panel->table->currentIndex();const auto draft=panel->draftRow;const auto before=loadTunnelRules(profileId);
        QSignalSpy configuration(panel,&PortsPanel::configurationChanged),start(panel,&PortsPanel::startRequested),stop(panel,&PortsPanel::stopRequested),restart(panel,&PortsPanel::restartRequested);
        QSignalSpy resets(panel->table->model(),&QAbstractItemModel::modelReset);
        auto bar=window.menuBar();auto settingsMenu=bar->actions().last()->menu();QVERIFY(settingsMenu);bool visited=false,preserved=false,savedScreenshot=false;
        QTimer answer;connect(&answer,&QTimer::timeout,&answer,[&]{
            auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!dialog || dialog->windowTitle()!="设置")return;
            answer.stop();visited=true;
            auto fonts=dialog->findChild<QWidget *>("fontSettings");auto spin=dialog->findChild<QSpinBox *>("listFontPx");
            preserved=dialog->property("preservePortDraft").toBool()&&fonts&&spin;
            auto scroll=dialog->findChild<QScrollArea *>();scroll->verticalScrollBar()->setValue(0);settle();
            savedScreenshot=dialog->grab().save(report+QString("/font-settings-default-%1.png").arg(light?"light":"dark"));
            if(spin){QTest::mouseClick(spin,Qt::LeftButton);spin->setValue(40);}
            UiZoom::instance().setLevel(2);settle();preserved &= spin && spin->value()==40 && panel->remoteInput->selectedText()=="08";UiZoom::instance().setLevel(0);settle();
            preserved &= panel->hasDraft() && panel->draftRow==draft && panel->remoteInput->text()=="8081" && panel->state(running.id)=="running";
            if(fonts){scroll->ensureWidgetVisible(fonts);settle();}
            savedScreenshot &= dialog->grab().save(report+QString("/font-settings-%1.png").arg(light?"light":"dark"));
            dialog->accept();
        });answer.start(10);
        QTest::mouseClick(bar,Qt::LeftButton,{},bar->actionGeometry(bar->actions().last()).center());settle();QVERIFY(settingsMenu->isVisible());
        QTest::mouseClick(settingsMenu,Qt::LeftButton,{},settingsMenu->actionGeometry(settingsMenu->actions().first()).center());settle();
        QVERIFY(visited);QVERIFY(preserved);QVERIFY(savedScreenshot);
        QVERIFY(panel->hasDraft());QCOMPARE(panel->draftRow,draft);QCOMPARE(panel->remoteInput->text(),QString("8081"));
        QCOMPARE(panel->remoteInput->selectedText(),QString("08"));QCOMPARE(panel->state(running.id),QString("running"));
        QCOMPARE(panel->table->currentIndex(),current);QCOMPARE(configuration.size(),0);QCOMPARE(start.size(),0);QCOMPARE(stop.size(),0);QCOMPARE(restart.size(),0);QCOMPARE(resets.size(),0);
        QCOMPARE(loadTunnelRules(profileId)[0].remotePort,before[0].remotePort);
        QCOMPARE(panel->table->font().pixelSize(),40);QCOMPARE(panel->localInput->font().pixelSize(),40);QCOMPARE(panel->remoteInput->font().pixelSize(),40);
        QCOMPARE(panel->table->horizontalHeader()->font().pixelSize(),40);
        const int line=QFontMetrics(panel->table->font()).height();
        QVERIFY(panel->table->rowHeight(1)>=2*line+14);QVERIFY(panel->localInput->height()>=line);
        QWidget narrow;auto split=new QVBoxLayout(&narrow);panel->setParent(&narrow);split->addWidget(panel);
        auto services=new ServicesPanel("仅离线字体预览",true);split->addWidget(services);
        narrow.resize(460,780);narrow.show();panel->show();settle();
        const auto service=services->table->topLevelItem(0);services->table->setCurrentItem(service);
        const auto identity=service->data(0,Qt::UserRole);const int count=services->table->topLevelItemCount();
        QSignalSpy connections(services,&ServicesPanel::connectionRequested),serviceResets(services->table->model(),&QAbstractItemModel::modelReset);
        auto pref=ContentFonts::instance().preferences();pref.listPx=39;ContentFonts::instance().setPreferences(pref);pref.listPx=40;ContentFonts::instance().setPreferences(pref);settle();
        QCOMPARE(services->table->font().pixelSize(),40);QCOMPARE(services->table->header()->font().pixelSize(),40);
        QVERIFY(services->table->visualItemRect(service).height()>=QFontMetrics(services->table->font()).height());
        QCOMPARE(services->table->currentItem(),service);QCOMPARE(service->data(0,Qt::UserRole),identity);QCOMPARE(services->table->topLevelItemCount(),count);
        QCOMPARE(connections.size(),0);QCOMPARE(serviceResets.size(),0);QVERIFY(!services->timer.isActive());
        QCOMPARE(services->search->font().pixelSize(),13);QCOMPARE(panel->draftRow->findChild<QPushButton *>("applyPortDraft")->font().pixelSize(),13);
        QVERIFY(panel->table->horizontalScrollBar()->maximum()>0);
        panel->table->horizontalScrollBar()->setValue(panel->table->horizontalScrollBar()->maximum());settle();
        QCOMPARE(panel->draftRow->x(),panel->table->columnViewportPosition(0));
        QVERIFY(services->table->horizontalScrollBar()->maximum()>0);
        QVERIFY(narrow.grab().save(report+QString("/font-lists-40px-%1.png").arg(light?"light":"dark")));
        // Settings closed: ordinary outside clicks retain the pre-existing commit behavior.
        QLineEdit outside;outside.show();QTest::mouseClick(&outside,Qt::LeftButton);settle();
        QVERIFY(!panel->hasDraft());QCOMPARE(configuration.size(),1);QCOMPARE(loadTunnelRules(profileId)[0].remotePort,quint16(8081));
    }
    void zoomLongLinePreservation_data() {
        QTest::addColumn<int>("boundary");
        QTest::newRow("wide-before-edge")<<-2;QTest::newRow("wide-on-edge")<<-1;QTest::newRow("wide-after-edge")<<0;
    }
    void zoomLongLinePreservation() {
        QFETCH(int,boundary);Terminal t;show(t);
        const auto large=ContentFonts::instance().font(ContentFontRole::Terminal);
        auto next=large;next.setPixelSize(40);const int newCols=qBound(10,(t.viewport()->width()-12)/qCeil(QFontMetricsF(next,t.viewport()).horizontalAdvance('M')),500);
        const int prefix=newCols+boundary;
        const QString original=QString(prefix,'x')+QString::fromUtf8("中é")+QString(t.cols-prefix-6,'y')+"  ";
        t.feed(original.toUtf8());QCOMPARE(logicalText(t),original);
        t.selectionStart={prefix-2,0};t.selectionEnd={prefix+2,0};t.selectionActive=true;
        const auto selected=t.selection();QCOMPARE(selected,QString::fromUtf8("xx中é"));
        QSignalSpy input(&t,&Terminal::input);
        auto pref=ContentFonts::instance().preferences();pref.terminalPx=40;ContentFonts::instance().setPreferences(pref);settle();
        QCOMPARE(logicalText(t),original);verifyWideCells(t);QVERIFY(t.selectionActive);QCOMPARE(t.selection(),selected);
        QCOMPARE(t.inputMethodQuery(Qt::ImCursorRectangle).toRect(),t.cursorRect());
        t.feed("Q");QCOMPARE(logicalText(t),original+"Q");
        pref.terminalPx=14;ContentFonts::instance().setPreferences(pref);settle();QCOMPARE(logicalText(t),original+"Q");
        QVERIFY(t.selectionActive);QCOMPARE(t.selection(),selected);verifyWideCells(t);
        t.feed("Z");QCOMPARE(logicalText(t),original+"QZ");QCOMPARE(input.size(),0);
    }
    void historyReflowPaddingAndCursor() {
        Terminal t;show(t);
        const auto next=[]{auto f=ContentFonts::instance().font(ContentFontRole::Terminal);f.setPixelSize(40);return f;}();
        const int narrowCols=(t.viewport()->width()-12)/qCeil(QFontMetricsF(next,t.viewport()).horizontalAdvance('M'));
        const QString longLine=QString(narrowCols-1,'h')+QString::fromUtf8("中é")+QString(40,'r')+"  ";
        t.feed(longLine.toUtf8());t.feed("\r\nshort\r\n");
        for(int n=0;n<t.rows;++n)t.feed("tail\r\n");
        QVERIFY(!t.history.empty());const auto before=logicalText(t);
        t.selectionStart={narrowCols-1,0};t.selectionEnd={narrowCols+1,0};t.selectionActive=true;
        auto pref=ContentFonts::instance().preferences();pref.terminalPx=40;ContentFonts::instance().setPreferences(pref);settle();
        QCOMPARE(logicalText(t),before);verifyWideCells(t);QVERIFY(t.selectionActive);
        auto selected=t.selection();selected.remove('\n');QCOMPARE(selected,QString::fromUtf8("中é"));
        // A short history row must fill the complete requested buffer with width=1,
        // default-colored cells, not zero-width cells that stall the native loop.
        t.pushHistory(1,QVector<VTermScreenCell>(1,[]{VTermScreenCell c{};c.chars[0]='p';c.width=1;vterm_color_rgb(&c.fg,1,2,3);vterm_color_rgb(&c.bg,4,5,6);return c;}()).constData());
        QVector<VTermScreenCell> popped(t.cols);QVERIFY(t.callbacks.sb_popline(t.cols,popped.data(),&t));
        QCOMPARE(popped[0].chars[0],uint32_t('p'));
        for(int i=1;i<popped.size();++i){QCOMPARE(popped[i].width,char(1));QVERIFY(VTERM_COLOR_IS_DEFAULT_FG(&popped[i].fg));QVERIFY(VTERM_COLOR_IS_DEFAULT_BG(&popped[i].bg));}
        // A too-wide history row remains owned by history on a small pop request.
        const auto historyCount=t.history.size();QCOMPARE(t.callbacks.sb_popline(1,popped.data(),&t),0);QCOMPARE(t.history.size(),historyCount);
        pref.terminalPx=14;ContentFonts::instance().setPreferences(pref);settle();QCOMPARE(logicalText(t),before);verifyWideCells(t);
        t.feed("cursor-tail");QCOMPARE(logicalText(t),before+"cursor-tail");
        // More rows pull history back into the screen; continued feed must follow
        // the original cursor rather than overwrite the backfilled history.
        t.resize(1000,800);settle();QCOMPARE(logicalText(t),before+"cursor-tail");t.feed("-ok");QCOMPARE(logicalText(t),before+"cursor-tail-ok");
    }
    void oversizedLogicalLineContinues() {
        Terminal t;show(t);t.resize(1000,120);settle();
        const QString original="prefix\r\n"+QString(t.cols-5,'X')+QString::fromUtf8("中é")+QString(t.cols-2,'Y');
        t.feed(original.toUtf8());const auto before=logicalText(t);
        auto pref=ContentFonts::instance().preferences();pref.terminalPx=40;ContentFonts::instance().setPreferences(pref);settle();
        QCOMPARE(t.rows,2);QCOMPARE(logicalText(t),before);verifyWideCells(t);
        t.feed("-after-small");QCOMPARE(logicalText(t),before+"-after-small");
        pref.terminalPx=14;ContentFonts::instance().setPreferences(pref);settle();
        QCOMPARE(logicalText(t),before+"-after-small");t.feed("-after-restore");QCOMPARE(logicalText(t),before+"-after-small-after-restore");
    }
    void alternateResizeAndHistoryLimit() {
        Terminal t;show(t);const QString primary=QString(t.cols-2,'P');t.feed(primary.toUtf8());
        const auto cols=t.cols;
        t.feed("\x1b[?1049h");t.feed(QByteArray(cols-2,'A'));QVERIFY(t.alternate);
        auto pref=ContentFonts::instance().preferences();pref.terminalPx=40;ContentFonts::instance().setPreferences(pref);settle();
        QVERIFY(t.alternate);QCOMPARE(logicalText(t),QString(t.cols,'A')); // fixed grid, no soft wrapping in alt
        for(const auto &line:t.history)for(const auto &cell:line)QVERIFY(cell.chars[0]!='A');
        t.feed("\x1b[?1049l");QCOMPARE(logicalText(t),primary);
        pref.terminalPx=14;ContentFonts::instance().setPreferences(pref);settle();QCOMPARE(logicalText(t),primary);t.feed("-primary");QCOMPARE(logicalText(t),primary+"-primary");
        // Bounded history, including eviction of an anchored endpoint.
        t.feed("\r\nanchor\r\n");
        for(int n=0;n<2100;++n)t.feed("row\r\n");
        QVERIFY(t.history.size()<=2000);QVERIFY(t.historyBytes()<=2*1024*1024);
        t.selectionStart={0,0};t.selectionEnd={1,0};t.selectionActive=true;
        // Wide history rows increase physical row count when narrowed, which
        // exercises the existing row cap and invalidates evicted endpoints.
        QVector<VTermScreenCell> full(t.cols);for(auto &c:full){c.width=1;c.chars[0]='z';}
        for(int n=0;n<100;++n)t.pushHistory(int(full.size()),full.data());
        pref.terminalPx=40;ContentFonts::instance().setPreferences(pref);settle();
        QVERIFY(!t.selectionActive);QVERIFY(t.history.size()<=2000);QVERIFY(t.historyBytes()<=2*1024*1024);verifyWideCells(t);
    }
    void fullRowPhantomAndInactivePrimary_data() {
        QTest::addColumn<bool>("alt");QTest::newRow("active-full-row")<<false;QTest::newRow("inactive-primary-overflow")<<true;
    }
    void fullRowPhantomAndInactivePrimary() {
        QFETCH(bool,alt);Terminal t;show(t);if(alt){t.resize(1000,120);settle();}
        const QString original(t.cols*(alt?2:1),'F');t.feed(original.toUtf8());QCOMPARE(logicalText(t),original);
        if(alt)t.feed("\x1b[?1049hALTERNATE");
        auto pref=ContentFonts::instance().preferences();pref.terminalPx=40;ContentFonts::instance().setPreferences(pref);settle();
        if(alt){for(const auto &line:t.history)for(const auto &c:line)QVERIFY(c.chars[0]!='A');t.feed("\x1b[?1049l");}
        QCOMPARE(logicalText(t),original);t.feed("-small");QCOMPARE(logicalText(t),original+"-small");
        pref.terminalPx=14;ContentFonts::instance().setPreferences(pref);settle();
        t.feed("-restored");QCOMPARE(logicalText(t),original+"-small-restored");
    }
    void editorLineSpacingGeometry() {
        CodeEdit code;code.resize(1000,430);code.show();settle();
        const QString text=QString::fromUtf8("const a = 1;\nconst b = 2;\n// 中文注释与标点\nconst title = \"账户信息\";\nconst combining = \"é\";\n\nreturn true;\n");
        code.setPlainText(text);settle();
        auto document=code.document();QVERIFY(qobject_cast<QPlainTextDocumentLayout *>(document->documentLayout()));
        QCOMPARE(code.font().pixelSize(),14);QCOMPARE(code.font().weight(),QFont::Medium);
        QPlainTextEdit natural;natural.setLineWrapMode(QPlainTextEdit::NoWrap);natural.setFont(code.font());natural.setPlainText(text);
        auto native=natural.document()->firstBlock();qreal previousBaseline=-1;QTextBlock previous;
        for(auto block=document->firstBlock();block.isValid();block=block.next(),native=native.next()) {
            const auto rect=code.blockBoundingRect(block);
            const qreal margin=block.next().isValid()?0:document->documentMargin();
            const auto naturalRect=natural.document()->documentLayout()->blockBoundingRect(native);
            QVERIFY(rect.height()-margin>=19);
            QVERIFY(rect.height()>=naturalRect.height()); // Never shorten taller fallback glyphs or the final margin.
            const auto line=block.layout()->lineAt(0);
            const qreal baseline=code.blockBoundingGeometry(block).top()+line.y()+line.ascent();
            if(previous.isValid()) {
                const qreal step=baseline-previousBaseline;
                QVERIFY2(qAbs(step-19)<=1,qPrintable(QString("block %1 baseline step %2").arg(block.blockNumber()).arg(step)));
                QCOMPARE(code.blockBoundingGeometry(block).top()-code.blockBoundingGeometry(previous).top(),code.blockBoundingRect(previous).height());
            }
            QTextCursor cursor(block);cursor.setPosition(block.position()+qMin(5,int(block.text().size())));
            const auto caret=code.cursorRect(cursor);
            QCOMPARE(code.cursorForPosition(QPoint(caret.left(),caret.center().y())).position(),cursor.position());
            previous=block;previousBaseline=baseline;
        }
        // Repeated geometry reads do not add padding to the already-positioned line.
        auto block=document->firstBlock();const auto rect=code.blockBoundingRect(block);const auto y=block.layout()->lineAt(0).y();
        for(int i=0;i<20;++i){QCOMPARE(code.blockBoundingRect(block),rect);QCOMPARE(block.layout()->lineAt(0).y(),y);}
        auto font=code.font();font.setPixelSize(20);code.setFont(font);settle();font.setPixelSize(14);code.setFont(font);settle();
        QCOMPARE(code.blockBoundingRect(block),rect);
        QCOMPARE(code.toPlainText(),text);QVERIFY(!document->isModified());QVERIFY(!document->isUndoAvailable());
    }
    void editorLineSpacingNavigationAndUndo() {
        CodeEdit code;code.resize(1000,330);code.show();code.activateWindow();code.setFocus();settle();
        QStringList lines;for(int i=0;i<2000;++i)lines<<QString("const row%1 = %1; // 定位").arg(i);
        const auto text=lines.join('\n');code.setPlainText(text);settle();
        auto document=code.document();
        int laidOut=0;for(auto block=document->firstBlock();block.isValid();block=block.next())laidOut+=block.layout()->lineCount()>0;
        QVERIFY2(laidOut<200,"Opening the visible region must retain Qt's lazy block layout.");
        code.verticalScrollBar()->setValue(400);settle();
        auto visible=code.firstVisibleBlock().next().next();QVERIFY(visible.isValid());
        QTextCursor hit(visible);hit.setPosition(visible.position()+6);const auto caret=code.cursorRect(hit);
        QVERIFY(code.viewport()->rect().contains(caret.center()));
        QTest::mouseClick(code.viewport(),Qt::LeftButton,{},QPoint(caret.left(),caret.center().y()));
        QCOMPARE(code.textCursor().position(),hit.position());
        const int before=code.textCursor().blockNumber();QTest::keyClick(&code,Qt::Key_PageDown);settle();
        const int after=code.textCursor().blockNumber();QVERIFY(after>before);QVERIFY(after-before<=code.viewport()->height()/19+1);
        QTest::keyClick(&code,Qt::Key_PageUp);settle();QVERIFY(qAbs(code.textCursor().blockNumber()-before)<=1);
        code.showFind();code.query->setText("row1530");QTRY_COMPARE_WITH_TIMEOUT(code.textCursor().selectedText(),QString("row1530"),5000);
        QCOMPARE(code.textCursor().blockNumber(),1530);QVERIFY(code.viewport()->rect().intersects(code.cursorRect()));
        code.findPanel->hide();code.setFocus();
        QTextCursor edit(document);edit.movePosition(QTextCursor::End);edit.insertText("\nconst draft = true;");
        QTextCursor selection(document->findBlockByNumber(1530));selection.movePosition(QTextCursor::Right,QTextCursor::KeepAnchor,12);code.setTextCursor(selection);
        const int position=selection.position(),anchor=selection.anchor();const QString selected=selection.selectedText(),draft=code.toPlainText();
        const int undoSteps=document->availableUndoSteps();
        for(int pixels:{20,14}) {
            auto pref=ContentFonts::instance().preferences();pref.editorPx=pixels;ContentFonts::instance().setPreferences(pref);settle();
            QCOMPARE(code.blockBoundingRect(document->findBlockByNumber(1530)).height(),qreal(qRound(pixels*19.0/14.0)));
            QCOMPARE(code.textCursor().position(),position);QCOMPARE(code.textCursor().anchor(),anchor);
        }
        for(int level:{2,0,2,0}) {
            UiZoom::instance().setLevel(level);settle();
            const auto block=document->findBlockByNumber(1530);const qreal height=code.blockBoundingRect(block).height();
            QCOMPARE(height,qreal(qRound(code.font().pixelSize()*19.0/14.0)));
            QCOMPARE(code.blockBoundingRect(block).height(),height);QCOMPARE(code.textCursor().selectedText(),selected);
        }
        QCOMPARE(code.document(),document);QCOMPARE(code.toPlainText(),draft);QVERIFY(document->isModified());
        QCOMPARE(document->availableUndoSteps(),undoSteps);
        code.undo();QCOMPARE(code.toPlainText(),text);code.redo();QCOMPARE(code.toPlainText(),draft);
    }
    void editorLineSpacingEvidence() {
        const QString directory=qEnvironmentVariable("RABBITSHELL_LINE_SPACING_REPORT_DIR",report+"/line-spacing-preview");QVERIFY(QDir().mkpath(directory));
        const QList<QPair<QString,QString>> examples{
            {"ascii",QString::fromUtf8("const menuConfigs = [{\n    uncheckText: commitFAQ.title,\n    key: '',\n    index: 0\n}, {\n    uncheckText: serviceFAQ.title,\n    key: '',\n    index: 1\n}];")},
            {"chinese",QString::fromUtf8("// 配置菜单，保留账户信息与中文说明\nmenuConfigs.push({\n    uncheckText: '账户信息',\n    key: '',\n    index: 2,\n    customData: {\n        faqIndex: undefined,\n        badge: BadgeTags.isCPPLCommitPanelChecked\n    }\n});\n\nconst status = '已就绪 · é';")}};
        QJsonObject metrics{{"font",contentFontFamily()},{"pixelSize",14},{"weight",500},{"targetLineHeight",19},{"qt",QT_VERSION_STR}};
        for(const auto &[name,text]:examples) {
            QWidget window;auto layout=new QVBoxLayout(&window);auto label=new QLabel("CodeEdit · Cascadia Code NF · 14 px · 500 · Dracula · "+name);layout->addWidget(label);
            auto code=new CodeEdit;layout->addWidget(code);code->setPlainText(text);code->setReadOnly(true);
            window.resize(1040,390);window.show();settle();
            QJsonArray baselines,steps,heights; qreal previous=-1;
            for(auto block=code->document()->firstBlock();block.isValid();block=block.next()) {
                const auto rect=code->blockBoundingRect(block);const auto line=block.layout()->lineAt(0);
                const qreal baseline=code->blockBoundingGeometry(block).top()+line.y()+line.ascent();
                baselines.append(baseline);heights.append(rect.height());if(previous>=0)steps.append(baseline-previous);previous=baseline;
            }
            metrics[name]=QJsonObject{{"baselines",baselines},{"steps",steps},{"blockHeights",heights},{"dpr",code->devicePixelRatioF()}};
            QVERIFY(window.grab().save(directory+"/implemented-"+name+".png"));
        }
        QFile file(directory+"/implemented-metrics.json");QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(metrics).toJson());
    }
    void metricsAndThemeIdentity() {
        Terminal t;
        show(t);
        QCOMPARE(QFontInfo(t.font()).family(), QString("Cascadia Code NF"));
        QCOMPARE(t.font().pixelSize(), 14);
        QVERIFY(!(t.font().styleStrategy() & QFont::NoFontMerging));
        t.feed(QString::fromUtf8("A中éZ").toUtf8());
        QCOMPARE(t.cursor.col, 5);
        auto cells = row(t, 0);
        QCOMPARE(cells[1].width, char(2));
        QCOMPARE(cells[3].chars[0], uint32_t('e'));
        QCOMPARE(cells[3].chars[1], uint32_t(0x301));
        t.feed("\x1b[1;3H"); // continuation cell of 中
        QCOMPARE(t.cursorRect(), QRect(6 + t.cellW, 4, 2 * t.cellW, t.cellH));
        const auto text = t.text().trimmed();
        auto font = t.font(); font.setPixelSize(20); t.setFont(font); settle();
        QCOMPARE(t.cellW, qCeil(QFontMetricsF(t.font(), t.viewport()).horizontalAdvance('M')));
        QCOMPARE(t.cellH, qCeil(QFontMetricsF(t.font(), t.viewport()).height()) + 2);
        QCOMPARE(t.inputMethodQuery(Qt::ImCursorRectangle).toRect(), t.cursorRect());
        QCOMPARE(t.text().trimmed(), text);
        QEvent dpi(QEvent::DevicePixelRatioChange); QApplication::sendEvent(&t, &dpi); settle();
        QCOMPARE(t.point(QPoint(6 + 3 * t.cellW + 1, 5)), QPoint(3, 0));
        t.selectionStart = {1, 0}; t.selectionEnd = {3, 0}; t.selectionActive = true;
        QCOMPARE(t.selection(), QString::fromUtf8("中é"));
        QSignalSpy input(&t, &Terminal::input);
        auto term = t.term;
        applyTheme(true); t.refreshTheme(); settle();
        QCOMPARE(t.term, term); QCOMPARE(t.text().trimmed(), text); QCOMPARE(t.font().pixelSize(), 20);
        QCOMPARE(t.selection(), QString::fromUtf8("中é"));
        QInputMethodEvent commit; commit.setCommitString(QString::fromUtf8("中文é"));
        QApplication::sendEvent(&t, &commit);
        QByteArray sent;
        for (const auto &event : input) sent += event[0].toByteArray();
        QCOMPARE(sent, QString::fromUtf8("中文é").toUtf8());
        QCOMPARE(t.text().trimmed(), text); // protocol input has no local echo

        Editor editor("font-test", "/isolated/font.js", "const n = 42;\r\n", "hash");
        editor.resize(800, 300); editor.show(); settle();
        auto code = editor.code;
        const auto document = code->document();
        const int gutter = code->gutterWidth();
        code->setFont(font); settle();
        QCOMPARE(code->tabStopDistance(), QFontMetricsF(code->font(), code->viewport()).horizontalAdvance(' ') * 4);
        QVERIFY(code->gutterWidth() > gutter);
        QCOMPARE(code->lineNumbers->width(), code->gutterWidth());
        code->showFind(); settle();
        QCOMPARE(QFontInfo(code->query->font()).family(), QFontInfo(qApp->font()).family());
        QCOMPARE(code->query->font().pixelSize(), 13);
        QVERIFY(QFontInfo(code->query->font()).family() != contentFontFamily());
        applyTheme(false); code->refreshTheme(); settle();
        QCOMPARE(code->document(), document);
        QCOMPARE(code->font().pixelSize(), 20);
        QCOMPARE(editor.bytes(), QByteArray("const n = 42;\r\n"));
        QJsonObject info{{"family", QFontInfo(t.font()).family()}, {"default_pixel_size", contentFont().pixelSize()},
                         {"tested_pixel_size", t.font().pixelSize()}, {"device_pixel_ratio", t.devicePixelRatioF()},
                         {"cell_width", t.cellW}, {"cell_height", t.cellH},
                         {"chinese_fallback", QFontMetrics(t.font()).inFont(QChar(u'中'))}};
        QFile out(report + "/font-metrics.json"); QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(info).toJson());
    }
    void ansiAndDefaultEnhancement_data() {
        QTest::addColumn<bool>("light");
        QTest::newRow("dracula") << false; QTest::newRow("github-light") << true;
    }
    void ansiAndDefaultEnhancement() {
        QFETCH(bool, light); applyTheme(light);
        Terminal t; show(t); QSignalSpy input(&t, &Terminal::input);
        t.feed(QString::fromUtf8("dev@host:~/中文$ echo ok\r\n[INFO] WARN ERROR FATAL XINFO INFO_\r\n").toUtf8());
        QCOMPARE(t.rowHighlights(row(t, 0)).size(), 4);
        QCOMPARE(t.rowHighlights(row(t, 1)).size(), 4);
        t.feed("\x1b[31mERROR\x1b[0m \x1b[44mWARN\x1b[0m \x1b[7mINFO\x1b[0m \x1b[1mFATAL\x1b[0m\r\n");
        QVERIFY(t.rowHighlights(row(t, 2)).isEmpty());
        t.feed("\x1b[38;5;196mERROR\x1b[0m \x1b[38;2;12;34;56mWARN\x1b[0m\r\n");
        QCOMPARE(ink(t, 3), QColor(255, 0, 0)); QCOMPARE(ink(t, 3, 6), QColor(12, 34, 56));
        QVERIFY(t.rowHighlights(row(t, 3)).isEmpty());
        const auto palette = terminalAnsiPalette();
        QCOMPARE(palette[3], QColor(light ? "#dbab09" : "#f1fa8c"));
        QCOMPARE(palette[4], QColor(light ? "#0366d6" : "#bd93f9"));
        QVERIFY(palette[1] != palette[9]);
        for (int i = 0; i < 16; ++i)
            t.feed("\x1b[" + QByteArray::number(i < 8 ? 30 + i : 90 + i - 8) + "mX");
        t.feed("\x1b[0m\x1b[?25l");
        for (int i = 0; i < 16; ++i) QCOMPARE(ink(t, 4, i), palette[i]);
        const QString before = t.text();
        auto image = t.viewport()->grab().toImage();
        // Exact solid glyph pixels prove the production painter uses the enhancement.
        const qreal dpr = image.devicePixelRatio();
        const QRect user(qRound(6 * dpr), qRound(4 * dpr), qRound(3 * t.cellW * dpr), qRound(t.cellH * dpr));
        QVERIFY(pixels(image, user, colors().green) > 0);
        t.selectionStart = {0, 0}; t.selectionEnd = {2, 0}; t.selectionActive = true;
        image = t.viewport()->grab().toImage();
        QCOMPARE(pixels(image, user, colors().green), 0); // selection uses original text color
        t.clearSelection();
        t.feed("\x1b[?1049hdev@host:~/app$ INFO WARN ERROR\x1b[?25l");
        QVERIFY(t.alternate); QVERIFY(t.rowHighlights(row(t, 0)).isEmpty());
        t.feed("\x1b[?1049l");
        QCOMPARE(t.text(), before);
        QCOMPARE(input.size(), 0); // coloring and rendering never emit remote input
    }
    void themeSwitchWithActiveSgrScreenAndHistory() {
        Terminal t; show(t);
        const QByteArray fixture = "plain\r\n\x1b[31;44mansi\r\n\x1b[38;5;196mindexed\r\n\x1b[38;2;12;34;56mtrue\r\n\x1b[0m";
        t.feed(fixture);
        for (int i = 0; i < t.rows + 2; ++i) t.feed("padding\r\n");
        const int start = int(t.history.size()) + t.cursor.row;
        t.feed(fixture);
        t.feed("\x1b[31;44m"); // active explicit pen when changing default theme
        const auto before = t.text();
        applyTheme(true); t.refreshTheme(); settle();
        QCOMPARE(t.text(), before);
        const auto palette = terminalAnsiPalette();
        for (int r : {0, start}) {
            VTermScreenCell sc{}; QVERIFY(t.cell(r, 0, &sc));
            QVERIFY(VTERM_COLOR_IS_DEFAULT_FG(&sc.fg)); QVERIFY(VTERM_COLOR_IS_DEFAULT_BG(&sc.bg));
            QCOMPARE(ink(t, r), colors().text); QCOMPARE(ink(t, r, 0, true), colors().background);
            QCOMPARE(ink(t, r + 1), palette[1]); QCOMPARE(ink(t, r + 1, 0, true), palette[4]);
            QCOMPARE(ink(t, r + 2), QColor(255, 0, 0)); QCOMPARE(ink(t, r + 3), QColor(12, 34, 56));
        }
        t.feed("new-explicit\r\n\x1b[0mnew-default");
        const int end = int(t.history.size()) + t.cursor.row;
        QCOMPARE(ink(t, end - 1), palette[1]); QCOMPARE(ink(t, end - 1, 0, true), palette[4]);
        QCOMPARE(ink(t, end), colors().text); QCOMPARE(ink(t, end, 0, true), colors().background);
        applyTheme(false); t.refreshTheme();
        QCOMPARE(ink(t, 0), colors().text); QCOMPARE(ink(t, end), colors().text);
        QCOMPARE(ink(t, end - 1), terminalAnsiPalette()[1]);
    }
    void syntaxPriorityAndSave_data() { ansiAndDefaultEnhancement_data(); }
    void syntaxPriorityAndSave() {
        QFETCH(bool, light); applyTheme(light);
        Editor e("syntax-test", "/isolated/test.js", sampleCode().toUtf8(), "hash");
        e.resize(1000, 500); e.show(); settle();
        auto &code = *e.code;
        code.refreshTheme(); settle();
        const auto c = colors();
        QCOMPARE(syntax(code, 0, "https"), c.string); QCOMPARE(syntax(code, 0, "return"), c.comment);
        QCOMPARE(syntax(code, 1, "# literal"), c.string); QCOMPARE(syntax(code, 1, "# tail"), c.comment);
        QCOMPARE(syntax(code, 2, "const"), c.comment); QCOMPARE(syntax(code, 3, "call"), c.comment);
        QCOMPARE(syntax(code, 3, "42"), c.syntaxNumber);
        QCOMPARE(syntax(code, 4, "//"), c.string); QCOMPARE(syntax(code, 5, "#"), c.string);
        QCOMPARE(syntax(code, 6, "greet"), c.syntaxFunction); QCOMPARE(syntax(code, 6, "return"), c.keyword);
        QCOMPARE(syntax(code, 6, "true"), c.syntaxNumber);
        QCOMPARE(syntax(code, 7, "prop"), c.syntaxProperty); QCOMPARE(syntax(code, 7, "value"), c.string);
        QCOMPARE(e.bytes(), sampleCode().toUtf8()); QVERIFY(!e.dirty());
        const auto document = code.document();
        code.moveCursor(QTextCursor::End); code.insertPlainText("// edited\n");
        const auto sent = e.bytes();
        QVERIFY(e.dirty()); applyTheme(!light); code.refreshTheme(); settle();
        QCOMPARE(code.document(), document); QCOMPARE(e.bytes(), sent); QVERIFY(e.dirty());
        code.undo(); QCOMPARE(e.bytes(), sampleCode().toUtf8()); code.redo(); QCOMPARE(e.bytes(), sent);
        QSignalSpy save(&e, &Editor::saveRequested);
        for (auto button : e.findChildren<QToolButton *>()) if (button->toolTip().startsWith("保存")) button->click();
        QCOMPARE(save.size(), 1); e.saved(sent, "saved-hash"); QVERIFY(!e.dirty());
    }
    void appearanceEvidence_data() { ansiAndDefaultEnhancement_data(); }
    void appearanceEvidence() {
        QFETCH(bool, light); applyTheme(light);
        QWidget window;
        auto layout = new QVBoxLayout(&window); layout->setContentsMargins(12, 8, 12, 8);
        auto label = new QLabel(QString("RabbitShell 0.1.18 · %1 · 终端 / 文件编辑 · Medium 500").arg(light ? "GitHub Light" : "Dracula"));
        layout->addWidget(label);
        auto split = new QSplitter(Qt::Vertical); layout->addWidget(split, 1);
        auto editor = new Editor("appearance-preview", "/preview/theme.js", sampleCode().toUtf8(), "hash");
        auto terminal = new Terminal;
        split->addWidget(editor); split->addWidget(terminal); split->setSizes({430, 330});
        window.resize(1180, 850); window.show(); settle();
        terminal->feed(QString::fromUtf8("demo@rabbit:~/project$ echo 字体与配色预览\r\n"
                       "[INFO] Ready  [WARN] Retry  [ERROR] Failed  [FATAL] Exit\r\n"
                       "普通文本保留原始内容 · 中文双格 · é ·   · -> != ===\r\n").toUtf8());
        for (int i = 0; i < 16; ++i)
            terminal->feed("\x1b[" + QByteArray::number(i < 8 ? 30 + i : 90 + i - 8) + "m " + QByteArray::number(i) + " ANSI ");
        terminal->feed("\x1b[0m\r\n\x1b[31mERROR remote red\x1b[0m | \x1b[44mWARN remote background\x1b[0m | \x1b[38;5;196m256-color\x1b[0m | \x1b[38;2;12;160;140mTrueColor\x1b[0m\r\n");
        terminal->feed("demo@rabbit:~/project$ \x1b[?25l");
        editor->code->refreshTheme(); settle();
        const auto suffix = light ? "light" : "dark";
        QVERIFY(window.grab().save(report + "/appearance-" + suffix + ".png"));
        QVERIFY(terminal->viewport()->grab().save(report + "/terminal-content-" + suffix + ".png"));
        QVERIFY(editor->grab().save(report + "/editor-content-" + suffix + ".png"));
        QCOMPARE(editor->code->font().pixelSize(), 14);
        QCOMPARE(terminal->font().pixelSize(), 14);
        QVERIFY(QFontInfo(label->font()).family() != contentFontFamily());
    }
};
} // namespace rs
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    rs::AppearanceTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_appearance.moc"
