#include "cachecleanupcontroller.h"
#include "../core/cachecleanup.h"
#include "../core/operationgate.h"
#include "../core/diagnostics.h"
#include "fileclipboard.h"
#include "messagebox.h"
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QLocale>
#include <QMimeData>
#include <QThreadPool>
#include <limits>

namespace rs {
namespace {
QString sizeText(quint64 n) { return QLocale().formattedDataSize(qint64(qMin(n,quint64((std::numeric_limits<qint64>::max)())))); }
}
CacheCleanupController::CacheCleanupController(QWidget *owner):QObject(owner),page(owner) {}
CacheCleanupController::~CacheCleanupController() { if(cancellation) cancellation->store(true); }
void CacheCleanupController::setTokenProvider(std::function<QSet<QString>()> provider) { tokenProvider=std::move(provider); }
void CacheCleanupController::setLoggingActiveProvider(std::function<bool()> provider) { loggingActiveProvider=std::move(provider); }
QSet<QString> CacheCleanupController::protectedTokens() const {
    if(tokenProvider) return tokenProvider();
    QSet<QString> tokens;
    const auto mime=QApplication::clipboard()->mimeData();
    if(mime && mime->hasFormat(QString::fromLatin1(FileClipboard::mimeType()))) {
        const QByteArray raw=mime->data(FileClipboard::mimeType());
        const QString token=QString::fromLatin1(raw);
        if(isCleanupToken(token)) tokens.insert(token);
    }
    return tokens;
}
void CacheCleanupController::end() {
    if(!busy)return;
    busy=false;workerRunning=false;prompt=nullptr;cancellation.reset();
    QPointer<CacheCleanupController> guard(this);
    emit busyChanged(false);if(guard)emit finished();
}
void CacheCleanupController::cancel() {
    if(!busy)return;
    if(cancellation)cancellation->store(true);
    QPointer<CacheCleanupController> guard(this);
    if(prompt)prompt->reject();
    if(guard&&!workerRunning)end();
}
void CacheCleanupController::start(const QString &dataDirectory,const QString &logDirectory) {
    const auto epoch=OperationGate::token();
    if(busy||!page||!OperationGate::accepts(epoch))return;
    busy=true;workerRunning=true;cancellation=std::make_shared<std::atomic_bool>(false);
    QPointer<CacheCleanupController> self(this);
    emit busyChanged(true);
    if(!self||!busy||!cancellation)return;
    const auto flag=cancellation;const auto tokens=protectedTokens();
    const bool logActive=loggingActiveProvider?loggingActiveProvider():DiagnosticLog::instance().isRunning();
    QThreadPool::globalInstance()->start([self,flag,tokens,dataDirectory,logDirectory,epoch,logActive] {
        const auto plan=planCacheCleanup(dataDirectory,logDirectory,tokens,[flag,epoch]{return flag->load()||!OperationGate::accepts(epoch);},logActive,logActive);
        QMetaObject::invokeMethod(QCoreApplication::instance(),[self,flag,plan,epoch,logActive] {
            if(!self)return;
            self->workerRunning=false;
            if(flag->load()||plan.canceled||!self->page||!OperationGate::accepts(epoch)){self->end();return;}
            if(!plan.supported) {
                AppMessageBox::warning(self->page,QStringLiteral("无法清理缓存"),QStringLiteral("此平台暂不支持安全清理，未删除任何文件。"));
                if(self)self->end();return;
            }
            auto box=new AppMessageBox(QMessageBox::Question,QStringLiteral("清理缓存"),
                QStringLiteral("预计可清理 %1，涉及 %2 个文件。\n仅清理日志和未使用的文件暂存；配置和未保存草稿会保留。%3\n在用项目会跳过。%4")
                .arg(sizeText(plan.bytes)).arg(plan.items).arg(logActive?QStringLiteral("日志启用时，诊断日志及其轮转文件会保留。"):QString())
                .arg(plan.partial?QStringLiteral("\n扫描受限，以上为部分结果。"):QString()),
                QMessageBox::Yes|QMessageBox::Cancel,self->page);
            box->setObjectName(QStringLiteral("cacheCleanupConfirm"));box->setDefaultButton(QMessageBox::Cancel);
            box->setAttribute(Qt::WA_DeleteOnClose);self->prompt=box;
            QObject::connect(box,&QMessageBox::finished,self,[self,flag,plan,epoch,logActive](int result) {
                if(!self)return;
                self->prompt=nullptr;
                if(result!=QMessageBox::Yes||flag->load()||!self->page||!OperationGate::accepts(epoch)){self->end();return;}
                const bool nowActive=self->loggingActiveProvider?self->loggingActiveProvider():DiagnosticLog::instance().isRunning();
                if(nowActive!=logActive){self->end();return;}
                // Recheck the custom token after confirmation, without changing the clipboard.
                const auto tokens=self->protectedTokens();self->workerRunning=true;
                QThreadPool::globalInstance()->start([self,flag,plan,tokens,epoch] {
                    const auto outcome=executeCacheCleanup(plan,tokens,[flag,epoch]{return flag->load()||!OperationGate::accepts(epoch);});
                    QMetaObject::invokeMethod(QCoreApplication::instance(),[self,flag,outcome,epoch] {
                        if(!self)return;
                        self->workerRunning=false;
                        if(!flag->load()&&self->page&&OperationGate::accepts(epoch)) {
                            AppMessageBox::information(self->page,QStringLiteral("清理完成"),
                                QStringLiteral("已清理 %1，删除 %2 个文件；跳过 %3 项，失败 %4 项。%5")
                                .arg(sizeText(outcome.bytes)).arg(outcome.items).arg(outcome.skipped).arg(outcome.failures)
                                .arg(outcome.partial?QStringLiteral("\n部分项目未能处理。"):QString()));
                        }
                        if(self) self->end();
                    },Qt::QueuedConnection);
                });
            });
            box->open();
        },Qt::QueuedConnection);
    });
}
}
