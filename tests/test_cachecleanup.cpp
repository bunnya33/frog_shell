#include "../src/core/cachecleanup.h"
#include "../src/core/operationgate.h"
#include "../src/ui/cachecleanupcontroller.h"
#include <QtTest>
#include <QFile>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QTemporaryDir>
#include <QThreadPool>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace rs;
namespace {
const QString a=QStringLiteral("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
const QString b=QStringLiteral("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");
const QString c=QStringLiteral("cccccccc-cccc-cccc-cccc-cccccccccccc");
void put(const QString &path,const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);
    if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()) qFatal("fixture write failed");
}
QByteArray read(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
}
class CacheCleanupTests : public QObject {
    Q_OBJECT
  private slots:
    void whitelistAndLocks() {
#ifndef Q_OS_WIN
        QSKIP("Handle-pinned cleanup is currently Windows-only");
#endif
        QTemporaryDir temp;QVERIFY(temp.isValid());const QString data=temp.filePath("data"), logs=temp.filePath("logs"), spool=data+"/clipboard-spool";
        const QStringList protectedNames{"connections.json","directory-bookmarks.json","tunnels.json","known_hosts","application-lock.json","application-lock.required","file-operation-locks/item"};
        for(const auto &name:protectedNames)put(data+"/"+name,name.toUtf8());
        put(data+"/drafts/unsaved","draft");
        put(data+"/settings/other","settings");put(logs+"/diagnostic.jsonl","active");
        put(logs+"/diagnostic.1.jsonl","archive");put(logs+"/crash-15.log","crash");
        put(logs+"/exit-00.log","exit");put(logs+"/crash-16.log","unrelated");
        put(spool+"/"+a+"/nested/file","cached");put(spool+"/"+b+"/file","locked");
        put(spool+"/"+c+"/file","protected");put(spool+"/unknown/file","unknown");
        QLockFile lock(spool+"/"+b+".lock");QVERIFY(lock.tryLock(0));
        const auto plan=planCacheCleanup(data,logs,{c});QVERIFY(!plan.partial);QCOMPARE(plan.items,4);QCOMPARE(plan.bytes,quint64(22));QCOMPARE(plan.skipped,2);
        const auto result=executeCacheCleanup(plan,{c});QCOMPARE(result.items,4);QCOMPARE(result.bytes,quint64(22));QCOMPARE(result.failures,0);
        QVERIFY(!QFileInfo::exists(logs+"/diagnostic.1.jsonl"));QVERIFY(!QFileInfo::exists(spool+"/"+a));
        QCOMPARE(read(logs+"/diagnostic.jsonl"),QByteArray("active"));QCOMPARE(read(logs+"/crash-16.log"),QByteArray("unrelated"));
        for(const auto &name:protectedNames)QCOMPARE(read(data+"/"+name),name.toUtf8());
        QCOMPARE(read(data+"/drafts/unsaved"),QByteArray("draft"));
        QCOMPARE(read(data+"/settings/other"),QByteArray("settings"));QCOMPARE(read(spool+"/"+b+"/file"),QByteArray("locked"));
        QCOMPARE(read(spool+"/"+c+"/file"),QByteArray("protected"));QCOMPARE(read(spool+"/unknown/file"),QByteArray("unknown"));
    }
    void changesAndCancel() {
#ifndef Q_OS_WIN
        QSKIP("Handle-pinned cleanup is currently Windows-only");
#endif
        QTemporaryDir temp;const auto data=temp.filePath("data"),logs=temp.filePath("logs"),spool=data+"/clipboard-spool";
        put(spool+"/"+a+"/file","old");put(logs+"/diagnostic.1.jsonl","old");
        auto plan=planCacheCleanup(data,logs);put(spool+"/"+a+"/file","changed");put(logs+"/diagnostic.1.jsonl","changed");
        auto result=executeCacheCleanup(plan);QCOMPARE(result.items,0);QVERIFY(result.skipped>=2);
        QCOMPARE(read(spool+"/"+a+"/file"),QByteArray("changed"));QCOMPARE(read(logs+"/diagnostic.1.jsonl"),QByteArray("changed"));
        plan=planCacheCleanup(data,logs);result=executeCacheCleanup(plan,{},[]{return true;});QVERIFY(result.canceled);QVERIFY(QFileInfo::exists(logs+"/diagnostic.1.jsonl"));
        QVERIFY(!isCleanupToken("../drafts"));QVERIFY(!isCleanupToken(a.toUpper()));
    }
    void inactiveCurrentLog() {
#ifndef Q_OS_WIN
        QSKIP("Handle-pinned cleanup is currently Windows-only");
#endif
        QTemporaryDir temp;const auto data=temp.filePath("data"),logs=temp.filePath("logs");
        put(data+"/drafts/draft","draft");put(logs+"/diagnostic.jsonl","current");
        auto active=planCacheCleanup(data,logs);QCOMPARE(active.items,0);
        auto inactive=planCacheCleanup(data,logs,{}, {},false);QCOMPARE(inactive.items,1);
        auto result=executeCacheCleanup(inactive);QCOMPARE(result.items,1);QCOMPARE(result.bytes,quint64(7));
        QCOMPARE(read(data+"/drafts/draft"),QByteArray("draft"));
    }
    void linksAreSkipped() {
#ifndef Q_OS_WIN
        QSKIP("Handle-pinned cleanup is currently Windows-only");
#else
        QTemporaryDir temp;const auto data=temp.filePath("data"),logs=temp.filePath("logs"),spool=data+"/clipboard-spool";
        put(data+"/drafts/valuable","keep");put(spool+"/"+a+"/placeholder","x");
        const auto source=QDir::toNativeSeparators(data+"/drafts/valuable"), hard=QDir::toNativeSeparators(spool+"/"+a+"/hardlink");
        QVERIFY(CreateHardLinkW(reinterpret_cast<LPCWSTR>(hard.utf16()),reinterpret_cast<LPCWSTR>(source.utf16()),nullptr));
        QDir().mkpath(logs);const auto target=QDir::toNativeSeparators(data+"/drafts"), link=QDir::toNativeSeparators(spool+"/"+b);
        QProcess junction;junction.start("cmd.exe",{"/c","mklink","/J",link,target});
        QVERIFY(junction.waitForFinished(5000));QCOMPARE(junction.exitCode(),0);
        const auto plan=planCacheCleanup(data,logs);QVERIFY(plan.batches.isEmpty());QVERIFY(plan.skipped>=2);
        const auto result=executeCacheCleanup(plan);QCOMPARE(result.items,0);
        QCOMPARE(read(data+"/drafts/valuable"),QByteArray("keep"));QVERIFY(QFileInfo(spool+"/"+b).isJunction());
#endif
    }
    void rootReplacementAndBounds() {
#ifndef Q_OS_WIN
        QSKIP("Handle-pinned cleanup is currently Windows-only");
#endif
        QTemporaryDir temp;const auto data=temp.filePath("data"),logs=temp.filePath("logs"),spool=data+"/clipboard-spool";
        put(spool+"/"+a+"/file","original");QDir().mkpath(logs);
        auto plan=planCacheCleanup(data,logs);QCOMPARE(plan.batches.size(),1);
        QVERIFY(QDir().rename(data,data+"-old"));put(spool+"/"+a+"/file","replacement");
        const auto result=executeCacheCleanup(plan);QVERIFY(result.failures>0);
        QCOMPARE(read(spool+"/"+a+"/file"),QByteArray("replacement"));
        const auto parentPath=planCacheCleanup(data+"/../data",logs);QVERIFY(parentPath.partial);QVERIFY(parentPath.batches.isEmpty());
        QString deep=spool+"/"+b;for(int i=0;i<26;++i)deep+="/d";
        put(deep+"/file","deep");const auto bounded=planCacheCleanup(data,logs);QVERIFY(bounded.partial);
        for(const auto &batch:bounded.batches)QVERIFY(batch.token!=b);
    }
    void controllerConfirmed() {
#ifndef Q_OS_WIN
        QSKIP("Handle-pinned cleanup is currently Windows-only");
#endif
        QTemporaryDir temp;const auto data=temp.filePath("data"),logs=temp.filePath("logs"),spool=data+"/clipboard-spool";
        put(data+"/connections.json","connections");put(data+"/drafts/unsaved","draft");put(data+"/settings/user","settings");
        put(logs+"/diagnostic.jsonl","old-current");put(logs+"/diagnostic.1.jsonl","rotated");put(spool+"/"+a+"/file","cache");
        QWidget page;CacheCleanupController controller(&page);controller.setTokenProvider([]{return QSet<QString>{};});
        QTimer closeResult;connect(&closeResult,&QTimer::timeout,&page,[]{
            if(auto box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                if(box->windowTitle()==QStringLiteral("清理完成"))box->button(QMessageBox::Ok)->click();
        });closeResult.start(5);
        bool active=true;controller.setLoggingActiveProvider([&active]{return active;});QSignalSpy finished(&controller,&CacheCleanupController::finished);
        controller.start(data,logs);QTRY_VERIFY_WITH_TIMEOUT(page.findChild<QMessageBox *>("cacheCleanupConfirm"),5000);
        page.findChild<QMessageBox *>("cacheCleanupConfirm")->done(QMessageBox::Yes);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,5000);
        QCOMPARE(read(logs+"/diagnostic.jsonl"),QByteArray("old-current"));QCOMPARE(read(logs+"/diagnostic.1.jsonl"),QByteArray("rotated"));
        QVERIFY(!QFileInfo::exists(spool+"/"+a));
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);active=false;controller.start(data,logs);
        QTRY_VERIFY_WITH_TIMEOUT(page.findChild<QMessageBox *>("cacheCleanupConfirm"),5000);
        page.findChild<QMessageBox *>("cacheCleanupConfirm")->done(QMessageBox::Yes);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(),2,5000);
        QVERIFY(!QFileInfo::exists(logs+"/diagnostic.jsonl"));QVERIFY(!QFileInfo::exists(logs+"/diagnostic.1.jsonl"));
        QCOMPARE(read(data+"/connections.json"),QByteArray("connections"));QCOMPARE(read(data+"/drafts/unsaved"),QByteArray("draft"));
        QCOMPARE(read(data+"/settings/user"),QByteArray("settings"));
    }
    void controllerRejectAndCancel() {
#ifndef Q_OS_WIN
        QSKIP("Handle-pinned cleanup is currently Windows-only");
#endif
        QTemporaryDir temp;auto data=temp.filePath("data"),logs=temp.filePath("logs");
        put(data+"/drafts/unsaved","draft");put(logs+"/diagnostic.1.jsonl","archive");
        QWidget page;CacheCleanupController controller(&page);controller.setTokenProvider([]{return QSet<QString>{};});controller.setLoggingActiveProvider([]{return true;});
        QSignalSpy finished(&controller,&CacheCleanupController::finished);controller.start(data,logs);
        QTRY_VERIFY_WITH_TIMEOUT(page.findChild<QMessageBox *>("cacheCleanupConfirm"),5000);
        auto box=page.findChild<QMessageBox *>("cacheCleanupConfirm");QCOMPARE(box->standardButton(box->defaultButton()),QMessageBox::Cancel);
        box->reject();QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,5000);QCOMPARE(read(logs+"/diagnostic.1.jsonl"),QByteArray("archive"));
        controller.start(data,logs);controller.cancel();QTRY_COMPARE_WITH_TIMEOUT(finished.count(),2,5000);
        QCOMPARE(read(logs+"/diagnostic.1.jsonl"),QByteArray("archive"));
        OperationGate::setLocked(true);controller.start(data,logs);QVERIFY(!controller.isBusy());
        OperationGate::setLocked(false);controller.start(data,logs);
        QTRY_VERIFY_WITH_TIMEOUT(page.findChild<QMessageBox *>("cacheCleanupConfirm"),5000);
        OperationGate::lockAgain();page.findChild<QMessageBox *>("cacheCleanupConfirm")->done(QMessageBox::Yes);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(),3,5000);OperationGate::setLocked(false);
        QCOMPARE(read(logs+"/diagnostic.1.jsonl"),QByteArray("archive"));
    }
    void destroyedPageCancels() {
#ifndef Q_OS_WIN
        QSKIP("Handle-pinned cleanup is currently Windows-only");
#endif
        QTemporaryDir temp;const auto data=temp.filePath("data"),logs=temp.filePath("logs");
        put(data+"/drafts/unsaved","draft");put(logs+"/diagnostic.1.jsonl","archive");
        auto page=new QWidget;auto controller=new CacheCleanupController(page);
        controller->setTokenProvider([]{return QSet<QString>{};});controller->setLoggingActiveProvider([]{return false;});
        controller->start(data,logs);QTRY_VERIFY_WITH_TIMEOUT(page->findChild<QMessageBox *>("cacheCleanupConfirm"),5000);
        delete page;QCoreApplication::processEvents();QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QCOMPARE(read(logs+"/diagnostic.1.jsonl"),QByteArray("archive"));QCOMPARE(read(data+"/drafts/unsaved"),QByteArray("draft"));
    }
};
QTEST_MAIN(CacheCleanupTests)
#include "test_cachecleanup.moc"
