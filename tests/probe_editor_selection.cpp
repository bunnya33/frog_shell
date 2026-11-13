// Isolated local-only full-feature editor fixture. No SSH, saved profiles,
// system clipboard operations or automatic timing assertions.
#include "../src/ui/editor.h"
#include "../src/ui/theme.h"
#include "../src/ui/contentfont.h"
#include <QtTest>
#include <algorithm>

namespace {
QJsonObject distribution(QVector<double> samples) {
    if(samples.isEmpty())return {{"count",0}};
    std::sort(samples.begin(),samples.end());
    return {{"count",samples.size()},{"p50_ms",samples[samples.size()/2]},
        {"p95_ms",samples[int((samples.size()-1)*.95)]},{"p99_ms",samples[int((samples.size()-1)*.99)]},{"max_ms",samples.last()}};
}
class MeasuredEditor : public rs::CodeEdit {
  public:
    QElapsedTimer clock;
    QVector<double> inputPaint,handlers,ticks;
    struct Input { int anchor,caret; double at; bool painted=false; };
    QVector<Input> changes;
    int changedInputs=0,paintedChanges=0,unpairedPaints=0;
    bool measuring=false;
    double now() const { return clock.nsecsElapsed()/1e6; }
  protected:
    void mouseMoveEvent(QMouseEvent *event) override {
        const auto begin=now();const auto before=send(SCI_GETCURRENTPOS),oldAnchor=send(SCI_GETANCHOR);
        rs::CodeEdit::mouseMoveEvent(event);
        if(measuring) {
            handlers<<now()-begin;
            const int anchor=int(send(SCI_GETANCHOR)),caret=int(send(SCI_GETCURRENTPOS));
            if(before!=caret||oldAnchor!=anchor) { ++changedInputs; changes.append({anchor,caret,begin}); }
        }
    }
    void paintEvent(QPaintEvent *event) override {
        rs::CodeEdit::paintEvent(event);
        const int anchor=int(send(SCI_GETANCHOR)),caret=int(send(SCI_GETCURRENTPOS));
        if(measuring) {
            bool paired=false;
            for(auto it=changes.rbegin();it!=changes.rend();++it)if(it->anchor==anchor&&it->caret==caret) {
                if(!it->painted) { inputPaint<<now()-it->at;it->painted=true;++paintedChanges; }
                paired=true;break;
            }
            if(!paired)++unpairedPaints;
        }
    }
};
QString source(bool longLines) {
    QString text;
    for(int i=0;i<(longLines?500:3000);++i)
        text+=QString("const value_%1 = \"").arg(i)+(longLines?QString("中文😀é 内容 ").repeated(250):QString("中文😀é 内容"))+"\"; // comment\n";
    return text;
}
void decorate(rs::CodeEdit &edit,const QString &text) {
    rs::SearchOutcome result;int position=0,row=1;
    for(const auto &line:text.split('\n')) {
        const int start=int(line.indexOf("value_"));
        if(start>=0)result.lines.append({row,position,line,{{start,6}}});
        position+=int(line.size())+1;++row;
    }
    edit.setAdvancedSearch(result);edit.setSyntaxPath("fixture.json");
}
QJsonObject run(int round,bool longLines,bool decorated,bool scroll) {
    MeasuredEditor edit;const auto text=source(longLines);edit.setPlainText(text);
    if(decorated)decorate(edit,text);
    edit.setWindowTitle("FrogShell isolated migrated editor benchmark");edit.resize(980,650);edit.show();edit.activateWindow();edit.setFocus();QTest::qWait(450);
    const QPoint anchor(140,150);QTest::mousePress(edit.viewport(),Qt::LeftButton,{},anchor);QTest::qWait(30);
    edit.clock.start();edit.measuring=true;
    QTimer timer;timer.setTimerType(Qt::PreciseTimer);timer.setInterval(8);QEventLoop loop;int count=0;
    QObject::connect(&timer,&QTimer::timeout,&loop,[&]{
        edit.ticks<<edit.now();const int phase=count%64,factor=phase<32?phase:64-phase;
        const QPoint point(anchor.x()+factor*19,anchor.y()+factor*9);
        if(scroll)edit.send(SCI_LINESCROLL,0,phase<32?2:-2);
        QMouseEvent move(QEvent::MouseMove,point,point,edit.viewport()->mapToGlobal(point),Qt::NoButton,Qt::LeftButton,Qt::NoModifier,Qt::MouseEventSynthesizedByApplication);
        QApplication::sendEvent(edit.viewport(),&move);
        if(++count==160){timer.stop();QTimer::singleShot(30,&loop,&QEventLoop::quit);}
    });timer.start();QTimer::singleShot(12000,&loop,&QEventLoop::quit);loop.exec();timer.stop();edit.measuring=false;
    QElapsedTimer release;release.start();QTest::mouseRelease(edit.viewport(),Qt::LeftButton,{},anchor);const double releaseMs=release.nsecsElapsed()/1e6;
    QVector<double> intervals;for(int i=1;i<edit.ticks.size();++i)intervals<<edit.ticks[i]-edit.ticks[i-1];
    return {{"round",round},{"long_lines",longLines},{"decorated",decorated},{"scroll",scroll},{"utf16_units",text.size()},
        {"input_count",count},{"changed_inputs",edit.changedInputs},{"painted_changes",edit.paintedChanges},{"unpaired_paints",edit.unpairedPaints},
        {"input_to_paint",distribution(edit.inputPaint)},{"mouse_handler",distribution(edit.handlers)},
        {"timer_interval",distribution(intervals)},{"release_ms",releaseMs},{"elapsed_ms",edit.clock.elapsed()}};
}
}
int main(int argc,char **argv) {
    if(argc==2&&QByteArray(argv[1])=="--internal-document-search-worker") { QCoreApplication worker(argc,argv);return rs::documentSearchWorker(); }
    QApplication app(argc,argv);QTemporaryDir data;QStandardPaths::setTestModeEnabled(true);
    qputenv("RABBITSHELL_DATA_DIR",data.path().toUtf8());
    QCoreApplication::setOrganizationName("FrogShellSelectionFixture");QCoreApplication::setApplicationName("Isolated");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,data.path());
    QApplication::setStyle("Fusion");rs::applyTheme(app.arguments().contains("--light"));
    if(app.arguments().contains("--interactive")) {
        QWidget window;window.setWindowTitle("FrogShell migrated editor verification — isolated");auto layout=new QVBoxLayout(&window);
        layout->addWidget(new QLabel("正式编辑内核 · 语法着色 / 搜索高亮 / 错误波浪线 · 仅本地测试文本"));
        auto edit=new rs::CodeEdit;const auto text=source(false);edit->setPlainText(text);decorate(*edit,text);layout->addWidget(edit);
        auto controls=new QHBoxLayout;layout->addLayout(controls);
        auto theme=new QPushButton("切换明暗主题"),find=new QPushButton("查找 / 替换"),clear=new QPushButton("清除高级高亮");
        auto wrap=new QCheckBox("自动换行");
        controls->addWidget(theme);controls->addWidget(find);controls->addWidget(clear);controls->addWidget(wrap);controls->addStretch();
        QObject::connect(theme,&QPushButton::clicked,edit,[edit]{rs::applyTheme(!rs::lightTheme());edit->refreshTheme();});
        QObject::connect(find,&QPushButton::clicked,edit,[edit]{edit->showFind(true);});
        QObject::connect(clear,&QPushButton::clicked,edit,&rs::CodeEdit::clearAdvancedSearch);
        QObject::connect(wrap,&QCheckBox::toggled,edit,[edit](bool on){edit->setLineWrapMode(on?rs::EditorSurface::WidgetWidth:rs::EditorSurface::NoWrap);});
        window.resize(1150,780);window.show();return app.exec();
    }
    QJsonArray cases;
    for(int round=0;round<3;++round)for(bool longLines:{false,true})for(bool decorated:{false,true})for(bool scroll:{false,true}) {
        cases.append(run(round,longLines,decorated,scroll));
        QSaveFile output(qEnvironmentVariable("PROBE_OUTPUT"));if(!output.open(QIODevice::WriteOnly))return 2;
        output.write(QJsonDocument(QJsonObject{{"engine","Scintilla-Qt migrated CodeEdit"},{"qt",QT_VERSION_STR},
            {"platform",QGuiApplication::platformName()},{"measurement","application-injected changed selection to completed QWidget paint; not physical input/display latency"},{"cases",cases}}).toJson());output.commit();
    }
    return 0;
}
