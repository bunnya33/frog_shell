#include "../src/ui/mainwindow.h"
#include "../src/ui/theme.h"
#include "../src/ui/uipreferences.h"
#include <QtTest>
namespace rs {
class BookmarkSecrets final:public SecretBackend {
  public: SecretResult read(const QString &) override{return {};}
    QString write(const QString &,const QByteArray &) override{return {};}
    QString remove(const QString &) override{return {};}
};
class BookmarkTests:public QObject {
    Q_OBJECT
    QTemporaryDir root;
    std::unique_ptr<QTemporaryDir> data;
    void settle(){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
    Profile profile(QString id="bookmark-test"){Profile p;p.id=id;p.name="开发服务器";p.host="127.0.0.1";p.user="fixture";return p;}
    void setup(Workspace &w){w.demo=false;w.online=true;w.directory="/home/fixture";w.files->clear();w.updateBreadcrumbs();w.session=new Session(w.profile,{},&w);w.attachSession(w.session);w.resize(1180,820);w.show();settle();}
    QList<Job> jobs(Workspace &w){QList<Job> out;while(!w.session->jobs.empty()){out<<w.session->jobs.front();w.session->jobs.pop_front();}w.session->queuedJobBytes=0;return out;}
    void complete(Workspace &w,int id,const QString &path){w.session->completed("list",id,{{"path",path},{"entries",QVariantList{}}});settle();}
    void accessible(SafeTreeWidget *tree){settle();auto iface=QAccessible::queryAccessibleInterface(tree);QVERIFY(iface);QSet<QAccessible::Id> seen;
        for(int i=0;i<iface->childCount();++i){auto child=iface->child(i);QVERIFY(child&&child->isValid());QVERIFY2(iface->indexOfChild(child)==i,qPrintable(QString("tree=%1 count=%2 i=%3 actual=%4 role=%5 name=%6").arg(tree->accessibleName()).arg(iface->childCount()).arg(i).arg(iface->indexOfChild(child)).arg(int(child->role())).arg(child->text(QAccessible::Name))));QVERIFY(!seen.contains(QAccessible::uniqueId(child)));seen.insert(QAccessible::uniqueId(child));}
        for(int start=0;start<iface->childCount();++start){auto current=iface->child(start);QSet<QAccessible::Id> chain;int count=0;
            while(current&&count++<128){auto id=QAccessible::uniqueId(current);QVERIFY(!chain.contains(id));chain.insert(id);QAccessibleInterface *next=nullptr;
                for(int n=iface->indexOfChild(current)+1;n<iface->childCount();++n)if(auto c=iface->child(n);c&&c->isValid()&&!c->state().invisible){next=c;break;}current=next;}QVERIFY(count<128);}
    }
    void triggerMenu(const std::function<void()> &show,const QString &action){
        bool found=false;QTimer driver;driver.setInterval(15);QElapsedTimer deadline;deadline.start();
        connect(&driver,&QTimer::timeout,this,[&]{for(auto widget:QApplication::topLevelWidgets())if(auto menu=qobject_cast<QMenu*>(widget);menu&&menu->isVisible()){
            for(auto a:menu->actions())if(a->text()==action){found=true;a->trigger();menu->close();return;}
            menu->close();return;}if(deadline.elapsed()>1500)for(auto widget:QApplication::topLevelWidgets())if(auto dialog=qobject_cast<QDialog*>(widget))dialog->reject();});
        driver.start();show();driver.stop();QVERIFY2(found,qPrintable(action));
    }
  private slots:
    void initTestCase(){QVERIFY(root.isValid());QStandardPaths::setTestModeEnabled(true);QCoreApplication::setOrganizationName("RabbitShellBookmarkTests");QCoreApplication::setApplicationName("Isolated");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication::setStyle("Fusion");
        for(auto f:{"C:/Windows/Fonts/msyh.ttc","C:/Windows/Fonts/consola.ttf"})if(QFileInfo::exists(f))QFontDatabase::addApplicationFont(f);QApplication::setFont(QFont("Microsoft YaHei UI",10));}
    void init(){data=std::make_unique<QTemporaryDir>(root.filePath("case-XXXXXX"));QVERIFY(data->isValid());qputenv("RABBITSHELL_DATA_DIR",data->path().toUtf8());QSettings().clear();UiZoom::instance().setLevel(0);applyTheme(false);}
    void storeRoundtrip(){
        DirectoryBookmarkStore store;store.setProfiles({"a","b"});QSignalSpy changed(&store,&DirectoryBookmarkStore::changed);
        QVERIFY(store.add("a","/项目 中文/目录 'quoted'/"));const auto first=store.entries("a").first();
        QVERIFY(store.add("a",first.path));QCOMPARE(store.entries("a").size(),1);QCOMPARE(changed.size(),1);
        QVERIFY(store.add("a","/"));QCOMPARE(store.entries("a").last().name(),QString("根目录"));
        QVERIFY(store.rename("a",first.id,"<别名>"));QCOMPARE(store.entry("a",first.id)->path,first.path);
        QVERIFY(store.rename("a",first.id,""));QCOMPARE(store.entry("a",first.id)->name(),QString("目录 'quoted'"));
        QVERIFY(store.add("b",first.path));QVERIFY(store.add("a",QString::fromUtf8("/emoji-😀/𠮷")));
        QVERIFY(!store.add("a","/link/../else"));QVERIFY(!store.add("a","/bad\nline"));QVERIFY(!store.add("a","relative"));QVERIFY(!store.add("removed","/x"));
        QVERIFY(store.add("a",first.path));QVERIFY(store.error().isEmpty());
        auto entries=store.entries("a");QStringList order;for(auto it=entries.crbegin();it!=entries.crend();++it)order<<it->id;QVERIFY(store.reorder("a",order));
        DirectoryBookmarkStore loaded;QCOMPARE(loaded.entries("a").first().id,order.first());QCOMPARE(loaded.entries("b").size(),1);
        QVERIFY(loaded.remove("a",first.id));QVERIFY(!loaded.entry("a",first.id));QCOMPARE(loaded.entries("b").size(),1);
        UiPreferences::instance().reset();DirectoryBookmarkStore afterReset;QCOMPARE(afterReset.entries("a").size(),2);
    }
    void storeLimitsAndFailure(){
        DirectoryBookmarkStore invalid(data->filePath("missing/bookmarks.json"));QSignalSpy failed(&invalid,&DirectoryBookmarkStore::saveFailed);QVERIFY(!invalid.add("p","/ok"));QVERIFY(invalid.entries("p").isEmpty());QCOMPARE(failed.size(),1);
        QFile corrupt(data->filePath("corrupt.json"));QVERIFY(corrupt.open(QIODevice::WriteOnly));corrupt.write("invalid");corrupt.close();DirectoryBookmarkStore protectedStore(corrupt.fileName());QVERIFY(!protectedStore.add("p","/x"));QVERIFY(corrupt.open(QIODevice::ReadOnly));QCOMPARE(corrupt.readAll(),QByteArray("invalid"));
        QJsonObject profiles;for(int i=0;i<2048;++i)profiles[QString::number(i)]=QJsonArray{};
        QFile limit(data->filePath("limit.json"));QVERIFY(limit.open(QIODevice::WriteOnly));limit.write(QJsonDocument(QJsonObject{{"version",1},{"profiles",profiles}}).toJson());limit.close();
        DirectoryBookmarkStore bounded(limit.fileName());QVERIFY(!bounded.add("new","/x"));QVERIFY(!bounded.add(QString(129,'p'),"/x"));
        QJsonArray entries;for(int i=0;i<256;++i)entries<<QJsonObject{{"id",QString::number(i)},{"path","/p"+QString::number(i)},{"alias",""}};
        QVERIFY(limit.open(QIODevice::WriteOnly|QIODevice::Truncate));limit.write(QJsonDocument(QJsonObject{{"version",1},{"profiles",QJsonObject{{"p",entries}}}}).toJson());limit.close();DirectoryBookmarkStore full(limit.fileName());QVERIFY(!full.add("p","/257"));QCOMPARE(full.entries("p").size(),256);
    }
    void contextAddAndModelDrop(){
        DirectoryBookmarkStore store;auto p=profile();Workspace w(p,{},true,nullptr,nullptr,&store);setup(w);
        auto folder=new QTreeWidgetItem(w.files,{"<目录>"});folder->setData(0,Qt::UserRole,QVariantMap{{"path","/other/<目录>"},{"directory",true}});settle();
        const auto point=w.files->visualItemRect(folder).center();for(int i=0;i<2;++i)triggerMenu([&]{w.fileMenu(point);},"添加到目录收藏");
        QCOMPARE(store.entries(p.id).size(),1);QCOMPARE(store.entries(p.id).first().path,QString("/other/<目录>"));
        QVERIFY(store.add(p.id,"/second"));QVERIFY(store.add(p.id,"/third"));auto tree=w.bookmarkTree;
        const auto first=store.entries(p.id).first().id;tree->setCurrentItem(tree->topLevelItem(0));
        std::unique_ptr<QMimeData> mime(tree->model()->mimeData({tree->model()->index(0,0)}));
        QVERIFY(tree->model()->dropMimeData(mime.get(),Qt::MoveAction,3,0,{}));QCOMPARE(store.entries(p.id).last().id,first);QCOMPARE(tree->currentItem()->data(0,BookmarkIdRole).toString(),first);
        QVERIFY(!tree->model()->dropMimeData(mime.get(),Qt::CopyAction,0,0,{}));
        BookmarkTree other(&store,"other");QVERIFY(!other.model()->dropMimeData(mime.get(),Qt::MoveAction,0,0,{}));
        DirectoryBookmarkStore reload;QCOMPARE(reload.entries(p.id).last().id,first);QCOMPARE(tree->topLevelItemCount(),3);for(int i=0;i<3;++i)QCOMPARE(tree->topLevelItem(i)->childCount(),0);
        const int width=tree->columnWidth(1)+21;tree->setColumnWidth(1,width);UiZoom::instance().setLevel(2);UiZoom::instance().setLevel(0);QCOMPARE(tree->columnWidth(1),width);
    }
    void navigationValidationAndExistingTerminal(){
        DirectoryBookmarkStore store;auto p=profile();const QString path="/中文 space/it's $(safe)";QVERIFY(store.add(p.id,path));const auto id=store.entries(p.id).first().id;
        Workspace w(p,{},true,nullptr,nullptr,&store);setup(w);const int before=w.terminalCount();jobs(w);
        w.sidebar->setCurrentIndex(0);w.bottomTabs->setCurrentIndex(1);w.openBookmark(id);const auto listing=jobs(w);QCOMPARE(listing.size(),1);QCOMPARE(listing.first().type,QString("list"));QCOMPARE(w.terminalCount(),before);
        complete(w,listing.first().id,path);auto opened=jobs(w);QCOMPARE(w.directory,path);QCOMPARE(w.sidebar->currentIndex(),1);QCOMPARE(w.terminalCount(),before+1);QCOMPARE(w.bottomTabs->currentIndex(),0);QVERIFY(w.terminals[w.focusedTerm].view->hasFocus());
        auto shell=std::find_if(opened.cbegin(),opened.cend(),[](const Job &j){return j.type=="open-terminal";});QVERIFY(shell!=opened.cend());QCOMPARE(shell->path,path);
        for(const auto &j:opened)QVERIFY(j.type!="input");QCOMPARE(shellQuote(path),QString("'/中文 space/it'\"'\"'s $(safe)'"));
        w.sidebar->setCurrentIndex(0);w.bottomTabs->setCurrentIndex(2);w.openBookmark(id,BookmarkOpen::Terminal);auto terminalList=jobs(w);complete(w,terminalList.first().id,path);QCOMPARE(w.directory,path);QCOMPARE(w.sidebar->currentIndex(),0);QCOMPARE(w.terminalCount(),before+2);QCOMPARE(w.bottomTabs->currentIndex(),0);QVERIFY(w.terminals[w.focusedTerm].view->hasFocus());
        jobs(w);w.bottomTabs->setCurrentIndex(1);w.openBookmark(id,BookmarkOpen::Directory);auto directoryList=jobs(w);complete(w,directoryList.first().id,path);QCOMPARE(w.sidebar->currentIndex(),1);QCOMPARE(w.terminalCount(),before+2);QCOMPARE(w.bottomTabs->currentIndex(),1);
    }
    void terminalRowsPromoteWithoutRestart(){
        auto p=profile();Workspace w(p,{},true);setup(w);jobs(w);
        auto root=w.terminalManager->topLevelItem(0);QVERIFY(root);QCOMPARE(root->childCount(),0);const int first=root->data(0,Qt::UserRole).toInt();
        auto child=w.terminalManager->topLevelItem(1);QVERIFY(child);const int second=child->data(0,Qt::UserRole).toInt(),group=w.terminals[first].group;auto view=w.terminals[second].view;jobs(w);
        w.closeTerminal(first);QCOMPARE(w.terminalManager->topLevelItem(0),child);QCOMPARE(w.terminals[second].view,view);QCOMPARE(w.terminals[second].group,group);QVERIFY(w.terminalManager->isHidden());
        for(const auto &job:jobs(w))QVERIFY(job.type!="open-terminal");
    }
    void stableFavoriteGroupsAndRecentFocus(){
        DirectoryBookmarkStore store;auto p=profile();QVERIFY(store.add(p.id,"/project/backend"));auto bookmark=store.entries(p.id).first();
        Workspace w(p,{},true,nullptr,nullptr,&store);setup(w);jobs(w);
        auto open=[&]{w.openBookmark(bookmark.id);const auto request=jobs(w);QVERIFY(!request.isEmpty());complete(w,request.first().id,bookmark.path);};
        const int before=w.terminalCount();open();const int first=w.focusedTerm,group=w.terminals[first].group;jobs(w);
        const auto original=w.terminals[first].view;for(int i=0;i<4;++i){open();QCOMPARE(w.terminalCount(),before+1);QCOMPARE(w.focusedTerm,first);for(auto j:jobs(w))QVERIFY(j.type!="input"&&j.type!="open-terminal");}
        w.addTerminal(group);const int second=w.focusedTerm;jobs(w);QCOMPARE(w.terminals[second].group,group);
        // Actual clicks/focus on split text update recent focus without sending shell input.
        w.terminals[first].view->setFocus();settle();w.terminals[second].view->setFocus();settle();
        w.addTerminal();const int independent=w.focusedTerm;QVERIFY(w.terminals[independent].group!=group);jobs(w);open();QCOMPARE(w.focusedTerm,second);QCOMPARE(w.terminals[first].view,original);jobs(w);
        QVERIFY(store.rename(p.id,bookmark.id,"后端 & <project>"));QCOMPARE(w.bookmarkGroups[group].name,QString("后端 & <project>"));QTextDocument tooltip;tooltip.setHtml(w.currentTerminal->toolTip());QVERIFY(tooltip.toPlainText().contains("后端 & <project>"));QVERIFY(w.currentTerminal->toolTip().contains("【启动目录】/project/backend"));
        auto header=w.terminalManager->topLevelItem(1);QVERIFY(header);QVERIFY(!w.bookmarkGroups.isEmpty());w.renameTerminal(first,"manual");QCOMPARE(w.terminalLabel(w.terminals[first]),QString("manual"));
        w.terminals[first].running=false;w.terminals[second].running=false;const int ended=w.terminalCount();open();QCOMPARE(w.terminalCount(),ended+1);const int live=w.focusedTerm;QCOMPARE(w.terminals[live].group,group);jobs(w);open();QCOMPARE(w.focusedTerm,live);QCOMPARE(w.terminalCount(),ended+1);jobs(w);
        w.openBookmark(bookmark.id,BookmarkOpen::Terminal);auto request=jobs(w);complete(w,request.first().id,bookmark.path);QCOMPARE(w.terminalCount(),ended+2);QVERIFY(w.terminals[w.focusedTerm].group!=group);jobs(w);
        const auto total=w.terminalCount();QVERIFY(store.remove(p.id,bookmark.id));QVERIFY(!w.bookmarkGroups.contains(group));QCOMPARE(w.terminalCount(),total);QCOMPARE(w.terminals[first].view,original);
        QVERIFY(store.add(p.id,bookmark.path));bookmark=store.entries(p.id).first();open();const int last=w.focusedTerm,lastGroup=w.terminals[last].group;QVERIFY(w.bookmarkGroups.contains(lastGroup));w.closeTerminal(last);QVERIFY(!w.bookmarkGroups.contains(lastGroup));
    }
    void pendingLatestCancelStaleAndFailure(){
        DirectoryBookmarkStore store;auto p=profile();QVERIFY(store.add(p.id,"/first"));QVERIFY(store.add(p.id,"/empty"));auto list=store.entries(p.id);
        Workspace w(p,{},true,nullptr,nullptr,&store);setup(w);const int before=w.terminalCount();jobs(w);w.online=false;w.mainConnectionState("正在连接…",true);
        w.openBookmark(list[0].id);w.openBookmark(list[1].id);QVERIFY(jobs(w).isEmpty());w.session->connected();w.monitorTimer.stop();
        auto requests=jobs(w);auto request=std::find_if(requests.cbegin(),requests.cend(),[](const Job &j){return j.type=="list";});QVERIFY(request!=requests.cend());QCOMPARE(request->path,QString("/empty"));
        for(const auto &j:requests)QVERIFY(j.type!="open-terminal");complete(w,request->id,"/empty");QCOMPARE(w.terminalCount(),before+1);jobs(w);
        w.openBookmark(list[0].id);auto old=jobs(w).first();w.requestDirectory("/first",true);const auto newer=jobs(w).first();complete(w,old.id,"/first");QCOMPARE(w.terminalCount(),before+1);QCOMPARE(w.newestDirectoryRequest.value("/first"),newer.id);
        complete(w,newer.id,"/first");w.openBookmark(list[1].id);old=jobs(w).first();w.navigate("/manual");QVERIFY(!w.bookmarkIntent);QVERIFY(w.bookmarkStatus->isHidden());complete(w,old.id,"/empty");QCOMPARE(w.terminalCount(),before+1);jobs(w);
        w.openBookmark(list[1].id);old=jobs(w).first();w.session->failed("list",old.id,"权限不足");QVERIFY(!w.bookmarkIntent);QVERIFY(w.bookmarkStatus->text().contains("权限不足"));QVERIFY(store.entry(p.id,list[1].id));
        w.openBookmark(list[1].id);old=jobs(w).first();QVERIFY(store.remove(p.id,list[1].id));complete(w,old.id,"/empty");QCOMPARE(w.terminalCount(),before+1);QVERIFY(!w.bookmarkIntent);
        w.openBookmark(list[0].id);jobs(w);w.cancelMainConnection();QVERIFY(!w.bookmarkIntent);QVERIFY(w.bookmarkStatus->isHidden());
    }
    void queueFullAndIdentity(){
        DirectoryBookmarkStore store;auto p=profile();QVERIFY(store.add(p.id,"/x"));auto id=store.entries(p.id).first().id;
        Workspace w(p,{},true,nullptr,nullptr,&store);setup(w);jobs(w);const auto count=w.terminalCount();
        for(int i=0;i<256;++i){Job j;j.type="list";j.id=1000+i;j.path="/ignored";w.session->submit(j);}
        w.openBookmark(id);QVERIFY(!w.bookmarkIntent);QVERIFY(w.bookmarkStatus->text().contains("队列已满"));QCOMPARE(w.terminalCount(),count);jobs(w);
        w.openBookmark(id);auto request=jobs(w).first();store.setProfiles({});complete(w,request.id,"/x");QCOMPARE(w.terminalCount(),count);QVERIFY(!store.add(p.id,"/orphan"));
        ConnectionRepository repository(nullptr,std::make_shared<BookmarkSecrets>());repository.profiles={p};store.setProfiles({p.id});
        Workspace changed(p,{},true,nullptr,&repository,&store);setup(changed);const int originalCount=changed.terminalCount();jobs(changed);changed.openBookmark(id);auto pending=jobs(changed).first();repository.profiles[0].host="changed.invalid";changed.updateCredentials(repository.profiles[0],{});complete(changed,pending.id,"/x");QVERIFY(!changed.bookmarkIntent);QCOMPARE(changed.terminalCount(),originalCount);
    }
    void managerStateActionsAndDispatch(){
        MainWindow m(false,std::make_shared<BookmarkSecrets>());auto a=profile("a"),b=profile("b"),c=profile("c");m.profiles={a,b,c};m.refresh();QVERIFY(m.bookmarks.add("b","/one"));QVERIFY(m.bookmarks.add("b","/two"));QVERIFY(m.bookmarks.add("c","/one"));
        m.resize(1280,850);m.show();settle();auto server=m.connections->topLevelItem(1);server->setExpanded(true);auto child=server->child(0);const auto id=child->data(0,BookmarkIdRole).toString();m.connections->setCurrentItem(child);child->setSelected(true);
        const auto entries=m.bookmarks.entries("b");QVERIFY(m.bookmarks.reorder("b",{entries[1].id,entries[0].id}));QCOMPARE(m.connections->currentItem()->data(0,BookmarkIdRole).toString(),id);QVERIFY(m.connections->currentItem()->isSelected());QVERIFY(server->isExpanded());
        m.profiles.removeFirst();m.refresh();server=m.connections->topLevelItem(0);QVERIFY(server->isExpanded());QCOMPARE(m.connections->currentItem()->data(0,BookmarkIdRole).toString(),id);QVERIFY(m.connections->currentItem()->isSelected());
        const auto oldName=m.profiles[0].name;m.connections->setFocus();QTest::keyClick(m.connections,Qt::Key_F2);QCOMPARE(m.profiles[0].name,oldName);
        child=m.connections->currentItem();QStringList actions;QTimer timer;timer.setInterval(10);connect(&timer,&QTimer::timeout,&m,[&]{for(auto w:QApplication::topLevelWidgets())if(auto menu=qobject_cast<QMenu*>(w);menu&&menu->isVisible()){for(auto a:menu->actions())actions<<a->text();menu->close();}});timer.start();m.connectionMenu(m.connections->visualItemRect(child).center());timer.stop();QVERIFY(actions.contains("移除收藏"));QVERIFY(!actions.contains("删除连接配置…"));
        auto workspace=new Workspace(b,{},true,nullptr,&m.repository,&m.bookmarks);setup(*workspace);const int initialTerms=workspace->terminalCount();jobs(*workspace);m.workspaces[b.id]=workspace;m.tabs->addTab(workspace,"fixture");m.tabs->setCurrentIndex(0);
        settle();const auto point=m.connections->visualItemRect(child).center();QTest::mouseClick(m.connections->viewport(),Qt::LeftButton,{},point);QTest::mouseDClick(m.connections->viewport(),Qt::LeftButton,{},point);settle();QCOMPARE(m.tabs->currentWidget(),workspace);auto requests=jobs(*workspace);QList<Job> request;for(const auto &job:requests){QVERIFY(job.type=="list"||job.type=="resize");if(job.type=="list")request<<job;}QCOMPARE(request.size(),1);QCOMPARE(request.first().path,QString("/one"));QCOMPARE(workspace->terminalCount(),initialTerms);complete(*workspace,request.first().id,"/one");QCOMPARE(workspace->terminalCount(),initialTerms+1);QVERIFY(m.bookmarks.entry("c",m.bookmarks.entries("c").first().id));
        m.connections->clearSelection();server->setSelected(true);m.connections->topLevelItem(1)->setSelected(true);m.profiles.prepend(a);m.refresh();QCOMPARE(m.connections->selectedItems().size(),2);
    }
    void isolatedBookmarkPty(){
        const int port=qEnvironmentVariableIntValue("RABBITSHELL_BOOKMARK_PORT");if(!port)QSKIP("Run with the private bookmark SSH/SFTP + no-startup-file bash fixture");
        auto p=profile();p.port=port;p.timeout=3;DirectoryBookmarkStore store;
        const QString path="/bookmarks/中文 space/it's $(safe)",second="/bookmarks/empty";
        QVERIFY(store.add(p.id,path));QVERIFY(store.add(p.id,second));QVERIFY(store.add(p.id,"/missing"));const auto entries=store.entries(p.id);
        Workspace w(p,QByteArray("fixture-only"),true,nullptr,nullptr,&store);w.demo=false;w.online=false;w.files->clear();const int before=w.terminalCount();
        w.beginMainConnection();w.session=new Session(p,"fixture-only",&w);w.attachSession(w.session);
        disconnect(w.session,&Session::hostKey,&w,nullptr);connect(w.session,&Session::hostKey,&w,[&](const QString &,bool changed){if(!changed)w.session->trust(true);});
        QHash<int,QByteArray> output;connect(w.session,&Session::terminalOutput,&w,[&](int id,const QByteArray &bytes){output[id]+=bytes;});
        w.openBookmark(entries[0].id);w.session->start();QTRY_COMPARE_WITH_TIMEOUT(w.terminalCount(),before+1,10000);w.monitorTimer.stop();QCOMPARE(w.directory,path);const int firstId=w.focusedTerm;
        auto printCwd=[&](int id){Job j;j.type="input";j.id=id;j.data="printf 'BOOKMARK_CWD='; pwd\r";w.session->submit(j);};
        printCwd(firstId);QTRY_VERIFY_WITH_TIMEOUT(output[firstId].contains("BOOKMARK_CWD=")&&output[firstId].contains(path.toUtf8()),5000);
        w.openBookmark(entries[1].id);QTRY_COMPARE_WITH_TIMEOUT(w.terminalCount(),before+2,5000);QCOMPARE(w.directory,second);const int secondId=w.focusedTerm;printCwd(secondId);
        QTRY_VERIFY_WITH_TIMEOUT(output[secondId].contains(second.toUtf8()),5000);output[firstId].clear();printCwd(firstId);QTRY_VERIFY_WITH_TIMEOUT(output[firstId].contains(path.toUtf8()),5000);
        w.openBookmark(entries[2].id);QTRY_VERIFY_WITH_TIMEOUT(!w.bookmarkIntent,5000);QCOMPARE(w.terminalCount(),before+2);QCOMPARE(w.directory,second);QVERIFY(w.bookmarkStatus->text().contains("无法打开收藏目录"));
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR");QFile evidence(report+"/pty-cwd.txt");QVERIFY(evidence.open(QIODevice::WriteOnly));evidence.write("Private loopback; actual no-startup-file MSYS bash, SFTP root mapped to fixture disk.\nFirst channel remained at its original cwd:\n");evidence.write(output[firstId]);evidence.write("\nNew channel cwd:\n");evidence.write(output[secondId]);
        w.session->stop();QVERIFY(w.session->wait(5000));
    }
    void accessibleNewTreesAndScreenshots(){
        QAccessible::setActive(true);auto restore=qScopeGuard([]{QAccessible::setActive(false);});MainWindow m(false,std::make_shared<BookmarkSecrets>());auto p=profile();m.profiles={p};m.refresh();
        for(const auto &path:{"/home/fixture/project","/var/log","/资料/季度 'report'","/"})QVERIFY(m.bookmarks.add(p.id,path));auto entries=m.bookmarks.entries(p.id);QVERIFY(m.bookmarks.rename(p.id,entries[0].id,"项目目录"));
        auto w=new Workspace(p,{},true,nullptr,&m.repository,&m.bookmarks);m.workspaces[p.id]=w;m.tabs->addTab(w,"目录收藏 · 原生预览");m.resize(1280,850);m.show();
        for(int round=0;round<5;++round){auto server=m.connections->topLevelItem(0);server->setExpanded(round%2==0);accessible(m.connections);accessible(w->bookmarkTree);
            auto iface=QAccessible::queryAccessibleInterface(m.connections);auto old=iface->child(qMin(5,iface->childCount()-1));const auto oldId=QAccessible::uniqueId(old);
            server->setExpanded(!server->isExpanded());QVERIFY(!QAccessible::accessibleInterface(oldId));accessible(m.connections);
            const auto id=m.bookmarks.entries(p.id).last().id;QVERIFY(m.bookmarks.rename(p.id,id,"别名 "+QString::number(round)));accessible(w->bookmarkTree);
            qInfo("access: manager-sort");m.connections->sortItems(0,round%2?Qt::AscendingOrder:Qt::DescendingOrder);accessible(m.connections);
            qInfo("access: favorite-sort");w->bookmarkTree->sortItems(0,Qt::DescendingOrder);accessible(w->bookmarkTree);qInfo("access: favorite-refresh");w->bookmarkTree->refresh();accessible(w->bookmarkTree);
            qInfo("access: manager-filter");m.search->setText("not-matching");accessible(m.connections);m.search->clear();accessible(m.connections);
            QVERIFY(m.bookmarks.add(p.id,"/temporary"));accessible(m.connections);accessible(w->bookmarkTree);QVERIFY(m.bookmarks.remove(p.id,m.bookmarks.entries(p.id).last().id));accessible(m.connections);accessible(w->bookmarkTree);
        }
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data->path());
        for(bool light:{false,true}){applyTheme(light);m.tabs->setCurrentWidget(w);settle();QVERIFY(m.grab().save(report+(light?"/bookmarks-workspace-light.png":"/bookmarks-workspace-dark.png")));m.tabs->setCurrentIndex(0);m.connections->topLevelItem(0)->setExpanded(true);settle();QVERIFY(m.grab().save(report+(light?"/bookmarks-manager-light.png":"/bookmarks-manager-dark.png")));}
    }
};
}
QTEST_MAIN(rs::BookmarkTests)
#include "test_bookmarks.moc"
