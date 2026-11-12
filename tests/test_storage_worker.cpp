#include "../src/core/storage.h"
#include "../src/core/diagnostics.h"
#include <QtTest>
using namespace rs;
class StorageTests:public QObject {
    Q_OBJECT
    QTemporaryDir root;
    QString path(const QString &n)const{return root.filePath(n);}
    void write(const QString &p,const QByteArray &data){QDir().mkpath(QFileInfo(p).absolutePath());QFile f(p);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(data),qint64(data.size()));}
private slots:
    void initTestCase(){QVERIFY(root.isValid());QCoreApplication::setOrganizationName("StorageFixture");QCoreApplication::setApplicationName("Fixture");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,path("registry-fixture"));}
    void validationAndTypedMigration(){
        auto source=path("源 数据"),target=path("目标 数据"),nested=source+"/nested",busy=path("busy"),pointer=path("pointer/location.json");
        QVERIFY(QDir().mkpath(nested));QVERIFY(QDir().mkpath(target));QVERIFY(QDir().mkpath(busy));write(busy+"/user.txt","keep");
        QString e;QVERIFY(!validateStorageTarget(source,nested,&e));QVERIFY(!validateStorageTarget(source,busy,&e));QVERIFY(!migrateStorage(source,busy,&e,false,pointer));
        write(source+"/connections.json","fixture-connection");write(source+"/drafts/中文.json","draft");write(source+"/application-lock.required","1");
        QSettings s;s.setValue("geometry",QByteArray("\0\1abc",5));s.setValue("network/nic",QString::fromUtf8("网卡"));s.setValue("columns",QVariantList{12,55,77});s.sync();QCOMPARE(s.status(),QSettings::NoError);
        QVERIFY2(migrateStorage(source,target,&e,false,pointer),qPrintable(e));
        QVERIFY(QFileInfo::exists(pointer));QVERIFY(!QFileInfo::exists(source+"/connections.json"));QVERIFY(QFileInfo::exists(target+"/drafts/中文.json"));
        QSettings out(target+"/settings/StorageFixture/Fixture.ini",QSettings::IniFormat);QCOMPARE(out.value("geometry"),s.value("geometry"));QCOMPARE(out.value("network/nic"),s.value("network/nic"));QCOMPARE(out.value("columns"),s.value("columns"));
        QFile keep(busy+"/user.txt");QVERIFY(keep.open(QIODevice::ReadOnly));QCOMPARE(keep.readAll(),QByteArray("keep"));
    }
    void retryAndSecondMove(){
        const auto source=path("second-source"),target=path("second-target"),invalid=path("invalid-pointer"),pointer=path("pointer2/location.json");
        QVERIFY(QDir().mkpath(source));QVERIFY(QDir().mkpath(target));write(invalid,"block");
        configureStorageSettings(source);{QSettings s;s.setValue("font/list",QVariantList{QString("中文"),42});s.sync();}
        write(source+"/connections.json","unchanged");write(source+"/clipboard-spool/batch/payload","clipboard fixture");QString e;
        QVERIFY(!migrateStorage(source,target,&e,false,invalid+"/location.json"));QVERIFY(QFileInfo::exists(source+"/connections.json"));
        write(target+"/connections.json","external edit");QVERIFY(!validateStorageTarget(source,target,&e));
        QFile edited(target+"/connections.json");QVERIFY(edited.open(QIODevice::ReadOnly));QCOMPARE(edited.readAll(),QByteArray("external edit"));edited.close();
        write(target+"/connections.json","unchanged");
        QVERIFY2(validateStorageTarget(source,target,&e),qPrintable(e));
        QVERIFY2(migrateStorage(source,target,&e,false,pointer),qPrintable(e));
        QVERIFY(!QFileInfo::exists(source+"/settings/StorageFixture/Fixture.ini"));
        QVERIFY(!QFileInfo::exists(target+"/.frogshell-migration.json"));QVERIFY(QDir(target).entryList({".frogshell-stage-*"},QDir::Dirs|QDir::Hidden).isEmpty());
        QSettings out(target+"/settings/StorageFixture/Fixture.ini",QSettings::IniFormat);QCOMPARE(out.value("font/list").toList(),(QVariantList{QString("中文"),42}));
    }
    void disabledLogging(){
        auto isolated=path("disabled");QVERIFY(QDir().mkpath(isolated));qputenv("RABBITSHELL_DATA_DIR",isolated.toUtf8());
        QSettings().remove("storage/logDirectory");QVERIFY(!DiagnosticLog::instance().isRunning());diagnose(DiagEvent::Started);QTest::qWait(20);QVERIFY(!QFileInfo::exists(isolated+"/logs"));
        QString e;QVERIFY2(DiagnosticLog::instance().startLogging(&e),qPrintable(e));QTest::qWait(30);DiagnosticLog::instance().stopLogging();QVERIFY(QFileInfo::exists(isolated+"/logs/diagnostic.jsonl"));
        qunsetenv("RABBITSHELL_DATA_DIR");
    }
};
QTEST_MAIN(StorageTests)
#include "test_storage_worker.moc"
