#include "../src/ui/workspace.h"
#include "../src/ui/theme.h"
#include "../src/core/operationgate.h"
#include <QtTest>
namespace rs {
class ProcessTableTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    QString report;
    void settle() {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
    QVector<ProcessInfo> sample(int count = 60) {
        QVector<ProcessInfo> result;
        for (int i = 0; i < count; ++i)
            result.append({1000 + i, QString("service-worker-%1 --config /srv/app/config.yaml").arg(i),
                           i % 2 ? "service" : "root", "S", 95.0 - i, quint64(512 + i * 31) * 1024 * 1024});
        return result;
    }
    void monitor(Workspace &workspace, const QVector<ProcessInfo> &snapshot) {
        QByteArray bytes = "__CPU__\ncpu 100 0 100 1000 0 0 0 0\n__MEM__\nMemTotal: 8388608 kB\n"
                           "MemAvailable: 4613734 kB\n__UP__\n1000 0\n__NET__\n__DISK__\n"
                           "Filesystem 1024-blocks Used Available Capacity Mounted\n"
                           "/dev/sda 100000 50000 50000 50% /\n__PROC__\nPID USER %CPU RSS STAT COMMAND\n";
        for (const auto &p : snapshot)
            bytes += QString("%1 %2 %3 %4 %5 %6\n")
                         .arg(p.pid).arg(p.user).arg(p.cpu).arg(p.rss / 1024).arg(p.state, p.name).toUtf8();
        workspace.monitor(bytes, 12);
        settle();
    }
    int row(const ProcessTable &table, qint64 pid) {
        for (int r = 0; r < table.rowCount(); ++r)
            if (table.item(r, 0)->data(ProcessIdRole).toLongLong() == pid)
                return r;
        return -1;
    }
    QSet<qint64> selected(const ProcessTable &table) {
        QSet<qint64> ids;
        for (const auto &index : table.selectionModel()->selectedRows())
            ids.insert(table.item(index.row(), 0)->data(ProcessIdRole).toLongLong());
        return ids;
    }
    void select(ProcessTable &table, const QList<qint64> &pids, qint64 current, int col) {
        table.clearSelection();
        for (const auto pid : pids)
            table.selectionModel()->select(table.model()->index(row(table, pid), 0),
                                           QItemSelectionModel::Select | QItemSelectionModel::Rows);
        table.selectionModel()->setCurrentIndex(table.model()->index(row(table, current), col),
                                                QItemSelectionModel::NoUpdate);
    }
    Profile profile() {
        Profile p;
        p.id = "process-snapshot-test";
        p.name = "离线进程快照";
        return p;
    }

  private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QString("offscreen"));
        QVERIFY(data.isValid());
        qputenv("RABBITSHELL_DATA_DIR", data.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellProcessTests");
        QCoreApplication::setApplicationName("RabbitShellProcessTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data.path());
        QApplication::setStyle("Fusion");
        for (auto path : {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/msyhbd.ttc", "C:/Windows/Fonts/consola.ttf"})
            if (QFileInfo::exists(path))
                QVERIFY(QFontDatabase::addApplicationFont(path) >= 0);
        QApplication::setFont(QFont("Microsoft YaHei UI", 10));
        report = qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR", data.path());
        QVERIFY(QDir().mkpath(report));
        applyTheme(false);
    }
    void init() { QSettings().remove("process-table"); }
    void filterPreservesRefreshIdentity() {
        Workspace w(profile(),{},true);w.resize(1440,900);w.show();w.showProcessPage();auto table=w.processes;
        auto snapshot=sample();snapshot[2].name="Python__worker";snapshot[2].user="Deploy";monitor(w,snapshot);
        auto filter=w.processPage->findChild<QLineEdit *>("processFilter");QVERIFY(filter);table->setColumnWidth(1,415);select(*table,{1002},1002,4);auto original=table->item(row(*table,1002),4);
        filter->setText("pYtHoN__");QCOMPARE(table->visibleProcessCount(),1);QCOMPARE(table->rowCount(),60);
        snapshot[2].cpu=99.2;monitor(w,snapshot);QCOMPARE(table->visibleProcessCount(),1);QCOMPARE(filter->text(),QString("pYtHoN__"));QCOMPARE(table->item(row(*table,1002),4),original);QCOMPARE(table->columnWidth(1),415);QCOMPARE(selected(*table),QSet<qint64>{1002});
        filter->setText("DEPLOY");QCOMPARE(table->visibleProcessCount(),1);filter->setText("1002");QCOMPARE(table->visibleProcessCount(),1);filter->setText("[.*]");QCOMPARE(table->visibleProcessCount(),0);
        filter->clear();QCOMPARE(table->visibleProcessCount(),60);QCOMPARE(table->horizontalHeader()->sortIndicatorSection(),3);
        // Incomplete monitoring data cannot erase a valid process snapshot.
        w.monitor("__CPU__\ncpu 1 0 1 2\n",10);QCOMPARE(table->rowCount(),60);
        w.closePage(w.processPage);w.showProcessPage();QCOMPARE(w.processes,table);
    }
    void listeningParsers() {
        const auto ss=QByteArray("RS_SS\ntcp LISTEN 0 128 0.0.0.0:22 0.0.0.0:* users:((\"sshd\",pid=2281,fd=3))\ntcp LISTEN 0 128 [::]:443 [::]:* users:((\"nginx__worker\",pid=20,fd=3),(\"nginx\",pid=21,fd=3))\nudp UNCONN 0 0 [fe80::1%eth0]:5353 [::]:*\nudp ESTAB 0 0 127.0.0.1:4000 127.0.0.1:53\ntcp ESTAB 0 0 127.0.0.1:9000 127.0.0.1:32\nRS_END\n");
        auto s=listening::parse({ss,{},0,false});QVERIFY(s.complete&&s.available);QCOMPARE(s.endpoints.size(),3);QCOMPARE(s.endpoints[0].pid,2281);QCOMPARE(s.endpoints[1].address,QString("::"));QCOMPARE(s.endpoints[1].process,QString("nginx__worker"));QCOMPARE(s.endpoints[2].address,QString("fe80::1%eth0"));QCOMPARE(s.endpoints[2].pid,0);
        s=listening::parse({"RS_SS\npartial\nRS_NETSTAT\nActive Internet connections\nProto Recv-Q Send-Q Local Address Foreign Address State PID/Program name\ntcp6 0 0 :::80 :::* LISTEN -\nudp 0 0 0.0.0.0:53 0.0.0.0:* 800/dnsmasq\nudp 0 0 127.0.0.1:4000 127.0.0.1:53 ESTABLISHED\nRS_END\n",{},0,false});QVERIFY(s.available);QCOMPARE(s.source,QString("netstat"));QCOMPARE(s.endpoints.size(),2);QCOMPARE(s.endpoints[0].address,QString("::"));QCOMPARE(s.endpoints[1].pid,800);
        auto empty=listening::parse({"RS_SS\n\nRS_END\n",{},0,false});QVERIFY(empty.available&&empty.complete);QVERIFY(empty.endpoints.isEmpty());
        auto missing=listening::parse({"RS_SS\npermission denied\nRS_UNAVAILABLE\n",{},0,false});QVERIFY(missing.complete&&!missing.available);QVERIFY(!missing.error.isEmpty());
        QVERIFY(!listening::parse({ss.left(ss.size()-8),{},0,false}).complete);QVERIFY(!listening::parse({ss,{},0,true}).complete);
        auto invalid=listening::parse({"RS_SS\ntcp LISTEN 0 0 [::]:99999 [::]:*\nRS_END\n",{},0,false});QVERIFY(!invalid.available);
        QByteArray bounded="RS_SS\n";for(int i=1;i<=4200;++i)bounded+="tcp LISTEN 0 0 *:"+QByteArray::number(i)+" *:*\n";bounded+="RS_END\n";s=listening::parse({bounded,{},0,false});QVERIFY(s.truncated);QCOMPARE(s.endpoints.size(),listening::maxRows);
        auto c=listening::command();QVERIFY(!c.mutation);QCOMPARE(c.timeoutMs,2000);QVERIFY(!c.text.contains("/proc"));QVERIFY(!c.text.contains("sudo"));
    }
    void listeningLifecycleAndGates() {
        ListeningTable initial(false);QCOMPARE(initial.accessibleDescription(),QString("等待采集"));initial.applySnapshot({});QCOMPARE(initial.accessibleDescription(),QString("等待采集"));
        Workspace w(profile(),{},true);w.resize(1440,900);w.show();w.showListeningPage();auto page=w.listeningPage;auto table=w.listeningTable;QVERIFY(page&&table);QCOMPARE(table->rowCount(),4);QVERIFY(!w.listeningSession);
        QVERIFY(table->item(0,0)->toolTip().contains("归属之一"));QVERIFY(w.listeningStatus->toolTip().contains("端点可能共享"));
        const auto snapshot=w.listeningSnapshot;w.applyListeningSnapshot({{},false,false,false,{},"采集超时"});QCOMPARE(table->rowCount(),4);QCOMPARE(w.miniListeningState->text(),QString("已过期"));QCOMPARE(w.listeningHealth->text(),QString("已过期"));QVERIFY(w.listeningHealth->toolTip().contains("超时"));
        auto partial=snapshot;partial.truncated=true;partial.error="仅显示部分端点";w.applyListeningSnapshot(partial);QCOMPARE(w.listeningHealth->text(),QString("部分"));QVERIFY(w.listeningHealth->toolTip().contains("部分端点"));
        w.floatPage(page);settle();QVERIFY(w.floating.contains(page));QVERIFY(w.floating[page]->windowTitle().contains("监听端口"));w.showListeningPage();QCOMPARE(w.floating.size(),1);
        w.movePage(page);settle();QVERIFY(w.groupFor(page));QVERIFY(!w.floating.contains(page));w.closePage(page);QVERIFY(!page->isVisible());w.showListeningPage();QCOMPARE(w.listeningPage,page);QCOMPARE(w.listeningTable,table);
        // An explicit authentication rejection latches retry off without changing
        // the established main-session status or its credentials.
        w.online=true;w.credential=QByteArray("fixture-only");w.listeningAuthRejected();w.demo=false;
        for(int i=0;i<3;++i){w.listeningClock.invalidate();w.requestListeningSnapshot();}QVERIFY(!w.listeningSession);QVERIFY(w.online);QCOMPARE(*w.credential,QByteArray("fixture-only"));QVERIFY(w.miniListeningState->toolTip().contains("停止自动重试"));
        w.listeningAuthBlocked=false;OperationGate::setLocked(true);w.requestListeningSnapshot();QVERIFY(!w.listeningSession);w.showListeningPage();QCOMPARE(w.listeningPage,page);OperationGate::setLocked(false);w.demo=true;w.online=false;
        w.applyListeningSnapshot(snapshot);w.stopListening("SSH 已断开");QCOMPARE(w.miniListeningState->text(),QString("已过期"));QCOMPARE(table->rowCount(),4);
    }
    void listeningDefaultFillAndCustomWidths(){
        QSettings().remove("listening-table");UiZoom::instance().setLevel(0);int customAddress=0;
        {ListeningTable table(false);table.resize(1200,300);table.show();settle();QCOMPARE(table.horizontalHeader()->length(),table.viewport()->width());
         table.setColumnWidth(0,137);customAddress=table.columnWidth(1);table.resize(1400,300);settle();QCOMPARE(table.columnWidth(0),137);QCOMPARE(table.columnWidth(1),customAddress);
         UiZoom::instance().setLevel(1);settle();QCOMPARE(table.columnWidth(0),uiPx(137));UiZoom::instance().setLevel(0);settle();QCOMPARE(table.columnWidth(0),137);QCOMPARE(table.columnWidth(1),customAddress);}
        ListeningTable restored(false);restored.resize(1500,300);restored.show();settle();QCOMPARE(restored.columnWidth(0),137);QCOMPARE(restored.columnWidth(1),customAddress);QSettings().remove("listening-table");
    }
    void listeningPortColorEvidence_data(){QTest::addColumn<bool>("light");QTest::addColumn<bool>("compact");for(bool light:{false,true})for(bool compact:{false,true})QTest::newRow(qPrintable(QString("%1-%2").arg(light?"light":"dark",compact?"mini":"full")))<<light<<compact;}
    void listeningPortColorEvidence(){
        QFETCH(bool,light);QFETCH(bool,compact);applyTheme(light);ListeningTable table(compact);table.resize(compact?360:960,220);
        listening::Snapshot snapshot;snapshot.available=snapshot.complete=true;snapshot.source="隔离样例";snapshot.endpoints={{"TCP","127.0.0.1",8080,42,"node"},{"TCP","0.0.0.0",443,50,"nginx"},{"UDP","0.0.0.0",5353,51,"mdns"}};table.applySnapshot(snapshot);table.show();table.selectRow(0);settle();
        const auto image=table.viewport()->grab().toImage().convertToFormat(QImage::Format_ARGB32);const auto rect=table.visualRect(table.model()->index(0,2)).intersected(image.rect());const auto expected=light?QColor("#074a9e"):colors().accent;int pixels=0;
        // Small antialiased glyphs need not contain a fully opaque ink pixel.
        // This tight tolerance excludes both the background and normal text.
        for(int y=rect.top();y<=rect.bottom();++y)for(int x=rect.left();x<=rect.right();++x){const auto pixel=image.pixelColor(x,y);if(qAbs(pixel.red()-expected.red())<=12&&qAbs(pixel.green()-expected.green())<=12&&qAbs(pixel.blue()-expected.blue())<=12)++pixels;}
        QVERIFY(table.grab().save(report+QString("/listening-port-color-%1-%2.png").arg(light?"light":"dark",compact?"mini":"full")));
        QVERIFY2(pixels>3,"Selected port digits must render with a legible emphasis color");
    }
    void headerDragPersistsFullAndCompactSeparately() {
        int fullWidth = 0, compactWidth = 0;
        {
            ProcessTable full(false), compact(true);
            full.resize(1000, 300); compact.resize(350, 250);
            full.show(); compact.show(); settle();
            auto drag = [&](ProcessTable &table, int column, int distance) {
                auto header = table.horizontalHeader();
                const QPoint edge(header->sectionViewportPosition(column) + header->sectionSize(column) - 1, 16);
                const int before = table.columnWidth(column);
                QTest::mousePress(header->viewport(), Qt::LeftButton, {}, edge);
                QTest::mouseMove(header->viewport(), edge + QPoint(distance, 0));
                QTest::mouseRelease(header->viewport(), Qt::LeftButton, {}, edge + QPoint(distance, 0));
                QCOMPARE(table.columnWidth(column), before + distance);
            };
            drag(full, 0, 45); drag(compact, 0, 30);
            fullWidth = full.columnWidth(0); compactWidth = compact.columnWidth(0);
            full.applySnapshot(sample(), 0); compact.applySnapshot(sample(5), 0);
            QCOMPARE(full.columnWidth(0), fullWidth); QCOMPARE(compact.columnWidth(0), compactWidth);
        } // close before the debounce fires must still persist
        ProcessTable restoredFull(false), restoredCompact(true);
        QCOMPARE(restoredFull.columnWidth(0), fullWidth);
        QCOMPARE(restoredCompact.columnWidth(0), compactWidth);
        restoredFull.resize(1300, 300); restoredFull.show(); settle();
        QCOMPARE(restoredFull.columnWidth(0), fullWidth);
        for (int c = 0; c < restoredFull.columnCount(); ++c)
            QCOMPARE(restoredFull.horizontalHeader()->sectionResizeMode(c), QHeaderView::Interactive);
    }
    void fullSnapshotIdentity_data() {
        QTest::addColumn<int>("column");
        QTest::addColumn<Qt::SortOrder>("order");
        QTest::newRow("cpu-ascending") << 3 << Qt::AscendingOrder;
        QTest::newRow("rss-descending") << 4 << Qt::DescendingOrder;
    }
    void fullSnapshotIdentity() {
        QFETCH(int, column);
        QFETCH(Qt::SortOrder, order);
        Workspace workspace(profile(), {}, true);
        workspace.resize(1200, 800);
        workspace.show();
        workspace.editArea->mergeAll();
        workspace.showProcessPage();
        auto &table = *workspace.processes;
        auto snapshot = sample();
        monitor(workspace, snapshot);
        table.sortItems(column, order);
        table.setColumnWidth(2, 600);
        select(table, {1002, 1042}, 1042, 4);
        table.setFocus();
        settle();
        table.horizontalScrollBar()->setValue(table.horizontalScrollBar()->maximum() / 2);
        table.verticalScrollBar()->setValue(180);
        const int scrollX = table.horizontalScrollBar()->value(), scrollY = table.verticalScrollBar()->value();
        QVERIFY(scrollX > 0);
        QVERIFY(scrollY > 0);
        const int oldRow = row(table, 1042), width = table.columnWidth(2);
        auto existing = table.item(oldRow, 4);
        for (auto &p : snapshot) {
            p.cpu = double(p.pid - 1000) * 2;
            p.rss = quint64(3000 - (p.pid - 1000) * 31) * 1024 * 1024;
        }
        std::reverse(snapshot.begin(), snapshot.end());
        monitor(workspace, snapshot);
        QCOMPARE(selected(table), (QSet<qint64>{1002, 1042}));
        QCOMPARE(table.currentItem()->data(ProcessIdRole).toLongLong(), 1042);
        QCOMPARE(table.currentColumn(), 4);
        QVERIFY(row(table, 1042) != oldRow);
        QCOMPARE(table.item(row(table, 1042), 4), existing);
        QCOMPARE(existing->data(Qt::UserRole).toULongLong(), quint64(1698) * 1024 * 1024);
        QCOMPARE(table.horizontalHeader()->sortIndicatorSection(), column);
        QCOMPARE(table.horizontalHeader()->sortIndicatorOrder(), order);
        QCOMPARE(table.horizontalScrollBar()->value(), scrollX);
        QCOMPARE(table.verticalScrollBar()->value(), scrollY);
        QCOMPARE(table.columnWidth(2), width);
        QVERIFY(table.hasFocus());
        snapshot.removeIf([](const auto &p) { return p.pid == 1002; });
        snapshot.append({9000, "newcomer", "root", "R", 100, 999});
        monitor(workspace, snapshot);
        QCOMPARE(selected(table), (QSet<qint64>{1042}));
        QCOMPARE(table.currentItem()->data(ProcessIdRole).toLongLong(), 1042);
        snapshot.removeIf([](const auto &p) { return p.pid == 1042; });
        monitor(workspace, snapshot);
        QVERIFY(selected(table).isEmpty());
        QVERIFY(!table.currentItem());
        monitor(workspace, {});
        QCOMPARE(table.rowCount(), 0);
        QVERIFY(!table.currentItem());
        QVERIFY(selected(table).isEmpty());
        QVERIFY(!workspace.session);
    }
    void miniTopFiveIdentity() {
        Workspace workspace(profile(), {}, true);
        workspace.resize(1200, 800);
        workspace.show();
        workspace.sidebar->setCurrentIndex(0);
        auto snapshot = sample();
        monitor(workspace, snapshot);
        auto &table = *workspace.miniProcesses;
        table.parentWidget()->setFixedHeight(132);
        table.setFixedHeight(70);
        table.setColumnWidth(1, 180);
        table.setColumnWidth(2, 200);
        table.sortItems(2, Qt::AscendingOrder);
        select(table, {1001, 1003}, 1003, 2);
        settle();
        table.horizontalScrollBar()->setValue(20);
        table.verticalScrollBar()->setValue(20);
        const int x = table.horizontalScrollBar()->value(), y = table.verticalScrollBar()->value();
        QVERIFY(x > 0 && y > 0);
        auto existing = table.item(row(table, 1001), 0);
        QVector<ProcessInfo> second = {snapshot[3], snapshot[0], snapshot[9], snapshot[1], snapshot[5]};
        second += snapshot.mid(6);
        second[0].rss = 99 * 1024;
        second[3].cpu = 92.1;
        monitor(workspace, second);
        QCOMPARE(table.rowCount(), 5);
        QCOMPARE(selected(table), (QSet<qint64>{1001, 1003}));
        QCOMPARE(table.currentItem()->data(ProcessIdRole).toLongLong(), 1003);
        QCOMPARE(table.currentColumn(), 2);
        QCOMPARE(table.item(row(table, 1001), 0), existing);
        QCOMPARE(table.horizontalHeader()->sortIndicatorSection(), 2);
        QCOMPARE(table.horizontalHeader()->sortIndicatorOrder(), Qt::AscendingOrder);
        QCOMPARE(table.horizontalScrollBar()->value(), x);
        QCOMPARE(table.verticalScrollBar()->value(), y);
        second[0].cpu = 0.1; // selected PID leaves the CPU top five but remains in the full sample
        monitor(workspace, second);
        QCOMPARE(selected(table), (QSet<qint64>{1001}));
        QVERIFY(!table.currentItem());
        monitor(workspace, {});
        QCOMPARE(table.rowCount(), 0);
        QVERIFY(selected(table).isEmpty());
    }
    void themeEvidence_data() {
        QTest::addColumn<bool>("light");
        QTest::newRow("dracula") << false;
        QTest::newRow("github-light") << true;
    }
    void themeEvidence() {
        QFETCH(bool, light);
        applyTheme(light);
        Workspace workspace(profile(), {}, true);
        workspace.resize(1440, 850);
        workspace.show();
        workspace.editArea->mergeAll();
        workspace.showProcessPage();
        workspace.sidebar->setCurrentIndex(0);
        workspace.miniProcesses->parentWidget()->setFixedWidth(290);
        monitor(workspace, sample(30));
        select(*workspace.processes, {1002, 1005}, 1005, 1);
        select(*workspace.miniProcesses, {1001}, 1001, 0);
        workspace.processes->setFocus();
        settle();
        const QString prefix = report + (light ? "/process-github-light" : "/process-dracula");
        QVERIFY(workspace.processPage->grab().save(prefix + "-full.png"));
        QVERIFY(workspace.miniProcesses->parentWidget()->grab().save(prefix + "-mini.png"));
        QVERIFY(!workspace.session);
    }
};
} // namespace rs
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    rs::ProcessTableTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_processes.moc"
