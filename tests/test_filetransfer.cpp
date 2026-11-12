#include "../src/ui/workspace.h"
#include "../src/ui/theme.h"
#include "../src/core/diagnostics.h"
#include "../src/core/fileoperationjournal.h"
#include "../src/ui/diagnosticapplication.h"
#include "../src/ui/windowstyle.h"
#include "../src/ui/fileclipboard.h"
#include "promptchecks.h"
#include <QClipboard>
#include <QtTest>
#include <qpa/qwindowsysteminterface.h>
#include <qpa/qplatformdrag.h>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace rs {
class HiddenReproWindows : public QObject {
  protected:
    bool eventFilter(QObject *object,QEvent *event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto widget=qobject_cast<QWidget *>(object);widget && widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
class FileTransferTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    HiddenReproWindows hiddenWindows;
    int id=100;
    QString local(const QString &name,QByteArray bytes="uploaded") {
        auto path=data.filePath(name);QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);if(!f.open(QIODevice::WriteOnly))return {};f.write(bytes);return path;
    }
    static QVariantMap entry(QString path,bool dir=false,bool link=false,quint64 size=42) {
        return {{"path",path},{"name",path.section('/',-1)},{"directory",dir},{"link",link},{"size",QVariant::fromValue(size)},{"modified",12345}};
    }
    void settle(){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
    QList<Job> jobs(Workspace &w) {
        QList<Job> out;while(!w.session->jobs.empty()){out<<w.session->jobs.front();w.session->jobs.pop_front();}w.session->queuedJobBytes=0;return out;
    }
    void setup(Workspace &w) {
        w.demo=false;w.online=true;w.directory="/root";w.files->clear();w.updateBreadcrumbs();
        w.session=new Session(w.profile,{},&w);w.attachSession(w.session);
        w.transferSession=new Session(w.profile,{},&w);w.attachSession(w.transferSession,true);
        w.resize(1180,820);w.show();settle();
    }
    void list(Workspace &w,const QString &path,QVariantList rows) {
        w.requestDirectory(path);const auto task=w.nextJob-1;
        w.session->completed("list",task,{{"path",path},{"entries",rows}});settle();jobs(w);
    }
    bool drop(FileView *view,QPoint point,const QString &file) {
        QMimeData mime;mime.setUrls({QUrl::fromLocalFile(file)});
        QDragEnterEvent enter(point,Qt::CopyAction|Qt::MoveAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(view->viewport(),&enter);
        QDragMoveEvent move(point,Qt::CopyAction|Qt::MoveAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(view->viewport(),&move);
        QDropEvent event(point,Qt::CopyAction|Qt::MoveAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(view->viewport(),&event);
        return event.isAccepted() && event.dropAction()==Qt::CopyAction;
    }
    bool platformInternalDrop(Workspace &w,const QVariantList &rows,QTreeWidgetItem *target) {
        auto view=w.files;auto source=w.fileItem(rows.first().toMap().value("path").toString());if(!source || !target)return false;
        const auto token=w.registerInternalDrag(rows);if(token.isEmpty())return false;
        auto cleanup=qScopeGuard([&]{QWindowSystemInterface::handleDrag(w.windowHandle(),nullptr,{},Qt::IgnoreAction,{},{});w.unregisterInternalDrag(token);});
        QMimeData mime;mime.setData("application/x-qt-windows-mime;value=\"RabbitShell.InternalFileMove.v1\"",token+QByteArray(4,'\0'));
        auto point=[&](QTreeWidgetItem *item){return view->viewport()->mapTo(&w,view->visualItemRect(item).center());};
        const auto enter=QWindowSystemInterface::handleDrag(w.windowHandle(),&mime,point(source),Qt::CopyAction|Qt::MoveAction,Qt::LeftButton,{});
        const auto sourceTarget=view->internalMoveTarget(view->visualItemRect(source).center());
        if(enter.isAccepted()!=w.internalDragNoop(token,sourceTarget.path))return false; // Parent and directory-self no-ops are neutral.
        const auto hover=QWindowSystemInterface::handleDrag(w.windowHandle(),&mime,point(target),Qt::CopyAction|Qt::MoveAction,Qt::LeftButton,{});
        if(!hover.isAccepted() || hover.acceptedAction()!=Qt::MoveAction)return false;
        const auto drop=QWindowSystemInterface::handleDrop(w.windowHandle(),&mime,point(target),Qt::CopyAction|Qt::MoveAction,Qt::LeftButton,{});
        return drop.isAccepted() && drop.acceptedAction()==Qt::MoveAction;
    }
    QString fixtureRoot(){return qEnvironmentVariable("RABBITSHELL_UPLOAD_ROOT");}
    Profile remoteProfile(){Profile p;p.id="private-upload";p.host="127.0.0.1";p.user="fixture";p.port=qEnvironmentVariableIntValue("RABBITSHELL_UPLOAD_PORT");p.timeout=3;return p;}
    void connectWorker(Session &s) {
        connect(&s,&Session::hostKey,&s,[&s](const QString &,bool changed){if(!changed)s.trust(true);});
        s.start();
    }
    bool run(Session &s,Job job,QVariantMap *result=nullptr,QString *error=nullptr) {
        QSignalSpy done(&s,&Session::completed),failed(&s,&Session::failed);s.submit(job);
        QElapsedTimer clock;clock.start();while(done.isEmpty() && failed.isEmpty() && clock.elapsed()<10000)QTest::qWait(10);
        if(!done.isEmpty()){if(result)*result=done[0][2].toMap();return true;}
        if(error)*error=failed.isEmpty()?"timeout":failed[0][2].toString();return false;
    }
    QByteArray read(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
    Job upload(const QString &name,const QString &path,QString batch={}) {
        Job j;j.id=id++;j.type="upload";j.path=path;j.destination=name;j.collision="ask";j.options["uploadBatch"]=batch;return j;
    }
  private slots:
    void initTestCase(){
        if(qEnvironmentVariableIsSet("RABBITSHELL_HIDDEN_WINDOWS")){
            installWindowStyle();qApp->installEventFilter(&hiddenWindows);
        }
        QVERIFY(data.isValid());qputenv("RABBITSHELL_DATA_DIR",data.path().toUtf8());QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellUploadTests");QCoreApplication::setApplicationName("Isolated");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,data.path());
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication::setStyle("Fusion");
        for(auto f:{"C:/Windows/Fonts/msyh.ttc","C:/Windows/Fonts/consola.ttf"})if(QFileInfo::exists(f))QFontDatabase::addApplicationFont(f);
        QApplication::setFont(QFont("Microsoft YaHei UI",10));applyTheme(false);
        DiagnosticLog::instance().startLogging();
    }
    void cleanupTestCase(){
        DiagnosticLog::instance().stopLogging();
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR");
        if(!report.isEmpty())for(const auto &name:QDir(DiagnosticLog::directory()).entryList({"*.jsonl"},QDir::Files)){
            const auto target=report+"/fixture-"+name;QFile::remove(target);QFile::copy(QDir(DiagnosticLog::directory()).filePath(name),target);
        }
    }
    void parentDirectoryPreview_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void parentDirectoryPreview(){
        QFETCH(bool,light);applyTheme(light);Workspace w(Profile{}, {},true);setup(w);
        list(w,"/root",{entry("/root/bin",true),entry("/root/root-file.txt")});
        auto parent=w.fileItem("/root/bin");parent->setExpanded(true);
        list(w,"/root/bin",{entry("/root/bin/deep",true),entry("/root/bin/hovered-file.txt"),entry("/root/bin/z-hidden")});
        auto deep=w.fileItem("/root/bin/deep");deep->setExpanded(true);list(w,"/root/bin/deep",{entry("/root/bin/deep/leaf.txt")});
        w.fileItem("/root/bin/z-hidden")->setHidden(true);
        auto file=w.fileItem("/root/bin/hovered-file.txt"),rootFile=w.fileItem("/root/root-file.txt");
        auto view=w.files;view->setCurrentItem(file);file->setSelected(true);
        QMimeData mime;mime.setUrls({QUrl::fromLocalFile(local("preview.txt"))});
        auto hover=[&](QTreeWidgetItem *item){
            const auto point=view->visualItemRect(item).center();
            QDragEnterEvent enter(point,Qt::CopyAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(view->viewport(),&enter);QVERIFY(enter.isAccepted());
            QDragMoveEvent move(point,Qt::CopyAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(view->viewport(),&move);QVERIFY(move.isAccepted());
        };
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path());
        hover(file);QCOMPARE(view->itemFromIndex(view->uploadRow),parent);QVERIFY(view->itemFromIndex(view->uploadRow)!=file);
        const auto block=view->uploadTargetRect();
        QCOMPARE(block.top(),qMax(1,view->visualItemRect(parent).top()));QCOMPARE(block.bottom(),view->visualItemRect(file).bottom());
        QVERIFY(block.contains(view->visualItemRect(w.fileItem("/root/bin/deep/leaf.txt")).center()));
        QVERIFY(!block.contains(view->visualItemRect(rootFile).center())); // excludes next sibling
        QCOMPARE(view->uploadMessage,QString("上传到 /root/bin"));QVERIFY(!w.breadcrumbs->property("uploadTarget").toBool());QVERIFY(file->isSelected());
        QVERIFY(w.grab().save(report+QString("/file-to-parent-%1.png").arg(light?"light":"dark")));
        parent->setExpanded(false);QVERIFY(!view->uploadPreview);hover(parent);
        QCOMPARE(view->uploadTargetRect().top(),qMax(1,view->visualItemRect(parent).top()));
        QCOMPARE(view->uploadTargetRect().bottom(),view->visualItemRect(parent).bottom());
        parent->setExpanded(true);QVERIFY(!view->uploadPreview);
        hover(rootFile);QVERIFY(!view->uploadRow.isValid());QVERIFY(w.breadcrumbs->property("uploadTarget").toBool());QVERIFY(w.pathEdit->property("uploadTarget").toBool());
        QCOMPARE(view->uploadTargetRect(),view->viewport()->rect().adjusted(1,1,-1,-1));
        QCOMPARE(view->uploadMessage,QString("上传到 /root"));QVERIFY(file->isSelected());
        QVERIFY(w.grab().save(report+QString("/root-file-to-path-%1.png").arg(light?"light":"dark")));
        QDragLeaveEvent leave;QApplication::sendEvent(view->viewport(),&leave);QVERIFY(!w.breadcrumbs->property("uploadTarget").toBool());
        hover(rootFile);list(w,"/root",{entry("/root/bin",true),entry("/root/root-file.txt",false,false,100)});
        QVERIFY(!view->uploadPreview);QVERIFY(!w.breadcrumbs->property("uploadTarget").toBool());QVERIFY(!w.pathEdit->property("uploadTarget").toBool());
        hover(rootFile);rootFile->setText(0,"changed");QVERIFY(!view->uploadPreview);QVERIFY(!w.pathEdit->property("uploadTarget").toBool());
        hover(file);delete parent;QVERIFY(!view->uploadPreview);QVERIFY(!w.breadcrumbs->property("uploadTarget").toBool());
        hover(rootFile);view->hide();QVERIFY(!w.pathEdit->property("uploadTarget").toBool());
        view->show();list(w,"/root",{entry("/root/bin",true),entry("/root/root-file.txt")});parent=w.fileItem("/root/bin");parent->setExpanded(true);
        QVariantList many;for(int n=0;n<100;++n)many<<entry(QString("/root/bin/file-%1").arg(n,3,10,QChar('0')));
        list(w,"/root/bin",many);file=w.fileItem("/root/bin/file-050");view->scrollToItem(file,QAbstractItemView::PositionAtTop);settle();hover(file);
        QCOMPARE(view->itemFromIndex(view->uploadRow),parent);QCOMPARE(view->uploadTargetRect().top(),1);
        QVERIFY(view->viewport()->rect().contains(view->uploadTargetRect()));
    }
    void targetsPreviewAndQueue_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void targetsPreviewAndQueue(){
        QFETCH(bool,light);applyTheme(light);Workspace w(Profile{}, {},true);setup(w);
        list(w,"/root",{entry("/root/A",true),entry("/root/B",true),entry("/root/link",true,true),entry("/root/file.txt")});
        auto a=w.fileItem("/root/A");a->setExpanded(true);list(w,"/root/A",{entry("/root/A/deep.txt")});
        auto file=w.fileItem("/root/A/deep.txt");auto view=w.files;const auto source=local("drag-source.txt");
        a->setSelected(true);const auto selection=view->selectedItems();
        const auto row=view->visualItemRect(a);
        const int sizeX=view->header()->sectionViewportPosition(1)+view->columnWidth(1)/2;
        // Qt itemAt normally resolves the size column too. This is evidence,
        // not an assumption that Qt's second-column hit test is broken.
        QCOMPARE(view->itemAt(QPoint(sizeX,row.center().y())),a);
        for(auto point:{QPoint(2,row.center().y()),QPoint(50,row.center().y()),QPoint(sizeX,row.center().y())}) {
            QCOMPARE(view->uploadTarget(point).path,QString("/root/A"));QVERIFY(drop(view,point,source));
            QCOMPARE(w.transfers[w.transferOrder.last()].path,QString("/root/A/drag-source.txt"));
        }
        QVERIFY(drop(view,view->visualItemRect(file).center(),source));QCOMPARE(w.transfers[w.transferOrder.last()].path,QString("/root/A/drag-source.txt"));
        const QPoint blank(12,view->viewport()->height()-50);QVERIFY(drop(view,blank,source));QCOMPARE(w.transfers[w.transferOrder.last()].path,QString("/root/drag-source.txt"));
        for(const auto &task:w.transfers){QCOMPARE(task.collision,QString("ask"));QVERIFY(!task.options["uploadBatch"].toString().isEmpty());}
        QCOMPARE(view->selectedItems(),selection);QVERIFY(!w.uploadConflictDialog);
        QMimeData mime;mime.setUrls({QUrl::fromLocalFile(source)});const auto point=view->visualItemRect(w.fileItem("/root/B")).center();
        QDragEnterEvent enter(point,Qt::CopyAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(view->viewport(),&enter);QVERIFY(enter.isAccepted());QVERIFY(view->uploadPreview);QCOMPARE(view->uploadMessage,QString("上传到 /root/B"));
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path());QVERIFY(w.grab().save(report+QString("/upload-preview-%1.png").arg(light?"light":"dark")));
        delete w.fileItem("/root/B");QVERIFY(!view->uploadPreview); // no stale path/root-frame fallback
        QDragLeaveEvent leave;QApplication::sendEvent(view->viewport(),&leave);QVERIFY(!view->uploadPreview);
        const auto link=view->visualItemRect(w.fileItem("/root/link")).center();QVERIFY(!view->uploadTarget(link).valid());QVERIFY(!drop(view,link,source));
        auto draft=new QTreeWidgetItem(view,{"draft"});draft->setData(0,FileDraftRole,true);QVERIFY(!view->uploadTarget(view->visualItemRect(draft).center()).valid());
        QMimeData remote;remote.setUrls({QUrl("sftp://host/fake")});QVERIFY(!view->previewUpload(blank,&remote,Qt::CopyAction));
        QMimeData missing;missing.setUrls({QUrl::fromLocalFile(data.filePath("absent"))});QVERIFY(!view->previewUpload(blank,&missing,Qt::CopyAction));
        QVERIFY(!view->previewUpload(blank,&mime,Qt::MoveAction));view->clearUploadPreview();
        QVERIFY(view->previewUpload(blank,&mime,Qt::CopyAction));QEvent exit(QEvent::Leave);QApplication::sendEvent(view->viewport(),&exit);QVERIFY(!view->uploadPreview);
        QVERIFY(view->previewUpload(blank,&mime,Qt::CopyAction));view->hide();QVERIFY(!view->uploadPreview);
    }
    void internalMovePreviewConfirmationAndLifetime_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void internalMoveWindowEntrySequence_data() {
        QTest::addColumn<QString>("start");QTest::addColumn<QString>("destination");QTest::addColumn<bool>("bridge");
        for(auto start:{"source","blank","link","draft"})QTest::newRow(start)<<QString(start)<<QString("/root/B")<<false;
        QTest::newRow("source-subdirectory")<<QString("source")<<QString("/root/A/sub")<<false;
        QTest::newRow("source-deep-windows-bridge")<<QString("source")<<QString("/root/A/sub/deep")<<true;
    }
    void internalMoveWindowEntrySequence() {
        QFETCH(QString,start);QFETCH(QString,destination);QFETCH(bool,bridge);Workspace w(Profile{}, {},true);setup(w);w.authenticatedFingerprint="SHA256:test";
        auto source=entry(start=="blank"?"/root/file.txt":"/root/A/file.txt");source["permissions"]=0100644;
        list(w,"/root",{entry("/root/A",true),entry("/root/B",true),entry("/root/link",true,true),entry("/root/file.txt")});
        auto a=w.fileItem("/root/A");a->setExpanded(true);list(w,"/root/A",{source,entry("/root/A/sub",true)});
        auto sub=w.fileItem("/root/A/sub");sub->setExpanded(true);list(w,"/root/A/sub",{entry("/root/A/sub/deep",true)});
        auto draft=new QTreeWidgetItem(w.files,{"draft"});draft->setData(0,FileDraftRole,true);settle();
        auto view=w.files;const auto token=w.registerInternalDrag({source});QVERIFY(!token.isEmpty());
        auto cleanup=qScopeGuard([&]{QWindowSystemInterface::handleDrag(w.windowHandle(),nullptr,{},Qt::IgnoreAction,{},{});w.unregisterInternalDrag(token);});
        QMimeData mime;mime.setData(bridge?QStringLiteral("application/x-qt-windows-mime;value=\"RabbitShell.InternalFileMove.v1\""):QString::fromLatin1(internalFileMoveMimeType()),bridge?token+QByteArray(4,'\0'):token);
        const QPoint blank(10,view->viewport()->height()-45);QVERIFY(!view->rowAt(blank));
        auto first=start=="blank"?blank:view->visualItemRect(start=="source"?w.fileItem("/root/A/file.txt"):start=="link"?w.fileItem("/root/link"):draft).center();
        auto route=[&](QPoint p){return QWindowSystemInterface::handleDrag(w.windowHandle(),&mime,view->viewport()->mapTo(&w,p),Qt::CopyAction|Qt::MoveAction,Qt::LeftButton,Qt::NoModifier);};
        const auto initial=route(first);
        qInfo()<<"entry"<<start<<initial.isAccepted()<<initial.acceptedAction();
        // A file's own parent is now a neutral no-op. Link/draft entry rows
        // remain invalid while the trusted source still establishes routing.
        const bool noop=start=="source" || start=="blank";
        QCOMPARE(initial.isAccepted(),noop);QVERIFY(!view->uploadPreview || !view->uploadAllowed);
        const auto target=view->visualItemRect(w.fileItem(destination)).center();
        const auto moved=route(target);qInfo()<<"then valid directory"<<moved.isAccepted()<<moved.acceptedAction()<<view->uploadMessage;
        QVERIFY2(moved.isAccepted(),"Qt routing lost the valid source at its invalid entry point");QCOMPARE(moved.acceptedAction(),Qt::MoveAction);
        QCOMPARE(view->uploadMessage,QString("移动到 ")+destination);
        QSignalSpy dropped(view,&FileView::internalMoveDropped);
        const auto result=QWindowSystemInterface::handleDrop(w.windowHandle(),&mime,view->viewport()->mapTo(&w,target),Qt::MoveAction,Qt::LeftButton,Qt::NoModifier);
        QVERIFY(result.isAccepted());QCOMPARE(result.acceptedAction(),Qt::MoveAction);QCOMPARE(dropped.size(),1);
        settle();auto dialog=w.findChild<QMessageBox *>();QVERIFY(dialog);dialog->reject();settle();QVERIFY(w.transferQueue.isEmpty());
    }
    void internalMoveSourceGuards() {
        Workspace w(Profile{}, {},true);setup(w);w.authenticatedFingerprint="SHA256:test";
        auto source=entry("/root/file.txt");source["permissions"]=0100644;
        list(w,"/root",{source,entry("/root/B",true)});const auto token=w.registerInternalDrag({source});QVERIFY(!token.isEmpty());
        auto cleanup=qScopeGuard([&]{w.unregisterInternalDrag(token);});
        auto rejected=[&](Workspace &target,const QByteArray &value) {
            QMimeData mime;mime.setData(internalFileMoveMimeType(),value);QSignalSpy drops(target.files,&FileView::internalMoveDropped);
            const auto point=target.files->viewport()->mapTo(&target,QPoint(20,10));
            const auto response=QWindowSystemInterface::handleDrag(target.windowHandle(),&mime,point,Qt::MoveAction,Qt::LeftButton,{});
            QVERIFY(!response.isAccepted());
            const auto drop=QWindowSystemInterface::handleDrop(target.windowHandle(),&mime,point,Qt::MoveAction,Qt::LeftButton,{});
            QVERIFY(!drop.isAccepted());QCOMPARE(drops.size(),0);
            QWindowSystemInterface::handleDrag(target.windowHandle(),nullptr,{},Qt::IgnoreAction,{},{});
        };
        rejected(w,"not-a-token");rejected(w,QUuid::createUuid().toByteArray(QUuid::WithoutBraces));
        Profile other;other.id="other-server";Workspace foreign(other,{},true);setup(foreign);foreign.authenticatedFingerprint="SHA256:other";rejected(foreign,token);
        ++w.credentialGeneration;rejected(w,token);--w.credentialGeneration;
        w.online=false;rejected(w,token);w.online=true;
        w.unregisterInternalDrag(token);rejected(w,token);
    }
    void internalMoveNestedScreenshots_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void internalMoveNestedScreenshots() {
        QFETCH(bool,light);applyTheme(light);Workspace w(Profile{}, {},true);setup(w);w.authenticatedFingerprint="SHA256:test";
        w.directory="/root/codexer";w.updateBreadcrumbs();
        auto source=entry("/root/codexer/install-fix.sh");source["permissions"]=0100755;
        list(w,w.directory,{source,entry("/root/codexer/codexer",true),entry("/root/codexer/README.md")});
        auto inner=w.fileItem("/root/codexer/codexer");inner->setExpanded(true);
        list(w,"/root/codexer/codexer",{entry("/root/codexer/codexer/docs",true),entry("/root/codexer/codexer/infra",true)});
        auto view=w.files;auto file=w.fileItem(source["path"].toString());view->setCurrentItem(file);file->setSelected(true);
        const auto token=w.registerInternalDrag({source});QVERIFY(!token.isEmpty());
        auto cleanup=qScopeGuard([&]{QWindowSystemInterface::handleDrag(w.windowHandle(),nullptr,{},Qt::IgnoreAction,{},{});w.unregisterInternalDrag(token);});
        QMimeData mime;mime.setData("application/x-qt-windows-mime;value=\"RabbitShell.InternalFileMove.v1\"",token+QByteArray(4,'\0'));
        auto route=[&](QPoint p){return QWindowSystemInterface::handleDrag(w.windowHandle(),&mime,view->viewport()->mapTo(&w,p),Qt::MoveAction,Qt::LeftButton,{});};
        QVERIFY(route(view->visualItemRect(file).center()).isAccepted());QVERIFY(!view->uploadPreview);
        auto destination=w.fileItem("/root/codexer/codexer/infra");const auto point=view->visualItemRect(destination).center();QVERIFY(route(point).isAccepted());
        QCOMPARE(view->itemFromIndex(view->uploadRow),destination);QVERIFY(file->isSelected());
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path());
        QVERIFY(w.grab().save(report+QString("/internal-move-nested-%1.png").arg(light?"light":"dark")));
        const auto nav=view->topLevelItem(0);QVERIFY(nav->data(0,ParentNavigationRole).toBool());const auto parent=view->visualItemRect(nav).center();
        QVERIFY(route(parent).isAccepted());QCOMPARE(view->uploadMessage,QString("移动到 /root"));QCOMPARE(view->itemFromIndex(view->uploadRow),nav);
        QVERIFY(w.grab().save(report+QString("/internal-move-parent-%1.png").arg(light?"light":"dark")));
        const auto response=QWindowSystemInterface::handleDrop(w.windowHandle(),&mime,view->viewport()->mapTo(&w,parent),Qt::MoveAction,Qt::LeftButton,{});QVERIFY(response.isAccepted());
        settle();auto dialog=w.findChild<QMessageBox *>();QVERIFY(dialog);QCOMPARE(dialog->defaultButton()->text(),QString("取消"));
        QVERIFY(dialog->grab().save(report+QString("/internal-move-parent-confirm-%1.png").arg(light?"light":"dark")));dialog->reject();settle();QVERIFY(w.transferQueue.isEmpty());
    }
    void internalMoveParentNavigationTarget() {
        Workspace w(Profile{}, {},true);setup(w);w.authenticatedFingerprint="SHA256:test";
        w.directory="/root/A";w.updateBreadcrumbs();auto source=entry("/root/A/file.txt");source["permissions"]=0100644;
        list(w,"/root/A",{source,entry("/root/A/sub",true)});
        auto view=w.files;auto nav=view->topLevelItem(0);QVERIFY(nav->data(0,ParentNavigationRole).toBool());
        const auto token=w.registerInternalDrag({source});auto cleanup=qScopeGuard([&]{w.unregisterInternalDrag(token);});
        QMimeData mime;mime.setData(internalFileMoveMimeType(),token);const auto point=view->visualItemRect(nav).center();
        QVERIFY(!view->uploadTarget(point).valid());
        QVERIFY2(view->previewInternalMove(point,&mime,Qt::MoveAction),"Internal parent target incorrectly follows upload navigation rejection");
        QCOMPARE(view->uploadMessage,QString("移动到 /root"));QVERIFY(!nav->data(0,Qt::UserRole).isValid());
        QDragEnterEvent enter(point,Qt::MoveAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(view->viewport(),&enter);QVERIFY(enter.isAccepted());
        QSignalSpy dropped(view,&FileView::internalMoveDropped);QDropEvent drop(point,Qt::MoveAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(view->viewport(),&drop);
        QVERIFY(drop.isAccepted());QCOMPARE(dropped.size(),1);QCOMPARE(dropped[0][1].toString(),QString("/root"));
        settle();auto dialog=w.findChild<QMessageBox *>();QVERIFY(dialog);QVERIFY(dialog->text().contains("/root"));dialog->reject();settle();QVERIFY(w.transferQueue.isEmpty());
    }
    void internalMovePreviewConfirmationAndLifetime(){
        QFETCH(bool,light);applyTheme(light);Workspace w(Profile{}, {},true);setup(w);w.authenticatedFingerprint="SHA256:test";
        auto source=entry("/root/A",true);source["permissions"]=0040755;
        auto target=entry("/root/B",true);target["permissions"]=0040755;
        auto child=entry("/root/B/file.txt");child["permissions"]=0100644;
        list(w,"/root",{source,target});auto b=w.fileItem("/root/B");b->setExpanded(true);list(w,"/root/B",{child});
        const auto token=w.registerInternalDrag({source});QVERIFY(isValidInternalFileMoveToken(token));
        QVERIFY(!w.validInternalDrag(token,"/root/A/sub"));QVERIFY(w.validInternalDrag(token,"/root"));QVERIFY(w.internalDragNoop(token,"/root"));QVERIFY(w.validInternalDrag(token,"/root/B"));
        QMimeData mime;mime.setData(internalFileMoveMimeType(),token);auto view=w.files;
        const auto point=view->visualItemRect(w.fileItem("/root/B/file.txt")).center();
        QDragEnterEvent enter(point,Qt::CopyAction|Qt::MoveAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(view->viewport(),&enter);QVERIFY(enter.isAccepted());QCOMPARE(enter.dropAction(),Qt::MoveAction);
        QDragMoveEvent hover(point,Qt::CopyAction|Qt::MoveAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(view->viewport(),&hover);QVERIFY(hover.isAccepted());
        QCOMPARE(view->itemFromIndex(view->uploadRow),b);QVERIFY(!view->uploadTargetRect().contains(point));
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path());QVERIFY(w.grab().save(report+QString("/internal-move-hover-%1.png").arg(light?"light":"dark")));
        QDropEvent dropEvent(point,Qt::CopyAction|Qt::MoveAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(view->viewport(),&dropEvent);QVERIFY(dropEvent.isAccepted());QCOMPARE(dropEvent.dropAction(),Qt::MoveAction);
        w.unregisterInternalDrag(token);settle();auto dialog=w.findChild<QMessageBox *>();QVERIFY(dialog);QVERIFY(dialog->text().contains("/root/B"));
        QVERIFY(dialog->grab().save(report+QString("/internal-move-confirm-%1.png").arg(light?"light":"dark")));
        QCOMPARE(dialog->defaultButton()->text(),QString("取消"));dialog->reject();settle();QVERIFY(w.transferQueue.isEmpty());
        const auto accepted=w.registerInternalDrag({source});mime.setData(internalFileMoveMimeType(),accepted);
        QDragEnterEvent secondEnter(point,Qt::MoveAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(view->viewport(),&secondEnter);QVERIFY(secondEnter.isAccepted());
        QDropEvent second(point,Qt::MoveAction,&mime,Qt::LeftButton,{});QApplication::sendEvent(view->viewport(),&second);QVERIFY(second.isAccepted());
        w.unregisterInternalDrag(accepted);settle();dialog=w.findChild<QMessageBox *>();QVERIFY(dialog);
        for(auto button:dialog->buttons())if(button->text()=="移动")button->click();settle();
        QCOMPARE(w.transferQueue.size(),1);const auto job=w.transfers[w.transferQueue.head()];QCOMPARE(job.type,QString("move"));QCOMPARE(job.path,QString("/root/A"));QCOMPARE(job.destination,QString("/root/B/A"));
        QSettings().setValue("files/confirmInternalMove",false);w.resetLocalPreferences();QVERIFY(QSettings().value("files/confirmInternalMove",true).toBool());
    }
    void internalMoveNoopAndMixedRoots_data(){
        QTest::addColumn<bool>("light");QTest::addColumn<bool>("bridge");
        for(bool light:{false,true})for(bool bridge:{false,true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(light?"light":"dark",bridge?"bridge":"qt")))<<light<<bridge;
    }
    void internalMoveNoopAndMixedRoots(){
        QFETCH(bool,light);QFETCH(bool,bridge);applyTheme(light);Workspace w(Profile{}, {},true);setup(w);w.authenticatedFingerprint="SHA256:test";
        auto file=entry("/root/A/file.txt");file["permissions"]=0100644;
        auto folder=entry("/root/A/folder",true);folder["permissions"]=0040755;
        auto other=entry("/root/other.txt");other["permissions"]=0100644;
        list(w,"/root",{entry("/root/A",true),entry("/root/B",true),other});auto a=w.fileItem("/root/A");a->setExpanded(true);list(w,"/root/A",{file,folder});
        w.fileItem("/root/A/folder")->setExpanded(true);list(w,"/root/A/folder",{entry("/root/A/folder/sub",true)});
        const auto token=w.registerInternalDrag({file,folder});auto cleanup=qScopeGuard([&]{QWindowSystemInterface::handleDrag(w.windowHandle(),nullptr,{},Qt::IgnoreAction,{},{});w.unregisterInternalDrag(token);});
        auto view=w.files;QMimeData mime;
        auto useToken=[&](const QByteArray &value){mime.setData(bridge?QStringLiteral("application/x-qt-windows-mime;value=\"RabbitShell.InternalFileMove.v1\""):QString::fromLatin1(internalFileMoveMimeType()),bridge?value+QByteArray(4,'\0'):value);};
        useToken(token);
        auto point=[&](QTreeWidgetItem *item){return view->viewport()->mapTo(&w,view->visualItemRect(item).center());};
        auto route=[&](QTreeWidgetItem *item){return QWindowSystemInterface::handleDrag(w.windowHandle(),&mime,point(item),Qt::MoveAction,Qt::LeftButton,{});};
        QVERIFY(route(w.fileItem(file["path"].toString())).isAccepted());QVERIFY(!view->uploadPreview);
        QVERIFY(route(w.fileItem("/root/B")).isAccepted());QVERIFY(view->uploadPreview);
        QVERIFY(route(a).isAccepted());QVERIFY(!view->uploadPreview);QVERIFY(view->uploadMessage.isEmpty());
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path());
        const auto next=w.nextJob;QSignalSpy business(view,&FileView::internalMoveDropped);
        const auto dropped=QWindowSystemInterface::handleDrop(w.windowHandle(),&mime,point(a),Qt::MoveAction,Qt::LeftButton,{});
        QVERIFY(dropped.isAccepted());QCOMPARE(dropped.acceptedAction(),Qt::MoveAction);QCOMPARE(business.size(),0);settle();
        QVERIFY(!w.findChild<QMessageBox *>());QCOMPARE(w.nextJob,next);QVERIFY(w.transferQueue.isEmpty());QVERIFY(w.transfers.isEmpty());QVERIFY(jobs(w).isEmpty());
        QVERIFY(!w.validInternalDrag(token,"/root/A/folder/sub"));
        // A directory's own row is also a neutral entry/drop. Moving back from
        // another valid directory must clear the entire preview, not just text.
        const auto directory=w.registerInternalDrag({folder});auto cleanupDirectory=qScopeGuard([&]{w.unregisterInternalDrag(directory);});useToken(directory);
        auto self=w.fileItem("/root/A/folder");view->setCurrentItem(self);self->setSelected(true);
        const auto enteredSelf=route(self);QVERIFY(enteredSelf.isAccepted());QCOMPARE(enteredSelf.acceptedAction(),Qt::MoveAction);
        QVERIFY(!view->uploadPreview);QVERIFY(view->uploadMessage.isEmpty());
        QVERIFY(route(w.fileItem("/root/B")).isAccepted());QVERIFY(view->uploadPreview);
        const auto returnedSelf=route(self);QVERIFY(returnedSelf.isAccepted());QCOMPARE(returnedSelf.acceptedAction(),Qt::MoveAction);
        QVERIFY(!view->uploadPreview);QVERIFY(!view->uploadRow.isValid());QVERIFY(view->uploadMessage.isEmpty());QVERIFY(!QToolTip::isVisible());
        QVERIFY(!w.breadcrumbs->property("uploadTarget").toBool());QVERIFY(!w.pathEdit->property("uploadTarget").toBool());
        const auto descendant=route(w.fileItem("/root/A/folder/sub"));QVERIFY(!descendant.isAccepted());QVERIFY(view->uploadPreview && !view->uploadAllowed);
        QCOMPARE(view->uploadMessage,QString("不能移动到此目标"));
        QVERIFY(route(self).isAccepted());QVERIFY(!view->uploadPreview);QVERIFY(view->uploadMessage.isEmpty());
        QVERIFY(view->grab().save(report+QString("/directory-self-hover-%1-%2.png").arg(light?"light":"dark",bridge?"bridge":"qt")));
        const auto selfDrop=QWindowSystemInterface::handleDrop(w.windowHandle(),&mime,point(self),Qt::MoveAction,Qt::LeftButton,{});
        QVERIFY(selfDrop.isAccepted());QCOMPARE(selfDrop.acceptedAction(),Qt::MoveAction);QCOMPARE(business.size(),0);
        w.acceptInternalDrag(directory,"/root/A/folder");settle();QVERIFY(!w.findChild<QMessageBox *>());QCOMPARE(w.nextJob,next);
        QVERIFY(w.transferQueue.isEmpty());QVERIFY(w.transfers.isEmpty());QVERIFY(jobs(w).isEmpty());
        for(const auto &path:{"/root/A/folder/sub","/root/A/folder/","/root/A/folder/.","/root/A/folder/sub/..","/root//A/folder"})QVERIFY(!w.validInternalDrag(directory,path));
        // Dropping mixed roots onto any selected directory itself keeps the
        // whole gesture inert, rather than moving the other root into it.
        const auto mixedSelf=w.registerInternalDrag({folder,entry("/root/B",true)});auto cleanupSelf=qScopeGuard([&]{w.unregisterInternalDrag(mixedSelf);});useToken(mixedSelf);
        QVERIFY(w.internalDragNoop(mixedSelf,"/root/A/folder"));QVERIFY(route(self).isAccepted());QVERIFY(!view->uploadPreview);
        QVERIFY(QWindowSystemInterface::handleDrop(w.windowHandle(),&mime,point(self),Qt::MoveAction,Qt::LeftButton,{}).isAccepted());
        w.acceptInternalDrag(mixedSelf,"/root/A/folder");settle();QCOMPARE(business.size(),0);QCOMPARE(w.nextJob,next);
        QVERIFY(!w.findChild<QMessageBox *>());QVERIFY(w.transferQueue.isEmpty());QVERIFY(w.transfers.isEmpty());QVERIFY(jobs(w).isEmpty());
        const auto mixed=w.registerInternalDrag({file,other});auto cleanupMixed=qScopeGuard([&]{w.unregisterInternalDrag(mixed);});useToken(mixed);
        QVERIFY(route(a).isAccepted());QVERIFY(!w.internalDragNoop(mixed,"/root/A"));
        QVERIFY(QWindowSystemInterface::handleDrop(w.windowHandle(),&mime,point(a),Qt::MoveAction,Qt::LeftButton,{}).isAccepted());settle();
        auto dialog=w.findChild<QMessageBox *>();QVERIFY(dialog);QVERIFY(dialog->text().contains("other.txt"));QVERIFY(!dialog->text().contains("2 项"));
        for(auto button:dialog->buttons())if(button->text()=="移动")button->click();settle();
        QCOMPARE(w.transferQueue.size(),1);const auto job=w.transfers[w.transferQueue.head()];QCOMPARE(job.path,QString("/root/other.txt"));QCOMPARE(job.destination,QString("/root/A/other.txt"));
    }
    void numberedMoveTargetsStayLocked(){
        Workspace w(Profile{}, {},true);setup(w);w.authenticatedFingerprint="SHA256:test";
        auto file=entry("/root/A/foo.txt");file["permissions"]=0100644;w.copyFileSelection(true,{file});
        w.pasteFiles("/root/B",FileClipboard::instance().current());QCOMPARE(w.pendingTransfers(),1);
        QVERIFY(w.clipboardPathLocked("/root/B/foo (1).txt"));QVERIFY(w.clipboardPathLocked("/root/B/foo (12).txt"));
        QVERIFY(w.clipboardMutationBlocked("/root/B/foo (12).txt"));QVERIFY(!w.clipboardPathLocked("/root/B/unrelated.txt"));
        QVERIFY(!w.clipboardPathLocked("/root/B/foo (1).log"));QVERIFY(!w.clipboardPathLocked("/root/B/foo (0).txt"));
    }
    void clipboardConflictWording_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void clipboardConflictWording(){
        QFETCH(bool,light);applyTheme(light);Workspace w(Profile{}, {},true);setup(w);
        auto active=upload(local("install.sh"),"/root/install.sh","copy-batch");active.options["clipboardPaste"]=true;
        w.activeTransfer=active.id;w.transfers[active.id]=active;auto worker=w.transferSession;
        worker->conflictWaiting=true;worker->conflictJob=active.id;worker->conflictNonce=31;
        w.showUploadConflict(worker,active.id,31,active.path,true);auto dialog=w.uploadConflictDialog;QVERIFY(dialog);
        QVERIFY(dialog->windowTitle().startsWith("复制文件"));QCOMPARE(dialog->defaultButton()->text(),QString("取消"));QVERIFY(dialog->checkBox());QVERIFY(w.taskSummary->text().startsWith("复制"));
        for(auto button:dialog->buttons())QVERIFY(!button->text().contains("上传"));
        QVERIFY(dialog->grab().save(qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path())+QString("/copy-conflict-%1.png").arg(light?"light":"dark")));
        QTest::keyClick(dialog,Qt::Key_Escape);QCOMPARE(worker->conflictAnswer,QString("cancel"));
    }
    void conflictPresentationAndCancellation_data(){
        QTest::addColumn<bool>("light");QTest::addColumn<int>("zoom");QTest::addColumn<bool>("longPath");
        QTest::newRow("dark")<<false<<0<<false;QTest::newRow("light")<<true<<0<<false;
        QTest::newRow("dark-long-zoom")<<false<<2<<true;QTest::newRow("light-long-zoom")<<true<<2<<true;
    }
    void conflictPresentationAndCancellation(){
        QFETCH(bool,light);QFETCH(int,zoom);QFETCH(bool,longPath);applyTheme(light);UiZoom::instance().setLevel(zoom);
        auto restore=qScopeGuard([]{UiZoom::instance().setLevel(0);});
        Profile profile;profile.name=longPath?QString(70,'S'):QString::fromUtf8("阿里云68轻量云");
        Workspace w(profile,{},true);setup(w);auto active=upload(local("conflict"),"/root/codexer/codexer","batch");
        w.activeTransfer=active.id;w.transfers[active.id]=active;auto worker=w.transferSession;
        worker->conflictWaiting=true;worker->conflictJob=active.id;worker->conflictNonce=301;
        const QString path=longPath?QString("/root/特殊&<文件>/")+QString(190,'x')+".txt":active.path;
        w.showUploadConflict(worker,active.id,301,path,true);auto dialog=w.uploadConflictDialog;QVERIFY(dialog);settle();
        auto label=dialog->findChild<QTextEdit *>("conflictPath");auto icon=dialog->findChild<QWidget *>("conflictIcon");
        auto details=dialog->findChild<QWidget *>("conflictDetails");QVERIFY(label && icon && details);
        QCOMPARE(label->toPlainText(),path);QCOMPARE(label->wordWrapMode(),QTextOption::WrapAnywhere);
        auto lineError=test::promptLineError(label->document());QVERIFY2(lineError.isEmpty(),qPrintable(lineError));QCOMPARE(label->font().pixelSize(),uiPx(14));
        QVERIFY(label->textInteractionFlags() & Qt::TextSelectableByMouse);
        QVERIFY2(label->height()>=label->heightForWidth(label->width()),"wrapped path is clipped");
        QVERIFY2(label->document()->documentLayout()->documentSize().height()<=label->viewport()->height()+1,"path end is outside text viewport");
        QCOMPARE(icon->size(),uiSize(40,40));
        const auto center=[&](QWidget *widget){return widget->mapTo(dialog,widget->rect().center()).y();};
        QVERIFY(qAbs(center(icon)-center(details))<=uiPx(2));
        auto buttons=dialog->buttons();QCOMPARE(buttons.size(),3);QVERIFY(buttons[0]->isEnabled());
        auto edgeError=test::promptBorderError(qobject_cast<QPushButton *>(buttons[0]),light);QVERIFY2(edgeError.isEmpty(),qPrintable(edgeError));
        QVERIFY(buttons[0]->height()>=uiPx(32));QVERIFY(buttons[0]->width()>=uiPx(84));
        QCOMPARE(buttons[0]->size(),buttons[1]->size());QCOMPARE(buttons[1]->size(),buttons[2]->size());
        QVERIFY(buttons[0]->x()<buttons[1]->x() && buttons[1]->x()<buttons[2]->x());
        QVERIFY(dialog->width()<=dialog->screen()->availableGeometry().width());
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path());
        QVERIFY(dialog->grab().save(report+QString("/conflict-%1-%2.png").arg(light?"light":"dark",longPath?"long-zoom":"normal")));
        QPointer<UploadConflictDialog> guard=dialog;QTest::keyClick(dialog,Qt::Key_Return);
        QCOMPARE(worker->conflictAnswer,QString("cancel"));settle();QVERIFY(guard.isNull());
    }
    void conflictMoveAndClose(){
        Workspace w(Profile{}, {},true);setup(w);Job active=upload(local("move"),"/root/file","batch");active.type="move";
        w.activeTransfer=active.id;w.transfers[active.id]=active;auto worker=w.transferSession;
        worker->conflictWaiting=true;worker->conflictJob=active.id;worker->conflictNonce=401;
        w.showUploadConflict(worker,active.id,401,active.path,true);auto dialog=w.uploadConflictDialog;QVERIFY(dialog);
        QVERIFY(dialog->windowTitle().startsWith("移动文件"));QVERIFY(dialog->checkBox()->isHidden());QVERIFY(!dialog->buttons().first()->isEnabled());
        dialog->close();QCOMPARE(worker->conflictAnswer,QString("cancel"));settle();QVERIFY(!w.uploadConflictDialog);
    }
    void conflictOverwriteAndLongScroll(){
        Workspace w(Profile{}, {},true);setup(w);Job active=upload(local("long-conflict"),"/root/file","batch");
        w.activeTransfer=active.id;w.transfers[active.id]=active;auto worker=w.transferSession;
        worker->conflictWaiting=true;worker->conflictJob=active.id;worker->conflictNonce=501;
        const QString path="/root/"+QString(2200,'x')+".txt";
        w.showUploadConflict(worker,active.id,501,path,true);auto dialog=w.uploadConflictDialog;QVERIFY(dialog);settle();
        auto text=dialog->findChild<QTextEdit *>("conflictPath");auto scroll=dialog->findChild<QScrollArea *>("conflictScroll");
        QVERIFY(text && scroll);QCOMPARE(text->toPlainText(),path);
        QVERIFY(text->document()->documentLayout()->documentSize().height()<=text->viewport()->height()+1);
        QVERIFY(scroll->verticalScrollBar()->maximum()>0);scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());settle();
        const auto last=text->mapTo(scroll->viewport(),QPoint(0,text->height()-1));
        QVERIFY2(last.y()<scroll->viewport()->height(),"last line must be reachable by outer scroll");
        QVERIFY(dialog->height()<=dialog->screen()->availableGeometry().height());
        dialog->buttons().first()->click();QCOMPARE(worker->conflictAnswer,QString("overwrite"));
    }
    void undoCreatedFilePreservesEditorDraft(){
        Workspace w(Profile{}, {},true);setup(w);w.authenticatedFingerprint="SHA256:test";
        w.openFile("/root/draft.txt");const auto requests=jobs(w);QVERIFY(!requests.isEmpty());const auto readJob=requests.last();QCOMPARE(readJob.type,QString("read"));
        w.session->completed("read",readJob.id,{{"path","/root/draft.txt"},{"data",QByteArray("original")},{"hash",QByteArray("old-hash")},{"permissions",0100644}});
        auto editor=w.editors.value("/root/draft.txt");QVERIFY(editor);editor->code->selectAll();editor->code->insertPlainText("unsaved");QVERIFY(editor->dirty());
        w.applyJournalUndo({{"old","/root/draft.txt"},{"path",QString()},{"kind","uploadNew"}});
        QCOMPARE(editor->path,QString("/root/draft.txt"));QVERIFY(editor->dirty());QCOMPARE(editor->bytes(),QByteArray("unsaved"));QVERIFY(editor->property("remoteRemovedByUndo").toBool());
        jobs(w);w.saveEditor(editor);QVERIFY(jobs(w).isEmpty());
        QCOMPARE(editor->path,QString("/root/draft.txt"));QVERIFY(editor->dirty());
    }
    void undoShortcutRespectsTextFocus(){
        Workspace w(Profile{}, {},true);setup(w);w.authenticatedFingerprint="SHA256:test";
        list(w,"/root",{entry("/root/a")});w.files->setFocus();settle();QVERIFY(w.files->hasFocus());
        QTest::keyClick(w.files,Qt::Key_Z,Qt::ControlModifier);auto requests=jobs(w);QCOMPARE(requests.size(),1);QCOMPARE(requests.first().type,QString("journalSnapshot"));
        w.session->completed("journalSnapshot",requests.first().id,{{"journal",QVariantMap{{"batches",QVariantList{}}}}});
        w.createEntry(false,"/root");QVERIFY(w.entryDraft.editor);w.entryDraft.editor->setFocus();w.entryDraft.editor->insert("abc");
        QTest::keyClick(w.entryDraft.editor,Qt::Key_Z,Qt::ControlModifier);QVERIFY(jobs(w).isEmpty());w.cancelEntryDraft();
        w.openFile("/root/a");requests=jobs(w);QVERIFY(!requests.isEmpty());const auto read=requests.last();
        w.session->completed("read",read.id,{{"path","/root/a"},{"data",QByteArray("base")},{"hash",QByteArray("h")},{"permissions",0100644}});
        auto editor=w.editors.value("/root/a");QVERIFY(editor);editor->code->setFocus();editor->code->insertPlainText("x");const auto changed=editor->bytes();
        QTest::keyClick(editor->code,Qt::Key_Z,Qt::ControlModifier);QVERIFY(editor->bytes()!=changed);QVERIFY(jobs(w).isEmpty());
    }
    void contextTargetSnapshot(){
        Workspace w(Profile{}, {},true);setup(w);list(w,"/root",{entry("/root/A",true),entry("/root/B",true)});
        auto a=w.fileItem("/root/A");a->setExpanded(true);list(w,"/root/A",{entry("/root/A/file.txt")});
        const auto source=local("menu-source.txt");bool opened=false,timedOut=false,scheduled=false;int uploads=0;
        QElapsedTimer deadline;deadline.start();
        QTimer tick;connect(&tick,&QTimer::timeout,&tick,[&]{
            if(auto dialog=qobject_cast<QFileDialog *>(QApplication::activeModalWidget())){
                if(!opened) {
                    opened=true;w.directory="/root/B";w.updateBreadcrumbs();w.files->clearSelection();
                    dialog->setDirectory(QFileInfo(source).absolutePath());
                    QTimer::singleShot(250,dialog,[dialog,source]{
                        dialog->selectFile(source);
                        auto edit=dialog->findChild<QLineEdit *>("fileNameEdit");
                        if(edit){edit->setText(QDir::toNativeSeparators(source));edit->setFocus();}
                        qInfo()<<"picker selection"<<dialog->selectedFiles();
                        auto buttons=dialog->findChild<QDialogButtonBox *>();
                        if(buttons && buttons->button(QDialogButtonBox::Open))buttons->button(QDialogButtonBox::Open)->click();
                        else static_cast<QDialog *>(dialog)->accept();
                    });
                }
                if(deadline.elapsed()>3000){timedOut=true;dialog->reject();}
            } else if(auto menu=qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                for(auto action:menu->actions())if(action->text()=="上传文件…" && !scheduled){scheduled=true;++uploads;QTimer::singleShot(0,menu,[action,menu]{action->trigger();menu->close();});break;}
            }
        });tick.start(10);
        w.fileMenu(w.files->visualItemRect(w.fileItem("/root/A/file.txt")).center());tick.stop();
        QVERIFY(!timedOut);QVERIFY(opened);QCOMPARE(uploads,1);QCOMPARE(w.transfers.size(),1);QCOMPARE(w.transfers.begin()->path,QString("/root/A/menu-source.txt"));
    }
    void accessibleSiblingNavigationMakesProgress(){
        Workspace w(Profile{}, {},true);setup(w);
        list(w,"/root",{entry("/root/A",true),entry("/root/B",true),entry("/root/README.md")});
        auto a=w.fileItem("/root/A");a->setExpanded(true);
        list(w,"/root/A",{entry("/root/A/first.md"),entry("/root/A/second.md")});
        settle();auto iface=QAccessible::queryAccessibleInterface(w.files);QVERIFY(iface);
        for(int i=0;i<iface->childCount();++i)QVERIFY(iface->child(i));
        w.files->sortItems(0,Qt::DescendingOrder);settle();
        for(int start=0;start<iface->childCount();++start){
            QSet<QAccessible::Id> seen;auto current=iface->child(start);int count=0;
            while(current && count++<100){
                const auto providerId=QAccessible::uniqueId(current);const int index=iface->indexOfChild(current);
                QVERIFY2(!seen.contains(providerId),qPrintable(QString("UIA NextSibling repeated provider id=%1 at current index=%2 start=%3").arg(providerId).arg(index).arg(start)));
                seen.insert(providerId);QAccessibleInterface *next=nullptr;
                for(int n=index+1;n<iface->childCount();++n)
                    if(auto child=iface->child(n);child && child->isValid() && !child->state().invisible){next=child;break;}
                current=next;
            }
            QVERIFY(count<100);
        }
    }
    void markdownClickAfterVisibleRowChanges(){
        Workspace w(Profile{}, {},true);setup(w);for(auto editor:w.editors)w.closePage(editor);
        list(w,"/root",{entry("/root/A",true),entry("/root/README.md")});
        auto a=w.fileItem("/root/A");a->setExpanded(true);list(w,"/root/A",{entry("/root/A/first.md")});
        auto iface=QAccessible::queryAccessibleInterface(w.files);QVERIFY(iface);
        auto check=[&]{for(int i=0;i<iface->childCount();++i){auto cell=iface->child(i);QVERIFY(cell && cell->isValid());QCOMPARE(iface->indexOfChild(cell),i);}};
        check();a->setExpanded(false);check();a->setExpanded(true);check();w.files->sortItems(0,Qt::DescendingOrder);settle();check();
        // Wait for Qt's public native row transition before selecting by its
        // final visual rectangle; reversal during motion is tested separately.
        QTest::qWait(400);
        auto markdown=w.fileItem("/root/README.md");w.files->scrollToItem(markdown);settle();jobs(w);
        const auto point=w.files->visualItemRect(markdown).center();QSignalSpy pressed(w.files,&QTreeWidget::itemPressed),clicked(w.files,&QTreeWidget::itemClicked);
        QTest::mouseClick(w.files->viewport(),Qt::LeftButton,{},point);settle();
        QCOMPARE(pressed.size(),1);QCOMPARE(clicked.size(),1);QCOMPARE(w.files->currentItem(),markdown);QVERIFY(markdown->isSelected());QVERIFY(jobs(w).isEmpty());
        QTest::mouseDClick(w.files->viewport(),Qt::LeftButton,{},point);settle();auto requests=jobs(w);
        auto read=std::find_if(requests.begin(),requests.end(),[](const Job &job){return job.type=="read";});QVERIFY(read!=requests.end());QCOMPARE(read->path,QString("/root/README.md"));
        const QByteArray body="# Private synthetic Markdown\n\n- content\n";
        w.session->completed("read",read->id,{{"path",read->path},{"data",body},{"hash",QByteArray("fixture-hash")},{"permissions",0644}});settle();
        auto editor=w.editors.value(read->path);QVERIFY(editor);QCOMPARE(editor->bytes(),body);w.closePage(editor);settle();QVERIFY(!w.editors.contains(read->path));check();
    }
    void accessibleOldIdsRetireAcrossChanges(){
        Workspace w(Profile{}, {},true);setup(w);
        list(w,"/root",{entry("/root/A",true),entry("/root/B",true),entry("/root/README.md")});
        auto a=w.fileItem("/root/A");a->setExpanded(true);list(w,"/root/A",{entry("/root/A/first.md")});
        auto iface=QAccessible::queryAccessibleInterface(w.files);QVERIFY(iface);QSet<QAccessible::Id> retired;
        for(int round=0;round<16;++round){
            settle();QSet<QAccessible::Id> live;
            for(int i=0;i<iface->childCount();++i){auto cell=iface->child(i);QVERIFY(cell && cell->isValid());
                QCOMPARE(iface->indexOfChild(cell),i);live.insert(QAccessible::uniqueId(cell));}
            QVERIFY(live.size()<=18);
            for(auto stale:retired)QVERIFY(!QAccessible::accessibleInterface(stale));
            a->setExpanded(!a->isExpanded());
            for(auto stale:live)QVERIFY(!QAccessible::accessibleInterface(stale));
            retired.unite(live);
        }
    }
    void accessibleCacheTracksVisibleRows_data(){
        QTest::addColumn<QString>("change");
        for(auto change:{"expand","collapse","sort","filter","refresh","delete"})QTest::newRow(change)<<QString(change);
    }
    void accessibleCacheTracksVisibleRows(){
        QFETCH(QString,change);QAccessible::setActive(true);
        auto restore=qScopeGuard([]{QAccessible::setActive(false);});
        Workspace w(Profile{}, {},true);setup(w);
#ifdef Q_OS_WIN
        if(QGuiApplication::platformName()=="windows"){
            DWORD_PTR result=0;SendMessageTimeoutW(reinterpret_cast<HWND>(w.winId()),WM_GETOBJECT,0,LPARAM(-25),SMTO_ABORTIFHUNG,1000,&result);
            QVERIFY(QAccessible::isActive());
        }
#endif
        list(w,"/root",{entry("/root/A",true),entry("/root/B",true),entry("/root/README.md")});
        auto a=w.fileItem("/root/A");a->setExpanded(true);
        list(w,"/root/A",{entry("/root/A/first.md"),entry("/root/A/second.md")});
        if(change=="expand")a->setExpanded(false);
        settle();auto accessible=QAccessible::queryAccessibleInterface(w.files);QVERIFY(accessible);
        auto verify=[&]{
            for(int i=0;i<accessible->childCount();++i){
                auto cell=accessible->child(i);QVERIFY(cell);QVERIFY(cell->isValid());
                QCOMPARE(accessible->indexOfChild(cell),i);
            }
        };
        verify(); // Prime the same logical-position cache used by the Windows UIA provider.
        if(change=="expand")a->setExpanded(true);
        if(change=="collapse")a->setExpanded(false);
        if(change=="sort")w.files->sortItems(0,Qt::DescendingOrder);
        if(change=="filter")w.fileFilter->setText("README");
        if(change=="refresh")list(w,"/root/A",{entry("/root/A/new.md"),entry("/root/A/second.md")});
        if(change=="delete")delete a;
        settle();verify();
    }
    void arrowClickBeforeList(){
        Workspace w(Profile{}, {},true);setup(w);
        for(auto editor:w.editors)w.closePage(editor);
        QVariantList roots;for(int n=0;n<17;++n)roots<<entry(QString("/root/entry-%1").arg(n,2,10,QChar('0')),true);
        roots<<entry("/root/project",true);list(w,"/root",roots);
        auto view=w.files;
        auto clickBranch=[&](QTreeWidgetItem *item){
            // Each click uses the settled visible geometry after the previous
            // expand or collapse animation.
            QTest::qWait(400);
            view->scrollToItem(item,QAbstractItemView::PositionAtBottom);settle();
            const auto row=view->visualItemRect(item);
            const QPoint point(row.left()-view->indentation()/2,row.center().y());
            QVERIFY(view->viewport()->rect().contains(point));
            QCOMPARE(view->itemAt(point),item);
            QTest::mouseClick(view->viewport(),Qt::LeftButton,{},point);settle();
        };
        auto project=w.fileItem("/root/project");clickBranch(project);QVERIFY(project->isExpanded());
        auto requests=jobs(w);QCOMPARE(requests.size(),1);QCOMPARE(requests[0].path,QString("/root/project"));
        QVariantList child;for(int n=0;n<6;++n)child<<entry(QString("/root/project/dir-%1").arg(n),true);
        child<<entry("/root/project/docs",true)<<entry("/root/project/infra",true);
        w.session->completed("list",requests[0].id,{{"path","/root/project"},{"entries",child}});settle();
        auto infra=w.fileItem("/root/project/infra"),docs=w.fileItem("/root/project/docs");
        clickBranch(infra);QVERIFY(infra->isExpanded());requests=jobs(w);QCOMPARE(requests.size(),1);
        w.session->completed("list",requests[0].id,{{"path","/root/project/infra"},{"entries",QVariantList{entry("/root/project/infra/a"),entry("/root/project/infra/b"),entry("/root/project/infra/c")}}});settle();
        // Next click is distinct from the completed three-entry render.
        clickBranch(docs);QVERIFY(docs->isExpanded());requests=jobs(w);QCOMPARE(requests.size(),1);QCOMPARE(requests[0].path,QString("/root/project/docs"));
        w.session->completed("list",requests[0].id,{{"path","/root/project/docs"},{"entries",QVariantList{entry("/root/project/docs/deep",true)}}});settle();
        auto deep=w.fileItem("/root/project/docs/deep");clickBranch(deep);QVERIFY(deep->isExpanded());requests=jobs(w);QCOMPARE(requests.size(),1);
        w.session->completed("list",requests[0].id,{{"path","/root/project/docs/deep"},{"entries",QVariantList{}}});settle();
        view->sortItems(1,Qt::DescendingOrder);settle();
        for(auto folder:{docs,infra,project}){clickBranch(folder);QVERIFY(!folder->isExpanded());clickBranch(folder);QVERIFY(folder->isExpanded());}
        QVERIFY(jobs(w).isEmpty());QTest::qWait(1200);
    }
    void delayedExpandRefreshAndTooltip(){
        QAccessible::setActive(true);
        Workspace w(Profile{}, {},true);setup(w);
        for(auto editor:w.editors)w.closePage(editor);
        QVariantList roots;for(int n=0;n<17;++n)roots<<entry(QString("/root/entry-%1").arg(n),n%2==0);
        roots<<entry("/root/project",true);list(w,"/root",roots);
        auto project=w.fileItem("/root/project");project->setExpanded(true);
        QVariantList child;for(int n=0;n<7;++n)child<<entry(QString("/root/project/item-%1").arg(n),n%2==0);
        child<<entry("/root/project/infra",true);list(w,"/root/project",child);
        auto infra=w.fileItem("/root/project/infra");infra->setExpanded(true);
        list(w,"/root/project/infra",{entry("/root/project/infra/Dockerfile"),entry("/root/project/infra/config.yml"),entry("/root/project/infra/script.sh")});
        w.files->scrollToItem(infra);w.files->setCurrentItem(infra);
        auto accessible=QAccessible::queryAccessibleInterface(w.files);QVERIFY(accessible);QVERIFY(accessible->childCount()<200);
        for(int n=0;n<accessible->childCount();++n)if(auto cell=accessible->child(n)){cell->text(QAccessible::Name);cell->rect();cell->state();}
        QTimer monitor;connect(&monitor,&QTimer::timeout,&w,[&]{
            w.monitor("__CPU__\ncpu 15 0 10 200 0 0 0 0\n__MEM__\nMemTotal: 8388608 kB\nMemAvailable: 4613734 kB\n__UP__\n2476800 0\n0.32 0.28 0.21\n__NET__\neth0: 10000 0 0 0 0 0 0 0 5000 0\n__DISK__\nFilesystem 1024-blocks Used Available Capacity Mounted\n/dev/sda1 167772160 88080384 79691776 53% /\n__PROC__\nPID USER %CPU RSS STAT COMMAND\n100 fixture 1.0 1000 S worker\n",20);
        });monitor.start(2000);
        QTimer::singleShot(800,&w,[&]{const auto pos=w.files->visualItemRect(infra).center();QHelpEvent event(QEvent::ToolTip,pos,w.files->viewport()->mapToGlobal(pos));QApplication::sendEvent(w.files->viewport(),&event);});
        QTest::qWait(3200);QVERIFY(w.fileItem("/root/project/infra/config.yml"));
        QToolTip::hideText();
        if(qEnvironmentVariableIsSet("RABBITSHELL_HIDDEN_WINDOWS"))for(auto window:QApplication::topLevelWidgets())QVERIFY(window->testAttribute(Qt::WA_DontShowOnScreen));
        QAccessible::setActive(false);
    }
    void refreshHierarchyAndStaleReplies(){
        Workspace w(Profile{}, {},true);setup(w);
        list(w,"/root",{entry("/root/A",true),entry("/root/C",true),entry("/root/root.txt",false,false,100)});
        auto a=w.fileItem("/root/A"),c=w.fileItem("/root/C");a->setExpanded(true);list(w,"/root/A",{entry("/root/A/deep",true),entry("/root/A/file",false,false,100)});
        auto deep=w.fileItem("/root/A/deep");deep->setExpanded(true);list(w,"/root/A/deep",{entry("/root/A/deep/leaf",false,false,100)});
        c->setExpanded(true);list(w,"/root/C",{entry("/root/C/cached")});c->setExpanded(false);
        w.createEntry(false,"/root/A");w.entryDraft.editor->setText("unfinished");auto draft=w.entryDraft.editor;draft->setFocus();
        auto leaf=w.fileItem("/root/A/deep/leaf");leaf->setSelected(true);auto index=QPersistentModelIndex(w.files->indexFromItem(leaf));
        const auto v=w.files->verticalScrollBar()->value();jobs(w);w.refreshFiles();auto root=jobs(w);QCOMPARE(root.size(),1);QCOMPARE(root[0].path,QString("/root"));
        QVERIFY(!c->data(0,DirectoryLoadedRole).toBool());QCOMPARE(c->childCount(),1);
        w.session->completed("list",root[0].id,{{"path","/root"},{"entries",QVariantList{entry("/root/A",true),entry("/root/C",true),entry("/root/root.txt",false,false,999)}}});
        auto children=jobs(w);QCOMPARE(children.size(),1);QCOMPARE(children[0].path,QString("/root/A"));
        w.session->completed("list",children[0].id,{{"path","/root/A"},{"entries",QVariantList{entry("/root/A/deep",true),entry("/root/A/file",false,false,200)}}});
        auto grandchildren=jobs(w);QCOMPARE(grandchildren.size(),1);QCOMPARE(grandchildren[0].path,QString("/root/A/deep"));
        w.session->completed("list",grandchildren[0].id,{{"path","/root/A/deep"},{"entries",QVariantList{entry("/root/A/deep/leaf",false,false,201)}}});
        QCOMPARE(w.files->itemFromIndex(index),leaf);QVERIFY(leaf->isSelected());QVERIFY(a->isExpanded());QVERIFY(deep->isExpanded());
        QCOMPARE(w.entryDraft.editor,draft);QCOMPARE(draft->text(),QString("unfinished"));QVERIFY(draft->hasFocus());QCOMPARE(w.files->verticalScrollBar()->value(),v);
        QCOMPARE(leaf->data(0,Qt::UserRole).toMap()["size"].toInt(),201);QVERIFY(leaf->toolTip(1).contains("201 bytes"));
        c->setExpanded(true);auto expanded=jobs(w);QCOMPARE(expanded.size(),1);QCOMPARE(expanded[0].path,QString("/root/C"));
        w.cancelEntryDraft();w.refreshFiles();auto initial=jobs(w);w.session->completed("list",initial[0].id,{{"path","/root"},{"entries",QVariantList{entry("/root/A",true)}}});auto old=jobs(w);
        QVERIFY(!w.fileItem("/root/C"));w.navigate("/elsewhere");auto navigation=jobs(w);
        for(const auto &job:old)w.session->completed("list",job.id,{{"path",job.path},{"entries",QVariantList{entry(job.path+"/STALE")}}});QVERIFY(!w.fileItem("/root/A/STALE"));
        w.session->completed("list",navigation.last().id,{{"path","/elsewhere"},{"entries",QVariantList{entry("/elsewhere/current")}}});QVERIFY(w.fileItem("/elsewhere/current"));QVERIFY(!w.fileItem("/root/A"));
        // Same-path type change discards descendants and pending parent identity.
        list(w,"/elsewhere",{entry("/elsewhere/folder",true)});auto folder=w.fileItem("/elsewhere/folder");folder->setExpanded(true);jobs(w);w.requestDirectory("/elsewhere/folder");auto pending=jobs(w);
        list(w,"/elsewhere",{entry("/elsewhere/folder",false)});w.session->completed("list",pending.last().id,{{"path","/elsewhere/folder"},{"entries",QVariantList{entry("/elsewhere/folder/STALE")}}});QVERIFY(!w.fileItem("/elsewhere/folder/STALE"));
    }
    void dialogIdentityAndBatch_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void selectedCurrentRemoval_data(){
        QTest::addColumn<int>("column");QTest::addColumn<bool>("directory");
        QTest::newRow("name-file")<<0<<false;QTest::newRow("size-file")<<1<<false;
        QTest::newRow("expanded-directory")<<0<<true;
    }
    void selectedCurrentRemoval(){
        QFETCH(int,column);QFETCH(bool,directory);Workspace w(Profile{}, {},true);setup(w);
        QVariantList rows{entry("/root/bin",true),entry("/root/upload.txt",directory),entry("/root/keep.txt")};
        list(w,"/root",rows);auto bin=w.fileItem("/root/bin");bin->setExpanded(true);
        QVariantList children;for(int i=0;i<80;++i)children<<entry(QString("/root/bin/child-%1").arg(i),true);
        list(w,"/root/bin",children);
        if(directory){w.fileItem("/root/upload.txt")->setExpanded(true);list(w,"/root/upload.txt",{entry("/root/upload.txt/child")});}
        w.files->sortItems(column,Qt::AscendingOrder);auto target=w.fileItem("/root/upload.txt");
        w.files->setCurrentItem(target);target->setSelected(true);w.files->setFocus();
        QPersistentModelIndex current=w.files->indexFromItem(target);jobs(w);
        QTimer answer;bool replied=false;
        connect(&answer,&QTimer::timeout,&w,[&]{
            if(auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if(!replied){replied=true;list(w,"/root",rows);}
                box->button(QMessageBox::Yes)->click();
            }
        });answer.start(5);
        QTest::keyClick(w.files,Qt::Key_Delete);answer.stop();QVERIFY(replied);
        auto pending=jobs(w);QCOMPARE(pending.size(),2);QCOMPARE(pending[0].type,QString("delete"));QCOMPARE(pending[1].type,QString("journalSeal"));
        w.session->completed("delete",pending[0].id,{{"path",pending[0].path}});
        QVERIFY(!current.isValid());QVERIFY(!w.fileItem("/root/upload.txt"));
        auto refresh=jobs(w);QCOMPARE(refresh.size(),1);
        w.session->completed("list",refresh[0].id,{{"path","/root"},{"entries",QVariantList{entry("/root/bin",true),entry("/root/keep.txt")}}});
        QVERIFY(w.fileItem("/root/keep.txt"));QVERIFY(bin->isExpanded());QCOMPARE(bin->childCount(),80);
    }
    void postDeletePendingSortAndDeepScroll(){
        Workspace w(Profile{}, {},true);setup(w);
        QVariantList roots{entry("/root/bin",true),entry("/root/.gitmodules"),entry("/root/ChannelNameMap.json",false,false,4261)};
        list(w,"/root",roots);auto bin=w.fileItem("/root/bin");bin->setExpanded(true);
        QVariantList children;
        for(int n=0;n<180;++n)children<<entry(QString("/root/bin/log_%1").arg(n),true);
        children<<entry("/root/bin/ChannelNameMap.json",false,false,4261);
        list(w,"/root/bin",children);w.files->sortItems(1,Qt::DescendingOrder);
        for(int n=0;n<4;++n){auto folder=w.fileItem(QString("/root/bin/log_%1").arg(n));folder->setExpanded(true);list(w,folder->data(0,Qt::UserRole).toMap()["path"].toString(),{entry(QString("/root/bin/log_%1/keep").arg(n))});}
        w.files->verticalScrollBar()->setValue(w.files->verticalScrollBar()->maximum());
        w.requestDirectory("/root");auto old=jobs(w);w.requestDirectory("/root/bin");auto recent=jobs(w);
        auto target=w.fileItem("/root/bin/ChannelNameMap.json");w.files->setCurrentItem(target);target->setSelected(true);
        w.syncFileMutation("/root/bin/ChannelNameMap.json",true);QVERIFY(!w.fileItem("/root/bin/ChannelNameMap.json"));
        auto deletionRefresh=jobs(w);QCOMPARE(deletionRefresh.size(),1);
        w.session->completed("list",recent.last().id,{{"path","/root/bin"},{"entries",children}}); // superseded by delete refresh
        QVERIFY(!w.fileItem("/root/bin/ChannelNameMap.json"));children.removeLast();
        w.session->completed("list",deletionRefresh.last().id,{{"path","/root/bin"},{"entries",children}});
        w.session->completed("list",old.last().id,{{"path","/root"},{"entries",roots}});
        QVERIFY(!w.fileItem("/root/bin/ChannelNameMap.json"));QVERIFY(w.fileItem("/root/ChannelNameMap.json"));
        // A selected/current subtree disappears in a root refresh while a
        // pending sort and one of its own list replies still exist.
        w.files->setCurrentItem(bin);w.files->clearSelection();bin->setSelected(true);
        bin->setText(0,"zz-bin");w.requestDirectory("/root/bin");auto stale=jobs(w);
        list(w,"/root",{entry("/root/ChannelNameMap.json",false,false,4261)});
        w.session->completed("list",stale.last().id,{{"path","/root/bin"},{"entries",children}});
        QVERIFY(!w.fileItem("/root/bin"));
        int realRows=0;for(int row=0;row<w.files->topLevelItemCount();++row)
            if(!w.files->topLevelItem(row)->data(0,ParentNavigationRole).toBool())++realRows;
        QCOMPARE(realRows,1);
    }
    void dialogIdentityAndBatch(){
        QFETCH(bool,light);applyTheme(light);Workspace w(Profile{}, {},true);setup(w);
        Job active=upload(local("dialog.txt"),"/root/dialog.txt","batch");w.activeTransfer=active.id;w.transfers[active.id]=active;
        Job queued=active;queued.id++;w.transfers[queued.id]=queued;
        auto s=w.transferSession;s->conflictWaiting=true;s->conflictJob=active.id;s->conflictNonce=20;
        w.showUploadConflict(s,active.id,20,active.path,true);auto old=w.uploadConflictDialog;QVERIFY(old);
        QCOMPARE(old->buttons().size(),3);old->checkBox()->setChecked(true);
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path());
        QVERIFY(old->grab().save(report+QString("/upload-conflict-%1.png").arg(light?"light":"dark")));
        for(auto button:old->buttons())if(button->text()=="保留两者")button->click();
        QCOMPARE(s->conflictAnswer,QString("keep"));QCOMPARE(w.transfers[queued.id].collision,QString("keep"));
        // Simulate retry of the same id while an old dialog still has a queued
        // finished delivery. Both dialog identity and worker nonce must match.
        s->conflictAnswer.clear();s->conflictNonce=21;w.transfers[queued.id].collision="ask";
        w.showUploadConflict(s,active.id,21,active.path,false);auto current=w.uploadConflictDialog;QVERIFY(current && current!=old);
        old->finished(0);QCOMPARE(w.transfers[queued.id].collision,QString("ask"));QVERIFY(s->conflictAnswer.isEmpty());
        QVERIFY(!s->answerUploadConflict(active.id,20,"overwrite",true));QVERIFY(s->conflictAnswer.isEmpty());
        for(auto button:current->buttons())if(button->text()=="替换文件")QVERIFY(!button->isEnabled());
        QTest::keyClick(current,Qt::Key_Escape);QCOMPARE(s->conflictAnswer,QString("cancel"));QCOMPARE(w.transfers[queued.id].collision,QString("ask"));
    }
    void isolatedUploadProtocol(){
        if(!remoteProfile().port)QSKIP("Run with the private upload SFTP fixture");
        Session s(remoteProfile(),"fixture-only");QSignalSpy ready(&s,&Session::connected);connectWorker(s);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);
        const auto root=fixtureRoot();QString choice="overwrite";bool all=false;int prompts=0;quint64 lastNonce=0;bool staleRejected=false;
        connect(&s,&Session::uploadConflict,this,[&](int job,quint64 nonce,const QString &,bool){
            ++prompts;lastNonce=nonce;staleRejected=!s.answerUploadConflict(job,nonce-1,"overwrite",true);
            QTimer::singleShot(80,&s,[&,job,nonce]{s.answerUploadConflict(job,nonce,choice,all);});
        });
        QVariantMap result;QString error;auto source=local("protocol.txt","NEW");
        QVERIFY(run(s,upload(source,"/root/fresh.txt"),&result,&error));QCOMPARE(prompts,0);QCOMPARE(read(root+"/root/fresh.txt"),QByteArray("NEW"));
        QElapsedTimer wall;wall.start();QVERIFY(run(s,upload(source,"/root/collision.txt"),&result,&error));QCOMPARE(prompts,1);QVERIFY(staleRejected);QCOMPARE(read(root+"/root/collision.txt"),QByteArray("NEW"));
        QVERIFY(wall.elapsed()-result["activeMs"].toLongLong()>=70); // intentional GUI decision wait is excluded
        choice="keep";QVERIFY(run(s,upload(source,"/root/collision.txt"),&result,&error));QCOMPARE(read(root+"/root/collision (1).txt"),QByteArray("NEW"));
        choice="skip";QVERIFY(run(s,upload(source,"/root/skip.txt"),&result,&error));QCOMPARE(result["skipped"].toInt(),1);QCOMPARE(read(root+"/root/skip.txt"),QByteArray("OLD"));
        choice="cancel";QVERIFY(!run(s,upload(source,"/root/skip.txt"),&result,&error));QVERIFY(error.contains("取消"));QCOMPARE(read(root+"/root/skip.txt"),QByteArray("OLD"));
        all=true;choice="skip";const auto before=prompts;
        QVERIFY(run(s,upload(source,"/root/batch1.txt","shared"),&result));QVERIFY(run(s,upload(source,"/root/batch2.txt","shared"),&result));QCOMPARE(prompts,before+1);
        all=false;choice="keep";QVERIFY(run(s,upload(source,"/root/race.txt"),&result,&error));QCOMPARE(read(root+"/root/race.txt"),QByteArray("RACE"));QCOMPARE(read(root+"/root/race (1).txt"),QByteArray("NEW"));
        choice="cancel";QVERIFY(!run(s,upload(source,"/root/race-cancel.txt"),&result,&error));QCOMPARE(read(root+"/root/race-cancel.txt"),QByteArray("RACE"));
        const auto deniedPrompts=prompts;
        QVERIFY(!run(s,upload(source,"/root/rename-denied.txt"),&result,&error));QVERIFY(error.contains("提交"));QCOMPARE(prompts,deniedPrompts);QVERIFY(!run(s,upload(source,"/root/stat-denied.txt"),&result,&error));QCOMPARE(prompts,deniedPrompts);QVERIFY(error.contains("检查"));QVERIFY(!QFileInfo::exists(root+"/root/stat-denied.txt"));
        const auto folder=data.filePath("recursive");QDir().mkpath(folder);local("recursive/existing.txt","RECURSIVE");local("recursive/new.txt","FRESH");
        choice="skip";QVERIFY(run(s,upload(folder,"/root/recursive"),&result,&error));QCOMPARE(read(root+"/root/recursive/existing.txt"),QByteArray("OLD"));QCOMPARE(read(root+"/root/recursive/new.txt"),QByteArray("FRESH"));
        choice="keep";QVERIFY(run(s,upload(source,"/root/type-dir"),&result,&error));QVERIFY(QFileInfo(root+"/root/type-dir").isDir());QCOMPARE(read(root+"/root/type-dir (1)"),QByteArray("NEW"));
        QVERIFY(run(s,upload(folder,"/root/type-file"),&result,&error));QCOMPARE(read(root+"/root/type-file"),QByteArray("OLD"));QVERIFY(QFileInfo(root+"/root/type-file (1)").isDir());
        auto remoteFile=[&](const QString &path,const QByteArray &body){QFile f(root+path);if(!f.open(QIODevice::WriteOnly))return false;return f.write(body)==body.size();};
        QVERIFY(remoteFile("/root/move-src.txt","MOVE"));QVERIFY(remoteFile("/root/B/move-src.txt","OLD"));
        Job move;move.id=id++;move.type="move";move.path="/root/move-src.txt";move.destination="/root/B/move-src.txt";move.collision="ask";
        choice="cancel";QVERIFY(!run(s,move,&result,&error));QCOMPARE(read(root+move.path),QByteArray("MOVE"));QCOMPARE(read(root+move.destination),QByteArray("OLD"));
        move.id=id++;choice="keep";QVERIFY(run(s,move,&result,&error));QCOMPARE(read(root+"/root/B/move-src (1).txt"),QByteArray("MOVE"));QCOMPARE(read(root+move.destination),QByteArray("OLD"));QVERIFY(!QFileInfo::exists(root+move.path));
        QVERIFY(remoteFile("/root/move-src.txt","REPLACE"));move.id=id++;choice="overwrite";QVERIFY(!run(s,move,&result,&error));QCOMPARE(read(root+move.path),QByteArray("REPLACE"));QCOMPARE(read(root+move.destination),QByteArray("OLD"));
        QVERIFY(remoteFile("/root/move-src.txt","PRESERVE"));move.id=id++;move.destination="/root/rename-denied.txt";QVERIFY(!run(s,move,&result,&error));QCOMPARE(read(root+move.path),QByteArray("PRESERVE"));
        QDir().mkpath(root+"/root/move-dir/sub");move.id=id++;move.path="/root/move-dir";move.destination="/root/move-dir/sub/inside";QVERIFY(!run(s,move,&result,&error));QVERIFY(QFileInfo(root+move.path).isDir());
        QVERIFY(lastNonce>0);s.stop();QVERIFY(s.wait(5000));
    }
    void isolatedKeepBothNames(){
        if(!remoteProfile().port)QSKIP("Run with the private upload SFTP fixture");
        const auto root=fixtureRoot();QVERIFY(QDir().mkpath(root+"/root/name-source"));
        auto seed=[&](const QString &path,const QByteArray &body){QFile f(path);QDir().mkpath(QFileInfo(path).absolutePath());return f.open(QIODevice::WriteOnly) && f.write(body)==body.size();};
        Session s(remoteProfile(),"fixture-only");QSignalSpy ready(&s,&Session::connected);connectWorker(s);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);
        connect(&s,&Session::uploadConflict,&s,[&](int job,quint64 nonce,const QString &,bool){s.answerUploadConflict(job,nonce,"keep");});
        const QList<QPair<QString,QString>> names{{"install.sh","install (2).sh"},{"archive.tar.gz","archive (2).tar.gz"},{".env",".env (2)"},{"plain","plain (2)"}};
        for(const auto &[name,expected]:names) {
            const auto first=expected;QString occupied=first;occupied.replace("(2)","(1)");
            const auto destination="/root/"+name;
            QVERIFY(seed(root+destination,"ORIGINAL"));QVERIFY(seed(root+"/root/"+occupied,"TAKEN"));
            auto put=upload(local("name-input/"+name,"COPY"),destination);put.options["clipboardPaste"]=true;
            QString error;QVERIFY2(run(s,put,nullptr,&error),qPrintable(error));
            QCOMPARE(read(root+"/root/"+expected),QByteArray("COPY"));QCOMPARE(read(root+destination),QByteArray("ORIGINAL"));QCOMPARE(read(root+"/root/"+occupied),QByteArray("TAKEN"));
            const auto downloaded=data.filePath("name-download/"+name);QVERIFY(seed(downloaded,"LOCAL"));QVERIFY(seed(data.filePath("name-download/"+occupied),"TAKEN"));
            Job get;get.id=id++;get.type="download";get.path=destination;get.destination=downloaded;get.collision="keep";
            QVERIFY2(run(s,get,nullptr,&error),qPrintable(error));QCOMPARE(read(data.filePath("name-download/"+expected)),QByteArray("ORIGINAL"));QCOMPARE(read(downloaded),QByteArray("LOCAL"));
            const auto source="/root/name-source/"+name,target="/root/B/"+name;
            QVERIFY(seed(root+source,"MOVE"));QVERIFY(seed(root+target,"OLD"));QVERIFY(seed(root+"/root/B/"+occupied,"TAKEN"));
            Job move;move.id=id++;move.type="move";move.path=source;move.destination=target;move.collision="ask";
            QVERIFY2(run(s,move,nullptr,&error),qPrintable(error));QCOMPARE(read(root+"/root/B/"+expected),QByteArray("MOVE"));QCOMPARE(read(root+target),QByteArray("OLD"));QVERIFY(!QFileInfo::exists(root+source));
        }
        const auto folder=data.filePath("name-input/folder.name");QVERIFY(seed(folder+"/child.txt","DIRECTORY"));
        QVERIFY(QDir().mkpath(root+"/root/folder.name"));QVERIFY(QDir().mkpath(root+"/root/folder.name (1)"));
        auto put=upload(folder,"/root/folder.name");put.options["clipboardPaste"]=true;QString error;
        QVERIFY2(run(s,put,nullptr,&error),qPrintable(error));QCOMPARE(read(root+"/root/folder.name (2)/child.txt"),QByteArray("DIRECTORY"));
        QVERIFY(QDir().mkpath(root+"/root/B/folder.name"));QVERIFY(QDir().mkpath(root+"/root/B/folder.name (1)"));
        Job move;move.id=id++;move.type="move";move.path="/root/folder.name (2)";move.destination="/root/B/folder.name";move.collision="ask";
        QVERIFY2(run(s,move,nullptr,&error),qPrintable(error));QCOMPARE(read(root+"/root/B/folder.name (2)/child.txt"),QByteArray("DIRECTORY"));
        s.stop();QVERIFY(s.wait(5000));
    }
    void isolatedUndoPartialAndMutationGuard(){
        if(!remoteProfile().port)QSKIP("Run with the private upload SFTP fixture");
        Session s(remoteProfile(),"fixture-only");QSignalSpy ready(&s,&Session::connected);connectWorker(s);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);
        const auto batch=FileOperationJournal::newId();QVariantMap result;QString error;
        for(const auto &path:{QString("/root/undo-first"),QString("/root/undo-second")}) {
            Job j;j.id=id++;j.type="touch";j.path=path;j.options={{"operationBatch",batch},{"operationBatchOpen",true}};
            QVERIFY2(run(s,j,&result,&error),qPrintable(error));
        }
        Job seal;seal.id=id++;seal.type="journalSeal";seal.options["operationBatch"]=batch;QVERIFY2(run(s,seal,&result,&error),qPrintable(error));
        QFile changed(fixtureRoot()+"/root/undo-first");QVERIFY(changed.open(QIODevice::WriteOnly));QCOMPARE(changed.write("changed"),qint64(7));changed.close();
        std::atomic_bool lockedDuringEffect{false};
        connect(&s,&Session::journalUndoApplied,&s,[&](int,const QVariantMap &){
            QString why;auto competing=s.operationJournal->acquireMutationGuard(&why);lockedDuringEffect=!competing;
        },Qt::DirectConnection);
        QSignalSpy applied(&s,&Session::journalUndoApplied);Job undo;undo.id=id++;undo.type="journalUndo";
        QVERIFY(!run(s,undo,&result,&error));QVERIFY2(error.contains("变化"),qPrintable(error));
        QCOMPARE(applied.size(),1);QCOMPARE(applied[0][1].toMap()["old"].toString(),QString("/root/undo-second"));
        QVERIFY(lockedDuringEffect);QVERIFY(!QFileInfo::exists(fixtureRoot()+"/root/undo-second"));QCOMPARE(read(fixtureRoot()+"/root/undo-first"),QByteArray("changed"));
        QVERIFY(applied[0][1].toMap()["path"].toString().isEmpty());
        QJsonObject snapshot;QVERIFY2(s.operationJournal->snapshot(&snapshot,&error),qPrintable(error));
        const auto entries=snapshot["batches"].toArray().last().toObject()["effects"].toArray();
        QCOMPARE(entries[0].toObject()["state"].toString(),QString("committed"));QCOMPARE(entries[1].toObject()["state"].toString(),QString("undone"));
        QString why;auto unlocked=s.operationJournal->acquireMutationGuard(&why);QVERIFY2(unlocked,qPrintable(why));unlocked.reset();
        s.stop();QVERIFY(s.wait(5000));
    }
    void isolatedUploadUndoAndOverwriteBarrier(){
        if(!remoteProfile().port)QSKIP("Run with the private upload SFTP fixture");
        Session s(remoteProfile(),"fixture-only");QSignalSpy ready(&s,&Session::connected);connectWorker(s);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);
        const auto root=fixtureRoot();const auto source=local("undo-upload-source","NEW DATA");QVariantMap result;QString error;
        auto seed=[&](const QString &path){QFile f(root+path);return f.open(QIODevice::WriteOnly) && f.write("OLD DATA")==8;};
        auto fresh=upload(source,"/root/undo-upload-new");fresh.collision="keep";
        QVERIFY2(run(s,fresh,&result,&error),qPrintable(error));QCOMPARE(read(root+fresh.path),QByteArray("NEW DATA"));
        QSignalSpy applied(&s,&Session::journalUndoApplied);Job undo;undo.id=id++;undo.type="journalUndo";
        QVERIFY2(run(s,undo,&result,&error),qPrintable(error));QCOMPARE(applied.size(),1);QVERIFY(!QFileInfo::exists(root+fresh.path));
        QVERIFY(seed("/root/undo-upload"));auto j=upload(source,"/root/undo-upload");j.collision="overwrite";
        QVERIFY2(run(s,j,&result,&error),qPrintable(error));QCOMPARE(read(root+j.path),QByteArray("NEW DATA"));
        undo.id=id++;QVERIFY(!run(s,undo,&result,&error));QVERIFY2(error.contains("不能撤销"),qPrintable(error));QCOMPARE(read(root+j.path),QByteArray("NEW DATA"));
        s.stop();QVERIFY(s.wait(5000));
    }
    void isolatedFileUndoAndDeleteBarrier(){
        if(!remoteProfile().port)QSKIP("Run with the private upload SFTP fixture");
        Session s(remoteProfile(),"fixture-only");QSignalSpy ready(&s,&Session::connected);connectWorker(s);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);
        const auto root=fixtureRoot();QVariantMap result;QString error;Job j;j.id=id++;j.type="touch";j.path="/root/undo-created";
        QVERIFY2(run(s,j,&result,&error),qPrintable(error));Job undo;undo.type="journalUndo";undo.id=id++;
        QVERIFY2(run(s,undo,&result,&error),qPrintable(error));QVERIFY(!QFileInfo::exists(root+j.path));
        j.id=id++;j.type="mkdir";j.path="/root/undo-directory";QVERIFY2(run(s,j,&result,&error),qPrintable(error));
        undo.id=id++;QVERIFY2(run(s,undo,&result,&error),qPrintable(error));QVERIFY(!QFileInfo::exists(root+j.path));
        j.id=id++;j.type="mkdir";j.path="/root/undo-nonempty";QVERIFY2(run(s,j,&result,&error),qPrintable(error));
        QFile child(root+j.path+"/external");QVERIFY(child.open(QIODevice::WriteOnly));child.write("outside");child.close();
        undo.id=id++;QVERIFY(!run(s,undo,&result,&error));QVERIFY(QFileInfo::exists(child.fileName()));
        QVERIFY(child.remove());undo.id=id++;QVERIFY2(run(s,undo,&result,&error),qPrintable(error));
        QFile source(root+"/root/undo-rename-old");QVERIFY(source.open(QIODevice::WriteOnly));source.write("before");source.close();
        j.id=id++;j.type="rename";j.path="/root/undo-rename-old";j.destination="/root/undo-rename-new";QVERIFY2(run(s,j,&result,&error),qPrintable(error));
        QFile changed(root+j.destination);QVERIFY(changed.open(QIODevice::WriteOnly));changed.write("after change");changed.close();
        undo.id=id++;QVERIFY2(run(s,undo,&result,&error),qPrintable(error));QCOMPARE(read(root+j.path),QByteArray("after change"));
        j.id=id++;j.type="delete";j.path="/root/undo-rename-old";j.recursive=true;QVERIFY2(run(s,j,&result,&error),qPrintable(error));QVERIFY(!QFileInfo::exists(root+j.path));
        undo.id=id++;QVERIFY(!run(s,undo,&result,&error));QVERIFY2(error.contains("不能撤销"),qPrintable(error));
        s.stop();QVERIFY(s.wait(5000));
    }
    void isolatedMixedBatchBarrier(){
        if(!remoteProfile().port)QSKIP("Run with the private upload SFTP fixture");
        Session s(remoteProfile(),"fixture-only");QSignalSpy ready(&s,&Session::connected);connectWorker(s);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);
        const auto root=fixtureRoot(),batch=FileOperationJournal::newId();QVariantMap result;QString error;
        auto first=upload(local("mixed-new","new"),"/root/mixed-new");first.collision="keep";first.options["operationBatch"]=batch;first.options["operationBatchOpen"]=true;
        QVERIFY2(run(s,first,&result,&error),qPrintable(error));
        QFile target(root+"/root/mixed-old");QVERIFY(target.open(QIODevice::WriteOnly));target.write("old");target.close();
        auto second=upload(local("mixed-overwrite","replacement"),"/root/mixed-old");second.collision="overwrite";second.options["operationBatch"]=batch;second.options["operationBatchOpen"]=true;
        QVERIFY2(run(s,second,&result,&error),qPrintable(error));Job seal;seal.type="journalSeal";seal.id=id++;seal.options["operationBatch"]=batch;QVERIFY2(run(s,seal,&result,&error),qPrintable(error));
        Job undo;undo.type="journalUndo";undo.id=id++;QVERIFY(!run(s,undo,&result,&error));QVERIFY2(error.contains("不能撤销"),qPrintable(error));
        QCOMPARE(read(root+"/root/mixed-new"),QByteArray("new"));QCOMPARE(read(root+"/root/mixed-old"),QByteArray("replacement"));
        s.stop();QVERIFY(s.wait(5000));
    }
    void isolatedMoveUndoAndOccupiedOriginal(){
        if(!remoteProfile().port)QSKIP("Run with the private upload SFTP fixture");
        Session s(remoteProfile(),"fixture-only");QSignalSpy ready(&s,&Session::connected);connectWorker(s);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);
        const auto root=fixtureRoot();QVariantMap result;QString error;
        QFile original(root+"/root/undo-move");QVERIFY(original.open(QIODevice::WriteOnly));QCOMPARE(original.write("moved"),qint64(5));original.close();
        Job move;move.id=id++;move.type="move";move.path="/root/undo-move";move.destination="/root/B/undo-move";move.collision="ask";
        QVERIFY2(run(s,move,&result,&error),qPrintable(error));QVERIFY(!QFileInfo::exists(root+move.path));QCOMPARE(read(root+move.destination),QByteArray("moved"));
        QVERIFY(original.open(QIODevice::WriteOnly));QCOMPARE(original.write("occupant"),qint64(8));original.close();
        Job undo;undo.id=id++;undo.type="journalUndo";QVERIFY(!run(s,undo,&result,&error));
        QCOMPARE(read(root+move.path),QByteArray("occupant"));QCOMPARE(read(root+move.destination),QByteArray("moved"));
        QVERIFY(original.remove());undo.id=id++;QVERIFY2(run(s,undo,&result,&error),qPrintable(error));
        QCOMPARE(read(root+move.path),QByteArray("moved"));QVERIFY(!QFileInfo::exists(root+move.destination));
        s.stop();QVERIFY(s.wait(5000));
    }
    void isolatedSaveTemporaryCleanup(){
        if(!remoteProfile().port)QSKIP("Run with the private upload SFTP fixture");
        const auto root=fixtureRoot();QFile f(root+"/root/save-fixture");QVERIFY(f.open(QIODevice::WriteOnly));f.write("before");f.close();
        Session s(remoteProfile(),"fixture-only");QSignalSpy ready(&s,&Session::connected);connectWorker(s);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);
        QVariantMap result;QString error;Job job;job.type="save";job.id=id++;job.path="/root/save-fixture";job.data="after";job.expectedHash="wrong";
        QVERIFY(!run(s,job,&result,&error));QVERIFY(error.startsWith("CONFLICT:"));QCOMPARE(read(root+job.path),QByteArray("before"));
        job.id=id++;job.expectedHash=QCryptographicHash::hash(QByteArray("before"),QCryptographicHash::Sha256);
        QVERIFY2(run(s,job,&result,&error),qPrintable(error));QCOMPARE(read(root+job.path),QByteArray("after"));
        QVERIFY(QDir(root+"/root").entryList({"*.part"},QDir::Files|QDir::Hidden).isEmpty());
        s.stop();QVERIFY(s.wait(5000));
    }
    void isolatedEventsRefreshAndWaitingClose(){
        if(!remoteProfile().port)QSKIP("Run with the private upload SFTP fixture");
        auto w=new Workspace(remoteProfile(),QByteArray("fixture-only"),true);setup(*w);
        delete w->session;w->session=new Session(w->profile,"fixture-only",w);
        connect(w->session,&Session::authenticatedHostIdentity,w,[w](const QString &fp){w->authenticatedFingerprint=fp;});
        connect(w->session,&Session::completed,w,[w](const QString &type,int id,const QVariantMap &r){if(type=="list")w->renderDirectory(r,id);});
        QSignalSpy ready(w->session,&Session::connected);connectWorker(*w->session);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);
        w->requestDirectory("/root");QTRY_VERIFY(w->fileItem("/root/A"));
        auto a=w->fileItem("/root/A"),b=w->fileItem("/root/B");a->setExpanded(true);QTRY_VERIFY(w->fileItem("/root/A/deep"));
        auto deep=w->fileItem("/root/A/deep");deep->setExpanded(true);QTRY_VERIFY(w->fileItem("/root/A/deep/external.txt"));
        auto leaf=w->fileItem("/root/A/deep/external.txt");leaf->setSelected(true);auto index=QPersistentModelIndex(w->files->indexFromItem(leaf));
        QFile external(fixtureRoot()+"/root/A/deep/external.txt");QVERIFY(external.open(QIODevice::WriteOnly));external.write(QByteArray(5001,'D'));external.close();
        QFile rootFile(fixtureRoot()+"/root/root.txt");QVERIFY(rootFile.open(QIODevice::WriteOnly));rootFile.write(QByteArray(3001,'R'));rootFile.close();
        w->refreshFiles();QTRY_COMPARE(leaf->data(0,Qt::UserRole).toMap()["size"].toULongLong(),quint64(5001));QTRY_COMPARE(w->fileItem("/root/root.txt")->data(0,Qt::UserRole).toMap()["size"].toULongLong(),quint64(3001));
        QCOMPARE(w->files->itemFromIndex(index),leaf);QVERIFY(leaf->isSelected());QVERIFY(a->isExpanded());QVERIFY(deep->isExpanded());
        // Protocol tests have intentionally created additional conflict targets.
        // Hide those unrelated rows so the blank-root drop is truly on blank
        // viewport space, rather than accidentally on another directory row.
        for(int row=0;row<w->files->topLevelItemCount();++row) {
            auto item=w->files->topLevelItem(row);
            if(!QStringList{"A","B","root.txt"}.contains(item->text(0)))item->setHidden(true);
        }
        const auto source=local("event.txt","ACTUAL DROP");
        const int sx=w->files->header()->sectionViewportPosition(1)+w->files->columnWidth(1)/2;
        QVERIFY(drop(w->files,QPoint(sx,w->files->visualItemRect(a).center().y()),source));
        QVERIFY(drop(w->files,w->files->visualItemRect(b).center(),source));
        QVERIFY(drop(w->files,w->files->visualItemRect(leaf).center(),source));
        const QPoint blank(10,w->files->viewport()->height()-50);QVERIFY(!w->files->rowAt(blank));
        QVERIFY(drop(w->files,blank,source));
        // Replace queued-only transfer worker with the production transfer session.
        delete w->transferSession;w->transferSession=nullptr;w->ensureTransferSession();
        QTRY_COMPARE_WITH_TIMEOUT(w->pendingTransfers(),0,15000);
        for(auto path:{"/root/A/event.txt","/root/B/event.txt","/root/A/deep/event.txt","/root/event.txt"})QCOMPARE(read(fixtureRoot()+path),QByteArray("ACTUAL DROP"));
        QVERIFY(!w->uploadConflictDialog);QTRY_VERIFY(w->fileItem("/root/A/deep/event.txt"));
        // Real UI replacement + batch response: one dialog for two colliding
        // roots, including propagation to a queued job in the same batch.
        const auto second=local("two/event.txt","SECOND");QSignalSpy conflicts(w->transferSession,&Session::uploadConflict);
        w->queueTransfers({source,second},true,"/root/A");QTRY_VERIFY(w->uploadConflictDialog);
        w->uploadConflictDialog->checkBox()->setChecked(true);
        for(auto button:w->uploadConflictDialog->buttons())if(button->text()=="替换文件")button->click();
        QTRY_COMPARE(w->pendingTransfers(),0);QCOMPARE(conflicts.size(),1);QCOMPARE(read(fixtureRoot()+"/root/A/event.txt"),QByteArray("SECOND"));
        // Actual worker blocked in a conflict: default/ESC is cancellation, UI remains responsive.
        QVERIFY(drop(w->files,w->files->visualItemRect(a).center(),source));QTRY_VERIFY(w->uploadConflictDialog);
        auto dialog=w->uploadConflictDialog;QCOMPARE(dialog->defaultButton()->text(),QString("取消"));
        const auto oldNonce=dialog->property("nonce").toULongLong();const auto task=w->activeTransfer;
        QVERIFY(!w->transferSession->answerUploadConflict(task,oldNonce-1,"overwrite",true));
        QTest::keyClick(dialog,Qt::Key_Escape);QTRY_COMPARE(w->pendingTransfers(),0);QCOMPARE(w->transferItems[task]->text(1),QString("已取消"));QCOMPARE(read(fixtureRoot()+"/root/A/event.txt"),QByteArray("SECOND"));
        QVERIFY(drop(w->files,w->files->visualItemRect(a).center(),source));QTRY_VERIFY(w->uploadConflictDialog);
        QVERIFY(w->uploadConflictDialog->property("nonce").toULongLong()>oldNonce);
        QPointer<Session> worker=w->transferSession;QPointer<UploadConflictDialog> box=w->uploadConflictDialog;QElapsedTimer elapsed;elapsed.start();delete w;
        QVERIFY(elapsed.elapsed()<3000);QVERIFY(worker.isNull());QVERIFY(box.isNull());
        for(QDirIterator it(fixtureRoot()+"/root",{"*.part"},QDir::Files,QDirIterator::Subdirectories);it.hasNext();)QFAIL(qPrintable("Leaked temporary upload: "+it.next()));
    }
    void isolatedInternalDragDestinationsAndUndo() {
        if(!remoteProfile().port)QSKIP("Run with the private upload SFTP fixture");
        const auto root=fixtureRoot();QVERIFY(QDir().mkpath(root+"/root/A/group-dir"));
        auto seed=[&](QString path,QByteArray body){QFile file(root+path);return file.open(QIODevice::WriteOnly) && file.write(body)==body.size();};
        QVERIFY(seed("/root/A/drag-parent.txt","PARENT"));QVERIFY(seed("/root/A/drag-sub.txt","SUB"));
        QVERIFY(seed("/root/A/deep/drag-other.txt","OTHER"));QVERIFY(seed("/root/A/group-file.txt","GROUP"));QVERIFY(seed("/root/A/group-dir/child.txt","CHILD"));
        Workspace w(remoteProfile(),QByteArray("fixture-only"),true);setup(w);
        delete w.session;w.session=new Session(w.profile,"fixture-only",&w);
        QSignalSpy fingerprint(w.session,&Session::authenticatedHostIdentity),ready(w.session,&Session::connected);
        connectWorker(*w.session);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);w.attachSession(w.session);QCOMPARE(fingerprint.size(),1);w.authenticatedFingerprint=fingerprint[0][0].toString();
        delete w.transferSession;w.transferSession=nullptr;
        w.navigate("/root/A");QTRY_VERIFY(w.fileItem("/root/A/drag-parent.txt"));QCOMPARE(w.directory,QString("/root/A"));
        auto selected=[&](const QString &path){return w.fileItem(path)->data(0,Qt::UserRole).toMap();};
        auto nav=w.files->topLevelItem(0);QVERIFY(nav->data(0,ParentNavigationRole).toBool());
        QVERIFY(platformInternalDrop(w,{selected("/root/A/drag-parent.txt")},nav));QTRY_VERIFY(w.findChild<QMessageBox *>());
        auto dialog=w.findChild<QMessageBox *>();QCOMPARE(dialog->defaultButton()->text(),QString("取消"));dialog->reject();settle();
        QCOMPARE(read(root+"/root/A/drag-parent.txt"),QByteArray("PARENT"));QCOMPARE(w.pendingTransfers(),0);
        auto confirm=[&]{auto box=w.findChild<QMessageBox *>();if(!box)return false;for(auto button:box->buttons())if(button->text()=="移动"){button->click();return true;}return false;};
        QVERIFY(platformInternalDrop(w,{selected("/root/A/drag-parent.txt")},nav));QTRY_VERIFY(w.findChild<QMessageBox *>());QVERIFY(confirm());
        QTRY_COMPARE_WITH_TIMEOUT(w.pendingTransfers(),0,10000);QCOMPARE(read(root+"/root/drag-parent.txt"),QByteArray("PARENT"));QVERIFY(!QFileInfo::exists(root+"/root/A/drag-parent.txt"));
        QVERIFY(platformInternalDrop(w,{selected("/root/A/drag-sub.txt")},w.fileItem("/root/A/deep")));QTRY_VERIFY(w.findChild<QMessageBox *>());QVERIFY(confirm());
        QTRY_COMPARE_WITH_TIMEOUT(w.pendingTransfers(),0,10000);QCOMPARE(read(root+"/root/A/deep/drag-sub.txt"),QByteArray("SUB"));QVERIFY(!QFileInfo::exists(root+"/root/A/drag-sub.txt"));
        w.navigate("/root");QTRY_VERIFY(w.fileItem("/root/B"));auto a=w.fileItem("/root/A");a->setExpanded(true);QTRY_VERIFY(w.fileItem("/root/A/deep"));
        auto deep=w.fileItem("/root/A/deep");deep->setExpanded(true);QTRY_VERIFY(w.fileItem("/root/A/deep/drag-other.txt"));
        QVERIFY(platformInternalDrop(w,{selected("/root/A/deep/drag-other.txt")},w.fileItem("/root/B")));QTRY_VERIFY(w.findChild<QMessageBox *>());QVERIFY(confirm());
        QTRY_COMPARE_WITH_TIMEOUT(w.pendingTransfers(),0,10000);QCOMPARE(read(root+"/root/B/drag-other.txt"),QByteArray("OTHER"));QVERIFY(!QFileInfo::exists(root+"/root/A/deep/drag-other.txt"));
        QVERIFY(platformInternalDrop(w,{selected("/root/A/group-file.txt"),selected("/root/A/group-dir")},w.fileItem("/root/B")));
        QTRY_VERIFY(w.findChild<QMessageBox *>());QVERIFY(w.findChild<QMessageBox *>()->text().contains("2 项"));QVERIFY(confirm());
        QTRY_COMPARE_WITH_TIMEOUT(w.pendingTransfers(),0,10000);QCOMPARE(read(root+"/root/B/group-file.txt"),QByteArray("GROUP"));QCOMPARE(read(root+"/root/B/group-dir/child.txt"),QByteArray("CHILD"));
        QTRY_VERIFY(w.directoryRequests.isEmpty());QSignalSpy done(w.session,&Session::completed),failed(w.session,&Session::failed),applied(w.session,&Session::journalUndoApplied);
        Job undo;undo.id=w.nextJob++;undo.type="journalUndo";w.session->submit(undo);
        auto finished=[&]{for(const auto &row:done)if(row[1].toInt()==undo.id)return true;return false;};
        QTRY_VERIFY_WITH_TIMEOUT(finished() || !failed.isEmpty(),10000);QVERIFY2(finished(),failed.isEmpty()?"undo not completed":qPrintable(failed.last()[2].toString()));QCOMPARE(applied.size(),2);
        QCOMPARE(read(root+"/root/A/group-file.txt"),QByteArray("GROUP"));QCOMPARE(read(root+"/root/A/group-dir/child.txt"),QByteArray("CHILD"));
        QVERIFY(!QFileInfo::exists(root+"/root/B/group-file.txt"));QVERIFY(!QFileInfo::exists(root+"/root/B/group-dir"));QVERIFY(a->isExpanded());QVERIFY(deep->isExpanded());
    }
    void isolatedClipboardTransferAndMove(){
        if(!remoteProfile().port)QSKIP("Run with the private upload SFTP fixture");
        const auto root=fixtureRoot();QVERIFY(QDir().mkpath(root+"/root/clipboard-folder/nested/empty"));
        QFile fixture(root+"/root/clipboard-folder/nested/file.txt");QVERIFY(fixture.open(QIODevice::WriteOnly));fixture.write("remote clipboard bytes");fixture.close();
        QVERIFY(QDir().mkpath(root+"/root/case-collision"));QVERIFY(QDir().mkpath(root+"/root/link-skip"));
        QFile caseFile(root+"/root/case-collision/A.txt");QVERIFY(caseFile.open(QIODevice::WriteOnly));caseFile.write("case");caseFile.close();
        QFile moving(root+"/root/clipboard-move.txt");QVERIFY(moving.open(QIODevice::WriteOnly));moving.write("move bytes");moving.close();
        auto w=new Workspace(remoteProfile(),QByteArray("fixture-only"),true);setup(*w);
        delete w->session;w->session=new Session(w->profile,"fixture-only",w);QSignalSpy fingerprint(w->session,&Session::authenticatedHostIdentity),ready(w->session,&Session::connected);connectWorker(*w->session);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);w->attachSession(w->session);QCOMPARE(fingerprint.size(),1);w->authenticatedFingerprint=fingerprint[0][0].toString();
        delete w->transferSession;w->transferSession=nullptr;w->requestDirectory("/root");QTRY_VERIFY(w->fileItem("/root/clipboard-folder"));
        for(const auto &path:{QString("/root/case-collision"),QString("/root/link-skip")}) {
            auto row=w->fileItem(path);QVERIFY(row);w->files->clearSelection();w->files->setCurrentItem(row);row->setSelected(true);w->copyFileSelection(false);
            auto failed=FileClipboard::instance().current();QVERIFY(failed);QTRY_COMPARE_WITH_TIMEOUT(failed->remaining,0,15000);
            QVERIFY(failed->failed);QVERIFY(!failed->published);QVERIFY(!QApplication::clipboard()->mimeData()->hasUrls());
        }
        auto folder=w->fileItem("/root/clipboard-folder");w->files->setCurrentItem(folder);folder->setSelected(true);w->files->setFocus();QTest::keyClick(w->files,Qt::Key_C,Qt::ControlModifier);
        auto batch=FileClipboard::instance().current();QVERIFY(batch);QVERIFY(!QApplication::clipboard()->mimeData()->hasUrls());
        QTRY_VERIFY_WITH_TIMEOUT(batch->ready,15000);QCOMPARE(QApplication::clipboard()->mimeData()->urls().size(),1);
        const auto staged=QApplication::clipboard()->mimeData()->urls().first().toLocalFile();QCOMPARE(read(staged+"/nested/file.txt"),QByteArray("remote clipboard bytes"));QVERIFY(QFileInfo(staged+"/nested/empty").isDir());
        w->pasteFiles("/root/B");QTRY_COMPARE_WITH_TIMEOUT(w->pendingTransfers(),0,15000);QCOMPARE(read(root+"/root/B/clipboard-folder/nested/file.txt"),QByteArray("remote clipboard bytes"));QVERIFY(QFileInfo(root+"/root/B/clipboard-folder/nested/empty").isDir());
        w->pasteFiles("/root");QTRY_VERIFY_WITH_TIMEOUT(w->uploadConflictDialog,10000);for(auto button:w->uploadConflictDialog->buttons())if(button->text()=="替换文件")QVERIFY(!button->isEnabled());
        for(auto button:w->uploadConflictDialog->buttons())if(button->text()=="保留两者")button->click();QTRY_COMPARE_WITH_TIMEOUT(w->pendingTransfers(),0,15000);
        QCOMPARE(read(root+"/root/clipboard-folder (1)/nested/file.txt"),QByteArray("remote clipboard bytes"));QCOMPARE(read(root+"/root/clipboard-folder/nested/file.txt"),QByteArray("remote clipboard bytes"));
        const auto localDir=data.filePath("local-clipboard");QVERIFY(QDir().mkpath(localDir+"/empty"));local("local-clipboard/content.txt","local bytes");
        auto localMime=new QMimeData;localMime->setUrls({QUrl::fromLocalFile(localDir)});QApplication::clipboard()->setMimeData(localMime);w->pasteFiles("/root/B");
        QTRY_COMPARE_WITH_TIMEOUT(w->pendingTransfers(),0,15000);QCOMPARE(read(root+"/root/B/local-clipboard/content.txt"),QByteArray("local bytes"));QVERIFY(QFileInfo(root+"/root/B/local-clipboard/empty").isDir());QVERIFY(QFileInfo(localDir+"/content.txt").exists());
        w->requestDirectory("/root");QTRY_VERIFY(w->fileItem("/root/clipboard-move.txt"));auto item=w->fileItem("/root/clipboard-move.txt");w->files->clearSelection();w->files->setCurrentItem(item);item->setSelected(true);w->activateWindow();w->files->setFocus();settle();QVERIFY(w->files->hasFocus());QTest::keyClick(w->files,Qt::Key_X,Qt::ControlModifier);
        auto cut=FileClipboard::instance().current();QVERIFY(cut);w->pasteFiles("/root/B");QTRY_COMPARE_WITH_TIMEOUT(w->pendingTransfers(),0,15000);
        QCOMPARE(read(root+"/root/B/clipboard-move.txt"),QByteArray("move bytes"));QVERIFY(!QFileInfo::exists(root+"/root/clipboard-move.txt"));QVERIFY(cut->roots.isEmpty());
        delete w;QVERIFY(QFileInfo(staged+"/nested/file.txt").exists());
    }
    void isolatedUploadThenDelete(){
        if(!remoteProfile().port)QSKIP("Run with the private upload SFTP fixture");
        DiagnosticLog::instance().startLogging();QVERIFY(DiagnosticLog::instance().isRunning());
        QVERIFY(DiagnosticLog::directory().startsWith(data.path()));
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path());
        auto stage=[&](const QString &text){QFile f(report+"/upload-delete-stage.txt");if(f.open(QIODevice::WriteOnly))f.write(text.toUtf8());};
        stage("begin");Workspace w(remoteProfile(),QByteArray("fixture-only"),true);setup(w);
        delete w.session;w.session=new Session(w.profile,"fixture-only",&w);
        QSignalSpy fingerprint(w.session,&Session::authenticatedHostIdentity),ready(w.session,&Session::connected);connectWorker(*w.session);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);
        w.attachSession(w.session);QCOMPARE(fingerprint.size(),1);w.authenticatedFingerprint=fingerprint[0][0].toString();delete w.transferSession;w.transferSession=nullptr;
        w.requestDirectory("/root");QTRY_VERIFY(w.fileItem("/root/bin"));
        w.fileItem("/root/bin")->setExpanded(true);QTRY_VERIFY(w.fileItem("/root/bin/sub-0"));
        w.fileItem("/root/bin/sub-0")->setExpanded(true);QTRY_VERIFY(w.fileItem("/root/bin/sub-0/keep"));
        const auto source=local("delete-after.txt","UPLOADED");
        stage("queue upload");w.queueTransfers({source},true,"/root");QTRY_COMPARE_WITH_TIMEOUT(w.pendingTransfers(),0,10000);
        QTRY_VERIFY(w.fileItem("/root/delete-after.txt"));
        auto item=w.fileItem("/root/delete-after.txt");w.files->setCurrentItem(item);item->setSelected(true);w.files->setFocus();
        QPersistentModelIndex index=w.files->indexFromItem(item);bool accepted=false,refreshing=false,listed=false;
        QTimer response;
        connect(&response,&QTimer::timeout,&w,[&]{
            auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(!box)return;
            if(!refreshing){stage("list during delete confirmation");refreshing=true;w.requestDirectory("/root");return;}
            if(!w.directoryRequests.isEmpty())return;
            stage("accept delete confirmation");listed=true;accepted=true;box->button(QMessageBox::Yes)->click();
        });response.start(5);
        stage("Delete key");QTest::keyClick(w.files,Qt::Key_Delete);response.stop();QVERIFY(accepted && listed);
        stage("wait delete completion");QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(fixtureRoot()+"/root/delete-after.txt"),5000);
        QTRY_VERIFY(!index.isValid());QTRY_VERIFY(w.directoryRequests.isEmpty());QVERIFY(w.fileItem("/root/bin")->isExpanded());
        stage("cancel collision then upload same name");
        w.queueTransfers({source},true,"/root/bin/sub-0");QTRY_COMPARE(w.pendingTransfers(),0);QTRY_VERIFY(w.fileItem("/root/bin/sub-0/delete-after.txt"));
        w.queueTransfers({source},true,"/root/bin/sub-0");QTRY_VERIFY(w.uploadConflictDialog);w.uploadConflictDialog->reject();QTRY_COMPARE(w.pendingTransfers(),0);
        w.queueTransfers({source},true,"/root/bin/sub-0");QTRY_VERIFY(w.uploadConflictDialog);
        for(auto button:w.uploadConflictDialog->buttons())if(button->text()=="替换文件")button->click();QTRY_COMPARE(w.pendingTransfers(),0);
        item=w.fileItem("/root/bin/sub-0/delete-after.txt");QVERIFY(item);w.files->clearSelection();w.files->setCurrentItem(item);item->setSelected(true);
        QTimer confirm;connect(&confirm,&QTimer::timeout,&w,[&]{if(auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))box->button(QMessageBox::Yes)->click();});confirm.start(5);
        stage("delete after cancelled and successful same name");w.deleteSelectedFiles();confirm.stop();
        QTRY_VERIFY(!QFileInfo::exists(fixtureRoot()+"/root/bin/sub-0/delete-after.txt"));QTRY_VERIFY(!w.fileItem("/root/bin/sub-0/delete-after.txt"));
        // One final production menu path: cancel keeps both selected targets,
        // then confirmed multi-selection removes a file and populated directory.
        local("delete-tree/child.txt","CHILD");
        w.queueTransfers({source,data.filePath("delete-tree")},true,"/root/bin/sub-0");QTRY_COMPARE(w.pendingTransfers(),0);
        QTRY_VERIFY(w.fileItem("/root/bin/sub-0/delete-tree"));QTRY_VERIFY(w.fileItem("/root/bin/sub-0/delete-after.txt"));
        auto tree=w.fileItem("/root/bin/sub-0/delete-tree");tree->setExpanded(true);QTRY_VERIFY(w.fileItem("/root/bin/sub-0/delete-tree/child.txt"));
        item=w.fileItem("/root/bin/sub-0/delete-after.txt");w.files->clearSelection();w.files->setCurrentItem(tree);tree->setSelected(true);item->setSelected(true);
        QTimer cancel;connect(&cancel,&QTimer::timeout,&w,[&]{if(auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))box->button(QMessageBox::Cancel)->click();});cancel.start(5);
        w.deleteSelectedFiles();cancel.stop();QVERIFY(QFileInfo::exists(fixtureRoot()+"/root/bin/sub-0/delete-tree/child.txt"));QVERIFY(QFileInfo::exists(fixtureRoot()+"/root/bin/sub-0/delete-after.txt"));QVERIFY(w.fileMutations.isEmpty());
        QTimer menuDriver;bool scheduled=false,menuDelete=false;QElapsedTimer menuDeadline;menuDeadline.start();
        connect(&menuDriver,&QTimer::timeout,&w,[&]{
            if(menuDeadline.elapsed()>3000){if(auto box=qobject_cast<QDialog *>(QApplication::activeModalWidget()))box->reject();if(auto menu=QApplication::activePopupWidget())menu->close();return;}
            if(auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())){box->button(QMessageBox::Yes)->click();return;}
            if(auto menu=qobject_cast<QMenu *>(QApplication::activePopupWidget()))for(auto action:menu->actions())if(action->text()=="删除…"&&!scheduled){
                scheduled=true;menuDelete=true;QTimer::singleShot(0,menu,[menu,action]{action->trigger();menu->close();});break;
            }
        });menuDriver.start(5);
        stage("right-click multi-directory delete");w.fileMenu(w.files->visualItemRect(item).center());menuDriver.stop();QVERIFY(menuDelete);
        QTRY_VERIFY(!QFileInfo::exists(fixtureRoot()+"/root/bin/sub-0/delete-tree"));QTRY_VERIFY(!QFileInfo::exists(fixtureRoot()+"/root/bin/sub-0/delete-after.txt"));
        QTRY_VERIFY(!w.fileItem("/root/bin/sub-0/delete-tree"));QTRY_VERIFY(!w.fileItem("/root/bin/sub-0/delete-after.txt"));
        QTRY_VERIFY(w.directoryRequests.isEmpty());
        DiagnosticLog::instance().stopLogging();
        QFile log(QDir(DiagnosticLog::directory()).filePath("diagnostic.jsonl"));QVERIFY(log.open(QIODevice::ReadOnly));
        const auto bytes=log.readAll();QList<QJsonObject> rows;
        for(const auto &line:bytes.split('\n'))if(!line.isEmpty())rows<<QJsonDocument::fromJson(line).object();
        int cursor=0;
        for(const auto &event:QStringList{"UploadDone","DeleteAccepted","DeleteSubmit","DeleteBegin","DeleteDone","DeleteUiBegin","RemoveBegin","RemoveEnd","DeleteUiEnd","RenderBegin","RenderSort","RenderFilter","RenderLayout","RenderEnd"}) {
            while(cursor<rows.size() && rows[cursor]["event"].toString()!=event)++cursor;
            QVERIFY2(cursor<rows.size(),qPrintable("Missing production log stage: "+event));++cursor;
        }
        QVERIFY(!bytes.contains("fixture-only"));QVERIFY(!bytes.contains("delete-after.txt"));QVERIFY(!bytes.contains("127.0.0.1"));
        stage("done");
    }
};
}
int main(int argc,char **argv){rs::DiagnosticApplication app(argc,argv);rs::FileTransferTests tests;return QTest::qExec(&tests,argc,argv);}
#include "test_filetransfer.moc"
