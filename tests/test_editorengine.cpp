#include "../src/ui/editor.h"
#include "../src/ui/editorlexer.h"
#include "../src/ui/contentfont.h"
#include "../src/ui/theme.h"
#include <QtTest>
#include <QAccessible>

using namespace rs;
class EditorEngineTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    static void settle() { QCoreApplication::processEvents(); QTest::qWait(30); }
    static QColor styleAt(CodeEdit &edit, int position) {
        edit.send(SCI_COLOURISE,0,-1);
        const int style=int(edit.send(SCI_GETSTYLEAT,edit.fromUtf16(position)));
        const int rgb=int(edit.send(SCI_STYLEGETFORE,style));
        return QColor(rgb&255,(rgb>>8)&255,(rgb>>16)&255);
    }
  private slots:
    void initTestCase() {
        QVERIFY(data.isValid());
        qputenv("RABBITSHELL_DATA_DIR",data.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("FrogShellEngineTests");
        QCoreApplication::setApplicationName("Isolated");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,data.path());
        QApplication::setStyle("Fusion"); applyTheme(false);
    }
    void utf16RoundTripAndEditing() {
        CodeEdit edit;
        const QString text=QString::fromUtf8("A中😀é\n第二行🚀\n尾");
        edit.setPlainText(text);
        QCOMPARE(edit.toPlainText(),text);
        QCOMPARE(edit.document()->characterCount(),text.size()+1);
        for(int i=0;i<=text.size();++i) {
            if(i>0&&i<text.size()&&text[i].isLowSurrogate())continue;
            QCOMPARE(edit.toUtf16(edit.fromUtf16(i)),i);
            QCOMPARE(edit.fromUtf16(i),text.left(i).toUtf8().size());
        }
        QCOMPARE(edit.fromUtf16(-1),0);
        QCOMPARE(edit.fromUtf16(10000),text.toUtf8().size());
        EditorCursor emoji(edit.document());emoji.setPosition(2);emoji.setPosition(4,QTextCursor::KeepAnchor);
        QCOMPARE(emoji.selectedText(),QString::fromUtf8("😀"));
        emoji.insertText("新");edit.setTextCursor(emoji);
        QCOMPARE(edit.toPlainText(),QString::fromUtf8("A中新é\n第二行🚀\n尾"));
        QCOMPARE(edit.textCursor().position(),3);
        QCOMPARE(edit.document()->characterCount(),edit.toPlainText().size()+1);
        edit.undo();QCOMPARE(edit.toPlainText(),text);
        edit.redo();QCOMPARE(edit.document()->characterCount(),edit.toPlainText().size()+1);
    }
    void anchorsUndoAndReset() {
        CodeEdit edit;edit.setPlainText("first\nneedle\nend");
        EditorCursor hit(edit.document());hit.setPosition(6);hit.setPosition(12,QTextCursor::KeepAnchor);
        auto copy=hit;copy.setPosition(0);QCOMPARE(hit.selectedText(),QString("needle"));
        EditorCursor insert(edit.document());insert.insertText("中文😀\n");
        QCOMPARE(hit.selectedText(),QString("needle"));QCOMPARE(hit.blockNumber(),2);
        edit.undo();QCOMPARE(hit.selectedText(),QString("needle"));QCOMPARE(hit.blockNumber(),1);
        EditorCursor change(edit.document());change.beginEditBlock();
        change.insertText("A");change.insertText("B");change.endEditBlock();
        QCOMPARE(edit.toPlainText(),QString("ABfirst\nneedle\nend"));
        edit.undo();QCOMPARE(edit.toPlainText(),QString("first\nneedle\nend"));
        edit.setPlainText("new");QVERIFY(hit.isNull());QVERIFY(copy.isNull());
        QCOMPARE(edit.document()->characterCount(),4);QVERIFY(!edit.document()->isModified());
        QVERIFY(!edit.document()->isUndoAvailable());
    }
    void newlineBomAndReadOnly() {
        const QByteArray source=QByteArray::fromHex("efbbbf")+QString("中文😀\r\nnext\r\n").toUtf8();
        Editor editor("fixture","/fixture.txt",source,{});
        QCOMPARE(editor.bytes(),source);QVERIFY(!editor.dirty());
        editor.code->moveCursor(QTextCursor::End);editor.code->insertPlainText("末尾\n");
        QVERIFY(editor.bytes().startsWith(QByteArray::fromHex("efbbbf")));
        QVERIFY(editor.bytes().endsWith(QString("末尾\r\n").toUtf8()));
        const auto draft=editor.code->toPlainText();editor.code->setReadOnly(true);
        editor.code->insertPlainText("x");editor.code->undo();QCOMPARE(editor.code->toPlainText(),draft);
        editor.code->setReadOnly(false);editor.code->undo();QCOMPARE(editor.bytes(),source);
    }
    void imeCommitAndNativeNavigation() {
        CodeEdit edit;edit.resize(700,250);edit.setPlainText("prefix ");edit.show();edit.activateWindow();edit.setFocus();settle();
        edit.moveCursor(QTextCursor::End);
        QInputMethodEvent preedit(QString::fromUtf8("zhong"),{});QApplication::sendEvent(&edit,&preedit);
        QInputMethodEvent commit;commit.setCommitString(QString::fromUtf8("中文😀"));QApplication::sendEvent(&edit,&commit);settle();
        QCOMPARE(edit.toPlainText(),QString::fromUtf8("prefix 中文😀"));
        QTest::keyClick(&edit,Qt::Key_Left);settle();
        QCOMPARE(edit.textCursor().position(),9); // Before the two-unit emoji.
        QTest::keyClick(&edit,Qt::Key_Right,Qt::ShiftModifier);settle();
        QCOMPARE(edit.textCursor().selectedText(),QString::fromUtf8("😀"));
        QTest::keyClick(&edit,Qt::Key_Tab);settle();
        QVERIFY(edit.hasFocus());QVERIFY(!edit.toPlainText().contains(QString::fromUtf8("😀")));
    }
    void lexerStateAndTheme() {
        CodeEdit edit;edit.resize(700,260);edit.show();
        const QString text="const a = 42;\n/* comment\nreturn 2;\n*/ const b = \"text\";\n\"prop\": true\n";
        edit.setPlainText(text);settle();
        QCOMPARE(styleAt(edit,text.indexOf("return")),colors().comment);
        QCOMPARE(styleAt(edit,text.indexOf("42")),colors().syntaxNumber);
        QCOMPARE(styleAt(edit,text.indexOf("text")),colors().string);
        QCOMPARE(styleAt(edit,text.indexOf("prop")),colors().syntaxProperty);
        EditorCursor remove(edit.document());remove.setPosition(text.indexOf("/*"));remove.setPosition(remove.position()+2,QTextCursor::KeepAnchor);remove.insertText("//");settle();
        QCOMPARE(styleAt(edit,edit.toPlainText().indexOf("return")),colors().keyword);
        applyTheme(true);edit.refreshTheme();QCOMPARE(styleAt(edit,edit.toPlainText().indexOf("42")),colors().syntaxNumber);
        applyTheme(false);
    }
    void accessibleLiveSelectionAndLifetime() {
        auto edit=new CodeEdit;edit->setPlainText("中文😀 content");
        auto accessible=QAccessible::queryAccessibleInterface(edit);QVERIFY(accessible);
        auto text=accessible->textInterface();auto editable=accessible->editableTextInterface();QVERIFY(text&&editable);
        QCOMPARE(text->characterCount(),12);QCOMPARE(text->text(0,-1),edit->toPlainText());
        text->setSelection(0,0,4);int start=-1,end=-1;text->selection(0,&start,&end);
        QCOMPARE(start,0);QCOMPARE(end,4);QCOMPARE(edit->textCursor().selectedText(),QString::fromUtf8("中文😀"));
        editable->replaceText(0,2,"替换");QCOMPARE(edit->toPlainText(),QString::fromUtf8("替换😀 content"));
        edit->setReadOnly(true);QVERIFY(accessible->state().readOnly);editable->insertText(0,"X");
        QVERIFY(!edit->toPlainText().startsWith('X'));
        QVERIFY(edit->findChildren<QTextDocument *>().isEmpty());
        QPointer<EditorDocument> document=edit->document();QPointer<EditorLexer> lexer=edit->findChild<EditorLexer *>();
        QVERIFY(lexer);EditorCursor retained(edit->document());delete edit;
        QVERIFY(document.isNull());QVERIFY(lexer.isNull());QVERIFY(retained.isNull());
    }
};
QTEST_MAIN(EditorEngineTests)
#include "test_editorengine.moc"
