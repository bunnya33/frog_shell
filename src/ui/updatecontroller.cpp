#include "updatecontroller.h"
#include "applock.h"
#include "layoutmetrics.h"
#include "promptstyle.h"
#include "theme.h"
#include "windowstyle.h"
#include "../core/operationgate.h"
#include <QtWidgets>
#include <QNetworkAccessManager>
#include <atomic>

namespace rs {
namespace {
using S = releaseupdate::State;
QString chineseError(const releaseupdate::Error &error) {
    using E = releaseupdate::ErrorCode;
    switch(error.code) {
    case E::Busy:return "已有更新任务正在进行。";
    case E::InvalidConfiguration:return "更新源无效，请检查 GitHub 用户或组织和仓库名称。";
    case E::InvalidMetadata:return "发布信息不完整或格式无效。";
    case E::InvalidVersion:return "发布版本格式无效，未使用此更新。";
    case E::UnsafeUrl:return "发布下载地址不符合安全规则。";
    case E::Http:return error.httpStatus==404?"未找到公开发布版本，请检查仓库名称及 Release。":QString("更新服务返回错误（HTTP %1）。").arg(error.httpStatus);
    case E::RateLimited:return error.retryAfterUtc.isValid()?"请求已被限流，请在 "+error.retryAfterUtc.toLocalTime().toString("MM-dd HH:mm")+" 后重试。":"请求已被限流，请稍后重试。";
    case E::Network:return "无法连接更新服务，请检查网络后重试。";
    case E::Tls:return "无法验证更新服务的安全连接。";
    case E::Timeout:return error.timeout==releaseupdate::TimeoutKind::Deadline?"更新任务超过总时限，请重试。":
        error.timeout==releaseupdate::TimeoutKind::Inactivity?"更新连接长时间未收到数据，请检查网络后重试。":"更新请求超时，请重试。";
    case E::SizeLimit:return "发布信息或下载文件超过大小限制。";
    case E::MissingDigest:return "此发布没有可验证的对应安装包，请从发布页手动下载。";
    case E::DigestMismatch:return "下载文件的 SHA-256 校验失败，未准备安装。";
    case E::Io:return "无法写入更新暂存目录，请检查本机空间和权限。";
    case E::Cancelled:return "已取消。";
    default:return "检查更新失败，请重试。";
    }
}
QLabel *plainLabel(const QString &text,QWidget *parent=nullptr) {
    auto label=new QLabel(text,parent);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);return label;
}
QString bytes(qint64 n) {
    return n>=1024*1024?QString::number(n/double(1024*1024),'f',1)+" MiB":QString::number(n/1024.0,'f',0)+" KiB";
}
// A prepare worker borrows the verified file. Keep its ownership until all
// readers have finished, including canceled and stale operations.
struct DownloadLease {
    releaseupdate::VerifiedDownload download;
    ~DownloadLease(){
        if(download.path.isEmpty())return;
        if(!QFileInfo::exists(download.path)||QFile::remove(download.path))QDir().rmdir(QFileInfo(download.path).absolutePath());
        else {
            // An external handle can still deny deletion. Retain a bounded
            // cleanup record; never turn this into arbitrary-path deletion.
            QSettings settings;auto pending=settings.value("update/pendingDownloadCleanup").toList();
            pending.append(QVariantMap{{"path",download.path},{"bytes",download.size},{"sha256",download.sha256.toHex()}});
            while(pending.size()>16)pending.removeFirst();settings.setValue("update/pendingDownloadCleanup",pending);
        }
    }
};
}
struct UpdateController::Impl {
    UpdateController *q;
    releaseupdate::Client *client;
    selfupdate::PortableOrigin origin;
    QString base,status,installStatus,priorReceipt,priorToken;
    releaseupdate::VerifiedDownload verified;
    std::shared_ptr<DownloadLease> downloadLease;
    QPointer<QDialog> about;
    selfupdate::PreparedInstall install;
    std::shared_ptr<std::atomic_bool> canceled;
    quint64 generation=0;
    bool preparing=false,restarting=false,approved=false,migrating=false;
    bool recoveryRequired=false,available=false,oneClick=false;
    bool startupDue=false,startupPending=true,automaticAttemptInProcess=false;
    QString availableVersion,retryStatus;
    qint64 received=0,total=0;
    Impl(UpdateController *q,const releaseupdate::Config &config,const selfupdate::PortableOrigin &origin,
         const QString &base,QNetworkAccessManager *transport):q(q),origin(origin),base(base) {
        auto source=config;QSettings settings;
        source.owner="bunnya33";source.repository="frog_shell";
        if(settings.contains("update/githubOwner")||settings.contains("update/githubRepo")) {
            if(settings.value("update/githubOwner").toString().compare(source.owner,Qt::CaseInsensitive)!=0 ||
               settings.value("update/githubRepo").toString().compare(source.repository,Qt::CaseInsensitive)!=0) {
                settings.remove("update/lastAutomaticAttemptUtc");settings.remove("update/lastNotifiedVersion");settings.remove("update/rateLimitUntilUtc");
            }
            settings.remove("update/githubOwner");settings.remove("update/githubRepo");
        }
        client=new releaseupdate::Client(source,q,transport);
        status=source.owner.isEmpty()&&source.repository.isEmpty()?"尚未配置更新源。":"点击“检查更新”获取最新稳定版本。";
    }
    bool automaticInstall() const {
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
        return origin.singleExecutable;
#else
        return false;
#endif
    }
    QString installHint() const {
        return automaticInstall()?"更新经过 SHA-256 校验，并在退出确认后重启安装。":"此运行方式仅支持手动更新，不会自动替换程序。";
    }
    void changed(const QString &text={}) {if(!text.isEmpty())status=text;emit q->changed();}
    void releaseDownload() {
        if(verified.path.isEmpty())return;
        verified={};downloadLease.reset();
    }
};
releaseupdate::Config UpdateController::runtimeConfig() {
    releaseupdate::Config config;config.owner="bunnya33";config.repository="frog_shell";config.currentVersion=QCoreApplication::applicationVersion();
    if(config.currentVersion.isEmpty())config.currentVersion=RABBITSHELL_VERSION;
#ifdef Q_OS_WIN
    config.platform="windows";
#elif defined(Q_OS_MACOS)
    config.platform="macos";
#else
    config.platform="linux";
#endif
    config.architecture=QSysInfo::currentCpuArchitecture().toLower();
    if(config.architecture=="amd64")config.architecture="x86_64";
    if(config.architecture=="aarch64")config.architecture="arm64";
    return config;
}
UpdateController::UpdateController(const releaseupdate::Config &runtime,const selfupdate::PortableOrigin &origin,
        const QString &base,QObject *parent,QNetworkAccessManager *transport):QObject(parent),d(std::make_unique<Impl>(this,runtime,origin,base,transport)) {
    refreshInstallStatus();
    auto periodic=new QTimer(this);periodic->setTimerType(Qt::PreciseTimer);periodic->setInterval(30*60*1000);
    connect(d->client,&releaseupdate::Client::stateChanged,this,[this](S state,const QString &){
        QString text;
        switch(state) {
        case S::Idle:text="点击“检查更新”获取最新稳定版本。";break;
        case S::Unconfigured:text="尚未配置更新源，请在设置中填写 GitHub 仓库。";break;
        case S::Disabled:text="自动检查已关闭。";break;
        case S::Throttled:text="检查间隔尚未到，或更新服务要求稍后重试。";break;
        case S::Checking:text="正在检查更新…";break;
        case S::UpToDate:text="当前已是最新稳定版本。";break;
        case S::Available:text="发现新版本 "+d->client->release().version+"。";break;
        case S::ManualDownload:text="此发布需手动更新，请打开发布页。";break;
        case S::Downloading:d->retryStatus.clear();text="正在下载更新，当前连接和任务将继续运行。";break;
        case S::Ready:text="下载已完成，SHA-256 校验通过。";break;
        case S::Cancelled:text="已取消更新任务。";break;
        case S::Failed:text=chineseError(d->client->lastError());if(d->client->canResumeDownload())text+=" 已保留下载进度，点击重试可继续下载。";break;
        }
        if(state==S::Failed||state==S::Cancelled)d->oneClick=false;
        d->changed(text);
    });
    connect(d->client,&releaseupdate::Client::automaticAttemptStarted,this,[this,periodic](const QDateTime &utc){d->automaticAttemptInProcess=true;QSettings().setValue("update/lastAutomaticAttemptUtc",utc);periodic->start(30*60*1000);});
    connect(d->client,&releaseupdate::Client::errorOccurred,this,[this](const auto &error){if(error.code==releaseupdate::ErrorCode::RateLimited&&error.retryAfterUtc.isValid())QSettings().setValue("update/rateLimitUntilUtc",error.retryAfterUtc);auto text=chineseError(error);if(d->client->canResumeDownload())text+=" 已保留下载进度，点击重试可继续下载。";d->changed(text);});
    connect(d->client,&releaseupdate::Client::checkFinished,this,[this](const releaseupdate::CheckResult &result){
        if(result.state==S::Available||result.state==S::ManualDownload){d->available=true;d->availableVersion=result.release.version;}
        else if(result.state==S::UpToDate){d->available=false;d->availableVersion.clear();}
        const bool proceed=d->oneClick&&result.state==S::Available&&!OperationGate::locked();
        if(result.state!=S::Available)d->oneClick=false;
        d->changed();
        if(result.shouldNotify&&!OperationGate::locked()) {
            QSettings().setValue("update/lastNotifiedVersion",result.release.version);emit updateAvailable(result.release.version);
        }
        if(proceed)download();
        if(d->startupDue&&d->startupPending)QTimer::singleShot(0,this,&UpdateController::tryStartupCheck);
    });
    connect(d->client,&releaseupdate::Client::downloadProgress,this,[this](qint64 received,qint64 total){d->received=received;d->total=total;const bool retried=!d->retryStatus.isEmpty();d->retryStatus.clear();d->changed(retried?QStringLiteral("正在继续下载更新…"):QString());});
    connect(d->client,&releaseupdate::Client::downloadRetry,this,[this](int attempt,int maximum,int delay,const releaseupdate::Error &reason){
        d->retryStatus=QString("%1 %2 秒后重试（%3/%4），保留下载进度…").arg(chineseError(reason)).arg((delay+999)/1000).arg(attempt).arg(maximum);d->changed(d->retryStatus);
    });
    connect(d->client,&releaseupdate::Client::downloadVerified,this,[this](const auto &download){d->releaseDownload();d->verified=download;d->downloadLease=std::make_shared<DownloadLease>();d->downloadLease->download=download;const bool proceed=d->oneClick;d->oneClick=false;d->changed("下载已完成，SHA-256 校验通过。");if(proceed&&!OperationGate::locked())requestRestart();});
    if(auto lock=AppLock::instance())connect(lock,&AppLock::lockedChanged,this,[this](bool locked){if(locked){const bool active=updateBusy();cancel();if(active)d->changed("软件已锁定，更新任务已取消。");}else tryStartupCheck();});
    QTimer::singleShot(1500,this,[this]{d->startupDue=true;tryStartupCheck();});
    connect(periodic,&QTimer::timeout,this,[this]{if(d->startupPending&&d->startupDue)tryStartupCheck();else check(true);});periodic->start();
}
UpdateController::~UpdateController(){if(d->canceled)d->canceled->store(true);if(d->about)delete d->about;d->client->cancel();d->releaseDownload();}
releaseupdate::Config UpdateController::configuration()const{return d->client->config();}
S UpdateController::state()const{return d->client->state();}
QString UpdateController::statusText()const{return d->status;}
QString UpdateController::installStatusText()const{return d->installStatus;}
void UpdateController::refreshInstallStatus(){
    QSettings settings;const auto receipt=settings.value("update/installReceipt").toString();
    d->recoveryRequired=false;d->installStatus.clear();
    if(receipt.isEmpty()){d->installStatus=settings.value("update/lastInstallStatus").toString();d->changed();return;}
    selfupdate::InstallStatus state;const auto result=selfupdate::readInstallStatus(receipt,&state);
    if(!result.ok())d->installStatus=settings.value("update/lastInstallStatus","上次更新未完成安装；原程序保持不变。").toString();
    else if(state.phase=="complete")d->installStatus="上次更新已安装，并完成正常启动确认。";
    else if(state.code==selfupdate::Code::RecoveryRequired){d->recoveryRequired=true;d->installStatus="上次更新需要手动恢复。请保留备份：\n"+state.backupPath;}
    else if(state.code==selfupdate::Code::RolledBack)d->installStatus="上次更新未能启动，已回滚原程序。";
    else if(state.phase=="installed-awaiting-startup"||state.phase=="startup-confirmed")d->installStatus="上次更新尚未完成启动确认，旧程序备份仍保留。";
    else d->installStatus="上次更新尚未完成安装。";
    d->changed();
}
bool UpdateController::ready()const{return !d->verified.path.isEmpty();}
bool UpdateController::preparing()const{return d->preparing;}
bool UpdateController::restartPending()const{return d->restarting||d->preparing;}
bool UpdateController::setAutomaticCheckEnabled(bool automatic,QString *error) {
    if(OperationGate::locked()||restartPending()){if(error)*error="请先完成或取消重启更新。";return false;}
    QSettings settings;const bool had=settings.contains("update/automaticCheckEnabled");const auto old=settings.value("update/automaticCheckEnabled");
    settings.setValue("update/automaticCheckEnabled",automatic);settings.sync();
    if(settings.status()!=QSettings::NoError){if(had)settings.setValue("update/automaticCheckEnabled",old);else settings.remove("update/automaticCheckEnabled");if(error)*error="更新设置未保存，请检查配置目录权限。";return false;}
    d->changed("更新设置已保存。");emit sourceChanged();return true;
}
bool UpdateController::hasUpdate()const{return d->available;}
bool UpdateController::automaticInstallSupported()const{return d->automaticInstall();}
bool UpdateController::updateBusy()const{return d->oneClick||d->client->busy()||restartPending();}
QString UpdateController::updateVersion()const{return d->availableVersion;}
QString UpdateController::progressText()const {
    if(state()==S::Downloading){if(!d->retryStatus.isEmpty())return d->retryStatus;return d->total>0?QString("正在下载更新 %1%…").arg(int(qMin<qint64>(100,d->received*100/d->total))):QStringLiteral("正在下载更新…");}
    if(d->preparing)return "正在准备更新…";
    if(d->restarting)return "正在确认退出…";
    return "正在检查更新…";
}
void UpdateController::requestUpdate(){
    if(OperationGate::locked()||updateBusy()||!d->available)return;
    if(!d->automaticInstall()||state()==S::ManualDownload){emit manualUpdateRequested();return;}
    if(ready()){requestRestart();return;}
    d->oneClick=true;
    if(state()==S::Available||d->client->canResumeDownload())download();else check();
    if(!d->client->busy()&&!restartPending())d->oneClick=false;
    d->changed();
}
void UpdateController::check(bool automatic) {
    checkScheduled(automatic?releaseupdate::CheckMode::Automatic:releaseupdate::CheckMode::Manual);
}
void UpdateController::tryStartupCheck() {
    if(!d->startupDue||!d->startupPending||OperationGate::locked())return;
    if(!QSettings().value("update/automaticCheckEnabled",true).toBool()){d->startupPending=false;return;}
    if(d->automaticAttemptInProcess){d->startupPending=false;return;}
    if(d->migrating||restartPending()||d->client->busy()||d->oneClick||ready())return;
    d->startupPending=false;
    checkScheduled(releaseupdate::CheckMode::Startup);
}
void UpdateController::checkScheduled(releaseupdate::CheckMode mode) {
    const bool automatic=mode!=releaseupdate::CheckMode::Manual;
    if(OperationGate::locked()||d->migrating||restartPending()||d->client->busy()||(automatic&&(d->oneClick||ready()||d->client->canResumeDownload())))return;
    if(automatic&&!QSettings().value("update/automaticCheckEnabled",true).toBool())return;
    d->releaseDownload();d->received=d->total=0;
    QSettings settings;releaseupdate::Schedule schedule;
    schedule.automaticEnabled=settings.value("update/automaticCheckEnabled",true).toBool();
    schedule.lastAutomaticAttemptUtc=settings.value("update/lastAutomaticAttemptUtc").toDateTime();schedule.lastNotifiedVersion=settings.value("update/lastNotifiedVersion").toString();
    schedule.rateLimitUntilUtc=settings.value("update/rateLimitUntilUtc").toDateTime();
    // Preserve a usable pending release when the periodic tick is too early.
    if(mode==releaseupdate::CheckMode::Automatic&&schedule.lastAutomaticAttemptUtc.isValid()&&
       schedule.lastAutomaticAttemptUtc.toUTC().secsTo(QDateTime::currentDateTimeUtc())<1800)return;
    d->client->check(mode,schedule);
}
void UpdateController::download() {
    if(OperationGate::locked()||restartPending()||d->client->busy())return;
    if(!QDir().mkpath(d->base+"/downloads")){d->changed("无法创建更新暂存目录，请检查本机空间和权限。");return;}
    d->received=d->total=0;d->client->download(d->base+"/downloads");
}
void UpdateController::cancelExitIntent() {
    ++d->generation;if(d->canceled)d->canceled->store(true);d->preparing=false;
    const bool had=d->restarting||d->install.valid();d->restarting=d->approved=false;
    QSettings settings;
    if(d->install.valid()&&settings.value("update/installToken").toString()==d->install.transactionToken()){
        if(d->priorReceipt.isEmpty()){settings.remove("update/installReceipt");settings.remove("update/installToken");}
        else{settings.setValue("update/installReceipt",d->priorReceipt);settings.setValue("update/installToken",d->priorToken);}settings.sync();
    }
    const auto result=selfupdate::discardPreparedInstall(&d->install);
    if(had)d->changed(result.ok()?"已取消重启，尚未安装更新。":"已取消重启；部分暂存文件未能清理。");else d->changed();
}
void UpdateController::cancel(){d->oneClick=false;cancelExitIntent();d->client->cancel();d->releaseDownload();d->changed("已取消更新任务。");}
void UpdateController::setMigrationPending(bool pending){d->migrating=pending;if(pending)cancelExitIntent();else tryStartupCheck();d->changed();}
void UpdateController::confirmExit(){d->approved=d->restarting&&d->install.valid();}
selfupdate::PreparedInstall UpdateController::takePreparedInstall(){if(!d->approved)return {};d->approved=d->restarting=false;return std::move(d->install);}
void UpdateController::requestRestart() {
    if(OperationGate::locked()||!ready()||d->preparing||d->restarting)return;
    if(d->migrating){d->changed("配置迁移尚未完成，不能同时重启更新。");return;}
    if(d->recoveryRequired){d->changed("请先处理上次更新的手动恢复，再安装新版本。");return;}
    if(!d->automaticInstall()){d->changed("此运行方式需手动更新，请打开下载目录或发布页。");return;}
    if(!QDir().mkpath(d->base)){d->changed("无法创建安装暂存目录，请检查本机空间和权限。");return;}
    d->preparing=true;d->changed("正在准备更新…");const auto generation=++d->generation,epoch=OperationGate::token();
    auto canceled=std::make_shared<std::atomic_bool>(false);d->canceled=canceled;
    struct Outcome{selfupdate::Result result;selfupdate::PreparedInstall install;};auto outcome=std::make_shared<Outcome>();
    const auto origin=d->origin;const selfupdate::VerifiedUpdate update{d->verified.path,d->verified.sha256,quint64(d->verified.size)};
    const auto base=d->base;const auto lease=d->downloadLease;QPointer<UpdateController> weak(this);
    QThreadPool::globalInstance()->start(QRunnable::create([weak,generation,epoch,canceled,outcome,origin,update,base,lease]{
        outcome->result=selfupdate::prepareInstall(origin,update,base,&outcome->install,[=]{return canceled->load()||!OperationGate::accepts(epoch);});
        if(!qApp)return;
        QMetaObject::invokeMethod(qApp,[weak,generation,epoch,canceled,outcome]{
            if(!weak||weak->d->generation!=generation||canceled->load()||!OperationGate::accepts(epoch))return;
            auto &d=*weak->d;d.preparing=false;
            if(!outcome->result.ok()){d.changed(outcome->result.message);return;}
            d.install=std::move(outcome->install);d.restarting=true;d.approved=false;
            QSettings settings;d.priorReceipt=settings.value("update/installReceipt").toString();d.priorToken=settings.value("update/installToken").toString();
            settings.setValue("update/installReceipt",d.install.receiptPath());settings.setValue("update/installToken",d.install.transactionToken());settings.remove("update/lastInstallStatus");settings.sync();
            if(settings.status()!=QSettings::NoError){weak->cancelExitIntent();d.changed("无法保存安装回执位置，尚未启动重启更新。");return;}
            if(d.about)d.about->hide();d.changed("更新已准备，正在确认退出…");emit weak->restartRequested();
        },Qt::QueuedConnection);
    }));
}

QWidget *UpdateController::createSettingsPage(QWidget *parent) {
    auto page=new QWidget(parent);auto layout=new QVBoxLayout(page);uiMargins(layout,0,0,0,0);uiSpacing(layout,12);
    auto title=plainLabel("更新");title->setProperty("heading",true);layout->addWidget(title);
    auto card=new QWidget;setFrameRole(card,"card");auto form=new QVBoxLayout(card);uiMargins(form,16,16,16,16);uiSpacing(form,10);layout->addWidget(card);
    form->addWidget(plainLabel("更新源：github.com/bunnya33/frog_shell"));
    auto automatic=new QCheckBox("启动时及每 30 分钟自动检查更新");automatic->setObjectName("updateAutomaticCheck");automatic->setChecked(QSettings().value("update/automaticCheckEnabled",true).toBool());form->addWidget(automatic);
    auto note=plainLabel("默认开启；发现更新后由你选择是否安装。服务限流或更新任务进行时会延后检查。");note->setProperty("muted",true);form->addWidget(note);
    auto feedback=plainLabel({});feedback->setObjectName("updateSettingsStatus");form->addWidget(feedback);
    auto save=button("保存更新设置");save->setObjectName("saveUpdateSettings");form->addWidget(save,0,Qt::AlignLeft);
    connect(save,&QPushButton::clicked,page,[=,this]{QString error;if(setAutomaticCheckEnabled(automatic->isChecked(),&error))feedback->setText("更新设置已保存。");else feedback->setText(error);});
    auto sync=[=,this]{const bool enabled=!restartPending()&&!OperationGate::locked();automatic->setEnabled(enabled);save->setEnabled(enabled);};
    connect(this,&UpdateController::changed,page,sync);sync();layout->addStretch();return page;
}
void UpdateController::showAbout(QWidget *parent) {
    if(OperationGate::locked())return;
    if(d->about){d->about->show();d->about->raise();d->about->activateWindow();return;}
    auto dialog=new QDialog(parent);d->about=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName("aboutFrogShellDialog");
    dialog->setWindowTitle("关于 FrogShell");dialog->setWindowModality(Qt::WindowModal);dialog->setProperty("preservePortDraft",true);
    auto outer=new QVBoxLayout(dialog);uiMargins(outer,0,0,0,0);uiSpacing(outer,0);
    auto body=new QWidget;auto layout=new QVBoxLayout(body);uiMargins(layout,24,22,24,20);uiSpacing(layout,14);outer->addWidget(body,1);
    auto heading=new QHBoxLayout;auto logo=new QLabel;logo->setPixmap(applicationIcon().pixmap(uiSize(64,64)));heading->addWidget(logo);heading->addSpacing(uiPx(14));
    auto identity=new QVBoxLayout;auto name=plainLabel("FrogShell");bindUiFont(name,22,QFont::DemiBold);identity->addWidget(name);
    auto version=plainLabel("版本 "+configuration().currentVersion+"  ·  Qt "+QString::fromLatin1(qVersion()));version->setObjectName("aboutVersion");identity->addWidget(version);
    auto platform=plainLabel(QSysInfo::prettyProductName()+" · "+QSysInfo::currentCpuArchitecture());platform->setProperty("muted",true);identity->addWidget(platform);heading->addLayout(identity,1);layout->addLayout(heading);
    auto card=new QWidget;setFrameRole(card,"card");auto box=new QVBoxLayout(card);uiMargins(box,14,14,14,14);uiSpacing(box,10);layout->addWidget(card);
    auto source=plainLabel({});source->setObjectName("aboutUpdateSource");auto sourceRow=new QHBoxLayout;sourceRow->addWidget(source,1);auto settingsButton=button("更新设置…");settingsButton->setObjectName("aboutUpdateSettings");sourceRow->addWidget(settingsButton);box->addLayout(sourceRow);
    auto status=plainLabel({});status->setObjectName("aboutUpdateStatus");bindUiFont(status,13,QFont::Medium);box->addWidget(status);
    auto hint=plainLabel(d->installHint());hint->setProperty("muted",true);box->addWidget(hint);
    auto lastInstall=plainLabel({});lastInstall->setObjectName("aboutInstallStatus");box->addWidget(lastInstall);
    auto progress=new QProgressBar;progress->setObjectName("aboutUpdateProgress");box->addWidget(progress);
    auto progressText=plainLabel({});progressText->setObjectName("aboutUpdateProgressText");progressText->setProperty("muted",true);box->addWidget(progressText);
    auto notes=new QPlainTextEdit;notes->setObjectName("aboutReleaseNotes");notes->setReadOnly(true);notes->setPlaceholderText("检查到新版本后，在这里显示发布说明。");notes->setMinimumHeight(uiPx(100));notes->setMaximumHeight(uiPx(220));layout->addWidget(notes,1);
    auto footer=new PanelBand(52,true,dialog);footer->setObjectName("aboutFooter");auto actions=new QHBoxLayout(footer);uiMargins(actions,16,8,16,8);uiSpacing(actions,8);outer->addWidget(footer);
    auto checkButton=button("检查更新"),downloadButton=button("下载更新"),cancelButton=button("取消"),restartButton=button("重启更新"),releaseButton=button("发布页"),folderButton=button("下载位置"),closeButton=button("关闭");
    checkButton->setObjectName("aboutCheckUpdate");downloadButton->setObjectName("aboutDownloadUpdate");cancelButton->setObjectName("aboutCancelUpdate");restartButton->setObjectName("aboutRestartUpdate");releaseButton->setObjectName("aboutReleasePage");closeButton->setObjectName("aboutClose");restartButton->setProperty("primary",true);
    folderButton->setObjectName("aboutDownloadLocation");
    for(auto button:{checkButton,downloadButton,cancelButton,restartButton,releaseButton,folderButton}){stylePromptButton(button);actions->addWidget(button);}
    stylePromptButton(closeButton);actions->addStretch();actions->addWidget(closeButton);
    auto sync=[=,this]{
        const auto config=configuration();source->setText(config.owner.isEmpty()?"更新源：未配置":"GitHub · "+config.owner+" / "+config.repository);status->setText(d->status);
        const bool busy=d->client->busy()||d->preparing;const bool gate=!OperationGate::locked();
        checkButton->setEnabled(gate&&!busy&&!d->restarting);downloadButton->setVisible((state()==S::Available||d->client->canResumeDownload())&&!ready());downloadButton->setText(d->client->canResumeDownload()?"重试下载":"下载更新");downloadButton->setEnabled(gate&&!busy);
        cancelButton->setVisible(busy||d->restarting||d->client->canResumeDownload());cancelButton->setEnabled(gate);restartButton->setVisible(ready()&&d->automaticInstall());restartButton->setEnabled(gate&&!busy&&!d->restarting&&!d->migrating);
        releaseButton->setVisible(d->client->release().releaseUrl.isValid());releaseButton->setEnabled(gate&&!d->restarting);
        folderButton->setVisible(ready()&&!d->automaticInstall());folderButton->setEnabled(gate);
        progress->setVisible(state()==S::Downloading||d->preparing);progressText->setVisible(state()==S::Downloading);
        if(d->total>0){progress->setRange(0,1000);progress->setValue(int(qMin<qint64>(1000,d->received*1000/d->total)));}else progress->setRange(0,0);
        progressText->setText(bytes(d->received)+(d->total>0?" / "+bytes(d->total):QString()));
        const auto text=d->client->release().notes;if(notes->toPlainText()!=text)notes->setPlainText(text);
        hint->setText(d->installHint());
        lastInstall->setText(d->installStatus);lastInstall->setVisible(!d->installStatus.isEmpty());
    };
    connect(this,&UpdateController::changed,dialog,sync);connect(this,&UpdateController::sourceChanged,dialog,sync);
    connect(checkButton,&QPushButton::clicked,this,[this]{check();});connect(downloadButton,&QPushButton::clicked,this,&UpdateController::download);connect(cancelButton,&QPushButton::clicked,this,&UpdateController::cancel);connect(restartButton,&QPushButton::clicked,this,&UpdateController::requestRestart);
    connect(releaseButton,&QPushButton::clicked,dialog,[this]{if(OperationGate::locked())return;const auto release=d->client->release();const auto config=configuration();const auto url=release.releaseUrl;const auto prefix="/"+config.owner+"/"+config.repository+"/releases";if(url.scheme()=="https"&&url.host()=="github.com"&&url.userInfo().isEmpty()&&url.port(-1)==-1&&url.path().startsWith(prefix,Qt::CaseInsensitive))QDesktopServices::openUrl(url);});
    connect(closeButton,&QPushButton::clicked,dialog,&QDialog::close);sync();dialog->resize(uiSize(610,440));keepWindowOnScreen(dialog);dialog->show();
    connect(folderButton,&QPushButton::clicked,dialog,[this]{if(!OperationGate::locked()&&ready())QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(d->verified.path).absolutePath()));});
    connect(settingsButton,&QPushButton::clicked,dialog,[this,dialog]{if(OperationGate::locked())return;dialog->hide();emit settingsRequested();});
}
}
