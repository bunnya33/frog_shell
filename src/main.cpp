#include "ui/messagebox.h"
#include "ui/mainwindow.h"
#include "ui/applock.h"
#include "core/singleinstance.h"
#include "core/storage.h"
#include "ui/theme.h"
#include "ui/windowstyle.h"
#include "core/diagnostics.h"
#include "ui/diagnosticapplication.h"
#include "core/documentsearch.h"
#include "core/selfupdate.h"
#include "ui/updatecontroller.h"
#include <QtWidgets>
int main(int argc, char **argv) {
    if(argc==3&&QByteArray(argv[1])==rs::selfupdate::HelperArgument){
        QCoreApplication helper(argc,argv);return rs::selfupdate::runInstallHelper(QCoreApplication::arguments().at(2));
    }
    if(argc==2&&QByteArray(argv[1])=="--internal-document-search-worker"){
        QCoreApplication worker(argc,argv);return rs::documentSearchWorker();
    }
    rs::DiagnosticApplication app(argc, argv);
    QCoreApplication::setOrganizationName("RabbitShell");
    QCoreApplication::setApplicationName("RabbitShell");
    QGuiApplication::setApplicationDisplayName("FrogShell");
    QCoreApplication::setApplicationVersion(RABBITSHELL_VERSION);
    QApplication::setStyle("Fusion");
    QApplication::setWindowIcon(rs::applicationIcon());
    rs::installWindowStyle();
    QCommandLineParser parser;
    parser.setApplicationDescription("FrogShell · Qt 原生 SSH 工作区");
    parser.addHelpOption();
    parser.addOption({QStringList{"v","version"},"显示版本"});
    parser.addOption({"demo", "打开离线界面预览，不连接服务器"});
    parser.addOption({"data-dir", "独立数据目录（测试与便携运行）", "path"});
    parser.addOption({rs::selfupdate::ReceiptArgument+2,"本机更新启动回执", "path"});
    parser.addOption({rs::selfupdate::ReceiptTokenArgument+2,"本机更新事务", "token"});
    parser.process(app);
    if(parser.isSet("version")){QTextStream(stdout)<<"FrogShell "<<RABBITSHELL_VERSION<<Qt::endl;return 0;}
    if(parser.isSet("data-dir")) {
#ifdef Q_OS_WIN
        const auto directory=parser.value("data-dir").toStdWString();
        if(_wputenv_s(L"RABBITSHELL_DATA_DIR",directory.c_str())!=0)return 2;
#else
        qputenv("RABBITSHELL_DATA_DIR",parser.value("data-dir").toUtf8());
#endif
    }
    QString migration,problem;int result=0;bool restart=false;
    rs::selfupdate::PreparedInstall preparedUpdate;
    {
        // Default identity remains locked even after a move: older versions use it.
        std::unique_ptr<rs::SingleInstance> legacy;
        if(!rs::storageOverride()) {
            legacy=std::make_unique<rs::SingleInstance>(rs::defaultDataPath());
            const auto status=legacy->acquire();
            if(status==rs::SingleInstance::Activated)return 0;
            if(status==rs::SingleInstance::Failed){rs::AppMessageBox::warning(nullptr,"FrogShell",legacy->error());return 2;}
        }
        QString source;
        if(!rs::resolveStoragePath(&source,&problem)){rs::AppMessageBox::warning(nullptr,"FrogShell",problem);return 2;}
        rs::setDataPath(source);
        std::unique_ptr<rs::SingleInstance> active;
#ifdef Q_OS_WIN
        constexpr auto pathCase=Qt::CaseInsensitive;
#else
        constexpr auto pathCase=Qt::CaseSensitive;
#endif
        if(rs::storageOverride() || QDir::cleanPath(source).compare(QDir::cleanPath(rs::defaultDataPath()),pathCase)!=0){
            active=std::make_unique<rs::SingleInstance>(source);
            const auto status=active->acquire();
            if(status==rs::SingleInstance::Activated)return 0;
            if(status==rs::SingleInstance::Failed){rs::AppMessageBox::warning(nullptr,"FrogShell",active->error());return 2;}
        }
        if(rs::storageOverride()){
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,source);
            QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,source);
        } else if(QFileInfo::exists(rs::storagePointerFile())) rs::configureStorageSettings(source);
        if(QSettings().value("storage/logEnabled",false).toBool()){
            QString error;if(!rs::DiagnosticLog::instance().startLogging(&error))rs::AppMessageBox::warning(nullptr,"无法启用日志",error);
        }
        QObject::connect(&app,&QCoreApplication::aboutToQuit,&app,[]{rs::DiagnosticLog::instance().stopLogging();});
        rs::applyTheme(QSettings().value("appearance/light",false).toBool());
        {
            rs::AppLock lock(source);
            rs::selfupdate::PortableOrigin origin;
            rs::selfupdate::originFromEnvironment(QProcessEnvironment::systemEnvironment(),&origin);
            auto updates=std::make_unique<rs::UpdateController>(rs::UpdateController::runtimeConfig(),origin,source+"/updates");
            std::unique_ptr<rs::MainWindow> window;
            auto acknowledge=[&]{
                QSettings settings;const bool receiptArgument=parser.isSet(rs::selfupdate::ReceiptArgument+2);
                const auto receipt=receiptArgument?parser.value(rs::selfupdate::ReceiptArgument+2):settings.value("update/installReceipt").toString();
                const auto token=receiptArgument?parser.value(rs::selfupdate::ReceiptTokenArgument+2):settings.value("update/installToken").toString();
                if(!receipt.isEmpty()&&!token.isEmpty()){
                    const auto ack=rs::selfupdate::acknowledgeNormalStartup(receipt,token,origin);
                    if(ack.ok()){settings.setValue("update/installReceipt",receipt);settings.setValue("update/installToken",token);settings.remove("update/lastInstallStatus");}
                }
                updates->refreshInstallStatus();
            };
            auto launch=[&]{if(!window){rs::diagnose(rs::DiagEvent::MainWindowStartup);window=std::make_unique<rs::MainWindow>(parser.isSet("demo"),rs::systemSecretBackend(),updates.get());window->show();app.setQuitOnLastWindowClosed(false);QTimer::singleShot(1500,updates.get(),[&]{updates->check(true);});}};
            QObject::connect(&lock,&rs::AppLock::unlocked,&app,launch);
            QObject::connect(active?active.get():legacy.get(),&rs::SingleInstance::activationRequested,&lock,&rs::AppLock::activate);
            if(legacy&&active)QObject::connect(legacy.get(),&rs::SingleInstance::activationRequested,&lock,&rs::AppLock::activate);
            if(lock.locked()){rs::diagnose(rs::DiagEvent::LockStartup);app.setQuitOnLastWindowClosed(false);lock.showStartup();}
            else launch();
            // A visible startup lock is a successful normal startup too. This
            // does not unlock, create workspaces or bypass the normal gate.
            QTimer::singleShot(0,&lock,acknowledge);
            result=app.exec();
            if(window)migration=window->migrationDestination();
            const bool confirmed=window&&window->exitWasConfirmed();
            window.reset();
            if(confirmed&&migration.isEmpty())preparedUpdate=updates->takePreparedInstall();
            else updates->cancelExitIntent();
        } // AppLock and its SecurityStore finish before copying.
        rs::DiagnosticLog::instance().stopLogging();
        QThreadPool::globalInstance()->waitForDone();
        if(!migration.isEmpty()){
            restart=true;
            if(rs::validateStorageTarget(source,migration,&problem)){
                rs::SingleInstance destination(migration);
                if(destination.acquire()!=rs::SingleInstance::Primary)problem="新目录正在使用或无法锁定。";
                else if(rs::migrateStorage(source,migration,&problem,true))problem.clear();
            }
        }
    } // Release both source locks before restarting.
    if(preparedUpdate.valid()&&!restart){
        const auto installed=rs::selfupdate::startAfterMainLoopExit(&preparedUpdate);
        if(!installed.ok()){QSettings().setValue("update/lastInstallStatus","上次更新未进入安装流程；原程序保持不变。"+installed.message);return 2;}
        return result;
    }
    if(restart){
        if(!problem.isEmpty())rs::AppMessageBox::warning(nullptr,"迁移未完成",problem+"\n原配置保持可用，将重新启动。");
        QStringList restartArguments;if(parser.isSet("demo"))restartArguments<<"--demo";
        if(!QProcess::startDetached(QCoreApplication::applicationFilePath(),restartArguments)) {
            rs::AppMessageBox::warning(nullptr,"无法重启","请手动启动 FrogShell。");return 2;
        }
    }
    return result;
}
