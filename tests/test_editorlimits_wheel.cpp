#include "../src/core/editorlimits.h"
#include "../src/core/storageusage.h"
#include "../src/ui/mainwindow.h"
#include "../src/ui/uipreferences.h"
#include "../src/ui/cachecleanupcontroller.h"
#include <QProcess>
#include <QtTest>
namespace rs {
class MenuTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary;
    QString report;
    static void wheel(QWidget *w, int delta=-120) {
        auto p=w->rect().center();QWheelEvent e(p,w->mapToGlobal(p),{},QPoint(0,delta),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(w,&e);QCoreApplication::processEvents();
    }
private slots:
    void initTestCase(){
        QVERIFY(temporary.isValid());qputenv("RABBITSHELL_DATA_DIR",temporary.path().toUtf8());QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("FrogShellWorkerControls");QCoreApplication::setApplicationName("Isolated");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
        QApplication::setStyle("Fusion");if(QFileInfo::exists("C:/Windows/Fonts/msyh.ttc")){QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QApplication::setFont(QFont("Microsoft YaHei UI"));}
        report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",temporary.path());QVERIFY(QDir().mkpath(report));applyTheme(false);
    }
    void limits(){
        QSettings s;s.remove("editor");QCOMPARE(editorlimits::sizeMiB(),5);
        for(const auto &v:QList<QVariant>{0,257,-1,QString("1.5"),QString("999999999999999999999999"),QString("invalid")}){s.setValue("editor/maxFileMiB",v);QCOMPARE(editorlimits::sizeMiB(),5);}
        s.setValue("editor/maxFileMiB",1);QCOMPARE(editorlimits::sizeBytes(),quint64(1024*1024));s.setValue("editor/maxFileMiB",256);QCOMPARE(editorlimits::sizeMiB(),256);
        UiPreferences::instance().reset();QCOMPARE(editorlimits::sizeMiB(),5);
    }
    void boundedQueue(){
        Profile p;Session s(p,{});QSignalSpy fail(&s,&Session::failed);Job j;j.type="save";j.id=1;j.data=QByteArray(9*1024*1024,'a');s.submit(j);QCOMPARE(fail.size(),0);
        j.id=2;s.submit(j);QCOMPARE(fail.size(),1);j={};j.type="input";j.id=3;j.data=QByteArray(8*1024*1024,'b');s.submit(j);QCOMPARE(fail.size(),1);
        j.id=4;j.data="x";s.submit(j);QCOMPARE(fail.size(),2);QVERIFY(fail.last()[2].toString().contains("稍后重试"));
    }
    void usageAccounting(){
        QTemporaryDir root;QVERIFY(root.isValid());QDir dir(root.path());QVERIFY(dir.mkpath("drafts"));QVERIFY(dir.mkpath("logs"));QVERIFY(dir.mkpath("settings"));QVERIFY(dir.mkpath("file-operation-locks"));
        auto write=[](const QString &path,int size){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(QByteArray(size,'x'))==size;};
        QVERIFY(write(dir.filePath("connections.json"),10));QVERIFY(write(dir.filePath("drafts/a"),20));QVERIFY(write(dir.filePath("settings/preferences.ini"),30));QVERIFY(write(dir.filePath("file-operation-locks/a.lock"),5));QVERIFY(write(dir.filePath("logs/diagnostic.jsonl"),40));QVERIFY(write(dir.filePath("logs/crash-01.log"),50));QVERIFY(write(dir.filePath("logs/exit-15.log"),3));QVERIFY(write(dir.filePath("logs/unrelated.txt"),200));
        auto usage=scanStorageUsage(root.path(),dir.filePath("settings/preferences.ini"),dir.filePath("logs"));
        QCOMPARE(usage.data.bytes,quint64(65));QCOMPARE(usage.logs.bytes,quint64(93));QVERIFY(!usage.data.partial&&!usage.logs.partial);
        auto absent=scanStorageUsage(dir.filePath("missing"),{},dir.filePath("missing"));QCOMPARE(absent.data.bytes,quint64(0));QCOMPARE(absent.logs.bytes,quint64(0));
        auto canceled=scanStorageUsage(root.path(),{},dir.filePath("logs"),[]{return true;});QVERIFY(canceled.data.canceled);
#ifdef Q_OS_WIN
        QVERIFY(dir.mkpath("foreign"));QVERIFY(write(dir.filePath("foreign/unrelated"),200));
        QCOMPARE(QProcess::execute("cmd.exe",{"/c","mklink","/J",QDir::toNativeSeparators(dir.filePath("drafts/alias")),QDir::toNativeSeparators(dir.filePath("foreign"))}),0);
        auto linked=scanStorageUsage(root.path(),{},dir.filePath("logs"));QVERIFY(linked.data.partial);QCOMPARE(linked.data.bytes,quint64(65));
#endif
        auto deep=QString("drafts")+QString("/d").repeated(66);QVERIFY(dir.mkpath(deep));QVERIFY(write(dir.filePath(deep+"/hidden"),100));
        auto bounded=scanStorageUsage(root.path(),{},dir.filePath("logs"));QVERIFY(bounded.data.partial);QVERIFY(bounded.data.bytes<165);
    }
    void networkAndDates(){
        Profile p;p.id="fixture-nic";Workspace workspace(p,std::nullopt,true);workspace.resize(1000,700);workspace.sidebar->setCurrentIndex(0);workspace.show();workspace.activateWindow();QCoreApplication::processEvents();
        auto nic=workspace.nic;nic->clear();nic->addItems({"eth0","eth1","eth2"});nic->setCurrentIndex(1);wheel(nic);QCOMPARE(nic->currentIndex(),1);
        for(auto parent=nic->parentWidget();parent;parent=parent->parentWidget())if(auto scroll=qobject_cast<QScrollArea *>(parent)){scroll->ensureWidgetVisible(nic);break;}QCoreApplication::processEvents();QVERIFY(nic->isVisible());
        nic->setFocus(Qt::OtherFocusReason);QTRY_VERIFY_WITH_TIMEOUT(nic->hasFocus(),3000);wheel(nic);QCOMPARE(nic->currentIndex(),1);
        QTest::mouseClick(nic,Qt::LeftButton);QVERIFY(nic->view()->isVisible());QTest::keyClick(nic->view(),Qt::Key_Escape);QCoreApplication::processEvents();
        // Offscreen cannot grab the keyboard; emulate Qt's popup focus return.
        if(!nic->hasFocus()){workspace.activateWindow();nic->setFocus(Qt::PopupFocusReason);QCoreApplication::processEvents();}
        QVERIFY(nic->hasFocus());wheel(nic);QVERIFY(nic->currentIndex()!=1);
        const int clickedValue=nic->currentIndex();
        nic->clearFocus();nic->setFocus(Qt::OtherFocusReason);wheel(nic);QCOMPARE(nic->currentIndex(),clickedValue);
        QTest::mouseClick(nic,Qt::LeftButton);QTest::keyClick(nic->view(),Qt::Key_Escape);QCoreApplication::processEvents();
        if(!nic->hasFocus()){workspace.activateWindow();nic->setFocus(Qt::PopupFocusReason);QCoreApplication::processEvents();}
        QTest::mouseClick(&workspace,Qt::LeftButton,Qt::NoModifier,QPoint(2,2));
        wheel(nic);QCOMPARE(nic->currentIndex(),clickedValue);
    }
    void dateControls(){
        QWidget w;QVBoxLayout layout(&w);QDateEdit date;QTimeEdit time;QLabel blank("blank");layout.addWidget(&date);layout.addWidget(&time);layout.addWidget(&blank);date.setDate(QDate(2026,11,11));time.setTime(QTime(12,30));w.show();w.activateWindow();QCoreApplication::processEvents();
        auto d=date.date();auto t=time.time();wheel(&date);wheel(&time);QCOMPARE(date.date(),d);QCOMPARE(time.time(),t);
        auto input=date.findChild<QLineEdit *>();QVERIFY(input);QTest::mouseClick(input,Qt::LeftButton);wheel(input);QVERIFY(date.date()!=d);d=date.date();QTest::mouseClick(&blank,Qt::LeftButton);wheel(input);QCOMPARE(date.date(),d);
    }
    void settingsUi(){
        for(bool light:{false,true}){
            applyTheme(light);MainWindow window(true);window.show();bool checked=false;
            QTimer::singleShot(0,&window,[&]{auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!dialog)return;auto close=qScopeGuard([&]{dialog->reject();});
                auto list=dialog->findChild<QListWidget *>("settingsCategories");auto spin=dialog->findChild<QSpinBox *>("editorMaxFileMiB");QVERIFY(list&&spin);QCOMPARE(list->item(3)->text(),QString("文件编辑"));
                list->setCurrentRow(1);QCoreApplication::processEvents();auto font=dialog->findChild<QSpinBox *>("editorFontPx");QVERIFY(font);auto fontValue=font->value();wheel(font);QCOMPARE(font->value(),fontValue);
                auto fontInput=font->findChild<QLineEdit *>();QVERIFY(fontInput);QTest::mouseClick(fontInput,Qt::LeftButton);wheel(fontInput);QVERIFY(font->value()!=fontValue);
                list->setCurrentRow(0);QCoreApplication::processEvents();auto zoom=dialog->findChild<QComboBox *>("uiZoomLevel");QVERIFY(zoom);int zoomValue=zoom->currentIndex();wheel(zoom);QCOMPARE(zoom->currentIndex(),zoomValue);
                list->setCurrentRow(3);QCoreApplication::processEvents();
                QCOMPARE(spin->minimum(),1);QCOMPARE(spin->maximum(),256);spin->setValue(7);QCOMPARE(editorlimits::sizeMiB(),7);
                auto input=spin->findChild<QLineEdit *>();QVERIFY(input);spin->setFocus();QCoreApplication::processEvents();wheel(input);QCOMPARE(spin->value(),7);
                QTest::mouseClick(input,Qt::LeftButton);wheel(input);QCOMPARE(spin->value(),6);
                auto blank=dialog->findChild<QListWidget *>("settingsCategories");QTest::mouseClick(blank->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(blank->viewport()->width()-3,blank->viewport()->height()-3));wheel(input);QCOMPARE(spin->value(),6);
                checked=dialog->grab().save(report+QString("/settings-editor-%1.png").arg(light?"light":"dark"));
            });window.settings();QVERIFY(checked);
            bool reopened=false;QTimer::singleShot(0,&window,[&]{auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!d)return;auto spin=d->findChild<QSpinBox *>("editorMaxFileMiB");reopened=spin&&spin->value()==6;d->reject();});window.settings();QVERIFY(reopened);
        }
    }
    void storageLayout(){
        const auto longPath=temporary.path()+"/"+QString(130,'x');QSettings().setValue("storage/logDirectory",longPath);qputenv("RABBITSHELL_DATA_DIR",longPath.toUtf8());
        auto restore=qScopeGuard([&]{qputenv("RABBITSHELL_DATA_DIR",temporary.path().toUtf8());QSettings().remove("storage/logDirectory");});
        for(bool light:{false,true}){
            applyTheme(light);MainWindow window(true);window.show();bool checked=false;
            QTimer::singleShot(0,&window,[&]{auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!d)return;auto close=qScopeGuard([&]{d->reject();});d->resize(720,720);
                auto categories=d->findChild<QListWidget *>("settingsCategories");QVERIFY(categories);categories->setCurrentRow(4);QCoreApplication::processEvents();
                auto path=d->findChild<QLabel *>("storageCurrentPath");auto log=d->findChild<QLabel *>("diagnosticLogPath");QVERIFY(path&&log);auto scroll=qobject_cast<QScrollArea *>(d->findChild<QStackedWidget *>("settingsPages")->currentWidget());QVERIFY(scroll);
                auto dataUsage=d->findChild<QLabel *>("storageDataUsage");auto logUsage=d->findChild<QLabel *>("storageLogUsage");QVERIFY(dataUsage&&logUsage);QTRY_VERIFY_WITH_TIMEOUT(!dataUsage->text().contains("统计中")&&!logUsage->text().contains("统计中"),10000);
                QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);QVERIFY(path->width()<=scroll->viewport()->width());QVERIFY(log->width()<=scroll->viewport()->width());
                for(auto label:{path,log}){auto view=label->findChild<QTextEdit *>();QVERIFY(view);QCOMPARE(view->toPlainText(),label->text());QCOMPARE(view->wordWrapMode(),QTextOption::WrapAnywhere);QVERIFY(view->document()->size().height()<=view->viewport()->height()+1);}
                checked=d->grab().save(report+QString("/settings-storage-%1.png").arg(light?"light":"dark"));
            });window.settings();QVERIFY(checked);
        }
    }
    void cleanupSettingsLifecycle(){
        MainWindow window(true);window.show();bool checked=false;
        QTimer::singleShot(0,&window,[&]{auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!d)return;auto close=qScopeGuard([&]{d->reject();});
            auto categories=d->findChild<QListWidget *>("settingsCategories");auto cleanup=d->findChild<CacheCleanupController *>();auto button=d->findChild<QPushButton *>("clearApplicationCache");QVERIFY(categories&&cleanup&&button);cleanup->setTokenProvider([]{return QSet<QString>{};});categories->setCurrentRow(4);QCoreApplication::processEvents();
            QTest::mouseClick(button,Qt::LeftButton);QVERIFY(cleanup->isBusy());QVERIFY(!button->isEnabled());QVERIFY(!d->findChild<QPushButton *>("migrateStorage")->isEnabled());QVERIFY(!d->findChild<QCheckBox *>("diagnosticLoggingEnabled")->isEnabled());QVERIFY(!d->findChild<QPushButton *>("chooseDiagnosticLogDirectory")->isEnabled());
            categories->setCurrentRow(0);QTRY_VERIFY_WITH_TIMEOUT(!cleanup->isBusy(),10000);QVERIFY(button->isEnabled());checked=true;
        });window.settings();QVERIFY(checked);
    }
    void fixtureReadSave(){
        const int port=qEnvironmentVariableIntValue("RABBITSHELL_TEST_PORT");const QString remote=qEnvironmentVariable("RABBITSHELL_FIXTURE_ROOT");if(!port||remote.isEmpty())QSKIP("Requires private loopback fixture");
        QFile file(QDir(remote).filePath("large.txt"));QVERIFY(file.open(QIODevice::WriteOnly));const QByteArray content(9*1024*1024,'a');QCOMPARE(file.write(content),qint64(content.size()));file.close();
        QFile growth(QDir(remote).filePath("growth.txt"));QVERIFY(growth.open(QIODevice::WriteOnly));QCOMPARE(growth.write("a"),qint64(1));growth.close();
        QSettings().setValue("editor/maxFileMiB",5);Profile p;p.host="127.0.0.1";p.port=port;p.user="fixture";Session s(p,"fixture-only");
        QSignalSpy connected(&s,&Session::connected),done(&s,&Session::completed),fail(&s,&Session::failed);connect(&s,&Session::hostKey,&s,[&](auto,bool){s.trust(true);});s.start();auto stop=qScopeGuard([&]{s.stop();s.wait(5000);});QTRY_COMPARE_WITH_TIMEOUT(connected.size(),1,15000);
        auto run=[&](const Job &j){int before=done.size()+fail.size();s.submit(j);QElapsedTimer t;t.start();while(done.size()+fail.size()==before&&t.elapsed()<60000)QTest::qWait(20);return done.size()+fail.size()>before;};
        Job j;j.type="read";j.path="/large.txt";j.id=1;QVERIFY(run(j));QCOMPARE(fail.size(),1);QVERIFY(fail.last()[2].toString().contains("5 MiB"));
        j.path="/growth.txt";j.id=2;QVERIFY(run(j));QCOMPARE(fail.size(),2);QVERIFY(fail.last()[2].toString().contains("5 MiB"));
        QSettings().setValue("editor/maxFileMiB",12);j.path="/large.txt";j.id=3;QVERIFY(run(j));QVERIFY(done.size()==1);auto original=done.last()[2].toMap();QCOMPARE(original["data"].toByteArray(),content);
        QSettings().setValue("editor/maxFileMiB",1);j={};j.type="save";j.path="/large.txt";j.id=4;j.data=content+"changed";j.expectedHash=original["hash"].toByteArray();QVERIFY(run(j));QVERIFY2(done.size()==2,qPrintable(fail.last()[2].toString()));
        j.id=5;j.expectedHash=original["hash"].toByteArray();QVERIFY(run(j));QVERIFY(fail.last()[2].toString().startsWith("CONFLICT:"));
    }
};
}
QTEST_MAIN(rs::MenuTests)
#include "test_editorlimits_wheel.moc"
