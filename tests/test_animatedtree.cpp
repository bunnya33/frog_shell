#include "../src/ui/animatedtree.h"
#include "../src/ui/theme.h"
#include <QtTest>

using namespace rs;

class MoveCounter : public QObject {
public:
    int count=0;
    bool eventFilter(QObject *, QEvent *event) override {
        if(event->type()==QEvent::MouseMove) ++count;
        return false;
    }
};

class AnimatedTreeTests : public QObject {
    Q_OBJECT
    QString evidence;
    static QList<QVariantAnimation *> active(AnimatedTreeWidget &tree) {
        QList<QVariantAnimation *> result;
        for (auto *a : tree.findChildren<QVariantAnimation *>())
            if (a->state() == QAbstractAnimation::Running) result.append(a);
        return result;
    }
    static QTreeWidgetItem *add(AnimatedTreeWidget &tree, const QString &name) {
        auto *item = new QTreeWidgetItem(&tree, {name, "1024"});
        return item;
    }
    void capture(QWidget *widget, const QString &name) {
        QVERIFY(widget->grab().save(evidence + "/" + name + ".png"));
    }
private slots:
    void initTestCase() {
        evidence = qEnvironmentVariable("TREE_EVIDENCE_DIR");
        QVERIFY(!evidence.isEmpty());
        QVERIFY(QDir().mkpath(evidence));
        applyTheme(false);
    }
    void geometrySelectionAndThemes() {
        AnimatedTreeWidget tree;
        tree.resize(540, 240); tree.setColumnCount(2);
        tree.header()->setSectionResizeMode(QHeaderView::Fixed);
        tree.setColumnWidth(0, 330); tree.setColumnWidth(1, 180);
        auto *root = add(tree, "directory");
        auto *child = new QTreeWidgetItem(root, {"nested file", "42"});
        tree.setTreeMotionEnabled(false);
        tree.show(); QVERIFY(QTest::qWaitForWindowExposed(&tree));
        root->setExpanded(true); child->setSelected(true);
        QTest::qWait(40);
        QCOMPARE(tree.indentation(), uiPx(24));
        QVERIFY(tree.visualItemRect(child).height() >= uiPx(28));
        const auto selectionBefore = tree.selectedItems();
        capture(tree.viewport(), "dark-selection");
        applyTheme(true); QTest::qWait(50); capture(tree.viewport(), "light-selection");
        QCOMPARE(tree.selectedItems(), selectionBefore);
        UiZoom::instance().setLevel(2); QTest::qWait(50);
        QCOMPARE(tree.indentation(), uiPx(24));
        QVERIFY(tree.visualItemRect(child).height() >= uiPx(28));
        capture(tree.viewport(), "light-zoom");
        UiZoom::instance().setLevel(0); applyTheme(false);
    }
    void motionFramesAndNativeDuration() {
        AnimatedTreeWidget tree;
        tree.resize(440, 260); tree.setHeaderHidden(true);
        auto *root = add(tree, "folder");
        for (int i=0; i<5; ++i) new QTreeWidgetItem(root, {QString("child %1").arg(i)});
        add(tree, "following row");
        tree.show(); QVERIFY(QTest::qWaitForWindowExposed(&tree)); QTest::qWait(80);
        capture(tree.viewport(), "native-before");
        root->setExpanded(true);
        QElapsedTimer elapsed; elapsed.start();
        QImage before = tree.viewport()->grab().toImage();
        QTest::qWait(65);
        auto rotations = active(tree);
        QVERIFY(!rotations.isEmpty());
        bool sawMiddle = false;
        for (auto *a : rotations) if (a->duration() >= 160 && a->currentValue().toReal() > 0.02 && a->currentValue().toReal() < 0.98) sawMiddle = true;
        QVERIFY(sawMiddle);
        QImage middle = tree.viewport()->grab().toImage();
        capture(tree.viewport(), "native-middle");
        QVERIFY(before != middle);
        root->setExpanded(false);
        auto reversed = active(tree); QVERIFY(!reversed.isEmpty());
        bool smooth = false;
        for (auto *a : reversed) if (a->endValue().toReal()==0 && a->startValue().toReal()>0 && a->startValue().toReal()<1) smooth=true;
        QVERIFY(smooth);
        QTest::qWait(300);
        root->setExpanded(true);
        elapsed.restart();
        QImage previous = tree.viewport()->grab().toImage();
        qint64 lastChange=0; int changes=0;
        while (elapsed.elapsed() < 400) {
            QTest::qWait(10);
            const QImage frame = tree.viewport()->grab().toImage();
            if (frame != previous) { lastChange=elapsed.elapsed(); ++changes; previous=frame; }
        }
        capture(tree.viewport(), "native-after");
        qInfo() << "RENDERED_EXPANSION_LAST_CHANGE_MS" << lastChange << "CHANGED_SAMPLES" << changes
                << "(includes 170ms chevron; see native-only measurement below)";
        QVERIFY(changes >= 2);
        // Sample below the root row, excluding the chevron; this captures Qt's
        // real animated row displacement without querying private Qt state.
        root->setExpanded(false); QTest::qWait(350);
        root->setExpanded(true); elapsed.restart();
        const int rootBottom = tree.visualItemRect(root).bottom();
        previous = tree.viewport()->grab().toImage().copy(0, rootBottom+1, tree.viewport()->width(), tree.viewport()->height()-rootBottom-1);
        lastChange=0; changes=0;
        while (elapsed.elapsed() < 400) {
            QTest::qWait(10);
            auto frame=tree.viewport()->grab().toImage().copy(0, rootBottom+1, tree.viewport()->width(), tree.viewport()->height()-rootBottom-1);
            if (frame != previous) {lastChange=elapsed.elapsed(); ++changes; previous=frame;}
        }
        qInfo() << "NATIVE_ROW_LAST_CHANGE_MS" << lastChange << "CHANGED_SAMPLES" << changes;
        QVERIFY(changes >= 2);
    }
    void hoverAndLifecycle() {
        AnimatedTreeWidget tree;
        tree.resize(400, 260); tree.setHeaderHidden(true);
        auto *first=add(tree,"first"); auto *second=add(tree,"second");
        auto *child=new QTreeWidgetItem(first,{"child"}); Q_UNUSED(child);
        tree.show(); QVERIFY(QTest::qWaitForWindowExposed(&tree)); QTest::qWait(50);
        MoveCounter counter; tree.viewport()->installEventFilter(&counter);
        QTest::mouseMove(tree.viewport(), QPoint(tree.viewport()->width()-5, tree.viewport()->height()-5)); QTest::qWait(150);
        const int beforeMove=counter.count;
        auto move=[&](QPoint pos) {
            QMouseEvent event(QEvent::MouseMove, pos, tree.viewport()->mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(tree.viewport(), &event);
        };
        move(tree.visualItemRect(first).center()); QTest::qWait(45);
        qInfo() << "VIEWPORT_MOUSE_MOVES" << counter.count-beforeMove;
        QVERIFY(counter.count>beforeMove);
        bool partial=false;
        for (auto *a:active(tree)) if (a->endValue().toReal()==1 && a->currentValue().toReal()>0 && a->currentValue().toReal()<1) partial=true;
        QVERIFY(partial); capture(tree.viewport(),"hover-middle");
        const auto hovered=tree.viewport()->grab().toImage();
        const int y=tree.visualItemRect(first).center().y()+8;
        const auto branchColor=hovered.pixelColor(18,y), contentColor=hovered.pixelColor(200,y);
        qInfo() << "HOVER_BRANCH_CONTENT" << branchColor << contentColor;
        QCOMPARE(branchColor,contentColor);
        move(tree.visualItemRect(second).center());
        bool reverse=false;
        for (auto *a:active(tree)) if(a->endValue().toReal()==0 && a->startValue().toReal()>0 && a->startValue().toReal()<1) reverse=true;
        QVERIFY(reverse);
        first->setExpanded(true); delete first;
        QCOMPARE(active(tree).size(), 0);
        tree.sortItems(0,Qt::DescendingOrder); tree.clear();
        QTest::qWait(30); QCOMPARE(active(tree).size(),0);
        auto *again=add(tree,"again"); new QTreeWidgetItem(again,{"child"}); again->setExpanded(true);
        tree.hide(); QCOMPARE(active(tree).size(),0);
        auto *dying=new AnimatedTreeWidget;
        dying->show(); auto *parent=add(*dying,"parent"); new QTreeWidgetItem(parent,{"child"}); parent->setExpanded(true); delete dying;
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    void blockedSignalsAndLargeTree() {
        AnimatedTreeWidget tree; tree.resize(420,300); tree.setHeaderHidden(true);
        for(int i=0;i<2500;++i) {auto *p=add(tree,QString("node %1").arg(i)); new QTreeWidgetItem(p,{"child"});}
        tree.show(); QVERIFY(QTest::qWaitForWindowExposed(&tree));
        for(int i=0;i<30;++i) {
            auto *p=tree.topLevelItem(i); p->setExpanded(true);
            QSignalBlocker blocker(&tree); delete tree.takeTopLevelItem(i);
        }
        tree.sortItems(0,Qt::AscendingOrder);
        QTest::qWait(350); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QCOMPARE(active(tree).size(),0);
        QCOMPARE(tree.findChildren<QVariantAnimation *>().size(),0);
        QSignalBlocker blocker(&tree); tree.clear();
        QCOMPARE(tree.topLevelItemCount(),0);
    }
    void terminalPeers() {
        AnimatedTreeWidget tree; tree.setTreeMode(AnimatedTreeWidget::TreeMode::TerminalPeers);
        tree.setProperty("sideTree",true);
        tree.resize(400,250); tree.setHeaderHidden(true);
        QPixmap icon(12,12); icon.fill(Qt::red);
        for (int i=1;i<=3;++i) { auto *p=add(tree,QString("peer %1").arg(i)); p->setData(0,TerminalPeerPositionRole,i); p->setIcon(0,icon); }
        auto *standalone=add(tree,"standalone"); standalone->setIcon(0,icon);
        tree.show(); QVERIFY(QTest::qWaitForWindowExposed(&tree));
        QTest::qWait(50); capture(tree.viewport(),"terminal-peers");
        const QImage image=tree.viewport()->grab().toImage();
        auto redLeft=[&](QTreeWidgetItem *item) {
            const auto row=tree.visualItemRect(item); int left=image.width();
            for(int y=row.top();y<=row.bottom();++y) for(int x=0;x<qMin(image.width(),100);++x) {
                const auto c=image.pixelColor(x,y); if(c.red()>220 && c.green()<50 && c.blue()<50) left=qMin(left,x);
            }
            return left;
        };
        qInfo() << "TERMINAL_ICON_LEFT grouped/standalone" << redLeft(tree.topLevelItem(0)) << redLeft(standalone);
        QCOMPARE(redLeft(tree.topLevelItem(0))-redLeft(standalone),uiPx(14));
        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(redLeft(standalone)+2,tree.visualItemRect(standalone).center().y()));
        QCOMPARE(tree.currentItem(),standalone);
        QCOMPARE(tree.topLevelItemCount(),4);
        for(int i=0;i<4;++i) QVERIFY(!tree.topLevelItem(i)->parent());
    }
    void deepTreeAndHorizontalScroll() {
        AnimatedTreeWidget tree; tree.resize(360,220); tree.setColumnCount(2); tree.setHeaderHidden(true);
        tree.header()->setStretchLastSection(false);
        tree.header()->setSectionResizeMode(QHeaderView::Fixed);
        tree.setColumnWidth(0,850); tree.setColumnWidth(1,200);
        tree.setTreeMotionEnabled(false);
        auto *root=add(tree,"root"); auto *node=root;
        QPixmap icon(12,12); icon.fill(Qt::red);
        for(int i=0;i<12;++i) {node=new QTreeWidgetItem(node,{QString("level %1").arg(i),"value"}); node->setIcon(0,icon); node->parent()->setExpanded(true);}
        tree.show(); QVERIFY(QTest::qWaitForWindowExposed(&tree));
        tree.scrollToItem(node); tree.horizontalScrollBar()->setValue(120); QTest::qWait(50);
        QVERIFY(tree.horizontalScrollBar()->value()>0);
        const auto rect=tree.visualItemRect(node); QVERIFY(rect.intersects(tree.viewport()->rect()));
        node->setSelected(true); QTest::qWait(30);
        capture(tree.viewport(),"deep-horizontal-scroll");
        QVERIFY(tree.selectedItems().contains(node));
        auto *accessible=QAccessible::queryAccessibleInterface(&tree);
        QVERIFY(accessible); QVERIFY(accessible->tableInterface());
        { QSignalBlocker blocked(&tree); delete root; }
        QCoreApplication::processEvents();
        QCOMPARE(tree.topLevelItemCount(),0);
        QVERIFY(QAccessible::queryAccessibleInterface(&tree));
    }
};

int main(int argc,char **argv) {
    QTemporaryDir profile;
    if (!profile.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,profile.path());
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName("AnimatedTreeIsolatedTest");
    QCoreApplication::setApplicationName("VirtualNodesOnly");
    AnimatedTreeTests test;
    return QTest::qExec(&test,argc,argv);
}
#include "test_animatedtree.moc"
