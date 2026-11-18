#include "../src/ui/mainwindow.h"
#include "../src/ui/theme.h"
#include "../src/ui/filecontextmenu.h"
#include "../src/ui/fileclipboard.h"
#include "../src/core/operationgate.h"
#include <QtTest>
namespace rs {
class MenuSecrets final : public SecretBackend {
  public:
    SecretResult read(const QString &) override { return {}; }
    QString write(const QString &, const QByteArray &) override { return {}; }
    QString remove(const QString &) override { return {}; }
};
class MenuTests : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    std::unique_ptr<QTemporaryDir> data;
    QString report;
    void settle() { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete); }
    Profile profile(QString id="menu-fixture") { Profile p; p.id=id; p.name="合成连接"; p.host="127.0.0.1"; p.user="fixture"; return p; }
    QVariantMap entry(QString path,bool dir=false) { return {{"name",path.section('/',-1)},{"path",path},{"directory",dir},{"size",42}}; }
    void setup(Workspace &w) {
        w.demo=false; w.online=true; w.directory="/root"; w.files->clear(); w.updateBreadcrumbs();
        w.session=new Session(w.profile,{},&w); w.attachSession(w.session); // Never started, no network.
        w.resize(1180,820); w.show(); w.activateWindow();settle();jobs(w);
    }
    QList<Job> jobs(Workspace &w) {
        QList<Job> out; while(!w.session->jobs.empty()){out<<w.session->jobs.front();w.session->jobs.pop_front();}w.session->queuedJobBytes=0;return out;
    }
    void noFileRequests(Workspace &w) {
        // Deferred terminal layout can legitimately synchronise PTY geometry.
        // Opening/closing these menus must not submit any file or terminal command.
        for(const auto &job:jobs(w)){
            QCOMPARE(job.type,QString("resize"));QVERIFY(w.terminals.contains(job.id));
            QVERIFY(job.columns>0 && job.rows>0);QVERIFY(job.path.isEmpty());
        }
    }
    QTreeWidgetItem *row(Workspace &w,const QString &path,bool dir=false) {
        auto item=new QTreeWidgetItem(w.files,{path.section('/',-1)});item->setData(0,Qt::UserRole,entry(path,dir));return item;
    }
    QStringList capture(const std::function<void()> &show,const QString &screenshot={},const std::function<void(QMenu*)> &inspect={}) {
        QStringList result; bool seen=false; QTimer driver;driver.setInterval(5);
        connect(&driver,&QTimer::timeout,this,[&]{
            auto menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)return;
            driver.stop();seen=true;
            // Qt keeps a collapsed separator's QAction visible, but gives it
            // no geometry. Compare the actual displayed menu, including this.
            for(auto a:menu->actions())if(a->isVisible() && (!a->isSeparator() || !menu->actionGeometry(a).isEmpty()))
                result<< (a->isSeparator()?"|":a->text());
            QVERIFY(!result.isEmpty());QVERIFY(result.first()!="|");QVERIFY(result.last()!="|");
            for(int i=1;i<result.size();++i)QVERIFY(result[i]!="|"||result[i-1]!="|");
            auto close=qScopeGuard([menu]{menu->close();});
            if(!screenshot.isEmpty())QVERIFY(menu->grab().save(report+"/"+screenshot+".png"));
            if(inspect)inspect(menu);
        });
        QTimer watchdog;watchdog.setSingleShot(true);connect(&watchdog,&QTimer::timeout,this,[]{
            if(auto menu=qobject_cast<QMenu*>(QApplication::activePopupWidget()))menu->close();
            if(auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();
        });watchdog.start(5000);driver.start();show();driver.stop();if(!seen)QTest::qFail("Production QMenu was not observed",__FILE__,__LINE__);return result;
    }
    static QAction *action(QMenu *m,const QString &text) { for(auto a:m->actions())if(a->text()==text)return a;return nullptr; }
  private slots:
    void applicationMenu_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void applicationMenu(){
        QFETCH(bool,light);applyTheme(light);MainWindow window(true,std::make_shared<MenuSecrets>());window.resize(1440,900);window.show();window.activateWindow();settle();
        auto bar=window.menuBar();QCOMPARE(bar->actions().size(),7);QCOMPARE(bar->height(),uiPx(28));
        QCOMPARE(bar->actions().last()->text(),QString("帮助"));
        QVERIFY(!window.tabs->cornerWidget(Qt::TopRightCorner));QVERIFY(!window.findChild<QToolButton *>("lockApplication"));
        auto w=qobject_cast<Workspace *>(window.tabs->currentWidget());QVERIFY(w);QVERIFY(!w->editors.isEmpty());auto editor=w->editors.constBegin().value().data();QVERIFY(editor);w->editArea->activatePage(editor);editor->code->setFocus();editor->code->selectAll();settle();
        auto edit=bar->actions()[2]->menu();auto copy=edit->actions()[4];QCOMPARE(copy->text(),QString("复制\tCtrl+C"));
        auto open=[&](int index){QTest::mouseClick(bar,Qt::LeftButton,{},bar->actionGeometry(bar->actions()[index]).center());settle();};
        open(2);QVERIFY(edit->isVisible());QVERIFY(copy->isEnabled());auto undo=edit->actions()[0];QVERIFY(undo->shortcut().isEmpty());
        // Native menu-bar hover switching retains the pre-menu editor context.
        const auto hover=bar->actionGeometry(bar->actions()[3]).center();
        // Offscreen has no native popup mouse grab to forward this move to the
        // menu bar. Deliver the same mapped event to Qt's actual menu-bar path.
        QMouseEvent move(QEvent::MouseMove,hover,bar->mapToGlobal(hover),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(bar,&move);settle();auto view=bar->actions()[3]->menu();QTRY_VERIFY(view->isVisible());QTest::keyClick(view,Qt::Key_Escape);settle();QVERIFY(!view->isVisible());
        auto term=w->terminals[w->focusedTerm].view;term->setFocus();settle();QSignalSpy input(term,&Terminal::input);open(2);QVERIFY(!copy->isEnabled());copy->trigger();QCOMPARE(input.count(),0);edit->hide();
        // A context saved before a lock/unlock cycle or server switch expires.
        editor->code->setFocus();editor->code->moveCursor(QTextCursor::End);editor->code->insertPlainText("x");settle();const auto text=editor->code->toPlainText();open(2);QVERIFY(undo->isEnabled());OperationGate::lockAgain();OperationGate::setLocked(false);undo->trigger();QCOMPARE(editor->code->toPlainText(),text);edit->hide();
        open(2);window.tabs->setCurrentIndex(0);undo->trigger();QCOMPARE(editor->code->toPlainText(),text);edit->hide();
        auto file=bar->actions()[1]->menu();open(1);for(auto a:file->actions())if(a->property("workspaceCommand").isValid())QVERIFY(!a->isEnabled());file->hide();
        window.tabs->setCurrentWidget(w);window.resize(800,600);UiZoom::instance().setLevel(2);settle();QVERIFY(bar->width()<=window.width());open(3);QVERIFY(view->isVisible());QTest::keyClick(view,Qt::Key_Down);QTest::keyClick(view,Qt::Key_Escape);UiZoom::instance().setLevel(0);window.resize(1440,900);settle();open(3);
        QVERIFY(window.grab().save(report+QString("/application-menu-window-%1.png").arg(light?"light":"dark")));QVERIFY(view->grab().save(report+QString("/application-menu-popup-%1.png").arg(light?"light":"dark")));view->hide();
        for(auto e:w->editors)e->discardDraft();
    }
    void initTestCase() {
        QVERIFY(root.isValid());QStandardPaths::setTestModeEnabled(true);QCoreApplication::setOrganizationName("RabbitShellMenuTests");QCoreApplication::setApplicationName("Isolated");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication::setStyle("Fusion");
        for(auto f:{"C:/Windows/Fonts/msyh.ttc","C:/Windows/Fonts/consola.ttf"})if(QFileInfo::exists(f))QFontDatabase::addApplicationFont(f);
        QApplication::setFont(QFont("Microsoft YaHei UI",10));report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",root.path());
    }
    void init() { data=std::make_unique<QTemporaryDir>(root.filePath("case-XXXXXX"));QVERIFY(data->isValid());qputenv("RABBITSHELL_DATA_DIR",data->path().toUtf8());QSettings().clear();QApplication::clipboard()->clear();applyTheme(false); }
    void fileScopes_data() {
        QTest::addColumn<QString>("scope");for(auto s:{"file","directory","multi","blank","newline","draft","invalid"})QTest::newRow(s)<<QString(s);
    }
    void fileScopes() {
        QFETCH(QString,scope);Workspace w(profile(),{},true);setup(w);auto folder=row(w,"/root/folder",true),file=row(w,"/root/file.txt");settle();
        w.files->setCurrentItem(file);file->setSelected(true);QPoint point;
        if(scope=="blank")point=QPoint(20,w.files->viewport()->height()-8);
        else if(scope=="directory"){point=w.files->visualItemRect(folder).center();applyTheme(true);}
        else if(scope=="multi"){folder->setSelected(true);point=w.files->visualItemRect(file).center();}
        else if(scope=="newline"){file->setData(0,Qt::UserRole,entry("/root/legal\nname"));point=w.files->visualItemRect(file).center();}
        else {point=w.files->visualItemRect(file).center();if(scope=="draft")file->setData(0,FileDraftRole,true);if(scope=="invalid")file->setData(0,Qt::UserRole,QVariantMap{});}
        if(scope=="draft"||scope=="invalid") { bool appeared=false;QTimer tick;connect(&tick,&QTimer::timeout,this,[&]{if(auto m=qobject_cast<QMenu*>(QApplication::activePopupWidget())){appeared=true;m->close();}});tick.start(5);w.fileMenu(point);QVERIFY(!appeared);return; }
        auto menu=capture([&]{emit w.files->customContextMenuRequested(point);},scope=="directory"?"file-directory-light":scope=="file"?"file-dark":"",[&](QMenu *m){
            if(auto rename=action(m,"重命名\tF2")){QVERIFY(rename->shortcut().isEmpty());QVERIFY(!action(m,"重命名  F2"));}
        });
        const QString view="刷新";
        const QString clipboard="剪切\tCtrl+X;复制\tCtrl+C";
        const QString create="新建文件;新建文件夹";
        QString expected;
        if(scope=="blank")expected=create+";上传文件…;|;撤销上次文件操作\tCtrl+Z;|;添加到目录收藏;|;"+view;
        else if(scope=="directory")expected="在此处打开终端;添加到目录收藏;|;下载目录文件;上传文件…;|;"+create+";|;"+clipboard+";重命名\tF2;|;"+view+";|;删除…;|;属性 / 权限";
        else if(scope=="multi")expected="下载所选项;上传文件…;|;"+create+";|;"+clipboard+";|;"+view+";|;删除…";
        else expected="下载;上传文件…;|;"+create+";|;"+clipboard+";重命名\tF2;|;"+view+";|;删除…;|;属性 / 权限";
        QCOMPARE(menu,expected.split(';'));for(const auto &job:jobs(w))QCOMPARE(job.type,QString("resize"));
        if(scope=="blank")QVERIFY(file->isSelected());
    }
    void fileCallbacksKeepCapturedTarget() {
        Workspace w(profile(),{},true);setup(w);auto a=row(w,"/root/A",true),b=row(w,"/root/B",true);settle();
        const auto point=w.files->visualItemRect(a).center();
        capture([&]{w.fileMenu(point);},{},[&](QMenu *menu){w.files->clearSelection();b->setSelected(true);action(menu,"属性 / 权限")->trigger();});
        auto tasks=jobs(w);QCOMPARE(tasks.size(),1);QCOMPARE(tasks.first().type,QString("stat"));QCOMPARE(tasks.first().path,QString("/root/A"));
        QTimer rename;rename.setInterval(5);connect(&rename,&QTimer::timeout,this,[]{if(auto d=qobject_cast<QInputDialog*>(QApplication::activeModalWidget())){d->setTextValue("renamed");d->accept();}});rename.start();
        capture([&]{w.fileMenu(point);},{},[&](QMenu *menu){w.files->clearSelection();b->setSelected(true);action(menu,"重命名\tF2")->trigger();});rename.stop();
        tasks=jobs(w);QCOMPARE(tasks.size(),1);QCOMPARE(tasks.first().type,QString("rename"));QCOMPARE(tasks.first().path,QString("/root/A"));QCOMPARE(tasks.first().destination,QString("/root/renamed"));
        capture([&]{w.fileMenu(QPoint(10,w.files->viewport()->height()-8));},{},[&](QMenu *menu){action(menu,"新建文件")->trigger();});
        QCOMPARE(w.entryDraft.parentPath,QString("/root"));w.cancelEntryDraft();
        a->setData(0,Qt::UserRole,entry("/root/directory\nname",true));
        capture([&]{w.fileMenu(point);},{},[&](QMenu *menu){QVERIFY(!action(menu,"添加到目录收藏")->isEnabled());QVERIFY(action(menu,"下载目录文件")->isEnabled());QVERIFY(action(menu,"删除…")->isEnabled());});
    }
    void compactResourceMenu_data() {
        QTest::addColumn<bool>("light");QTest::addColumn<QString>("scope");QTest::addColumn<int>("zoom");
        for(bool light:{false,true})for(const auto &scope:{QString("file"),QString("directory"),QString("multi"),QString("blank")})
            QTest::newRow(qPrintable(QString("%1-%2").arg(light?"light":"dark",scope)))<<light<<scope<<0;
        QTest::newRow("dark-zoom144")<<false<<QString("file")<<2;
        QTest::newRow("light-zoom144")<<true<<QString("file")<<2;
    }
    void compactResourceMenu() {
        QFETCH(bool,light);QFETCH(QString,scope);QFETCH(int,zoom);
        UiZoom::instance().setLevel(zoom);auto reset=qScopeGuard([]{UiZoom::instance().setLevel(0);});applyTheme(light);
        Workspace w(profile(),{},true);setup(w);auto folder=row(w,"/root/folder",true),file=row(w,"/root/file.txt");settle();
        w.files->setCurrentItem(file);file->setSelected(true);w.files->setFocus();settle();QVERIFY(w.files->hasFocus());
        QPoint point=w.files->visualItemRect(file).center();
        if(scope=="directory")point=w.files->visualItemRect(folder).center();
        else if(scope=="multi")folder->setSelected(true);
        else if(scope=="blank")point=QPoint(20,w.files->viewport()->height()-8);
        capture([&]{w.fileMenu(point);},{},[&](QMenu *base){
            auto menu=dynamic_cast<FileContextMenu *>(base);QVERIFY(menu);QVERIFY(!action(menu,"打开文件"));
            QCOMPARE(menu->style()->pixelMetric(QStyle::PM_SmallIconSize,nullptr,menu),uiPx(18));
            auto accessible=QAccessible::queryAccessibleInterface(menu);QVERIFY(accessible);QCOMPARE(accessible->role(),QAccessible::PopupMenu);
            if(scope=="blank") {QVERIFY(!action(menu,"剪切\tCtrl+X"));QVERIFY(!action(menu,"复制\tCtrl+C"));}
            for(auto a:menu->actions())if(a->isVisible()&&!a->isSeparator()){
                QVERIFY2(!a->icon().isNull(),qPrintable(a->text()));QVERIFY(a->shortcut().isEmpty());
                QCOMPARE(menu->actionGeometry(a).height(),uiPx(29));
                QVERIFY(!qobject_cast<QWidgetAction *>(a));
            }
            QVERIFY(!action(menu,"显示隐藏文件"));QVERIFY(!action(menu,"显示修改时间列"));
            QVERIFY(!action(menu,"粘贴\tCtrl+V")->isVisible());
            menu->setActiveAction(action(menu,"上传文件…"));settle();
            const auto on=menu->grab().toImage();const auto rect=menu->actionGeometry(action(menu,"刷新"));
            const qreal dpr=on.devicePixelRatio();
            if(!zoom) {
                QCOMPARE(menu->width(),248);QCOMPARE(rect.left(),5);QCOMPARE(rect.right(),242);
                QCOMPARE(menu->iconRect(action(menu,"刷新")).left(),16);
                const auto selected=menu->actionGeometry(action(menu,"上传文件…"));
                auto pixel=[&](QPoint p){return on.pixelColor(qRound(p.x()*dpr),qRound(p.y()*dpr));};
                const QColor bg(light?"#ffffff":"#21222c"),hover(light?"#eaeef2":"#44475a"),border(light?"#8c959f":"#555c73");
                QCOMPARE(pixel(QPoint(2,selected.center().y())).rgb(),bg.rgb()); // The outside 4px padding stays clear.
                QCOMPARE(pixel(QPoint(selected.right()-4,selected.center().y())).rgb(),hover.rgb());
                QVERIFY(pixel(selected.topLeft()).rgb()!=hover.rgb()); // 2px hover corner is rounded inside the action.
                QCOMPARE(pixel(QPoint(menu->width()/2,0)).rgb(),border.rgb());
            }
            const auto name=QString("/resource-menu-%1-%2-z%3.png").arg(light?"light":"dark",scope).arg(zoom);
            QVERIFY(menu->grab().save(report+name));
            QTest::keyClick(menu,Qt::Key_Home);QVERIFY(menu->activeAction());QVERIFY(menu->activeAction()->isEnabled());
            auto first=menu->activeAction();QTest::keyClick(menu,Qt::Key_Down);QVERIFY(menu->activeAction()!=first);
            QTest::keyClick(menu,Qt::Key_Escape);
        });
        settle();QVERIFY(w.files->hasFocus());
        noFileRequests(w);
    }
    void fileDoubleClickStillOpens() {
        Workspace w(profile(),{},true);setup(w);auto file=row(w,"/root/activate.txt");settle();
        auto point=w.files->visualItemRect(file).center();
        QTest::mouseClick(w.files->viewport(),Qt::LeftButton,{},point);QTest::mouseDClick(w.files->viewport(),Qt::LeftButton,{},point);settle();
        auto requests=jobs(w);QCOMPARE(requests.size(),1);QCOMPARE(requests[0].type,QString("read"));QCOMPARE(requests[0].path,QString("/root/activate.txt"));
    }
    void filePasteAvailabilityTracksClipboard() {
        Workspace w(profile(),{},true);setup(w);w.authenticatedFingerprint="SHA256:private";
        auto folder=row(w,"/root/B",true);settle();
        auto clipboard=QApplication::clipboard();clipboard->setText("only text");
        auto cleanup=qScopeGuard([&]{clipboard->clear();});
        QFile file(data->filePath("local.txt"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("fixture");file.close();
        const auto localDir=data->filePath("local-dir");QVERIFY(QDir().mkpath(localDir));
        auto urls=[&](QList<QUrl> values){auto mime=new QMimeData;mime->setUrls(values);clipboard->setMimeData(mime);settle();};
        capture([&]{w.fileMenu(w.files->visualItemRect(folder).center());},{},[&](QMenu *menu){
            auto paste=action(menu,"粘贴\tCtrl+V");QVERIFY(paste);QVERIFY(!paste->isVisible());
            QVERIFY2(!paste->isEnabled(),"Text clipboard must not enable file paste");QVERIFY(!paste->isVisible());
            urls({QUrl::fromLocalFile(file.fileName())});QVERIFY(paste->isEnabled());QVERIFY(paste->isVisible());QVERIFY(menu->grab().save(report+"/resource-menu-paste-visible.png"));
            urls({QUrl::fromLocalFile(localDir)});QVERIFY(paste->isEnabled());QVERIFY(paste->isVisible());
            urls({QUrl("https://example.invalid/file")});QVERIFY(!paste->isEnabled());QVERIFY(!paste->isVisible());
            urls({QUrl::fromLocalFile(data->filePath("missing"))});QVERIFY(!paste->isEnabled());QVERIFY(!paste->isVisible());
            QList<QUrl> tooMany;for(int i=0;i<257;++i)tooMany<<QUrl::fromLocalFile(file.fileName());urls(tooMany);QVERIFY(!paste->isEnabled());QVERIFY(!paste->isVisible());
            auto info=entry("/root/A/cut.txt");info["permissions"]=0100644;
            w.copyFileSelection(true,{info});settle();QVERIFY(paste->isEnabled());QVERIFY(paste->isVisible());
            auto cut=FileClipboard::instance().current();QVERIFY(cut);
            ++w.credentialGeneration;FileClipboard::instance().offer(cut);settle();QVERIFY(!paste->isEnabled());QVERIFY(!paste->isVisible());--w.credentialGeneration;
            w.online=false;FileClipboard::instance().offer(cut);settle();QVERIFY(!paste->isEnabled());QVERIFY(!paste->isVisible());w.online=true;
            Workspace other(profile("other-server"),{},true);other.demo=false;other.online=true;
            other.session=new Session(other.profile,{},&other);other.authenticatedFingerprint="SHA256:other";
            other.copyFileSelection(true,{info});settle();QVERIFY(!paste->isEnabled());QVERIFY(!paste->isVisible());
            QString error;auto preparing=FileClipboard::instance().create(FileClipboardBatch::Kind::Copy,&error);QVERIFY2(preparing,qPrintable(error));
            FileClipboard::instance().offer(preparing);settle();QVERIFY(!paste->isEnabled());QVERIFY(!paste->isVisible());
            preparing->failed=true;FileClipboard::instance().offer(preparing);settle();QVERIFY(!paste->isEnabled());QVERIFY(!paste->isVisible());
            auto fake=new QMimeData;fake->setData(FileClipboard::mimeType(),QUuid::createUuid().toByteArray(QUuid::WithoutBraces));fake->setUrls({QUrl::fromLocalFile(file.fileName())});clipboard->setMimeData(fake);settle();QVERIFY(!paste->isEnabled());QVERIFY(!paste->isVisible());
            clipboard->setText("replaced while menu open");settle();QVERIFY(!paste->isEnabled());QVERIFY(!paste->isVisible());
        });
        QCOMPARE(jobs(w).size(),0);QVERIFY(w.transferQueue.isEmpty());
    }
    void resourceMenuToggleUsesQtInput() {
        Workspace w(profile(),{},true);setup(w);auto file=row(w,"/root/file.txt"),hiddenFile=row(w,"/root/.secret");
        w.showHiddenFiles=false;w.files->setColumnHidden(2,true);w.filterFiles();settle();QVERIFY(hiddenFile->isHidden());
        Q_UNUSED(file);auto more=w.findChild<QToolButton *>("resourceMore");QVERIFY(more);
        w.files->header()->setSectionResizeMode(2,QHeaderView::Interactive);w.files->setColumnWidth(2,137);
        capture([&]{QTest::mouseClick(more,Qt::LeftButton);},{},[&](QMenu *base){
            auto menu=dynamic_cast<FileContextMenu *>(base);QVERIFY(menu);auto hidden=action(menu,"显示隐藏文件");QVERIFY(!hidden->isChecked());
            QTest::mouseClick(menu,Qt::LeftButton,{},menu->checkRect(hidden).center());
        });
        QVERIFY(w.showHiddenFiles);QVERIFY(QSettings().value("files/show-hidden").toBool());QVERIFY(!hiddenFile->isHidden());
        capture([&]{QTest::mouseClick(more,Qt::LeftButton);},{},[&](QMenu *menu){
            auto modified=action(menu,"显示修改时间列");QVERIFY(!modified->isChecked());menu->setActiveAction(modified);
            QTest::keyClick(menu,Qt::Key_Return);
        });
        QVERIFY(!w.files->isColumnHidden(2));QCOMPARE(w.files->columnWidth(2),137);
        capture([&]{QTest::mouseClick(more,Qt::LeftButton);},{},[&](QMenu *menu){
            QVERIFY(action(menu,"显示隐藏文件")->isChecked());QVERIFY(action(menu,"显示修改时间列")->isChecked());
            action(menu,"显示修改时间列")->trigger();
        });
        QVERIFY(w.files->isColumnHidden(2));
        capture([&]{QTest::mouseClick(more,Qt::LeftButton);},{},[&](QMenu *menu){action(menu,"显示修改时间列")->trigger();});
        QCOMPARE(w.files->columnWidth(2),137);QCOMPARE(jobs(w).size(),0);
    }
    void resourceViewMenuStyle_data() {
        QTest::addColumn<bool>("light");QTest::addColumn<int>("zoom");
        for(bool light:{false,true})for(int zoom:{0,2})QTest::newRow(qPrintable(QString("%1-z%2").arg(light?"light":"dark").arg(zoom)))<<light<<zoom;
    }
    void resourceViewMenuStyle() {
        QFETCH(bool,light);QFETCH(int,zoom);UiZoom::instance().setLevel(zoom);auto reset=qScopeGuard([]{UiZoom::instance().setLevel(0);});applyTheme(light);
        Workspace w(profile(),{},true);setup(w);auto more=w.findChild<QToolButton *>("resourceMore");QVERIFY(more);QVERIFY(!more->icon().isNull());
        QCOMPARE(more->accessibleName(),QString("更多"));
        auto checkFocus=[&](bool keyboard) {
            // Offscreen has no window manager to reactivate the owner after
            // the native popup closes. Restore only activation; Qt must still
            // restore the actual button focus and its input-mode styling.
            if(QGuiApplication::platformName()=="offscreen")QApplication::setActiveWindow(&w);
            settle();
            auto focus=QApplication::focusWidget();
            QVERIFY2(more->hasFocus(),qPrintable(QString("keyboard=%1 active=%2 focus=%3/%4 visible=%5 geometry=%6,%7,%8,%9").arg(keyboard).arg(w.isActiveWindow()).arg(focus?focus->metaObject()->className():"none").arg(focus?focus->objectName():QString()).arg(more->isVisible()).arg(more->x()).arg(more->y()).arg(more->width()).arg(more->height())));QCOMPARE(more->property("keyboardFocus").toBool(),keyboard);
            const auto image=more->grab().toImage();const int edge=qMax(1,qRound(image.devicePixelRatio())),inset=4*edge;
            int n=0;for(int y=0;y<image.height();++y)for(int x=inset;x<image.width();++x)
                if((y<edge || y>=image.height()-edge || (x>=image.width()-edge && y>=inset && y<image.height()-inset)) && image.pixelColor(x,y)==colors().accent)++n;
            if(keyboard)QVERIFY(n>0);else QCOMPARE(n,0);
        };
        auto toolbarEvidence=[&](const QString &mode) {
            auto parent=more->parentWidget();const QRect strip(0,more->y()-uiPx(3),parent->width(),more->height()+uiPx(6));
            QVERIFY(parent->grab(strip.intersected(parent->rect())).save(report+QString("/resource-toolbar-%1-%2-z%3.png").arg(mode,light?"light":"dark").arg(zoom)));
        };
        for(auto button:more->parentWidget()->findChildren<QToolButton *>(QString(),Qt::FindDirectChildrenOnly))if(button!=more)QVERIFY(button->x()<more->x());
        capture([&]{QTest::mouseClick(more,Qt::LeftButton);},{},[&](QMenu *base){
            auto menu=dynamic_cast<FileContextMenu *>(base);QVERIFY(menu);QCOMPARE(menu->actions().size(),2);
            auto hidden=action(menu,"显示隐藏文件"),modified=action(menu,"显示修改时间列");QVERIFY(hidden&&modified);QVERIFY(hidden->isCheckable());QVERIFY(modified->isCheckable());
            hidden->setChecked(false);modified->setChecked(false);settle();const auto off=menu->grab().toImage();
            hidden->setChecked(true);modified->setChecked(true);settle();const auto on=menu->grab().toImage();
            const auto rect=menu->actionGeometry(hidden),mark=menu->checkRect(hidden);QVERIFY(mark.left()>rect.center().x());
            QStyleOptionMenuItem option;menu->initStyleOption(&option,hidden);QVERIFY(hidden->isChecked());QVERIFY(!option.checked);QCOMPARE(option.checkType,QStyleOptionMenuItem::NotCheckable);QVERIFY(!option.icon.isNull());
            const auto dpr=on.devicePixelRatio();auto physical=[&](QRect area){return QRect(qRound(area.x()*dpr),qRound(area.y()*dpr),qRound(area.width()*dpr),qRound(area.height()*dpr));};
            QCOMPARE(off.copy(physical(QRect(rect.left(),rect.top(),uiPx(34),rect.height()))),on.copy(physical(QRect(rect.left(),rect.top(),uiPx(34),rect.height()))));
            QVERIFY(off.copy(physical(mark))!=on.copy(physical(mark)));
            QVERIFY(menu->grab().save(report+QString("/resource-more-%1-z%2.png").arg(light?"light":"dark").arg(zoom)));
            QVERIFY(w.grab().save(report+QString("/resource-toolbar-%1-z%2.png").arg(light?"light":"dark").arg(zoom)));
            QTest::keyClick(menu,Qt::Key_Escape);
        });
        settle();checkFocus(false);toolbarEvidence("mouse-escape");
        // Traverse the actual focus chain, then open/close the actual popup.
        QTest::keyClick(more,Qt::Key_Backtab);settle();auto previous=QApplication::focusWidget();QVERIFY(previous && previous!=more);
        QTest::keyClick(previous,Qt::Key_Tab);settle();checkFocus(true);
        capture([&]{QTest::keyClick(more,Qt::Key_Space);},{},[&](QMenu *menu){QTest::keyClick(menu,Qt::Key_Escape);});
        settle();checkFocus(true);toolbarEvidence("keyboard-escape");
        capture([&]{QTest::mouseClick(more,Qt::LeftButton);},{},[&](QMenu *menu){
            menu->setActiveAction(action(menu,"显示修改时间列"));QTest::keyClick(menu,Qt::Key_Return);
        });
        settle();checkFocus(false);toolbarEvidence("mouse-enter");
        capture([&]{QTest::mouseClick(more,Qt::LeftButton);},{},[&](QMenu *menu){
            QTest::mouseClick(menu,Qt::LeftButton,{},menu->actionGeometry(action(menu,"显示隐藏文件")).center());
        });
        settle();checkFocus(false);toolbarEvidence("mouse-select");
        noFileRequests(w);
    }
    void resourceMenuSingleColumnScroll() {
        UiZoom::instance().setLevel(4);auto reset=qScopeGuard([]{UiZoom::instance().setLevel(0);});
        Workspace w(profile(),{},true);setup(w);auto folder=row(w,"/root/folder",true);settle();
        capture([&]{w.fileMenu(w.files->visualItemRect(folder).center());},{},[&](QMenu *base){
            auto menu=dynamic_cast<FileContextMenu *>(base);QVERIFY(menu);
            QList<QAction *> enabled;
            for(auto a:menu->actions())if(!a->isSeparator()&&a->isEnabled())enabled<<a;
            QVERIFY(!enabled.isEmpty());auto first=enabled.first(),last=enabled.last();
            const int firstY=menu->actionGeometry(first).top();
            QVERIFY(menu->actionGeometry(last).bottom()>menu->height()); // Real native overflow, not just a style hint.
            const int x=menu->actionGeometry(first).left();
            for(auto a:enabled)QCOMPARE(menu->actionGeometry(a).left(),x);
            const int h=menu->style()->pixelMetric(QStyle::PM_MenuScrollerHeight,nullptr,menu);
            QTest::mouseMove(menu,QPoint(menu->width()/2,menu->height()-uiPx(5)-h/2));
            QTRY_VERIFY_WITH_TIMEOUT(menu->actionGeometry(first).top()<firstY,900);
            QTest::keyClick(menu,Qt::Key_End);settle();QCOMPARE(menu->activeAction(),last);
            QVERIFY(menu->itemViewport().contains(menu->actionGeometry(last).center()));
            QVERIFY(menu->grab().save(report+"/resource-menu-scroll-bottom.png"));
            QTest::mouseMove(menu,QPoint(menu->width()/2,uiPx(5)+h/2));
            QTRY_VERIFY_WITH_TIMEOUT(menu->itemViewport().contains(menu->actionGeometry(first).center()),1500);
            QTest::keyClick(menu,Qt::Key_Home);settle();QCOMPARE(menu->activeAction(),first);
            QSet<QAction *> reached{first};
            for(int i=1;i<enabled.size();++i) {QTest::keyClick(menu,Qt::Key_Down);settle();reached.insert(menu->activeAction());}
            QCOMPARE(reached.size(),enabled.size());QCOMPARE(menu->activeAction(),last);
            QVERIFY(menu->grab().save(report+"/resource-menu-single-column-overflow.png"));
        });
    }
    void managerAndBookmarks() {
        MainWindow window(false,std::make_shared<MenuSecrets>());const auto p=profile(),q=profile("second");window.profiles={p,q};window.refresh();window.resize(1280,850);window.show();settle();
        auto tree=window.connections;auto first=tree->topLevelItem(0);const auto point=tree->visualItemRect(first).center();
        const QString groups="连接 / 切换工作区;|;编辑连接;端口配置;复制配置;|;收藏 / 取消收藏;移动到分组;|;复制地址;导出配置;|;删除连接配置…";
        QCOMPARE(capture([&]{window.connectionMenu(point);},"manager-dark"),groups.split(';'));
        auto w=new Workspace(p,{},true,nullptr,&window.repository,&window.bookmarks);window.workspaces[p.id]=w;window.tabs->addTab(w,"fixture");window.tabs->setCurrentIndex(0);settle();
        auto online=capture([&]{window.connectionMenu(point);});QCOMPARE(online.indexOf("断开会话"),1);
        tree->clearSelection();first->setSelected(true);tree->topLevelItem(1)->setSelected(true);
        QCOMPARE(capture([&]{window.connectionMenu(point);}),QString("连接所选服务器;|;移动到分组;|;导出配置;|;删除连接配置…").split(';'));
        QVERIFY(window.bookmarks.add(p.id,"/favorite"));first->setExpanded(true);auto child=first->child(0);settle();applyTheme(true);
        const auto bookmarkGroups=QString("打开目录和终端组;打开目录;在此处新建终端;|;设置别名…;|;移除收藏").split(';');
        QCOMPARE(capture([&]{window.connectionMenu(tree->visualItemRect(child).center());},"bookmark-light"),bookmarkGroups);
        QCOMPARE(capture([&]{bookmarkMenu(w,&window.bookmarks,p.id,window.bookmarks.entries(p.id).first().id,w->mapToGlobal(QPoint(300,100)),[](BookmarkOpen){});}),bookmarkGroups);
        tree->clearSelection();first->setSelected(true);child->setSelected(true);
        QCOMPARE(capture([&]{window.connectionMenu(point);}),QStringList{"请仅选择连接行进行批量操作"});
        capture([&]{window.connectionMenu(tree->visualItemRect(child).center());},{},[&](QMenu *m){action(m,"移除收藏")->trigger();});
        QVERIFY(window.bookmarks.entries(p.id).isEmpty());QCOMPARE(window.profiles.size(),2);
    }
    void terminalAndTabs() {
        Workspace w(profile(),{},true);setup(w);auto term=w.terminals.begin()->view;QSignalSpy input(term,&Terminal::input);
        auto body=[&]{QContextMenuEvent e(QContextMenuEvent::Mouse,QPoint(30,20),term->viewport()->mapToGlobal(QPoint(30,20)));QApplication::sendEvent(term->viewport(),&e);};
        const auto base=QString("复制所选;复制全部输出;粘贴…;|;清屏;|;中断命令（直接发送；无选区时 Ctrl+C）").split(';');
        QCOMPARE(capture(body,"terminal-dark"),base);
        capture(body,{},[&](QMenu *m){action(m,"中断命令（直接发送；无选区时 Ctrl+C）")->trigger();});QCOMPARE(input.size(),1);QCOMPARE(input.first()[0].toByteArray(),QByteArray(1,3));
        QAction back("返回主窗",nullptr);term->addAction(&back);auto extended=base;extended<<"|"<<"返回主窗";QCOMPARE(capture(body),extended);term->removeAction(&back);
        if(w.terminalCount()<2){w.addTerminal(w.terminals.begin()->group);settle();}
        auto item=w.terminalManager->topLevelItem(0);QVERIFY(item);const auto target=item->data(0,Qt::UserRole).toInt();const int count=w.terminalCount();
        const auto layout=QString("拆分终端;取消拆分，成为独立组;移到独立窗口;|;重命名;|;关闭终端").split(';');
        QCOMPARE(capture([&]{w.terminalMenu(w.terminalManager->visualItemRect(item).center());}),layout);
        capture([&]{w.terminalMenu(w.terminalManager->visualItemRect(item).center());},{},[&](QMenu *m){action(m,"关闭终端")->trigger();});QCOMPARE(w.terminalCount(),count-1);QVERIFY(!w.terminals.contains(target));
        auto editor=w.editArea->groups().first();auto fileMenu=capture([&]{emit editor->tabBar()->customContextMenuRequested(editor->tabBar()->tabRect(0).center());});
        QCOMPARE(fileMenu,QString("关闭;关闭其他;关闭右侧;关闭已保存;全部关闭;|;打开文本对比;格式化 JSON;移动到另一个编辑组;移到独立窗口").split(';'));
        auto split=editor->findChild<QToolButton*>("splitEditorGroup");QVERIFY(split);QCOMPARE(split->menu()->actions().size(),2);for(auto a:split->menu()->actions())QVERIFY(!a->isSeparator());
        MainWindow window(true,std::make_shared<MenuSecrets>());window.resize(1280,850);window.show();settle();
        QCOMPARE(capture([&]{emit window.tabs->tabBar()->customContextMenuRequested(window.tabs->tabBar()->tabRect(1).center());}),QString("重新连接;|;关闭工作区").split(';'));
    }
    void portsAndServices() {
        auto p=profile();TunnelRule rule;rule.profileId=p.id;rule.localPort=18080;rule.remotePort=8080;QVERIFY(saveTunnelRules(p.id,{rule}));
        PortsPanel panel(p.id,true);panel.resize(680,350);panel.show();settle();auto global=panel.mapToGlobal(QPoint(120,50));QSignalSpy starts(&panel,&PortsPanel::startRequested);
        QCOMPARE(capture([&]{panel.showMenu(rule.id,global);}),QString("连接并启动;|;编辑端口;高级设置;|;复制本地地址;|;删除").split(';'));
        capture([&]{panel.showMenu(rule.id,global);},{},[&](QMenu *menu){action(menu,"连接并启动")->trigger();});QCOMPARE(starts.size(),1);QCOMPARE(qvariant_cast<TunnelRule>(starts.first()[0]).id,rule.id);
        panel.offline=false;applyTheme(true);QCOMPARE(capture([&]{panel.showMenu(rule.id,global);},"ports-light"),QString("编辑端口;高级设置;|;复制本地地址;|;删除").split(';'));
        panel.setRuleState(rule.id,"running");QTimer cancel;cancel.setInterval(5);connect(&cancel,&QTimer::timeout,this,[]{if(auto box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))box->button(QMessageBox::Cancel)->click();});cancel.start();
        capture([&]{panel.showMenu(rule.id,global);},{},[&](QMenu *menu){action(menu,"删除")->trigger();});cancel.stop();QVERIFY(panel.rule(rule.id));
        panel.setRuleState(rule.id,"starting");panel.showMenu(rule.id,global);QVERIFY(!QApplication::activePopupWidget());
        ServicesPanel services("合成服务",true);services.resize(900,480);services.show();services.timer.stop();
        QVariantMap info{{"Id","fixture.service"},{"LoadState","loaded"},{"ActiveState","active"},{"UnitFileState","enabled"},{"CanReload","yes"}};services.snapshot({info});
        const auto expected=QString("启动;停止…;重启…;重新加载配置;|;启用开机自启;禁用开机自启;|;详情;最近日志;复制名称;|;删除服务…").split(';');
        applyTheme(false);QCOMPARE(capture([&]{services.menu("fixture.service",services.mapToGlobal(QPoint(120,50)));},"services-dark",[&](QMenu *m){QVERIFY(!action(m,"启动")->isEnabled());}),expected);
        services.demo=false;services.busy=true;capture([&]{services.menu("fixture.service",global);},{},[&](QMenu *m){for(auto a:m->actions())if(!a->isSeparator())QVERIFY(!a->isEnabled());});
        services.busy=false;info["Id"]="template@.service";services.snapshot({info});capture([&]{services.menu("template@.service",global);},{},[&](QMenu *m){for(auto a:m->actions()){if(a->isSeparator())continue;QCOMPARE(a->isEnabled(),a->data()=="detail"||a->data()=="copy");}});
    }
    void transfersByState() {
        Workspace w(profile(),{},true);setup(w);w.uploadList->clear();w.transferItems.clear();w.transfers.clear();w.transferQueue.clear();w.activeTransfer=0;
        Job j;j.type="upload";j.id=900;j.path="/root/file";j.destination="private-local";w.transfers[j.id]=j;
        auto batch=new QTreeWidgetItem(w.uploadList,{"合成批次"});auto item=new QTreeWidgetItem(batch,{"file","已完成"});item->setData(0,Qt::UserRole,j.id);batch->setExpanded(true);w.transferItems[j.id]=item;w.transferPanel->show();settle();
        auto show=[&]{w.transferMenu(w.uploadList,w.uploadList->visualItemRect(item).center());};
        QCOMPARE(capture(show),QStringList{"任务详情"});item->setText(1,"失败");QCOMPARE(capture(show),QString("失败项从头重试;|;任务详情").split(';'));
        w.transferQueue.enqueue(j.id);item->setText(1,"等待");applyTheme(true);
        QCOMPARE(capture(show,"transfer-light"),QString("暂停;继续 / 重新连接传输;|;任务详情;|;取消").split(';'));
        capture(show,{},[&](QMenu *m){action(m,"暂停")->trigger();});QVERIFY(w.pausedTransfers.contains(j.id));
        capture(show,{},[&](QMenu *m){action(m,"取消")->trigger();});QVERIFY(!w.transferQueue.contains(j.id));QCOMPARE(item->text(1),QString("已取消"));
        item->setText(1,"失败");w.transfers[j.id].options["nativeDrop"]=true;QCOMPARE(capture(show),QStringList{"任务详情"});
    }
};
}
QTEST_MAIN(rs::MenuTests)
#include "test_menus.moc"
