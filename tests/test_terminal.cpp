#include "../src/ui/terminal.h"
#include "../src/ui/theme.h"
#include "../src/ui/contentfont.h"
#include "../src/ui/vterm_extensions.h"
#include "../src/core/diagnostics.h"
#include <QtTest>

namespace rs {
#ifdef Q_OS_MACOS
constexpr auto controlKey = Qt::Key_Meta;
constexpr auto controlModifier = Qt::MetaModifier;
#else
constexpr auto controlKey = Qt::Key_Control;
constexpr auto controlModifier = Qt::ControlModifier;
#endif
class PaintCounter : public QObject {
  public:
    int paints = 0;
    QRegion region;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Paint) {
            ++paints;
            region += static_cast<QPaintEvent *>(event)->region();
        }
        return false;
    }
};
class TerminalTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    QString report;
    int originalFlash = 0;

    void settle() {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
    void show(Terminal &t) {
        t.resize(800, 300);
        t.show();
        t.activateWindow();
        t.setFocus();
        settle();
    }
    QPoint at(const Terminal &t, int col, int row = 0) {
        return QPoint(6 + col * t.cellW + t.cellW / 2, 4 + row * t.cellH + t.cellH / 2);
    }
    void drag(Terminal &t, int from, int to) {
        QTest::mousePress(t.viewport(), Qt::LeftButton, {}, at(t, from));
        QMouseEvent move(QEvent::MouseMove, at(t, to), t.viewport()->mapToGlobal(at(t, to)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(t.viewport(), &move);
        QTest::mouseRelease(t.viewport(), Qt::LeftButton, {}, at(t, to));
    }
    void controlC(Terminal &t, bool repeat = false) {
        // Model the physical modifier press separately: this was the destructive path.
        QKeyEvent control(QEvent::KeyPress, controlKey, controlModifier);
        QApplication::sendEvent(&t, &control);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_C, controlModifier, "c", repeat);
        QApplication::sendEvent(&t, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_C, controlModifier, "c", repeat);
        QApplication::sendEvent(&t, &release);
        QKeyEvent controlRelease(QEvent::KeyRelease, controlKey, Qt::NoModifier);
        QApplication::sendEvent(&t, &controlRelease);
    }
    QImage frame(Terminal &t) {
        return t.viewport()->grab().toImage();
    }
    void resizeGrid(Terminal &t,int columns,int lines) {
        for(int n=0;n<4;++n){
            t.resize(t.width()+(columns-t.cols)*t.cellW,t.height()+(lines-t.rows)*t.cellH);settle();
            if(t.cols==columns&&t.rows==lines)break;
        }
        QCOMPARE(t.cols,columns);QCOMPARE(t.rows,lines);
    }
    QStringList physicalRows(const Terminal &t) {
        QStringList result;
        for(int row=0;row<int(t.history.size())+t.rows;++row){
            QString text;const int used=t.usedColumns(row);
            for(int column=0;column<used;++column){VTermScreenCell cell{};if(!t.cell(row,column,&cell)||cell.chars[0]==0xffffffffU)continue;
                if(!cell.chars[0])text+=' ';
                else for(auto ch:cell.chars){if(!ch)break;if(ch!=0xffffffffU)text+=QString::fromUcs4(reinterpret_cast<const char32_t *>(&ch),1);}
            }
            result<<text;
        }
        while(!result.isEmpty()&&result.last().isEmpty())result.removeLast();
        return result;
    }
    QStringList pm2Table() {
        QStringList lines{QString("┌")+QString(148,QChar(0x2500))+"┐",
            QString("│ id │ service             │ namespace │ version │ mode │ pid │ uptime │ status │ cpu │ memory │ user │ watching").leftJustified(149,' ')+"│",
            QString("├")+QString(148,QChar(0x2500))+"┤"};
        for(int i=0;i<3;++i)lines<<QString("│ %1 │ relay-%1 │ default │ 1.0 │ fork │ 381%1 │ 5M │ online │ 0% │ 35MiB │ demo │ disabled").arg(i).leftJustified(149,' ')+"│";
        lines<<QString("└")+QString(148,QChar(0x2500))+"┘";return lines;
    }
    QStringList logicalRows(const Terminal &t) {
        QStringList result;const auto physical=physicalRows(t);
        for(int row=0;row<physical.size();++row){if(!result.isEmpty()&&t.continuesPrevious(row))result.last()+=physical[row];else result<<physical[row];}
        return result;
    }
    QString screenRow(const Terminal &t,int row) {
        QString text;
        for(int col=0;col<t.cols;++col) {
            VTermScreenCell cell{};vterm_screen_get_cell(t.screen,{row,col},&cell);
            if(cell.chars[0]==0xffffffffU)continue;
            if(!cell.chars[0])text+=' ';
            for(auto ch:cell.chars){if(!ch)break;text+=QString::fromUcs4(reinterpret_cast<const char32_t *>(&ch),1);}
        }
        while(text.endsWith(' '))text.chop(1);
        return text;
    }
    QStringList grid(const Terminal &t) {QStringList lines;for(int r=0;r<t.rows;++r)lines<<screenRow(t,r);return lines;}
    void clearMenu(Terminal &t,bool expectedEnabled=true) {
        bool seen=false;QTimer driver;driver.setInterval(5);
        connect(&driver,&QTimer::timeout,this,[&]{if(auto menu=qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            driver.stop();for(auto a:menu->actions())if(a->text()=="清屏") {
                seen=true;QCOMPARE(a->isEnabled(),expectedEnabled);if(a->isEnabled())a->trigger();break;
            }menu->close();
        }});
        QTimer watchdog;watchdog.setSingleShot(true);connect(&watchdog,&QTimer::timeout,this,[]{if(auto m=qobject_cast<QMenu *>(QApplication::activePopupWidget()))m->close();});
        driver.start();watchdog.start(1500);QContextMenuEvent e(QContextMenuEvent::Mouse,QPoint(20,20),t.viewport()->mapToGlobal(QPoint(20,20)));
        QApplication::sendEvent(t.viewport(),&e);QVERIFY(seen);settle();
    }

  private slots:
    void initTestCase() {
        // Never use the native clipboard or the user's preferences, even when run directly.
        QCOMPARE(QGuiApplication::platformName(), QString("offscreen"));
        QVERIFY(data.isValid());
        qputenv("RABBITSHELL_DATA_DIR", data.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellTerminalTests");
        QCoreApplication::setApplicationName("RabbitShellTerminalTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data.path());
        QApplication::setStyle("Fusion");
        for (auto path : {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/consola.ttf"})
            if (QFileInfo::exists(path))
                QVERIFY(QFontDatabase::addApplicationFont(path) >= 0);
        QFont::insertSubstitution("monospace", "Consolas");
        report = qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR", data.path());
        QVERIFY(QDir().mkpath(report));
        originalFlash = QGuiApplication::styleHints()->cursorFlashTime();
    }
    void init() {
        applyTheme(false);
        QGuiApplication::styleHints()->setCursorFlashTime(200);
        QApplication::clipboard()->setText("sentinel");
    }
    void historyTableRoundTrip_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void historyTableRoundTrip(){
        QFETCH(bool,light);applyTheme(light);Terminal t;show(t);resizeGrid(t,180,10);QSignalSpy input(&t,&Terminal::input),sizes(&t,&Terminal::sizeChanged);
        const auto lines=pm2Table();for(int n=0;n<2;++n)for(const auto &line:lines)t.feed(line.toUtf8()+"\r\n");t.feed("fixture$ draft");
        const auto before=physicalRows(t);QCOMPARE(before.size(),15);QVERIFY(!t.history.empty());
        resizeGrid(t,72,10);resizeGrid(t,180,10);QCOMPARE(physicalRows(t),before);
        QCOMPARE(t.cursor.col,14);t.feed("-tail");QVERIFY(physicalRows(t).last().endsWith("fixture$ draft-tail"));QVERIFY(input.isEmpty());
        QVERIFY(!sizes.isEmpty());QCOMPARE(sizes.last().at(0).toInt(),t.cols);QCOMPARE(sizes.last().at(1).toInt(),t.rows);
        t.verticalScrollBar()->setValue(0);settle();QVERIFY(frame(t).save(report+QString("/pm2-history-roundtrip-%1.png").arg(light?"light":"dark")));
    }
    void historyHardBreaksAndRealSpaces(){
        Terminal t;show(t);resizeGrid(t,80,8);const QString exact(80,'H');
        const QString soft=QString(112,'x')+QString::fromUtf8("中é")+QString(15,'y')+"  ";
        t.feed((exact+"\r\n\r\n\r\nshort  \r\n"+soft+"\r\ntail\r\n").toUtf8());
        for(int n=0;n<9;++n)t.feed(QString("line-%1\r\n").arg(n).toUtf8());
        const auto physical=physicalRows(t),logical=logicalRows(t);QVERIFY(physical.contains(exact));QVERIFY(logical.contains("short  "));QVERIFY(logical.contains(soft));
        QCOMPARE(logical.mid(0,4),QStringList({exact,"","","short  "}));QSignalSpy input(&t,&Terminal::input);
        resizeGrid(t,43,5);resizeGrid(t,120,13);resizeGrid(t,80,8);
        QCOMPARE(physicalRows(t),physical);QCOMPARE(logicalRows(t),logical);QVERIFY(input.isEmpty());
    }
    void historyScreenSeam_data(){QTest::addColumn<bool>("alt");QTest::newRow("primary")<<false;QTest::newRow("inactive-primary")<<true;}
    void historyScreenSeam(){
        QFETCH(bool,alt);Terminal t;show(t);resizeGrid(t,80,4);
        const QString longLine=QString(80*7+10,'x')+QString::fromUtf8("中é")+"  ";
        t.feed("prefix\r\n\x1b[1;3;38;2;1;2;3m"+longLine.toUtf8()+"\x1b[0m");
        const auto physical=physicalRows(t),logical=logicalRows(t);QVERIFY(!t.history.empty());QVERIFY(t.continuesPrevious(int(t.history.size())));
        QCOMPARE(logical,QStringList({"prefix",longLine}));
        QString selected;if(!alt){t.selectionStart={77,2};t.selectionEnd={5,int(t.history.size())+1};t.selectionActive=true;selected=t.selection();QVERIFY(!selected.contains('\n'));}
        QSignalSpy input(&t,&Terminal::input);
        if(alt)t.feed("\x1b[?1049hALT fixed grid");
        for(int n=0;n<5;++n){resizeGrid(t,43,3);resizeGrid(t,120,9);resizeGrid(t,80,4);QVERIFY(t.history.size()<=2000);QVERIFY(t.historyBytes()<=2*1024*1024);}
        if(alt){QVERIFY(t.alternate);QVERIFY(t.text().contains("ALT fixed grid"));t.feed("\x1b[?1049l");}
        QCOMPARE(physicalRows(t),physical);QCOMPARE(logicalRows(t),logical);if(!alt){QVERIFY(t.hasSelection());QCOMPARE(t.selection(),selected);}
        bool wide=false,combined=false;
        for(int row=0;row<int(t.history.size())+t.rows;++row)for(int col=0;col<t.usedColumns(row);++col){VTermScreenCell c{};t.cell(row,col,&c);
            if(c.chars[0]==0x4e2d){wide=true;QCOMPARE(c.width,char(2));QVERIFY(c.attrs.bold);QVERIFY(c.attrs.italic);QCOMPARE(c.fg.rgb.red,uchar(1));QCOMPARE(c.fg.rgb.green,uchar(2));QCOMPARE(c.fg.rgb.blue,uchar(3));}
            if(c.chars[0]=='e'&&c.chars[1]==0x301)combined=true;
        }
        QVERIFY(wide&&combined);t.feed("-tail");QCOMPARE(logicalRows(t).last(),longLine+"-tail");QVERIFY(input.isEmpty());
    }
    void protectedCellsSurviveResize(){
        Terminal t;show(t);resizeGrid(t,80,8);t.feed("\x1b[1\"qKEEP\x1b[0\"q erase");
        resizeGrid(t,43,6);resizeGrid(t,80,8);t.feed("\x1b[?2J");
        QVERIFY(t.text().contains("KEEP"));QVERIFY(!t.text().contains("erase"));
    }
    void resizeScrollAnchorAndHistoryBudget(){
        Terminal t;show(t);resizeGrid(t,100,8);for(int n=0;n<1100;++n)t.feed(QString("%1 ").arg(n,4,10,QChar('0')).toUtf8()+QByteArray(85,'x')+"\r\n");
        t.feed("fixture$ ");t.verticalScrollBar()->setValue(100);const auto anchor=t.glyphOrdinal({0,100});QVERIFY(anchor);
        const auto discarded=t.discardedGlyphs;resizeGrid(t,50,8);QCOMPARE(t.verticalScrollBar()->value(),t.glyphPosition(*anchor-(t.discardedGlyphs-discarded)).y());
        resizeGrid(t,100,8);const auto rowCount=t.history.size();const auto bytes=t.historyBytes();
        for(int n=0;n<12;++n){resizeGrid(t,72,5);resizeGrid(t,100,8);QVERIFY(t.history.size()<=2000);QVERIFY(t.historyBytes()<=2*1024*1024);}
        // Narrowing may evict rows at the cap; repeating the same cycle cannot
        // grow caches or manufacture additional logical content.
        QVERIFY(t.history.size()<=rowCount);QVERIFY(t.historyBytes()<=bytes);t.feed("done");QVERIFY(logicalRows(t).last().endsWith("fixture$ done"));
    }
    void clearPreservesCursorAndInput() {
        Terminal t;show(t);QSignalSpy input(&t,&Terminal::input);
        t.feed("old output\r\nolder output\r\nfixture$ echo ABCDE\x1b[3D\x1b[?25l");
        const auto before=t.cursor;const auto line=screenRow(t,before.row);
        QVERIFY(frame(t).save(report+"/clear-before.png"));clearMenu(t);
        QVERIFY(frame(t).save(report+"/clear-after.png"));QCOMPARE(input.size(),0);
        QCOMPARE(t.cursor.row,before.row);QCOMPARE(t.cursor.col,before.col);QCOMPARE(screenRow(t,before.row),line);
        QVERIFY(!t.text().contains("old output"));
    }
    void clearVisualEvidence_data() {
        QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;
    }
    void clearVisualEvidence() {
        QFETCH(bool,light);applyTheme(light);Terminal t;show(t);
        t.feed(QString("earlier output\r\nfixture@loopback:~$ echo 中文 é ABCDE\x1b[3D\x1b[2 q").toUtf8());
        const auto before=grid(t);const auto position=t.cursor;QSignalSpy input(&t,&Terminal::input);
        const auto suffix=QString(light?"light":"dark");QVERIFY(frame(t).save(report+"/clear-input-before-"+suffix+".png"));
        clearMenu(t);t.activateWindow();t.setFocus();settle();QVERIFY(t.cursorInView());
        QVERIFY(frame(t).save(report+"/clear-input-after-"+suffix+".png"));QCOMPARE(input.size(),0);
        QCOMPARE(screenRow(t,position.row),before[position.row]);
        // Diagnostic rendering of the exact OLD local VT sequence, separate from
        // the production menu. Red production assertions are retained in terminal-red.txt.
        t.feed("\x1b[2J\x1b[H");QVERIFY(screenRow(t,position.row).isEmpty());QCOMPARE(t.cursor.row,0);
        QVERIFY(frame(t).save(report+"/legacy-clear-sequence-"+suffix+".png"));
    }
    void clearWrappedLogicalLine() {
        Terminal t;show(t);t.feed("previous output\r\n");
        const auto bytes=QString("fixture$ "+QString(t.cols-14,'x')+"中é-TAIL").toUtf8();t.feed(bytes);
        t.feed("\x1b[1A\x1b[8G\x1b[?25l");const auto before=t.cursor;const auto lines=grid(t);
        clearMenu(t);QCOMPARE(t.cursor.row,before.row);QCOMPARE(t.cursor.col,before.col);
        for(int r=1;r<=2;++r)QCOMPARE(screenRow(t,r),lines[r]);
        QVERIFY(!t.text().contains("previous output"));
    }
    void clearRelativeEditingAndModes() {
        Terminal t;show(t);t.feed("old\r\nfixture$ abcdef\x1b[3D");QSignalSpy input(&t,&Terminal::input);
        const auto original=t.cursor;clearMenu(t);QCOMPARE(t.cursor.row,original.row);QCOMPARE(t.cursor.col,original.col);
        for(int row=0;row<t.rows;++row)if(row!=original.row)QVERIFY(screenRow(t,row).isEmpty());
        QTest::keyClick(&t,Qt::Key_Left);QCOMPARE(input.takeFirst()[0].toByteArray(),QByteArray("\x1b[D"));t.feed("\x1b[D");
        QTest::keyClicks(&t,"Z");QCOMPARE(input.takeFirst()[0].toByteArray(),QByteArray("Z"));t.feed("\x1b[@Z");
        QCOMPARE(screenRow(t,original.row),QString("fixture$ abZcdef"));
        QTest::keyClick(&t,Qt::Key_Delete);QCOMPARE(input.takeFirst()[0].toByteArray(),QByteArray("\x1b[3~"));t.feed("\x1b[P");
        QCOMPARE(screenRow(t,original.row),QString("fixture$ abZdef"));
        QTest::keyClick(&t,Qt::Key_Right);QCOMPARE(input.takeFirst()[0].toByteArray(),QByteArray("\x1b[C"));t.feed("\x1b[C");
        QTest::keyClick(&t,Qt::Key_Return);QCOMPARE(input.takeFirst()[0].toByteArray(),QByteArray("\r"));t.feed("\r\nexecuted\r\nfixture$ ");
        clearMenu(t);clearMenu(t);QVERIFY(input.isEmpty());QVERIFY(!t.text().contains("executed"));
        // Origin/scroll region and DEC saved cursor survive; clear does not issue CUP.
        t.feed("\x1b[3;8r\x1b[?6h\x1b[2;5H\x1b[1;31mCOLOR\x1b[?1048h");
        const auto saved=t.cursor;VTermScreenCell ink{};vterm_screen_get_cell(t.screen,{saved.row,saved.col-1},&ink);
        clearMenu(t);QVERIFY(input.isEmpty());VTermScreenCell after{};vterm_screen_get_cell(t.screen,{saved.row,saved.col-1},&after);
        QCOMPARE(after.attrs.bold,ink.attrs.bold);QCOMPARE(after.fg.type,ink.fg.type);QCOMPARE(after.fg.indexed.idx,ink.fg.indexed.idx);
        t.feed("X\x1b[?1048l");QCOMPARE(t.cursor.row,saved.row);QCOMPARE(t.cursor.col,saved.col);
        t.feed("\x1b[1;1H");QCOMPARE(t.cursor.row,2);QCOMPARE(t.cursor.col,0); // origin still at region top
        t.feed("\x1b[6;1HQ\n");QCOMPARE(t.cursor.row,7); // LF scrolls at original region bottom
        QCOMPARE(input.size(),0);
    }
    void clearParserAndPendingWrap() {
        Terminal t;show(t);QSignalSpy input(&t,&Terminal::input);
        t.feed("old\r\nfixture$ ");const auto where=t.cursor;
        const auto wide=QString("中").toUtf8();t.feed(wide.left(1));clearMenu(t);t.feed(wide.mid(1));
        QCOMPARE(screenRow(t,where.row),QString("fixture$ 中"));
        t.feed("e");clearMenu(t);t.feed(QString("́").toUtf8());VTermScreenCell combined{};
        vterm_screen_get_cell(t.screen,{where.row,where.col+2},&combined);QCOMPARE(combined.chars[0],uint32_t('e'));QCOMPARE(combined.chars[1],uint32_t(0x301));
        t.feed("\x1b[38;2;");clearMenu(t);t.feed("1;2;3mQ");VTermScreenCell colored{};
        vterm_screen_get_cell(t.screen,{where.row,t.cursor.col-1},&colored);QVERIFY(VTERM_COLOR_IS_RGB(&colored.fg));QCOMPARE(colored.fg.rgb.red,uchar(1));QCOMPARE(colored.fg.rgb.green,uchar(2));QCOMPARE(colored.fg.rgb.blue,uchar(3));
        t.feed("\r\x1b[2K");t.feed(QByteArray(t.cols,'a'));const auto full=t.cursor;clearMenu(t);t.feed("b");
        QCOMPARE(t.cursor.row,full.row+1);QCOMPARE(t.cursor.col,1);QCOMPARE(screenRow(t,full.row),QString(t.cols,'a'));QCOMPARE(screenRow(t,full.row+1),QString("b"));
        QCOMPARE(input.size(),0);
    }
    void clearHistoryResizeAndGuards() {
        Terminal t;show(t);for(int i=0;i<t.rows+12;++i)t.feed(QString("history-%1\r\n").arg(i).toUtf8());
        t.feed("fixture$ input");settle();QVERIFY(!t.history.empty());t.selectionActive=true;t.selectionStart={0,0};t.selectionEnd={4,1};
        const auto cursor=t.cursor;clearMenu(t);QCOMPARE(t.historyBytes(),qsizetype(0));QVERIFY(t.history.empty());QVERIFY(!t.selectionActive);QCOMPARE(t.verticalScrollBar()->maximum(),0);
        // The scrollbar's disappearance must not relocate the input through an
        // unrequested grid resize. The effective PTY width remains authoritative.
        QCOMPARE(t.cursor.row,cursor.row);QCOMPARE(screenRow(t,t.cursor.row),QString("fixture$ input"));
        t.resize(t.width()-150,t.height()+70);settle();QVERIFY(t.text().contains("fixture$ input"));QVERIFY(!t.text().contains("history-"));
        t.feed("\x1b[?1049h\x1b[HEDITOR\x1b[4;1Hstatus");const auto alt=grid(t);const auto position=t.cursor;QSignalSpy input(&t,&Terminal::input);
        clearMenu(t,false);t.clearOutput();QCOMPARE(grid(t),alt);QCOMPARE(t.cursor.row,position.row);QCOMPARE(t.cursor.col,position.col);QCOMPARE(input.size(),0);
        t.feed("\x1b[?1049l");QVERIFY(t.text().contains("fixture$ input"));
        // A command longer than the viewport is left untouched, with explicit
        // disabled-action explanation instead of discarding an unknown prefix.
        t.feed("\r\n"+QByteArray(t.cols*(t.rows+2),'x'));settle();QVERIFY(!t.history.empty());
        QCOMPARE(rabbitshell_vterm_screen_can_clear_output(t.screen),0);const auto all=t.text();clearMenu(t,false);t.clearOutput();QCOMPARE(t.text(),all);
    }
    void clearThenFullscreenContrast() {
        Terminal t;show(t);t.feed("old\r\nfixture$ abcdef\x1b[3D");const auto start=t.cursor;clearMenu(t);
        for(int cycle=0;cycle<2;++cycle) {
            const auto before=grid(t);t.feed("\x1b[?1049h\x1b[H\x1b[2JEDITOR\x1b[5;1Hstatus");
            t.feed("\x1b[?1049l"); // synthetic Vim suspend/return, no invented shell output
            QCOMPARE(grid(t),before);QCOMPARE(t.cursor.row,start.row);QCOMPARE(t.cursor.col,start.col);
        }
        t.feed("\x1b[D\x1b[@Z");QCOMPARE(screenRow(t,start.row),QString("fixture$ abZcdef"));
    }
    void alternateWithHistory_data() {
        QTest::addColumn<int>("mode");QTest::addColumn<bool>("reserveScrollbar");
        for(int mode:{1047,1049})for(bool reserve:{false,true})
            QTest::newRow(qPrintable(QString("mode%1-%2").arg(mode).arg(reserve?"fixed-scrollbar":"as-needed")))<<mode<<reserve;
    }
    void alternateWithHistory() {
        QFETCH(int,mode);QFETCH(bool,reserveScrollbar);Terminal t;show(t);
        if(reserveScrollbar){t.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);settle();}
        for(int i=0;i<t.rows+14;++i)t.feed(QString("history-%1 retained\r\n").arg(i).toUtf8());
        t.feed("fixture$ vim README.md\r\nlegitimate lower output\x1b[1A\rfixture$ \x1b[?25l");settle();
        const auto before=t.text();const auto cursor=t.cursor;const auto cols=t.cols;const auto rows=t.rows;
        const auto enter=QString("\x1b[?%1h\x1b[H\x1b[2JEDITOR\x1b[5;1Hstatus").arg(mode).toLatin1();
        const auto leave=QString("\x1b[?%1l").arg(mode).toLatin1();
        for(int cycle=0;cycle<3;++cycle) {
            t.feed(enter);settle();QVERIFY(t.alternate);QCOMPARE(t.verticalScrollBar()->maximum(),0);
            if(mode==1047)t.feed(QString("\x1b[%1;%2H").arg(cursor.row+1).arg(cursor.col+1).toLatin1());
            t.feed(leave);settle();QVERIFY(!t.alternate);
            QFile evidence(report+QString("/alternate-baseline-%1-%2.json").arg(mode).arg(reserveScrollbar));
            QVERIFY(evidence.open(QIODevice::WriteOnly));evidence.write(QJsonDocument(QJsonObject{{"cycle",cycle},{"before_cols",cols},{"after_cols",t.cols},{"before_rows",rows},{"after_rows",t.rows},{"before_cursor_row",cursor.row},{"after_cursor_row",t.cursor.row},{"before",before},{"after",t.text()}}).toJson());
            QCOMPARE(t.cols,cols);QCOMPARE(t.rows,rows);QCOMPARE(t.text(),before);
            QCOMPARE(t.cursor.row,cursor.row);QCOMPARE(t.cursor.col,cursor.col);
            const auto image=frame(t);t.viewport()->update();settle();QCOMPARE(frame(t),image);
        }
    }
    void alternateResizeHistoryAndPaint() {
        Terminal t;show(t);for(int i=0;i<t.rows+15;++i)t.feed(QString("history-%1\r\n").arg(i).toUtf8());
        t.feed("fixture$ abcdef\r\nlegitimate lower output\x1b[1A\r\x1b[12G\x1b[?25l");settle();
        auto logical=t.text();logical.remove('\n');
        const auto ordinal=t.glyphOrdinal({t.cursor.col,t.cursor.row+int(t.history.size())});QVERIFY(ordinal);
        t.feed("\x1b[?1049h\x1b[H\x1b[2JEDITOR\x1b[4;1Hstatus");
        t.resize(650,380);settle();t.feed("\x1b[H\x1b[2JREDRAW\x1b[3;1Hstatus");
        t.feed("\x1b[?1049l");settle();QVERIFY(!t.alternate);
        auto after=t.text();after.remove('\n');QCOMPARE(after,logical);
        QCOMPARE(t.glyphOrdinal({t.cursor.col,t.cursor.row+int(t.history.size())}),ordinal);
        QVERIFY(t.text().contains("legitimate lower output"));
        const auto old=screenRow(t,t.cursor.row);const auto col=t.cursor.col;t.feed("X");auto expected=old;expected[col]='X';QCOMPARE(screenRow(t,t.cursor.row),expected);
        t.verticalScrollBar()->setValue(0);const auto historyFrame=frame(t);t.viewport()->update();settle();QCOMPARE(frame(t),historyFrame);
        t.verticalScrollBar()->setValue(t.verticalScrollBar()->maximum());t.clearFocus();settle();
        const auto unfocused=frame(t);t.viewport()->update();settle();QCOMPARE(frame(t),unfocused);
        t.setFocus();settle();QCOMPARE(frame(t),unfocused); // remote cursor hidden; no phantom caret
        QVERIFY(unfocused.save(report+"/alternate-resize-return.png"));
    }
    void terminalNumericDiagnostics() {
        QTemporaryDir logs;QVERIFY(logs.isValid());const auto previous=qgetenv("RABBITSHELL_DATA_DIR");
        qputenv("RABBITSHELL_DATA_DIR",logs.path().toUtf8());auto &logger=DiagnosticLog::instance();logger.startLogging();
        auto cleanup=qScopeGuard([&]{logger.stopLogging();qputenv("RABBITSHELL_DATA_DIR",previous);});
        Terminal t;show(t);t.feed("SYNTHETIC_CONTENT_MUST_NOT_BE_LOGGED\r\nfixture$ input");clearMenu(t);
        t.feed("\x1b[?1049h\x1b[HTUI\x1b[?1049l");t.resize(700,350);settle();logger.stopLogging();
        QFile file(logs.filePath("logs/diagnostic.jsonl"));QVERIFY(file.open(QIODevice::ReadOnly));const auto bytes=file.readAll();
        QVERIFY(!bytes.contains("SYNTHETIC_CONTENT"));QVERIFY(!bytes.contains("fixture$"));QVERIFY(!bytes.contains("TUI"));
        QSet<QString> events;for(const auto &line:bytes.split('\n')) {
            if(line.isEmpty())continue;QVERIFY(line.size()<512);const auto object=QJsonDocument::fromJson(line).object();const auto event=object["event"].toString();
            if(!QStringList{"TerminalGrid","TerminalClear","TerminalScreenState"}.contains(event))continue;
            events.insert(event);QVERIFY(object["terminal"].isString());
            for(const auto key:{"columns","rows","cursorRow","cursorColumn","historyRows","scroll","scrollMaximum"})QVERIFY(object[key].isDouble());
            QVERIFY(object["alternate"].isBool());QVERIFY(!object.contains("pathHash"));
        }
        QCOMPARE(events.size(),3);QFile evidence(report+"/terminal-numeric-diagnostics.jsonl");QVERIFY(evidence.open(QIODevice::WriteOnly));evidence.write(bytes);
    }
    void cleanupTestCase() {
        QGuiApplication::styleHints()->setCursorFlashTime(originalFlash);
    }
    void embeddedContentFont() {
        QCOMPARE(contentFontFamily(), QString("Cascadia Code NF"));
        QFile resource(":/fonts/CascadiaCodeNF.ttf");
        QVERIFY(resource.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(resource.readAll(), QCryptographicHash::Sha256).toHex(),
                 QByteArray("16d00fae9fc289cda8c9ee4c578ccb4d410f3fa54ec0e5a3901c059b0248a970"));
        Terminal t;
        show(t);
        QCOMPARE(QFontInfo(t.font()).family(), QString("Cascadia Code NF"));
        QCOMPARE(t.font().pixelSize(), 14);
        const QFontMetricsF metrics(t.font(), t.viewport());
        QVERIFY(qAbs(metrics.horizontalAdvance('i') - metrics.horizontalAdvance('M')) < .01);
        QVERIFY(QRawFont::fromFont(t.font()).supportsCharacter(0xe0b0));
        QVERIFY(QRawFont::fromFont(t.font()).supportsCharacter(0xf120));
        QVERIFY(!QRawFont::fromFont(t.font()).supportsCharacter(0x4e2d));
        QVERIFY(metrics.inFont(QChar(u'中'))); // system fallback remains enabled
    }
    void copyThenInterrupt_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("to");
        QTest::addColumn<QString>("expected");
        QTest::newRow("indentation") << QByteArray("  alpha  tail") << 0 << 8 << QString("  alpha  ");
        QTest::newRow("blank") << QByteArray("      ") << 1 << 4 << QString("    ");
        QTest::newRow("reverse") << QByteArray("  alpha  tail") << 8 << 0 << QString("  alpha  ");
        QTest::newRow("wide") << QString("  中文 X").toUtf8() << 0 << 6 << QString("  中文 ");
        QTest::newRow("wide-reverse") << QString("  中文 X").toUtf8() << 6 << 0 << QString("  中文 ");
        QTest::newRow("wide-continuation") << QString("  中文 X").toUtf8() << 3 << 5 << QString("中文");
    }
    void copyThenInterrupt() {
        QFETCH(QByteArray, bytes);
        QFETCH(int, from);
        QFETCH(int, to);
        QFETCH(QString, expected);
        Terminal t;
        show(t);
        t.feed(bytes);
        drag(t, from, to);
        QVERIFY(t.selectionActive);
        QCOMPARE(t.selection(), expected);
        QSignalSpy input(&t, &Terminal::input);
        PaintCounter paints;
        t.viewport()->installEventFilter(&paints);
        settle();
        paints.paints = 0;
        controlC(t);
        QCOMPARE(QApplication::clipboard()->text(), expected);
        QCOMPARE(input.size(), 0);
        QVERIFY(!t.selectionActive);
        QTRY_VERIFY(paints.paints > 0);
        controlC(t, true);
        QCOMPARE(input.size(), 0);
        controlC(t);
        QCOMPARE(input.size(), 1);
        QCOMPARE(input[0][0].toByteArray(), QByteArray(1, 3));
        controlC(t, true);
        QCOMPARE(input.size(), 1);
    }
    void modifiersClickAndPureCopy() {
        Terminal t;
        show(t);
        t.feed("abc def");
        drag(t, 0, 2);
        QSignalSpy input(&t, &Terminal::input);
        for (auto key : {Qt::Key_Control, Qt::Key_Shift, Qt::Key_Alt, Qt::Key_Meta, Qt::Key_AltGr}) {
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
            QApplication::sendEvent(&t, &press);
            QVERIFY(t.selectionActive);
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
            QApplication::sendEvent(&t, &release);
        }
        QCOMPARE(input.size(), 0);
        QTest::keyClick(&t, Qt::Key_C, controlModifier | Qt::ShiftModifier);
        QCOMPARE(QApplication::clipboard()->text(), QString("abc"));
        QVERIFY(t.selectionActive);
        QCOMPARE(input.size(), 0);
        QTest::mouseClick(t.viewport(), Qt::LeftButton, {}, at(t, 2));
        QVERIFY(!t.selectionActive);
        QTest::keyClick(&t, Qt::Key_C, controlModifier | Qt::ShiftModifier);
        QCOMPARE(input.size(), 0);
        controlC(t);
        QCOMPARE(input.size(), 1);
        QCOMPARE(input[0][0].toByteArray(), QByteArray(1, 3));
        QCOMPARE(QApplication::clipboard()->text(), QString("abc"));
    }
    void findUsesTerminalColumns() {
        Terminal t;
        show(t);
        t.feed(QString("  中文 needle  ").toUtf8());
        QVERIFY(t.findSelection("needle"));
        QCOMPARE(t.selectionStart, QPoint(7, 0));
        QSignalSpy input(&t, &Terminal::input);
        controlC(t);
        QCOMPARE(QApplication::clipboard()->text(), QString("needle"));
        QCOMPARE(input.size(), 0);
        QVERIFY(!t.selectionActive);
        QVERIFY(t.findSelection("中文"));
        controlC(t);
        QCOMPARE(QApplication::clipboard()->text(), QString("中文"));
        QCOMPARE(input.size(), 0);
        controlC(t);
        QCOMPARE(input.size(), 1);
    }
    void cursorOnlyMovementAndInput() {
        QGuiApplication::styleHints()->setCursorFlashTime(0);
        Terminal t;
        show(t);
        t.feed("abcdef\x1b[3D");
        QTest::qWait(25); // let the existing 16 ms text damage batch finish
        QCOMPARE(t.cursor.col, 3);
        const QImage before = frame(t);
        PaintCounter paints;
        t.viewport()->installEventFilter(&paints);
        QSignalSpy input(&t, &Terminal::input);
        const auto old = t.cursorRect();
        QTest::keyClick(&t, Qt::Key_Left);
        QCOMPARE(input.size(), 1);
        QCOMPARE(input[0][0].toByteArray(), QByteArray("\x1b[D"));
        QCOMPARE(t.cursor.col, 3); // no local echo/editing: wait for remote feedback
        t.feed("\x1b[D");
        QCOMPARE(t.cursor.col, 2);
        QCOMPARE(t.text().trimmed(), QString("abcdef")); // movement without text damage
        // The key's old-position repaint may arrive before the 16 ms remote-output batch.
        QTRY_VERIFY(paints.region.intersects(t.cursorRect()));
        QVERIFY(paints.region.intersects(old));
        QVERIFY(frame(t) != before);
        QCOMPARE(t.inputMethodQuery(Qt::ImCursorRectangle).toRect(), t.cursorRect());
        for (auto key : {Qt::Key_Right, Qt::Key_Home, Qt::Key_End}) {
            const int col = t.cursor.col;
            QTest::keyClick(&t, key);
            QCOMPARE(t.cursor.col, col);
        }
        QCOMPARE(input.size(), 4);
        t.feed("\x1b[C\r\x1b[5C");
        QCOMPARE(t.cursor.col, 5);
    }
    void tabStaysInTerminal() {
        QWidget window;
        QVBoxLayout layout(&window);
        QLineEdit before, after;
        Terminal t;
        layout.addWidget(&before);
        layout.addWidget(&t);
        layout.addWidget(&after);
        QWidget::setTabOrder(&before, &t);
        QWidget::setTabOrder(&t, &after);
        window.resize(800, 350);
        window.show();
        window.activateWindow();
        t.setFocus();
        settle();
        QSignalSpy input(&t, &Terminal::input);
        QTest::keyClick(&t, Qt::Key_Tab);
        QCOMPARE(input.size(), 1);
        QCOMPARE(input[0][0].toByteArray(), QByteArray(1, '\t'));
        QVERIFY(t.hasFocus());
        QTest::keyClick(&t, Qt::Key_Tab, Qt::ShiftModifier);
        QCOMPARE(input.size(), 2);
        QCOMPARE(input[1][0].toByteArray(), QByteArray("\x1b[Z"));
        QVERIFY(t.hasFocus());
        QTest::keyClick(&t, Qt::Key_Backtab);
        QCOMPARE(input.size(), 3);
        QCOMPARE(input[2][0].toByteArray(), QByteArray("\x1b[Z"));
        QTest::keyClick(&t, Qt::Key_Tab);
        QKeyEvent repeated(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, "\t", true);
        QApplication::sendEvent(&t, &repeated);
        QCOMPARE(input.size(), 5);
        QCOMPARE(input[3][0].toByteArray(), QByteArray(1, '\t'));
        QCOMPARE(input[4][0].toByteArray(), QByteArray(1, '\t'));
        QVERIFY(t.hasFocus());
        before.setFocus();
        QTest::keyClick(&before, Qt::Key_Tab);
        QVERIFY(t.hasFocus());
        QCOMPARE(input.size(), 5); // a normal field's Tab traverses focus without terminal bytes
        after.setFocus();
        QTest::keyClick(&after, Qt::Key_Tab, Qt::ShiftModifier);
        QVERIFY(t.hasFocus());
        QCOMPARE(input.size(), 5);
    }
    void blinkVisibilityAndShapes() {
        Terminal t;
        show(t);
        t.feed("abc\x1b[2D");
        QVERIFY(t.cursorTimer.isActive());
        QVERIFY(t.cursorOn);
        const QImage on = frame(t);
        QTRY_VERIFY_WITH_TIMEOUT(!t.cursorOn, 500);
        QVERIFY(frame(t) != on);
        t.feed("\x1b[D");
        QVERIFY(t.cursorOn);
        QTRY_VERIFY_WITH_TIMEOUT(!t.cursorOn, 500);
        QTest::keyClick(&t, Qt::Key_Right);
        QVERIFY(t.cursorOn);
        QTRY_VERIFY_WITH_TIMEOUT(!t.cursorOn, 500);
        QTest::keyClick(&t, Qt::Key_A);
        QVERIFY(t.cursorOn);
        QTest::qWait(25);
        PaintCounter paints;
        t.viewport()->installEventFilter(&paints);
        t.feed("\x1b[?25l");
        QVERIFY(!t.cursorVisible);
        QVERIFY(!t.cursorTimer.isActive());
        QTRY_VERIFY(paints.paints > 0);
        const auto hidden = frame(t);
        QTest::keyClick(&t, Qt::Key_A);
        QVERIFY(!t.cursorVisible);
        QCOMPARE(frame(t), hidden);
        paints.paints = 0;
        t.feed("\x1b[?25h");
        QVERIFY(t.cursorVisible);
        QVERIFY(t.cursorTimer.isActive());
        QVERIFY(t.cursorOn);
        QTRY_VERIFY(paints.paints > 0);
        for (int mode = 1; mode <= 6; ++mode) {
            t.feed("\x1b[" + QByteArray::number(mode) + " q");
            QCOMPARE(t.cursorShape, mode <= 2 ? VTERM_PROP_CURSORSHAPE_BLOCK :
                                    mode <= 4 ? VTERM_PROP_CURSORSHAPE_UNDERLINE : VTERM_PROP_CURSORSHAPE_BAR_LEFT);
            QCOMPARE(t.cursorBlink, bool(mode % 2));
            QCOMPARE(t.cursorTimer.isActive(), bool(mode % 2));
            const QImage visible = frame(t);
            const QRect caret = t.cursorRect();
            const QPoint probe = mode <= 2 ? caret.topLeft() : mode <= 4 ? caret.bottomLeft() : caret.topLeft();
            QCOMPARE(visible.pixelColor(probe), colors().text);
            t.feed("\x1b[?25l");
            QVERIFY(frame(t) != visible);
            t.feed("\x1b[?25h");
        }
        t.feed("\x1b[?12h");
        QVERIFY(t.cursorTimer.isActive());
        t.feed("\x1b[?12l");
        QVERIFY(!t.cursorTimer.isActive());
        t.feed("\x1b[1 q");
        QGuiApplication::styleHints()->setCursorFlashTime(0);
        QVERIFY(!t.cursorTimer.isActive());
        QVERIFY(t.cursorOn);
    }
    void cursorLifecycle() {
        QWidget window;
        QVBoxLayout layout(&window);
        Terminal t;
        QLineEdit other;
        layout.addWidget(&t);
        layout.addWidget(&other);
        window.resize(800, 300);
        window.show();
        window.activateWindow();
        t.setFocus();
        settle();
        QVERIFY(t.cursorTimer.isActive());
        other.setFocus();
        QVERIFY(!t.cursorTimer.isActive());
        t.setFocus();
        QVERIFY(t.cursorTimer.isActive());
        QVERIFY(t.cursorOn);
        t.hide();
        QVERIFY(!t.cursorTimer.isActive());
        t.show();
        t.setFocus();
        settle();
        QVERIFY(t.cursorTimer.isActive());
        QWidget inactive;
        inactive.show();
        inactive.activateWindow();
        settle();
        QVERIFY(!t.cursorTimer.isActive());
        window.activateWindow();
        t.setFocus();
        settle();
        QVERIFY(t.cursorTimer.isActive());
        window.showMinimized();
        settle();
        QVERIFY(!t.cursorTimer.isActive());
        window.showNormal();
        window.activateWindow();
        t.setFocus();
        settle();
        QVERIFY(t.cursorTimer.isActive());
        for (int i = 0; i < 80; ++i)
            t.feed("history\r\n");
        QVERIFY(t.verticalScrollBar()->maximum() > 0);
        t.verticalScrollBar()->setValue(0);
        QVERIFY(!t.cursorTimer.isActive());
        t.verticalScrollBar()->setValue(t.verticalScrollBar()->maximum());
        QVERIFY(t.cursorTimer.isActive());
        QVERIFY(t.cursorOn);
        window.hide();
        QVERIFY(!t.cursorTimer.isActive());
    }
    void interruptReturnsToLiveScreen() {
        Terminal t;
        show(t);
        for (int i = 0; i < 80; ++i)
            t.feed("history line\r\n");
        t.verticalScrollBar()->setValue(0);
        drag(t, 0, 6);
        QSignalSpy input(&t, &Terminal::input);
        controlC(t);
        QCOMPARE(input.size(), 0);
        QCOMPARE(t.verticalScrollBar()->value(), 0);
        QCOMPARE(QApplication::clipboard()->text(), QString("history"));
        controlC(t);
        QCOMPARE(input.size(), 1);
        QCOMPARE(input[0][0].toByteArray(), QByteArray(1, 3));
        QCOMPARE(t.verticalScrollBar()->value(), t.verticalScrollBar()->maximum());
        QVERIFY(t.cursorTimer.isActive());
        QVERIFY(t.cursorOn);
    }
    void cursorThemeEvidence_data() {
        QTest::addColumn<bool>("light");
        QTest::newRow("dracula") << false;
        QTest::newRow("github-light") << true;
    }
    void cursorThemeEvidence() {
        QFETCH(bool, light);
        applyTheme(light);
        Terminal t;
        show(t);
        // Capture the real Terminal/libvterm rendering with deterministic fixture text.
        t.feed(QString("RabbitShell 0.1.5 | libvterm cursor rendering\r\n"
                       "  echo 中文 cursor-in-line\r\n"
                       "  Block / underline / bar follow remote DECSCUSR\r\n"
                       "  Ctrl+C: selection copies; no selection sends 0x03\x1b[2;9H").toUtf8());
        QCOMPARE(t.cursor.col, 8); // continuation of 中: cursor spans its two cells
        QCOMPARE(t.cursorRect().width(), 2 * t.cellW);
        const QString prefix = report + "/terminal-" + (light ? "github-light" : "dracula");
        QVERIFY(t.cursorOn);
        const auto on = frame(t);
        QVERIFY(on.save(prefix + "-cursor-on.png"));
        QTRY_VERIFY_WITH_TIMEOUT(!t.cursorOn, 500);
        const auto off = frame(t);
        QVERIFY(off != on);
        QVERIFY(off.save(prefix + "-cursor-off.png"));
        t.feed("\x1b[4 q");
        QVERIFY(frame(t).save(prefix + "-underline.png"));
        t.feed("\x1b[6 q");
        QVERIFY(frame(t).save(prefix + "-bar.png"));
    }
};
} // namespace rs

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    rs::TerminalTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_terminal.moc"
