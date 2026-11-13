#include "../src/ui/editorarea.h"
#include "../src/ui/workspace.h"
#include "../src/ui/mainwindow.h"
#include "../src/ui/icons.h"
#include "../src/ui/theme.h"
#include <QtTest>
#include "../src/ui/editorlexer.h"
#include <memory>
namespace rs {
class ClosePositionStyle final : public QProxyStyle {
  public:
    explicit ClosePositionStyle(bool left) : QProxyStyle(QStyleFactory::create("Fusion")), left(left) {}
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *result) const override {
        if (hint == SH_TabBar_CloseButtonPosition)
            return left ? QTabBar::LeftSide : QTabBar::RightSide;
        return QProxyStyle::styleHint(hint, option, widget, result);
    }
  private:
    bool left;
};
class ControlledDragArea : public EditorArea {
  public:
    std::function<Qt::DropAction(QDrag *)> execute;
  protected:
    Qt::DropAction executeDrag(QDrag *drag) override {
        auto callback=execute;
        return callback ? callback(drag) : EditorArea::executeDrag(drag);
    }
};
class EditorAreaTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    int serial = 0;
    QString report;
    QWidget *page(EditorArea &area, const QString &label = {}) {
        auto p = new QPlainTextEdit;
        p->setPlainText("preserved content\nline 2\n");
        area.addPage(p,label.isEmpty()?QString("page %1").arg(++serial):label);
        return p;
    }
    void settle() {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
    QPoint center(EditorTabs *group) {
        return QPoint(group->width()/2,group->tabBar()->height()+(group->height()-group->tabBar()->height())/2);
    }
    bool enter(EditorTabs *group, const QMimeData *mime, const QPoint &position) {
        QDragEnterEvent event(position,Qt::MoveAction,mime,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(group,&event);
        return event.isAccepted();
    }
    bool move(EditorTabs *group, const QMimeData *mime, const QPoint &position) {
        QDragMoveEvent event(position,Qt::MoveAction,mime,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(group,&event);
        return event.isAccepted();
    }
    bool drop(EditorTabs *group, const QMimeData *mime, const QPoint &position) {
        QDropEvent event(position,Qt::MoveAction,mime,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(group,&event);
        return event.isAccepted() && event.dropAction()==Qt::MoveAction;
    }
    void save(QWidget *widget, const QString &name) {
        settle();
        QVERIFY2(widget->grab().save(report+"/"+name+".png"),qPrintable(name));
    }
  private slots:
    void initTestCase() {
        QVERIFY(data.isValid());
        qputenv("RABBITSHELL_DATA_DIR",data.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellEditorAreaTests");
        QCoreApplication::setApplicationName("RabbitShellEditorAreaTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,data.path());
        QApplication::setStyle("Fusion");
        // Offscreen's bundled font search path is empty in this SDK. Load real
        // system fonts in the test setup only; production uses the OS database.
        for (auto font : {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/msyhbd.ttc", "C:/Windows/Fonts/consola.ttf"})
            if (QFileInfo::exists(font)) QVERIFY(QFontDatabase::addApplicationFont(font)>=0);
        QApplication::setFont(QFont("Microsoft YaHei UI",10));
        QFont::insertSubstitution("monospace","Consolas");
        QVERIFY(QFontMetrics(QApplication::font()).inFont(QChar(u'编')));
        report = qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path());
        QVERIFY(QDir().mkpath(report));
        applyTheme(false);
    }
    void emptyGroups_data() {
        QTest::addColumn<bool>("removeLeft");
        QTest::newRow("left-empty") << true;
        QTest::newRow("right-empty") << false;
    }
    void emptyGroups() {
        QFETCH(bool,removeLeft);
        EditorArea area;
        area.resize(1000,600); area.show();
        auto a = page(area), b = page(area);
        auto left = area.activeGroup();
        QVERIFY(area.movePage(b,left,DropZone::Right));
        auto right = area.groupFor(b);
        QPointer<EditorTabs> removed = removeLeft?left:right;
        auto kept = removeLeft?right:left;
        QVERIFY(area.movePage(removeLeft?a:b,kept));
        settle();
        QVERIFY(removed.isNull());
        QCOMPARE(area.groups().size(),1);
        QCOMPARE(area.activeGroup(),kept);
        QCOMPARE(kept->count(),2);
        QVERIFY(area.findChildren<QSplitter *>().isEmpty());
        area.removePage(a); area.removePage(b);
        settle();
        QCOMPARE(area.groups().size(),1);
        QCOMPARE(area.activeGroup()->count(),0);
        QCOMPARE(a->parentWidget(),&area);
    }
    void nestedGridMergeAndIdentity() {
        EditorArea area;
        area.resize(1200,800); area.show();
        QByteArray text;
        for(int i=0;i<250;++i) text += "original line\n";
        auto editor = new Editor("isolation","/grid.txt",text,"hash");
        area.addPage(editor,"grid.txt");
        auto a=page(area), b=page(area), c=page(area);
        auto original=area.groupFor(editor);
        QVERIFY(area.movePage(a,original,DropZone::Right));
        auto right=area.groupFor(a);
        QVERIFY(area.movePage(b,original,DropZone::Bottom));
        QVERIFY(area.movePage(c,right,DropZone::Bottom));
        settle();
        QCOMPARE(area.groups().size(),4);
        QCOMPARE(area.findChildren<QSplitter *>().size(),3);
        editor->code->moveCursor(QTextCursor::End);
        editor->code->insertPlainText("changed");
        editor->code->verticalScrollBar()->setValue(22);
        auto document = editor->code->document();
        const int scroll=editor->code->verticalScrollBar()->value();
        area.activatePage(b);
        auto target=area.activeGroup();
        area.mergeAll(); settle();
        QCOMPARE(area.groups().size(),1);
        QCOMPARE(area.activeGroup(),target);
        QCOMPARE(target->count(),4);
        QCOMPARE(editor->code->document(),document);
        QCOMPARE(editor->code->verticalScrollBar()->value(),scroll);
        QVERIFY(editor->dirty());
        QVERIFY(document->isUndoAvailable());
        editor->code->undo();
        QCOMPARE(editor->bytes(),text);
        QVERIFY(area.findChildren<QSplitter *>().isEmpty());
    }
    void ordinaryMoveKeepsSizesAndActiveRouting() {
        EditorArea area;
        area.resize(1100,700); area.show();
        auto a=page(area), b=page(area), c=page(area), d=page(area);
        auto left=area.activeGroup();
        QVERIFY(area.movePage(b,left,DropZone::Right));
        auto right=area.groupFor(b);
        QVERIFY(area.movePage(c,right)); settle();
        auto split=qobject_cast<QSplitter *>(area.root);
        QVERIFY(split); split->setSizes({730,360});
        const auto sizes=split->sizes();
        QVERIFY(area.movePage(d,right)); settle();
        QCOMPARE(split->sizes(),sizes);
        area.activatePage(a);
        auto incoming=page(area);
        QCOMPARE(area.groupFor(incoming),left);
        QCOMPARE(area.groupFor(b),right);
        QCOMPARE(area.groupFor(c),right);
    }
    void singlePageSplitIsNoOp() {
        EditorArea area;
        auto p=page(area);
        QVERIFY(!area.canSplit());
        for (auto zone : {DropZone::Left,DropZone::Right,DropZone::Top,DropZone::Bottom})
            QVERIFY(!area.movePage(p,area.activeGroup(),zone));
        area.splitActive(Qt::Vertical); settle();
        QCOMPARE(area.groups().size(),1);
        QCOMPARE(area.activeGroup()->widget(0),p);
    }
    void replacedTabButtonsAreReleased_data() {
        QTest::addColumn<bool>("leftClose");
        QTest::newRow("native-left") << true;
        QTest::newRow("native-right") << false;
    }
    void replacedTabButtonsAreReleased() {
        QFETCH(bool, leftClose);
        EditorArea area;
        auto group = area.activeGroup();
        auto bar = group->tabBar();
        auto style = new ClosePositionStyle(leftClose);
        style->setParent(bar);
        bar->setStyle(style);
        const auto baseline = bar->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly).size();
        for (int i = 0; i < 20; ++i) {
            auto p = page(area);
            QPointer<QWidget> button = bar->tabButton(0, QTabBar::RightSide);
            QVERIFY(button);
            QVERIFY(!bar->tabButton(0, QTabBar::LeftSide));
            area.removePage(p); p->deleteLater(); settle();
            QVERIFY(button.isNull());
            QCOMPARE(bar->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly).size(), baseline);
        }
        MainWindow window(true); settle();
        auto workspace = window.workspaces.cbegin().value();
        auto mainBar = window.tabs->tabBar();
        const auto mainBaseline = mainBar->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly).size();
        for (int i = 0; i < 20; ++i) {
            QPointer<QWidget> old = mainBar->tabButton(window.tabs->indexOf(workspace), QTabBar::RightSide);
            window.configureWorkspaceTab(workspace); settle();
            QVERIFY(old.isNull());
            QCOMPARE(mainBar->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly).size(), mainBaseline);
        }
    }
    void lazyFindRetainsBehaviorAndLifetime() {
        auto editor = new Editor("lazy-find", "/find.txt", "alpha beta alpha", "hash");
        QPointer<EditorDocument> document = editor->code->document();
        QPointer<EditorLexer> highlighter = editor->code->findChild<EditorLexer *>();
        editor->resize(700, 400); editor->show(); settle();
        QCOMPARE(editor->code->findChildren<QLineEdit *>().size(), 0);
        QTest::keyClick(editor->code, Qt::Key_Escape);
        editor->code->showFind(); settle();
        auto fields = editor->code->findChildren<QLineEdit *>();
        QCOMPARE(fields.size(), 2);
        auto query = fields[0]; query->setText("alpha");
        QTRY_COMPARE_WITH_TIMEOUT(editor->code->textCursor().selectedText(), QString("alpha"), 5000);
        editor->code->showFind(true); settle();
        QCOMPARE(editor->code->findChildren<QLineEdit *>(), fields);
        QVERIFY(fields[1]->isVisible());
        QTest::keyClick(query, Qt::Key_Escape); settle();
        QVERIFY(!query->isVisible());
        applyTheme(true); editor->code->refreshTheme();
        editor->code->showFind(); settle();
        QCOMPARE(query->text(), QString("alpha"));
        editor->deleteLater(); settle();
        QVERIFY(document.isNull());
        QVERIFY(highlighter.isNull());
        applyTheme(false);
    }
    void groupButtonsTargetTheirOwnGroup() {
        EditorArea area;
        area.resize(1000, 500); area.show();
        auto a = page(area), b = page(area), c = page(area), d = page(area);
        auto left = area.activeGroup();
        QVERIFY(area.movePage(c, left, DropZone::Right));
        auto right = area.groupFor(c);
        QVERIFY(area.movePage(d, right)); settle();
        auto rightSplit = right->findChild<QToolButton *>("splitEditorGroup");
        area.activatePage(a);
        rightSplit->click(); settle(); // programmatic activation also must ignore the previous active group
        QCOMPARE(area.groups().size(), 3);
        QCOMPARE(area.groupFor(a), left);
        QCOMPARE(area.groupFor(b), left);
        QCOMPARE(area.groupFor(c), right);
        QVERIFY(area.groupFor(d) != right);
        QVERIFY(!rightSplit->isEnabled());
        area.activatePage(d);
        auto leftSplit = left->findChild<QToolButton *>("splitEditorGroup");
        leftSplit->menu()->actions().at(1)->trigger(); settle();
        QCOMPARE(area.groups().size(), 4);
        QCOMPARE(qobject_cast<QSplitter *>(area.groupFor(a)->parentWidget())->orientation(), Qt::Vertical);
        auto kept = right;
        right->findChild<QToolButton *>("mergeEditorGroups")->click(); settle();
        QCOMPARE(area.groups().size(), 1);
        QCOMPARE(area.activeGroup(), kept);
        QCOMPARE(kept->count(), 4);
        QCOMPARE(kept->currentWidget(), c);
        QVERIFY(!kept->findChild<QToolButton *>("mergeEditorGroups")->isEnabled());
    }
    void groupToolbarRejectsDropsAndProtectsMenu() {
        EditorArea area;
        area.resize(700, 420); area.show();
        auto a = page(area), b = page(area), c = page(area);
        auto left = area.activeGroup();
        QVERIFY(area.movePage(c, left, DropZone::Right));
        auto right = area.groupFor(c); settle();
        std::unique_ptr<QMimeData> mime(area.beginDrag(left, a));
        QVERIFY(enter(right, mime.get(), center(right)));
        QVERIFY(area.previewVisible());
        const auto corner = right->cornerWidget(Qt::TopRightCorner);
        QVERIFY(!move(right, mime.get(), corner->geometry().center()));
        QVERIFY(!area.previewVisible());
        QVERIFY(!drop(right, mime.get(), corner->geometry().center()));
        QCOMPARE(area.groupFor(a), left);
        area.liveMime.clear();
        auto menu = left->findChild<QToolButton *>("splitEditorGroup")->menu();
        menu->popup(left->mapToGlobal(QPoint(20, 30))); settle();
        QVERIFY(left->interactionActive());
        QPointer<EditorTabs> guarded(left);
        QVERIFY(area.movePage(a, right));
        QVERIFY(area.movePage(b, right)); settle();
        QVERIFY(guarded);
        menu->hide(); settle();
        QVERIFY(guarded.isNull());
    }
    void inlineGroupActionsEvidence() {
        const bool originalTheme = lightTheme();
        EditorArea area;
        area.resize(680, 360); area.show();
        auto a = page(area, "long-configuration-production.json");
        auto b = page(area, "long-server-deployment-settings.yaml");
        auto c = page(area, "long-application-service-routing.cpp");
        auto d = page(area, "long-environment-development.conf");
        auto left = area.groupFor(a);
        QVERIFY(area.movePage(c, left, DropZone::Right));
        QVERIFY(area.movePage(d, area.groupFor(c)));
        area.activatePage(b); settle();
        for (bool light : {false, true}) {
            applyTheme(light); settle();
            for (auto group : area.groups()) {
                auto tools = group->cornerWidget(Qt::TopRightCorner);
                QVERIFY(!tools->geometry().intersects(group->tabBar()->geometry()));
                QVERIFY(group->rect().contains(tools->geometry()));
                QVERIFY(qAbs(tools->geometry().center().y() - group->tabBar()->geometry().center().y()) <= 1);
                for (auto button : group->tabBar()->findChildren<QToolButton *>())
                    if (button->isVisible()) QVERIFY(!tools->geometry().intersects(QRect(button->mapTo(group, QPoint()), button->size())));
            }
            save(&area, light ? "inline-tabs-light-narrow" : "inline-tabs-dark-narrow");
        }
        applyTheme(originalTheme);
    }
    void repeatedHorizontalSplitsAndEmptyMiddleColumn() {
        EditorArea area;
        area.resize(1600,700); area.show();
        auto a=page(area), b=page(area), c=page(area), d=page(area);
        auto left=area.activeGroup(); settle();
        area.activatePage(b); area.splitActive(Qt::Horizontal); settle();
        auto split=qobject_cast<QSplitter *>(area.root);
        QVERIFY(split); QCOMPARE(split->count(),2);
        split->setSizes({950,643});
        const int peerWidth=area.groupFor(b)->width();
        const int originalWidth=left->width();
        area.activatePage(c); area.splitActive(Qt::Horizontal); settle();
        QCOMPARE(split->count(),3);
        QCOMPARE(area.findChildren<QSplitter *>().size(),1);
        QCOMPARE(area.groupFor(b)->width(),peerWidth);
        QVERIFY(qAbs(left->width()+area.groupFor(c)->width()+split->handleWidth()-originalWidth)<=1);
        area.activatePage(d); area.splitActive(Qt::Horizontal); settle();
        QCOMPARE(split->count(),4);
        QCOMPARE(area.groups().size(),4);
        QCOMPARE(area.findChildren<QSplitter *>().size(),1);
        for(auto group : area.groups()) {
            QCOMPARE(group->parentWidget(),split);
            QCOMPARE(group->y(),0); QCOMPARE(group->height(),area.height());
        }
        QPointer<EditorTabs> middle=area.groupFor(d);
        QVERIFY(area.movePage(d,area.groupFor(a))); settle();
        QVERIFY(middle.isNull()); QCOMPARE(split->count(),3);
        QCOMPARE(area.groups().size(),3);
        QCOMPARE(split->widget(0),area.groupFor(a));
        QCOMPARE(split->widget(1),area.groupFor(c));
        QCOMPARE(split->widget(2),area.groupFor(b));
        area.resize(720,500); settle();
        for(auto group : area.groups()) { QCOMPARE(group->y(),0); QCOMPARE(group->height(),area.height()); }
        area.mergeAll(); settle();
        QCOMPARE(area.groups().size(),1); QCOMPARE(area.activeGroup()->count(),4);
    }
    void dropFourthColumnMatchesTargetHalfPreview() {
        EditorArea area;
        area.resize(1600,700); area.show();
        auto a=page(area), b=page(area), c=page(area), d=page(area);
        auto target=area.activeGroup();
        QVERIFY(area.movePage(b,target,DropZone::Right));
        QVERIFY(area.movePage(c,target,DropZone::Right)); settle();
        auto split=qobject_cast<QSplitter *>(area.root);
        split->setSizes({800,400,386}); settle();
        const int bWidth=area.groupFor(b)->width(),cWidth=area.groupFor(c)->width();
        const int original=target->width();
        std::unique_ptr<QMimeData> mime(area.beginDrag(target,d));
        auto point=QPoint(target->width()-3,center(target).y());
        QVERIFY(enter(target,mime.get(),point)); QVERIFY(move(target,mime.get(),point));
        auto preview=area.findChild<QWidget *>("editorDropPreview");
        QVERIFY(preview); const QRect geometry=preview->geometry();
        QVERIFY(drop(target,mime.get(),point)); settle();
        auto fresh=area.groupFor(d);
        QCOMPARE(split->count(),4);
        QCOMPARE(area.groupFor(b)->width(),bWidth); QCOMPARE(area.groupFor(c)->width(),cWidth);
        QVERIFY(qAbs(fresh->width()-(original-split->handleWidth())/2)<=1);
        QVERIFY(qAbs(fresh->mapTo(&area,QPoint()).x()-geometry.left())<=split->handleWidth());
        QVERIFY(qAbs(fresh->width()-geometry.width())<=split->handleWidth());
        QCOMPARE(fresh->y(),0); QCOMPARE(fresh->height(),area.height());
        QCOMPARE(area.groupFor(a),target);
        area.resize(800,500); settle();
        QCOMPARE(split->count(),4);
        for(auto group : area.groups()) { QCOMPARE(group->y(),0); QCOMPARE(group->height(),area.height()); }
    }
    void dragEdges_data() {
        QTest::addColumn<int>("zone");
        QTest::newRow("left") << int(DropZone::Left);
        QTest::newRow("right") << int(DropZone::Right);
        QTest::newRow("top") << int(DropZone::Top);
        QTest::newRow("bottom") << int(DropZone::Bottom);
    }
    void dragEdges() {
        QFETCH(int,zone);
        EditorArea area;
        area.resize(1000,650); area.show();
        auto a=page(area), b=page(area);
        auto target=area.activeGroup(); settle();
        std::unique_ptr<QMimeData> mime(area.beginDrag(target,b));
        QVERIFY(mime);
        QPoint point=center(target);
        if (DropZone(zone)==DropZone::Left) point.setX(3);
        if (DropZone(zone)==DropZone::Right) point.setX(target->width()-4);
        if (DropZone(zone)==DropZone::Top) point.setY(target->tabBar()->height()+3);
        if (DropZone(zone)==DropZone::Bottom) point.setY(target->height()-4);
        QVERIFY(enter(target,mime.get(),point));
        QVERIFY(move(target,mime.get(),point));
        QVERIFY(area.previewVisible());
        QCOMPARE(area.groupFor(b),target); // preview has not removed the page
        QCOMPARE(target->count(),2);
        QVERIFY(drop(target,mime.get(),point));
        QVERIFY(!area.previewVisible()); settle();
        QCOMPARE(area.groups().size(),2);
        QVERIFY(area.groupFor(a)!=area.groupFor(b));
        auto split=qobject_cast<QSplitter *>(area.root);
        QCOMPARE(split->orientation(),zone<=int(DropZone::Right)?Qt::Horizontal:Qt::Vertical);
        QCOMPARE(split->widget(zone==int(DropZone::Left)||zone==int(DropZone::Top)?0:1),area.groupFor(b));
    }
    void dragTabOrderAndCenterAcrossGroups() {
        EditorArea area;
        area.resize(1000,650); area.show();
        auto a=page(area,"a"), b=page(area,"b"), c=page(area,"c");
        auto group=area.activeGroup(); settle();
        {
            std::unique_ptr<QMimeData> mime(area.beginDrag(group,a));
            auto point=group->tabBar()->geometry().topLeft()+group->tabBar()->tabRect(2).topRight()-QPoint(1,-5);
            QVERIFY(enter(group,mime.get(),point));
            QVERIFY(move(group,mime.get(),point));
            QVERIFY(drop(group,mime.get(),point));
            QCOMPARE(group->widget(0),b); QCOMPARE(group->widget(1),c); QCOMPARE(group->widget(2),a);
        }
        QVERIFY(area.movePage(c,group,DropZone::Right));
        auto target=area.groupFor(c); settle();
        {
            std::unique_ptr<QMimeData> mime(area.beginDrag(group,a));
            QVERIFY(enter(target,mime.get(),center(target)));
            QVERIFY(move(target,mime.get(),center(target)));
            QVERIFY(drop(target,mime.get(),center(target)));
            QCOMPARE(area.groupFor(a),target);
        }
        // Target a real child viewport, as Qt's native drop delivery does.
        {
            std::unique_ptr<QMimeData> mime(area.beginDrag(group,b));
            auto view=qobject_cast<QPlainTextEdit *>(a);
            auto viewport=view->viewport();
            QPoint point(viewport->width()/2,viewport->height()/2);
            QDragEnterEvent event(point,Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(viewport,&event); QVERIFY(event.isAccepted());
            QDragMoveEvent moving(point,Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(viewport,&moving); QVERIFY(moving.isAccepted());
            QDropEvent dropped(point,Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(viewport,&dropped); QVERIFY(dropped.isAccepted());
        }
        settle();
        QCOMPARE(area.groups().size(),1); QCOMPARE(area.activeGroup()->count(),3);
    }
    void multipleAreasRejectForeignAndForgedDrag() {
        EditorArea first;
        first.resize(1000,650); first.show();
        auto a=page(first), b=page(first);
        auto source=first.activeGroup();
        QVERIFY(first.movePage(b,source,DropZone::Right));
        auto target=first.groupFor(b);
        EditorArea newer;
        page(newer); newer.hide(); settle();
        QSignalSpy floated(&first,&EditorArea::floatPageRequested);
        std::unique_ptr<QMimeData> mime(first.beginDrag(source,a));
        QVERIFY(enter(target,mime.get(),center(target))); // hidden newer filter must pass through
        QVERIFY(move(target,mime.get(),center(target)));
        QVERIFY(!enter(newer.activeGroup(),mime.get(),QPoint(50,70)));
        QVERIFY(!newer.previewVisible());
        QMimeData forged;
        forged.setData("application/x-rabbitshell-editor-tab","internal-tab");
        QVERIFY(!enter(target,&forged,center(target)));
        QWidget sidebar; sidebar.setAcceptDrops(true);
        QDragEnterEvent invalid(QPoint(1,1),Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&sidebar,&invalid);
        QVERIFY(!invalid.isAccepted());
        QCOMPARE(first.groupFor(a),source); QCOMPARE(floated.size(),0);
        QVERIFY(enter(target,mime.get(),center(target)));
        QDragLeaveEvent leave; QApplication::sendEvent(target,&leave);
        QVERIFY(!first.previewVisible());
        QVERIFY(enter(target,mime.get(),center(target)));
        QVERIFY(drop(target,mime.get(),center(target)));
        QCOMPARE(first.groupFor(a),target);
        QVERIFY(!enter(target,mime.get(),center(target))); // committed payload cannot replay
    }
    void escapeAndNativeDragCancellation() {
        ControlledDragArea area;
        area.resize(1000,650); area.show();
        auto a=page(area), b=page(area);
        auto source=area.activeGroup(); settle();
        QSignalSpy floated(&area,&EditorArea::floatPageRequested);
        {
            std::unique_ptr<QMimeData> mime(area.beginDrag(source,a));
            QVERIFY(enter(source,mime.get(),center(source)));
            QTest::keyClick(source,Qt::Key_Escape);
            QVERIFY(!area.previewVisible());
        }
        // Offscreen's platform drag returns immediately. Run the same lifetime
        // boundary with a controlled nested loop; no OS input injection.
        area.execute=[&](QDrag *drag) {
            drag->pixmap().save(report+"/dark-drag-tab.png");
            QEventLoop loop;
            QTimer::singleShot(0,&loop,[&] { QTest::keyClick(source,Qt::Key_Escape); loop.quit(); });
            loop.exec();
            return Qt::IgnoreAction;
        };
        area.startDrag(source,a);
        QVERIFY(!area.previewVisible());
        QCOMPARE(area.groupFor(a),source); QCOMPARE(area.groupFor(b),source);
        QCOMPARE(floated.size(),0); QCOMPARE(source->count(),2);
        area.execute={};
        area.startDrag(source,a); // actual QDrag::exec smoke on the available QPA
    }
    void floatCursorAndReleaseGuard_data(){
        QTest::addColumn<int>("scenario");QTest::newRow("outside-release")<<0;QTest::newRow("inside-invalid")<<1;QTest::newRow("other-app-window")<<2;QTest::newRow("escape-outside")<<3;QTest::newRow("native-cancel-no-release")<<4;
    }
    void floatCursorAndReleaseGuard(){
        QFETCH(int,scenario);ControlledDragArea area;area.resize(900,600);area.show();auto p=page(area,"sample.cpp");auto source=area.activeGroup();
        QWidget other;other.setProperty("rabbitUi",true);other.resize(300,250);other.move(1050,40);other.show();settle();
        QSignalSpy floated(&area,&EditorArea::floatPageRequested);const auto outside=QPoint(-1200,-1200);const auto inside=area.mapToGlobal(QPoint(20,20));
        bool checks=true;
        area.execute=[&](QDrag *drag)->Qt::DropAction{
            checks &= !drag->pixmap().isNull();EditorArea::dragFeedback(drag,outside);const auto floatKey=drag->dragCursor(Qt::IgnoreAction).cacheKey();checks &= drag->property("floatCursorOutside").toBool();
            EditorArea::dragFeedback(drag,inside);checks &= !drag->property("floatCursorOutside").toBool();checks &= floatKey!=drag->dragCursor(Qt::IgnoreAction).cacheKey();
            QMouseEvent move(QEvent::MouseMove,QPointF(2,2),QPointF(outside),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(source,&move);checks &= drag->property("floatCursorOutside").toBool();
            drag->pixmap().save(report+"/0.1.19-tab-drag-badge.png");drag->dragCursor(Qt::IgnoreAction).save(report+"/0.1.19-tab-float-cursor.png");
            if(scenario==3)QTest::keyClick(source,Qt::Key_Escape);
            if(scenario!=4){const auto pos=scenario==1?inside:scenario==2?other.mapToGlobal(QPoint(30,30)):outside;QMouseEvent release(QEvent::MouseButtonRelease,QPointF(2,2),QPointF(pos),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(source,&release);}
            return Qt::IgnoreAction;
        };
        area.startDrag(source,p);QVERIFY(checks);QCOMPARE(floated.size(),scenario==0?1:0);QCOMPARE(area.groupFor(p),source);
    }
    void invalidatedSourceAndTargetCleanup() {
        EditorArea area;
        area.resize(1000,650); area.show();
        auto a=page(area), b=page(area);
        auto source=area.activeGroup();
        QVERIFY(area.movePage(b,source,DropZone::Right));
        QPointer<EditorTabs> target=area.groupFor(b); settle();
        std::unique_ptr<QMimeData> mime(area.beginDrag(source,a));
        QVERIFY(enter(target,mime.get(),center(target)));
        area.removePage(b); settle();
        QVERIFY(target.isNull()); QVERIFY(!area.previewVisible());
        area.removePage(a); delete a;
        QVERIFY(!area.validDrag(mime.get()));
        QVERIFY(!enter(area.activeGroup(),mime.get(),QPoint(40,70)));
        QCOMPARE(area.groups().size(),1);
    }
    void closeAreaDuringDragLoop() {
        auto controlled=new ControlledDragArea;
        QPointer<EditorArea> area=controlled;
        area->resize(900,600); area->show();
        auto p=page(*area); page(*area);
        QPointer<EditorTabs> source=area->activeGroup(); settle();
        controlled->execute=[area](QDrag *) {
            QEventLoop loop;
            QTimer::singleShot(0,&loop,[&] { if(area) delete area.data(); loop.quit(); });
            loop.exec();
            return Qt::IgnoreAction;
        };
        area->startDrag(source,p);
        QVERIFY(area.isNull()); QVERIFY(source.isNull());
    }
    void batchCloseDefersGroupDestruction() {
        EditorArea area;
        area.resize(1000,650); area.show();
        auto a=page(area), b=page(area), c=page(area);
        auto left=area.activeGroup();
        QVERIFY(area.movePage(c,left,DropZone::Right)); settle();
        QPointer<EditorTabs> source=left;
        connect(&area,&EditorArea::closePageRequested,&area,[&](QWidget *p) {
            area.removePage(p); p->deleteLater();
            settle(); // models an intervening dirty-page confirmation loop
            QVERIFY(source); // menu remains on the stack
        });
        QTimer::singleShot(0,qApp,[] {
            auto menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            for(auto action:menu->actions()) if(action->text()=="全部关闭") { action->trigger(); break; }
            menu->close();
        });
        left->tabBar()->customContextMenuRequested(left->tabBar()->tabRect(0).center());
        settle();
        QVERIFY(source.isNull()); QCOMPARE(area.groups().size(),1);
        QCOMPARE(area.groupFor(c),area.activeGroup());
        Q_UNUSED(a); Q_UNUSED(b);
    }
    void dirtyCancelCloseFloatAndReturn() {
        Profile profile; profile.id="close-float-test";
        Workspace workspace(profile,{},true);
        workspace.resize(1280,850); workspace.show(); settle();
        auto area=workspace.editArea;
        auto editor=workspace.editors.value("/home/demo/project/config.json");
        QVERIFY(editor);
        QPointer<EditorTabs> source=area->groupFor(editor);
        auto document=editor->code->document();
        editor->code->moveCursor(QTextCursor::End); editor->code->insertPlainText("draft");
        const auto content=editor->bytes();
        QTimer::singleShot(0,[] { if(auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Cancel)->click(); });
        workspace.closePage(editor); settle();
        QVERIFY(source); QCOMPARE(area->groupFor(editor),source.data());
        QCOMPARE(area->groups().size(),3); QCOMPARE(editor->bytes(),content);
        workspace.floatPage(editor); settle();
        QVERIFY(source.isNull()); QCOMPARE(area->groups().size(),2);
        QVERIFY(workspace.floating.contains(editor));
        QVERIFY(editor->isVisible()); QCOMPARE(editor->code->document(),document);
        QPointer<QDialog> floated = workspace.floating.value(editor);
        QTimer::singleShot(10,[] { if(auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Cancel)->click(); });
        floated->close(); settle();
        QVERIFY(floated); QVERIFY(workspace.floating.contains(editor)); QCOMPARE(editor->bytes(),content);
        QToolButton *back = nullptr;
        for (auto button : editor->findChildren<QToolButton *>()) if (button->accessibleName() == "收回工作区") back = button;
        QVERIFY(back); back->click(); settle(); QVERIFY(floated.isNull()); QVERIFY(back->isHidden());
        QVERIFY(!workspace.floating.contains(editor));
        QCOMPARE(area->groupFor(editor),area->activeGroup());
        QCOMPARE(editor->bytes(),content); QVERIFY(document->isUndoAvailable());
        editor->code->undo(); QVERIFY(!editor->bytes().endsWith("draft"));
        workspace.closePage(editor); settle();
        QCOMPARE(workspace.editors.size(),3);
        const auto remaining=workspace.editors.values();
        for (auto e : remaining) workspace.closePage(e);
        settle();
        QCOMPARE(area->groups().size(),1); QCOMPARE(area->activeGroup()->count(),0);
    }
    void processAndTerminalPagesStayUnique() {
        Profile profile; profile.id="process-terminal-test";
        Workspace workspace(profile,{},true);
        workspace.resize(1280,850); workspace.show(); settle();
        workspace.showProcessPage();
        auto process=workspace.processPage;
        auto target=workspace.editArea->groups().first();
        workspace.movePage(process,target);
        workspace.showProcessPage();
        QCOMPARE(workspace.groupFor(process),target);
        workspace.floatPage(process); settle();
        auto processWindow=workspace.floating.value(process);
        workspace.showProcessPage();
        QCOMPARE(workspace.floating.value(process),processWindow);
        QVERIFY(!workspace.groupFor(process));
        workspace.closePage(process); settle();
        workspace.showProcessPage();
        QCOMPARE(workspace.processPage,process);
        QCOMPARE(workspace.groupFor(process),workspace.editArea->activeGroup());
        const int id=workspace.terminals.begin().key();
        const auto original=workspace.terminals[id];
        original.view->feed("\r\npreserved-terminal-token\r\n");
        const auto text=original.view->text();
        const auto nextId=workspace.nextTerm;
        const auto editorGroups = workspace.editArea->groups();
        const auto editorCount = workspace.editArea->activeGroup()->count();
        workspace.movePage(original.frame,workspace.editArea->activeGroup());
        QVERIFY(!workspace.groupFor(original.frame));
        workspace.editArea->addPage(original.frame, "rejected terminal");
        QVERIFY(!workspace.groupFor(original.frame));
        QVERIFY(!workspace.editArea->movePage(original.frame,workspace.editArea->activeGroup(),DropZone::Bottom));
        workspace.floatPage(original.frame); settle();
        workspace.movePage(original.frame); settle();
        QVERIFY(workspace.floating.contains(original.frame));
        auto window = workspace.floating.value(original.frame);
        QPointer<QAction> back = window->findChild<QAction *>("returnToWorkspace");
        QVERIFY(back); QVERIFY(original.view->actions().contains(back));
        bool menuFound = false, actionFound = false;
        QTimer::singleShot(0,[back, &menuFound, &actionFound] {
            auto menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            menuFound = menu;
            if (!menu) return;
            actionFound = menu->actions().contains(back);
            if (actionFound && back) back->trigger();
            menu->close(); // Close even when the expected action is absent; assert after exec returns.
        });
        QContextMenuEvent menu(QContextMenuEvent::Mouse,QPoint(10,10),original.view->mapToGlobal(QPoint(10,10)));
        QApplication::sendEvent(original.view,&menu); settle();
        QVERIFY(menuFound); QVERIFY(actionFound);
        QVERIFY(back.isNull()); QVERIFY(original.view->actions().isEmpty());
        QVERIFY(!workspace.floating.contains(original.frame));
        QCOMPARE(workspace.editArea->groups(), editorGroups);
        QCOMPARE(workspace.editArea->activeGroup()->count(), editorCount);
        QCOMPARE(workspace.nextTerm,nextId);
        QCOMPARE(workspace.terminals[id].view,original.view);
        QCOMPARE(workspace.terminals[id].frame,original.frame);
        QCOMPARE(workspace.terminals[id].running,original.running);
        QCOMPARE(original.view->text().trimmed(),text.trimmed());
        QVERIFY(original.view->text().contains("preserved-terminal-token"));
        QCOMPARE(original.frame->parentWidget(),workspace.terminalGroups[original.group]);
        QVERIFY(!workspace.session); // offline path has no session to restart
    }
    void iconGeometryThemeAndStates() {
        applyTheme(false);
        auto b=std::unique_ptr<QToolButton>(iconButton(Icon::Settings,"设置",nullptr,IconSize::Toolbar,"Ctrl+,"));
        QCOMPARE(b->size(),QSize(28,28)); QCOMPARE(b->iconSize(),QSize(18,18));
        QCOMPARE(b->text(),QString()); QCOMPARE(b->accessibleName(),QString("设置"));
        QCOMPARE(b->toolTip(),QString("设置 · Ctrl+,"));
        auto image=b->icon().pixmap(QSize(20,20),2.0,QIcon::Normal,QIcon::Off);
        QCOMPARE(image.size(),QSize(40,40)); QCOMPARE(image.devicePixelRatio(),2.0);
        auto disabled=b->icon().pixmap(QSize(20,20),1.0,QIcon::Disabled,QIcon::Off).toImage();
        auto normal=b->icon().pixmap(QSize(20,20),1.0,QIcon::Normal,QIcon::Off).toImage();
        QVERIFY(normal!=disabled);
        applyTheme(true);
        QVERIFY(normal!=b->icon().pixmap(QSize(20,20),1.0,QIcon::Normal,QIcon::Off).toImage());
        applyTheme(false);
    }
    void offlineVisualEvidence() {
        Profile profile; profile.id="visual-demo"; profile.name="离线演示"; profile.host="demo.invalid"; profile.user="demo";
        Workspace workspace(profile,{},true);
        workspace.resize(1440,1000); workspace.show(); settle();
        auto area=workspace.editArea;
        auto third=workspace.editors.value("/home/demo/project/deploy.sh");
        auto fourth=workspace.editors.value("/home/demo/project/service.yaml");
        QVERIFY(third); QVERIFY(fourth);
        QCOMPARE(area->groups().size(),3);
        settle();
        for (bool light : {false,true}) {
            applyTheme(light); workspace.refreshTheme();
            third->code->send(SCI_COLOURISE,0,-1);
            auto colorAt=[&](int line,int column){
                const auto block=third->code->document()->findBlockByNumber(line);
                const auto style=third->code->send(SCI_GETSTYLEAT,third->code->fromUtf16(block.position()+column));
                const int rgb=int(third->code->send(SCI_STYLEGETFORE,style));
                return QColor(rgb&255,(rgb>>8)&255,(rgb>>16)&255);
            };
            QCOMPARE(colorAt(1,0),colors().comment);
            const auto quoted=third->code->document()->findBlockByNumber(2).text().indexOf('\'');
            QVERIFY(quoted>=0); QCOMPARE(colorAt(2,int(quoted)),colors().string);
            workspace.sidebar->setCurrentIndex(1);
            save(&workspace,light?"light-3-columns":"dark-3-columns");
        }
        auto right=area->groupFor(fourth);
        QVERIFY(area->movePage(fourth,right,DropZone::Right)); settle();
        QCOMPARE(area->groups().size(),4);
        for(auto group : area->groups()) {
            QCOMPARE(group->height(),area->height());
            QCOMPARE(group->y(),0);
        }
        for (bool light : {false,true}) {
            applyTheme(light); workspace.refreshTheme();
            workspace.sidebar->setCurrentIndex(0);
            save(&workspace,light?"light-4-columns-status":"dark-4-columns-status");
            workspace.sidebar->setCurrentIndex(1);
            std::unique_ptr<QMimeData> mime(area->beginDrag(area->groupFor(third),third));
            auto target=area->groups().first();
            auto point=QPoint(target->width()-4,center(target).y());
            QVERIFY(enter(target,mime.get(),point));
            QVERIFY(move(target,mime.get(),point));
            save(&workspace,light?"light-drop-preview":"dark-drop-preview");
            area->clearPreview();
            auto tools=workspace.findChildren<QToolButton *>();
            auto split=*std::find_if(tools.begin(),tools.end(),[](auto b){return b->accessibleName()=="新建文件";});
            QToolTip::showText(split->mapToGlobal(QPoint(0,split->height())),split->toolTip(),split);
            settle();
            for(auto window : QApplication::topLevelWidgets())
                if(window->windowType()==Qt::ToolTip && window->isVisible())
                    save(window,light?"light-icon-tooltip":"dark-icon-tooltip");
            QToolTip::hideText();
            workspace.transferPanel->show(); workspace.transferPanel->raise();
            save(&workspace,light?"light-transfer-frame":"dark-transfer-frame");
            workspace.transferPanel->hide();
        }
        workspace.resize(1024,760);
        for(bool light : {false,true}) {
            applyTheme(light); workspace.refreshTheme();
            save(&workspace,light?"light-narrow":"dark-narrow");
            QVERIFY(workspace.terminalPanel->layout()->itemAt(0)->widget()->height()<=48);
        }
        MainWindow manager(true);
        manager.resize(1280,850); manager.show();
        manager.tabs->setCurrentIndex(0);
        for(bool light : {false,true}) {
            applyTheme(light);
            save(&manager,light?"light-connection-manager":"dark-connection-manager");
        }
        QFile info(report+"/render-context.txt");
        QVERIFY(info.open(QIODevice::WriteOnly));
        info.write(QString("platform=%1\nDPR=%2\nUI font=%3\nChinese glyph verified=true\nSystem fonts explicitly loaded in test setup only.\nScreenshots are QWidget renders; tooltips are separate actual QToolTip widget renders.\nNative OS drag/drop and physical monitor DPI require separate desktop review.\n").arg(QGuiApplication::platformName()).arg(workspace.devicePixelRatioF()).arg(QApplication::font().family()).toUtf8());
    }
    void cleanupTestCase() { qunsetenv("RABBITSHELL_DATA_DIR"); }
};
}
QTEST_MAIN(rs::EditorAreaTests)
#include "test_editorarea.moc"
