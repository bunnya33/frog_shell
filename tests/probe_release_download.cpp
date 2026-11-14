#include "../src/core/releaseupdate.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

class TemporaryFailureReply final : public QNetworkReply {
  public:
    TemporaryFailureReply(const QNetworkRequest &request,QObject *parent):QNetworkReply(parent) {
        setRequest(request);setUrl(request.url());open(ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute,503);
        QTimer::singleShot(0,this,[this]{emit metaDataChanged();});
    }
    void abort() override {setFinished(true);setError(OperationCanceledError,"probe cancelled");emit finished();}
    qint64 readData(char *,qint64) override {return -1;}
};
class ProbeTransport final : public QNetworkAccessManager {
    bool injectFailure;
  public:
    explicit ProbeTransport(bool recover):injectFailure(recover) {}
  protected:
    QNetworkReply *createRequest(Operation operation,const QNetworkRequest &request,QIODevice *data) override {
        if(injectFailure&&request.url().host()=="github.com"&&request.url().path().contains("/releases/download/")) {
            injectFailure=false;return new TemporaryFailureReply(request,this);
        }
        return QNetworkAccessManager::createRequest(operation,request,data);
    }
};
// Explicit, unauthenticated public-network diagnostic. Not a routine CTest and
// never installs the file or uses the application's settings or credentials.
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().size()<3||app.arguments().size()>4||(app.arguments().size()==4&&app.arguments()[3]!="--recover")) { std::fprintf(stderr,"Usage: probe OWNER REPOSITORY [--recover]\n"); return 2; }
    using namespace rs::releaseupdate;
    QElapsedTimer clock; clock.start();
    auto log=[&](QJsonObject row) { row["elapsed_ms"]=clock.elapsed(); const auto text=QJsonDocument(row).toJson(QJsonDocument::Compact); std::printf("%s\n",text.constData()); std::fflush(stdout); };
    const bool recover=app.arguments().contains("--recover");
    ProbeTransport transport(recover);
    log({{"event","start"},{"synthetic_first_request_failure",recover}});
    QObject::connect(&transport,&QNetworkAccessManager::finished,&app,[&](QNetworkReply *reply) {
        log({{"event","response"},{"host",reply->url().host()},{"status",reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()},
             {"network_error",int(reply->error())},{"http2",reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool()},
             {"content_length",QString::fromLatin1(reply->rawHeader("Content-Length"))},{"range",QString::fromLatin1(reply->rawHeader("Content-Range"))}});
    });
    Limits limits; limits.downloadInactivityTimeoutMs=15000; limits.downloadTimeoutMs=120000;
    Client client({app.arguments()[1],app.arguments()[2],"0.0.0","windows","x86_64"},nullptr,&transport,limits);
    QTemporaryDir staging; qint64 last=0;
    QObject::connect(&client,&Client::stateChanged,&app,[&](State state,const QString &) { log({{"event","state"},{"state",int(state)}}); });
    QObject::connect(&client,&Client::checkFinished,&app,[&](const CheckResult &result) {
        if(result.state!=State::Available) { log({{"event","check_failed"},{"error",result.error.message}}); app.exit(1); return; }
        log({{"event","release"},{"version",result.release.version},{"bytes",result.release.size}}); client.download(staging.path());
    });
    QObject::connect(&client,&Client::downloadProgress,&app,[&](qint64 bytes,qint64 total) {
        if(bytes-last>=1024*1024||bytes==total) { last=bytes; log({{"event","progress"},{"bytes",bytes},{"total",total}}); }
    });
    QObject::connect(&client,&Client::downloadRetry,&app,[&](int attempt,int,int,const Error &e) { log({{"event","retry"},{"attempt",attempt},{"error",e.message},{"http",e.httpStatus}}); });
    QObject::connect(&client,&Client::downloadVerified,&app,[&](const VerifiedDownload &download) {
        log({{"event","PASS"},{"bytes",download.size},{"sha256",QString::fromLatin1(download.sha256.toHex())}});
        QDir(QFileInfo(download.path).absolutePath()).removeRecursively(); app.exit(0);
    });
    QObject::connect(&client,&Client::errorOccurred,&app,[&](const Error &e) { log({{"event","FAIL"},{"error",e.message},{"code",int(e.code)},{"http",e.httpStatus}}); app.exit(1); });
    QTimer::singleShot(0,&client,[&]{ client.check(); });
    return app.exec();
}
