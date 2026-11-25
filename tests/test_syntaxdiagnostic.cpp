#include "../src/core/syntaxdiagnostic.h"
#include "../src/ui/editor.h"
#include "../src/ui/theme.h"
#include <QtTest>
using namespace rs;
namespace rs {
class SyntaxDiagnosticTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    static bool redAtAnchor(CodeEdit &edit) {
        const auto image = edit.viewport()->grab().toImage();
        const auto rect = edit.syntaxAnchorRect();
        const auto red = colors().red;
        int count = 0;
        for (int x = rect.left(); x <= rect.right(); ++x)
            for (int y = rect.center().y(); y <= rect.bottom(); ++y)
                if (image.rect().contains(x,y) && image.pixelColor(x,y) == red) ++count;
        if (count < 5) {
            const auto output=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR");
            if(!output.isEmpty()){QDir().mkpath(output);image.save(output+"/diagnostic-anchor-failure.png");}
            qWarning()<<"Native diagnostic"<<rect<<edit.syntaxResult.offset<<edit.extraSelections().size()
                <<edit.send(SCI_INDICATORVALUEAT,10,edit.fromUtf16(edit.syntaxResult.offset));
        }
        return count >= 5;
    }
private slots:
    void initTestCase() {
        QVERIFY(data.isValid());
        qputenv("RABBITSHELL_DATA_DIR", data.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellSyntaxTests");
        QCoreApplication::setApplicationName("RabbitShellSyntaxTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data.path());
        QApplication::setStyle("Fusion");
        if (QFileInfo::exists("C:/Windows/Fonts/msyh.ttc"))
            QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    }
    void formats() {
        QCOMPARE(syntaxFormatForPath("/tmp/a.JSONC"), SyntaxFormat::Jsonc);
        QCOMPARE(syntaxFormatForPath("a.yml"), SyntaxFormat::Yaml);
        QCOMPARE(syntaxFormatForPath("a.txt"), SyntaxFormat::None);
    }
    void parser_data() {
        QTest::addColumn<int>("format"); QTest::addColumn<QString>("text"); QTest::addColumn<bool>("error");
        auto add=[](const char *name, SyntaxFormat f, const QString &text, bool error){QTest::newRow(name) << int(f) << text << error;};
        add("json-object",SyntaxFormat::Json,"{\"x\": 1}",false);
        add("json-number",SyntaxFormat::Json,"12",false);
        add("json-string",SyntaxFormat::Json,"\"hello\"",false);
        add("json-null",SyntaxFormat::Json,"null",false);
        add("json-empty",SyntaxFormat::Json,"",true);
        add("json-space",SyntaxFormat::Json," \r\n\t",true);
        add("json-bom-empty",SyntaxFormat::Json,QString(QChar(0xfeff)),true);
        add("json-roots",SyntaxFormat::Json,"1,2",true);
        add("json-objects",SyntaxFormat::Json,"{} , {}",true);
        add("json-trailing",SyntaxFormat::Json,"{\"a\":1,}",true);
        add("json-raw-newline",SyntaxFormat::Json,QStringLiteral("{\"a\":\"line\nnext\"}"),true);
        add("json-raw-tab",SyntaxFormat::Json,QStringLiteral("\"a\tb\""),true);
        add("json-escaped-newline",SyntaxFormat::Json,QStringLiteral("\"a\\nb\""),false);
        add("jsonc-raw-control",SyntaxFormat::Jsonc,QStringLiteral("/*comment*/ \"a\tb\""),true);
        add("jsonc-comments",SyntaxFormat::Jsonc,"/* x */ {\"x\": [1,2,],}",false);
        add("jsonc-empty-array-comma",SyntaxFormat::Jsonc,"[,]",true);
        add("jsonc-empty-object-comma",SyntaxFormat::Jsonc,"{ /* comment */ ,}",true);
        add("jsonc-double-comma",SyntaxFormat::Jsonc,"[1,,]",true);
        add("jsonc-empty",SyntaxFormat::Jsonc,"// only\n /* here */",true);
        add("jsonc-unclosed",SyntaxFormat::Jsonc,"{} /*",true);
        add("xml-valid",SyntaxFormat::Xml,"<r>text &amp; more</r>",false);
        add("xml-mismatch",SyntaxFormat::Xml,"<r><x></r>",true);
        add("xml-entity",SyntaxFormat::Xml,"<r>&missing;</r>",true);
        add("yaml-multidoc",SyntaxFormat::Yaml,"---\na: 'one'\n---\nb: \"two\"\n",false);
        add("yaml-bad",SyntaxFormat::Yaml,"a: [one, two\n",true);
        add("toml-valid",SyntaxFormat::Toml,"[a]\nx = [1, 2]\n",false);
        add("toml-duplicate",SyntaxFormat::Toml,"x=1\nx=2\n",true);
        add("toml-type",SyntaxFormat::Toml,"x = 01\n",true);
    }
    void parser() {
        QFETCH(int,format); QFETCH(QString,text); QFETCH(bool,error);
        const auto result=checkSyntax(SyntaxFormat(format),text);
        QCOMPARE(result.error,error);
        if (error) { QVERIFY(result.offset>=0 && result.offset<=text.size()); QVERIFY(!result.message.isEmpty()); }
    }
    void positions() {
        auto r=checkSyntax(SyntaxFormat::Json,QString::fromUtf8("{\"中😀\":1}, 2"));
        QVERIFY(r.error); QCOMPARE(r.offset,9); QCOMPARE(r.column,10);
        r=checkSyntax(SyntaxFormat::Json,QString::fromUtf8("{\"中😀\": ?}"));
        QVERIFY(r.error); QCOMPARE(r.offset,8); QCOMPARE(r.column,9);
        r=checkSyntax(SyntaxFormat::Json,QStringLiteral("{\"x\":1,}"));
        QVERIFY(r.error); QCOMPARE(r.offset,6);
        auto y=checkSyntax(SyntaxFormat::Yaml,QString::fromUtf8("a: 中😀\nb: [x,\n"));
        QVERIFY(y.error); QCOMPARE(y.offset, int(QString::fromUtf8("a: 中😀\nb: [x,\n").size()));
        y=checkSyntax(SyntaxFormat::Yaml,QString(QChar(0xfeff)) + QStringLiteral("a: [1,}"));
        QVERIFY(y.error); QCOMPARE(y.offset, 7);
        auto t=checkSyntax(SyntaxFormat::Toml,QString::fromUtf8("a = \"中😀\"\nb = 01"));
        QVERIFY(t.error); QVERIFY(t.offset>=int(QString::fromUtf8("a = \"中😀\"\nb = ").size()));
    }
    void editor() {
        applyTheme(false);
        CodeEdit edit;
        edit.resize(650,240); edit.show(); edit.setSyntaxPath("file.json");
        edit.setPlainText("{\"x\": ?}");
        QCOMPARE(edit.syntaxTimer->interval(), 300); QVERIFY(edit.syntaxTimer->isActive());
        QTRY_VERIFY_WITH_TIMEOUT(edit.syntaxResult.error,5000);
        QVERIFY(!edit.document()->isModified() || edit.toPlainText()=="{\"x\": ?}");
        QVERIFY(redAtAnchor(edit));
        const QPoint hover = edit.syntaxAnchorRect().center();
        QHelpEvent help(QEvent::ToolTip, hover, edit.viewport()->mapToGlobal(hover));
        QApplication::sendEvent(edit.viewport(), &help);
        QVERIFY(QToolTip::text().contains("语法错误"));
        QVERIFY(QToolTip::text().contains(edit.syntaxResult.message));
        QToolTip::hideText();
        const auto output=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR");
        if(!output.isEmpty()){QDir().mkpath(output); QVERIFY(edit.grab().save(output+"/worker-syntax-dark.png"));}
        applyTheme(true); edit.refreshTheme(); QTest::qWait(50);
        if(!output.isEmpty()) QVERIFY(edit.grab().save(output+"/worker-syntax-light.png"));
        edit.showFind();
        auto query = edit.findChild<QLineEdit *>("findQuery");
        auto status = edit.findChild<QLabel *>("findStatus");
        QVERIFY(query && status);
        query->setText("x");
        QTRY_VERIFY_WITH_TIMEOUT(status->text().endsWith("共 1 项"), 5000);
        bool searchBackground = false;
        for (const auto &selection : edit.extraSelections()) {
            if (selection.format.background().style() != Qt::NoBrush) searchBackground = true;
        }
        QVERIFY(searchBackground); QVERIFY(redAtAnchor(edit)); QVERIFY(edit.textCursor().hasSelection());
        auto cursor = edit.textCursor(); cursor.clearSelection(); edit.setTextCursor(cursor);
        QVERIFY(!edit.textCursor().hasSelection());
        QVERIFY(redAtAnchor(edit));
        edit.setPlainText("{\"x\": 1}"); QVERIFY(!edit.syntaxResult.error);
        QTest::qWait(600); QVERIFY(!edit.syntaxResult.error);
        edit.setPlainText("["); edit.startSyntax(); edit.setPlainText("[]");
        QTest::qWait(1000); QVERIFY(!edit.syntaxResult.error);
        edit.setPlainText("["); QTRY_VERIFY_WITH_TIMEOUT(edit.syntaxResult.error,5000);
        edit.setSyntaxPath("file.txt"); QVERIFY(!edit.syntaxResult.error);
        edit.setSyntaxPath("file.json"); edit.setPlainText(QString(1024*1024+1, u' '));
        QSignalSpy spy(&edit, &CodeEdit::syntaxStatusChanged); edit.scheduleSyntax();
        QVERIFY(spy.last().at(0).toString().contains("超过"));
        auto destroyed=new CodeEdit; destroyed->setPlainText("["); destroyed->setSyntaxPath("a.json"); destroyed->startSyntax(); delete destroyed; QTest::qWait(300);
    }
    void fullEditorWave() {
        const QByteArray content("{\n  \"payload\": {\n    \"oneof\": [12\n      \"login\",\n      \"requestRoomData\"\n    ]\n  }\n}\n");
        applyTheme(false);
        Editor editor("isolated", "/fixture/test.json", content, {});
        editor.resize(950,440); editor.show(); editor.code->setFocus();
        QTRY_VERIFY_WITH_TIMEOUT(editor.code->syntaxResult.error, 5000);
        QVERIFY(redAtAnchor(*editor.code));
        QVERIFY(!editor.dirty());
        const auto output=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR");
        if (!output.isEmpty()) QVERIFY(editor.grab().save(output+"/worker-syntax-editor-dark.png"));
        applyTheme(true); editor.code->refreshTheme();
        QVERIFY(redAtAnchor(*editor.code));
        if (!output.isEmpty()) QVERIFY(editor.grab().save(output+"/worker-syntax-editor-light.png"));
        applyTheme(false); editor.code->refreshTheme();
        QVERIFY(redAtAnchor(*editor.code));
        editor.code->showFind();
        editor.code->findChild<QLineEdit *>("findQuery")->setText("login");
        QTRY_VERIFY_WITH_TIMEOUT(!editor.code->matches.isEmpty(), 5000);
        QVERIFY(redAtAnchor(*editor.code));
        if (!output.isEmpty()) QVERIFY(editor.grab().save(output+"/worker-syntax-editor-find.png"));
        editor.code->setPlainText("{\"valid\":true}");
        QVERIFY(!editor.code->syntaxResult.error);
    }
    void emptyErrorAnchor() {
        applyTheme(false);
        CodeEdit edit;
        edit.resize(500, 150); edit.show();
        edit.setPlainText(""); edit.setSyntaxPath("empty.json");
        QTRY_VERIFY_WITH_TIMEOUT(edit.syntaxResult.error, 5000);
        QCOMPARE(edit.syntaxResult.offset, 0);
        QTest::qWait(50);
        const auto shot = edit.viewport()->grab().toImage();
        int red = 0;
        for (int y = 0; y < shot.height(); ++y)
            for (int x = 0; x < shot.width(); ++x)
                if (shot.pixelColor(x,y) == colors().red) ++red;
        QVERIFY(red > 0);
    }
};
}
QTEST_MAIN(rs::SyntaxDiagnosticTests)
#include "test_syntaxdiagnostic.moc"
