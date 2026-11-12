#include "../src/ui/editor.h"
#include "../src/ui/contentfont.h"
#include "../src/ui/theme.h"
#include <QtTest>
#include <QTextBlock>

using namespace rs;

class EditorHighlightTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    QString report;

    static void settle() { QCoreApplication::processEvents(); QTest::qWait(30); }
    static QImage image(CodeEdit &edit) { settle(); return edit.viewport()->grab().toImage(); }
    static QColor pixel(const QImage &image, QPoint point) {
        const qreal dpr = image.devicePixelRatio();
        return image.pixelColor(qRound(point.x() * dpr), qRound(point.y() * dpr));
    }
    static QColor rowBackground(CodeEdit &edit, const QImage &shot) {
        return pixel(shot, QPoint(edit.viewport()->width() - 12, edit.cursorRect().center().y()));
    }
    static int edgePixels(CodeEdit &edit, const QImage &shot, QColor color) {
        int count = 0;
        for (int y = 0; y < edit.viewport()->height(); ++y)
            if (pixel(shot, QPoint(0, y)) == color) ++count;
        return count;
    }
    static int colorPixels(const QImage &shot, const QColor &color) {
        int count = 0;
        for (int y = 0; y < shot.height(); ++y)
            for (int x = 0; x < shot.width(); ++x)
                if (shot.pixelColor(x, y) == color) ++count;
        return count;
    }
    static int firstEdge(CodeEdit &edit, const QImage &shot, const QColor &color) {
        for (int y = 0; y < edit.viewport()->height(); ++y)
            if (pixel(shot, QPoint(0, y)) == color) return y;
        return -1;
    }
    void save(const QImage &shot, const QString &name) {
        QVERIFY2(shot.save(report + "/editor-highlight-" + name + ".png"), qPrintable(name));
    }
    static void placeOnLine(CodeEdit &edit, int number) {
        auto cursor = edit.textCursor();
        cursor.setPosition(edit.document()->findBlockByNumber(number).position());
        edit.setTextCursor(cursor);
        edit.ensureCursorVisible();
    }

  private slots:
    void initTestCase() {
        QVERIFY(data.isValid());
        qputenv("RABBITSHELL_DATA_DIR", data.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellEditorHighlightTests");
        QCoreApplication::setApplicationName("RabbitShellEditorHighlightTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data.path());
        QApplication::setStyle("Fusion");
        for (auto font : {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/consola.ttf"})
            if (QFileInfo::exists(font)) QVERIFY(QFontDatabase::addApplicationFont(font) >= 0);
        QFont::insertSubstitution("monospace", "Consolas");
        report = qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR", data.path());
        QVERIFY(QDir().mkpath(report));
    }
    void init() {
        UiZoom::instance().setLevel(0);
        ContentFonts::instance().resetDefaults();
        applyTheme(false);
    }
    void states_data() {
        QTest::addColumn<bool>("light");
        QTest::newRow("dracula") << false;
        QTest::newRow("github-light") << true;
    }
    void states() {
        QFETCH(bool, light);
        applyTheme(light);
        CodeEdit edit;
        edit.resize(760, 300);
        edit.setPlainText("// first\n\nconst a = 1;\n// filler\nconst needle = needle;\nlast\n");
        const QString original = edit.toPlainText();
        const bool modified = edit.document()->isModified();
        edit.show();
        edit.activateWindow();
        edit.setFocus();
        QTRY_VERIFY(edit.hasFocus());
        placeOnLine(edit, 4);
        const auto background = colors().background;
        const QColor border(light ? "#d0d7de" : "#44475a");
        const QImage normal = image(edit);
        QCOMPARE(rowBackground(edit, normal), background);
        QVERIFY(edgePixels(edit, normal, border) >= edit.cursorRect().height());
        QVERIFY(edit.viewport()->geometry().left() > 0); // Gutter is outside the frame.
        QCOMPARE(pixel(normal, QPoint(edit.viewport()->width() - 1, edit.cursorRect().center().y())), border);
        save(normal, light ? "light-cursor" : "dark-cursor");

        // A real keyboard selection removes the entire line frame and does not
        // paint a full-width selection in the unused part of the line.
        QTest::keyClick(&edit, Qt::Key_Right, Qt::ShiftModifier);
        QVERIFY(edit.textCursor().hasSelection());
        const QImage selected = image(edit);
        QCOMPARE(edgePixels(edit, selected, border), 0);
        QCOMPARE(rowBackground(edit, selected), background);
        save(selected, light ? "light-selection" : "dark-selection");
        QTest::keyClick(&edit, Qt::Key_Left);
        QVERIFY(!edit.textCursor().hasSelection());
        QVERIFY(edgePixels(edit, image(edit), border) >= edit.cursorRect().height());

        // Enter through the actual shortcut; wait for the asynchronous search.
        QTest::keyClick(&edit, Qt::Key_F, Qt::ControlModifier);
        auto query = edit.findChild<QLineEdit *>("findQuery");
        auto status = edit.findChild<QLabel *>("findStatus");
        QVERIFY(query && status);
        QTest::keyClicks(query, "needle");
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(" / 2"), 5000);
        QVERIFY(query->hasFocus());
        QVERIFY(edit.textCursor().hasSelection());
        QCOMPARE(edit.textCursor().selectedText(), QString("needle"));
        const QImage search = image(edit);
        save(search, light ? "light-search" : "dark-search");
        QVERIFY(rowBackground(edit, search) != background);
        const QTextCursor previous(edit.textCursor().block().previous());
        QCOMPARE(pixel(search,QPoint(edit.viewport()->width()-12,edit.cursorRect(previous).center().y())),background);
        QCOMPARE(edgePixels(edit, search, border), 0);
        // All matches are drawn, with a distinct current match and a line tint.
        const auto selections = edit.extraSelections();
        QCOMPARE(selections.size(), 3);
        QVERIFY(selections[1].format.background().color() != selections[2].format.background().color());
        QVERIFY(colorPixels(search, selections[1].format.background().color()) > 0);
        QVERIFY(colorPixels(search, selections[2].format.background().color()) > 0);
        edit.setFocus();
        QTRY_VERIFY(edit.hasFocus());
        const QImage focusedSearch = image(edit);
        QVERIFY(colorPixels(focusedSearch, selections[1].format.background().color()) > 0);
        save(focusedSearch, light ? "light-search-editor-focus" : "dark-search-editor-focus");
        QTest::keyClick(&edit, Qt::Key_Right);
        QVERIFY(!edit.textCursor().hasSelection());
        QCOMPARE(rowBackground(edit, image(edit)), background);
        QVERIFY(edit.styleSheet().isEmpty());
        query->setFocus();

        QVERIFY(query->hasFocus());
        QTest::keyClick(query, Qt::Key_Return);
        QTRY_VERIFY(status->text().startsWith("2 /"));
        QCOMPARE(edit.textCursor().selectedText(), QString("needle"));
        QCOMPARE(edit.toPlainText(),original);
        QTest::keyClick(query,Qt::Key_Return,Qt::ShiftModifier);QTRY_VERIFY(status->text().startsWith("1 /"));QCOMPARE(edit.textCursor().selectedText(),QString("needle"));QCOMPARE(edit.toPlainText(),original);
        QTest::keyClick(query,Qt::Key_Enter);QTRY_VERIFY(status->text().startsWith("2 /"));QCOMPARE(edit.textCursor().selectedText(),QString("needle"));QCOMPARE(edit.toPlainText(),original);
        auto expand=edit.findChild<QToolButton *>("findToggleReplace");QVERIFY(expand);QTest::mouseClick(expand,Qt::LeftButton);
        auto replacement=edit.findChild<QLineEdit *>("findReplacement");QVERIFY(replacement&&replacement->isVisible());replacement->setFocus();replacement->setText("replacement");
        for(const auto key:{Qt::Key_Return,Qt::Key_Enter}){QTest::keyClick(replacement,key);QCOMPARE(edit.toPlainText(),original);QCOMPARE(edit.document()->isModified(),modified);}
        query->setFocus();
        QTest::keyClick(query, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(query, Qt::Key_Backspace);
        QTRY_VERIFY(edit.extraSelections().isEmpty());
        QVERIFY(!edit.textCursor().hasSelection());
        QCOMPARE(rowBackground(edit, image(edit)), background);
        QTest::keyClicks(query, "absent");
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("无结果"), 5000);
        QVERIFY(edit.extraSelections().isEmpty());
        auto regex = edit.findChild<QToolButton *>("findRegex");
        QVERIFY(regex);
        QTest::mouseClick(regex, Qt::LeftButton);
        query->setFocus();
        QTest::keyClick(query, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(query, "(");
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("正则表达式无效"), 5000);
        QVERIFY(edit.extraSelections().isEmpty());
        QTest::mouseClick(regex, Qt::LeftButton);
        query->setFocus();
        QTest::keyClick(query, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(query, "needle");
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(" / 2"), 5000);
        QTest::keyClick(query, Qt::Key_Escape);
        QTRY_VERIFY(edit.hasFocus());
        QVERIFY(edit.extraSelections().isEmpty());
        QVERIFY(!edit.textCursor().hasSelection());
        QTest::keyClick(&edit, Qt::Key_Right);
        QVERIFY(!edit.textCursor().hasSelection());
        QVERIFY(edgePixels(edit, image(edit), border) >= edit.cursorRect().height());
        QCOMPARE(edit.toPlainText(), original);
        QCOMPARE(edit.document()->isModified(), modified);
    }
    void closingFindPreservesManualSelectionAndRejectsStaleResults() {
        CodeEdit edit; edit.setPlainText(QString(150000,'x')+" needle end"); edit.resize(700,300); edit.show();
        edit.showFind(); auto query=edit.findChild<QLineEdit *>("findQuery"); auto status=edit.findChild<QLabel *>("findStatus");
        query->setText("needle"); QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(" / 1"),5000);
        QVERIFY(edit.textCursor().hasSelection());
        QTextCursor manual(edit.document());manual.setPosition(0);manual.setPosition(4,QTextCursor::KeepAnchor);edit.setTextCursor(manual);
        QTest::keyClick(query,Qt::Key_Escape); QCOMPARE(edit.textCursor(),manual);
        edit.showFind();query->setText("x");QTest::qWait(110);query->clear();
        QTest::qWait(350);QVERIFY(edit.extraSelections().isEmpty());QCOMPARE(edit.textCursor(),manual);
        QVERIFY(status->text().isEmpty());
    }
    void movementScrollZoomAndFocus() {
        QWidget host;
        auto layout = new QHBoxLayout(&host);
        auto first = new CodeEdit;
        auto second = new CodeEdit;
        layout->addWidget(first);
        layout->addWidget(second);
        QString text;
        for (int i = 0; i < 100; ++i) text += i == 70 ? "\n" : QString("line %1\n").arg(i);
        first->setPlainText(text);
        second->setPlainText("another editor\n");
        host.resize(900, 280);
        host.show(); host.activateWindow(); first->setFocus();
        QTRY_VERIFY(first->hasFocus());
        const QColor border("#44475a");
        const QImage before = image(*first);
        QVERIFY(edgePixels(*first, before, border) >= first->cursorRect().height());
        QTest::keyClick(first, Qt::Key_Down);
        const QImage moved = image(*first);
        QVERIFY(before != moved);
        QVERIFY(firstEdge(*first, moved, border) > firstEdge(*first, before, border));
        QVERIFY(edgePixels(*first, moved, border) >= first->cursorRect().height());
        placeOnLine(*first, 70); // Empty line, well below the initial viewport.
        QVERIFY(first->verticalScrollBar()->value() > 0);
        QVERIFY(first->textCursor().block().text().isEmpty());
        QVERIFY(edgePixels(*first, image(*first), border) >= first->cursorRect().height());
        auto prefs = ContentFonts::instance().preferences();
        prefs.editorPx = 19;
        ContentFonts::instance().setPreferences(prefs);
        UiZoom::instance().setLevel(1);
        settle();
        first->ensureCursorVisible();
        QVERIFY(edgePixels(*first, image(*first), border) >= first->cursorRect().height());
        save(image(*first), "dark-empty-scrolled-zoomed");

        QTest::mouseClick(second->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(30, 10));
        QTRY_VERIFY(second->hasFocus());
        QCOMPARE(edgePixels(*first, image(*first), border), 0);
        QVERIFY(edgePixels(*second, image(*second), border) >= second->cursorRect().height());
        // An external search result is just an ordinary native selection.
        auto cursor = second->textCursor();
        cursor.setPosition(0); cursor.setPosition(7, QTextCursor::KeepAnchor);
        second->setTextCursor(cursor);
        QVERIFY(second->textCursor().hasSelection());
        QCOMPARE(edgePixels(*second, image(*second), border), 0);
        QCOMPARE(rowBackground(*second, image(*second)), colors().background);
    }
    void wrappedFindLine_data() { QTest::addColumn<bool>("light"); QTest::newRow("dark") << false; QTest::newRow("light") << true; }
    void wrappedFindLine() {
        QFETCH(bool, light); applyTheme(light); CodeEdit edit; edit.resize(560, 480);
        edit.setLineWrapMode(QPlainTextEdit::WidgetWidth);
        edit.setPlainText("before\n// 修复 " + QString("中文配置与状态 ").repeated(32) + "修复\nother\n"); edit.show(); edit.activateWindow();
        edit.showFind(); auto query=edit.findChild<QLineEdit *>("findQuery"); auto status=edit.findChild<QLabel *>("findStatus");
        QVERIFY(query&&status); query->setText("修复"); QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(" / 2"),5000);
        const auto block=edit.document()->firstBlock().next(); QVERIFY(block.layout()->lineCount()>=3); QTextCursor start(block);
        const int top=edit.cursorRect(start).top(),height=edit.fontMetrics().lineSpacing(); const auto shot=image(edit); const QColor row(light?"#f0edf8":"#363447");
        for(int i=0;i<3;++i){const int y=top+i*height+height/2;QVERIFY(y>=0&&y<edit.viewport()->height());QCOMPARE(pixel(shot,QPoint(edit.viewport()->width()-8,y)),row);}
        auto checkLastRow=[&](const QImage &current){
            const auto layout=block.layout(); const auto last=layout->lineAt(layout->lineCount()-1); QTextCursor cursor(block);
            cursor.setPosition(block.position()+last.textStart()); const int y=edit.cursorRect(cursor).center().y();
            QVERIFY(y>=0&&y<edit.viewport()->height()); QCOMPARE(pixel(current,QPoint(edit.viewport()->width()-3,y)),row);
        };
        checkLastRow(shot);
        const QTextCursor previous(block.previous()); QCOMPARE(pixel(shot,QPoint(edit.viewport()->width()-8,edit.cursorRect(previous).center().y())),colors().background);
        QVERIFY(colorPixels(shot,QColor("#eaa13b"))>0); QVERIFY(colorPixels(shot,QColor("#f6d75d"))>0);
        save(shot,light?"light-find-wrap":"dark-find-wrap");
        const int oldCount=block.layout()->lineCount(); edit.resize(420,480); settle(); QVERIFY(block.layout()->lineCount()>oldCount); checkLastRow(image(edit));
        QFont larger=edit.font(); larger.setPixelSize(qMax(1,edit.fontMetrics().height()+3)); edit.setFont(larger); settle(); checkLastRow(image(edit));
    }
    void denseAdvancedSelectionDoesNotRebuildHighlights() {
        CodeEdit edit; edit.resize(700, 360);
        SearchOutcome result; QString text;
        for (int row=0;row<2500;++row) {
            const int offset=int(text.size()); const QString line="aa aa aa aa";
            result.lines.append({row+1,offset,line,{{0,2},{3,2},{6,2},{9,2}}}); text+=line+'\n';
        }
        edit.setPlainText(text); edit.document()->setModified(false); edit.show(); edit.activateWindow(); edit.setFocus(); settle();
        edit.setAdvancedSearch(result); QTextCursor current(edit.document()); current.setPosition(0); current.setPosition(2,QTextCursor::KeepAnchor);
        edit.setAdvancedSearchCurrent(0,current); const auto rebuilds=edit.highlightRebuildCount;
        edit.setTextCursor(current); QVERIFY(edit.styleSheet().contains("selection-color"));
        for (int i=0;i<300;++i) {
            QTextCursor drag(edit.document()); drag.setPosition(0); drag.setPosition(3+i,QTextCursor::KeepAnchor); edit.setTextCursor(drag);
        }
        QVERIFY(edit.styleSheet().isEmpty()); QCOMPARE(edit.highlightRebuildCount,rebuilds);
        QCOMPARE(edit.extraSelections().size(),10001); QCOMPARE(edit.toPlainText(),text); QVERIFY(!edit.document()->isModified());
        edit.setTextCursor(current); QVERIFY(edit.styleSheet().contains("selection-color")); QCOMPARE(edit.highlightRebuildCount,rebuilds);
    }
    void clearedAdvancedHitDoesNotRecolorManualSelection() {
        CodeEdit edit;edit.setPlainText("needle and text\n");edit.show();
        SearchOutcome result;result.lines.append({1,0,"needle and text",{{0,6}}});edit.setAdvancedSearch(result);
        QTextCursor hit(edit.document());hit.setPosition(0);hit.setPosition(6,QTextCursor::KeepAnchor);
        edit.setAdvancedSearchCurrent(0,hit);edit.setTextCursor(hit);QVERIFY(edit.styleSheet().contains("selection-color"));
        edit.clearAdvancedSearch();QCOMPARE(edit.textCursor(),hit);QVERIFY(edit.styleSheet().isEmpty());
        QTextCursor other(edit.document());other.setPosition(0);other.setPosition(7,QTextCursor::KeepAnchor);
        for(int i=0;i<4;++i){edit.setTextCursor(other);edit.setTextCursor(hit);QCOMPARE(edit.textCursor(),hit);QVERIFY(edit.styleSheet().isEmpty());}
    }
    void plainDragRepaintsOnlyAffectedRows() {
        struct PaintObserver : QObject {
            QList<QRect> paints;
            bool eventFilter(QObject *object, QEvent *event) override {
                if (event->type()==QEvent::Paint) paints.append(static_cast<QPaintEvent *>(event)->rect());
                return QObject::eventFilter(object,event);
            }
        } observer;
        CodeEdit edit; edit.resize(700,360); QString text;
        for(int i=0;i<10000;++i) text+=QString("const item_%1 = 中文选择文本;\n").arg(i);
        edit.setPlainText(text); edit.show(); edit.activateWindow(); edit.setFocus(); settle();
        edit.viewport()->installEventFilter(&observer); QTest::mousePress(edit.viewport(),Qt::LeftButton,{},QPoint(30,15)); settle(); observer.paints.clear();
        const QPoint point(180,15); QMouseEvent move(QEvent::MouseMove,point,edit.viewport()->mapToGlobal(point),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(edit.viewport(),&move); QTRY_VERIFY(!observer.paints.isEmpty());
        for(const auto &rect:observer.paints) QVERIFY2(rect.height()<edit.viewport()->height(),"A single-line drag repainted the entire editor viewport");
        QTest::mouseRelease(edit.viewport(),Qt::LeftButton,{},point); QVERIFY(edit.textCursor().hasSelection()); QCOMPARE(edit.toPlainText(),text);
    }
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    EditorHighlightTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_editorhighlights.moc"
