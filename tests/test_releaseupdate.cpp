#include "../src/core/releaseupdate.h"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QSslError>
using namespace rs::releaseupdate;

struct Response {
    int status = 200;
    QByteArray body;
    QList<QPair<QByteArray,QByteArray>> headers;
    int chunk = 16384, interval = 1;
    bool stall = false, omitLength = false;
    qint64 closeAfter = -1;
};
class HttpFixture : public QObject {
  public:
    QTcpServer server;
    QHash<QString,Response> routes;
    QStringList received;
    HttpFixture() {
        if (!server.listen(QHostAddress::LocalHost,0)) qFatal("fixture listen failed");
        connect(&server,&QTcpServer::newConnection,this,[this]{
            while(server.hasPendingConnections()) {
                auto socket=server.nextPendingConnection();socket->setParent(this);
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                auto request=std::make_shared<QByteArray>();
                connect(socket,&QTcpSocket::readyRead,this,[this,socket,request]{
                    if(socket->property("handled").toBool())return;
                    *request+=socket->readAll();
                    if(!request->contains("\r\n\r\n"))return;
                    socket->setProperty("handled",true);
                    const auto first=request->split('\n').value(0).split(' ');
                    const auto path=QString::fromLatin1(first.value(1));received<<path;
                    const auto response=routes.value(path,Response{404,"fixture missing"});
                    QByteArray headers="HTTP/1.1 "+QByteArray::number(response.status)+" Fixture\r\nConnection: close\r\n";
                    bool hasLength=false;
                    for(const auto &h:response.headers){headers+=h.first+": "+h.second+"\r\n";if(h.first.toLower()=="content-length")hasLength=true;}
                    if(!hasLength&&!response.omitLength)headers+="Content-Length: "+QByteArray::number(response.body.size())+"\r\n";
                    socket->write(headers+"\r\n");
                    if(response.stall)return;
                    auto offset=std::make_shared<qint64>(0);
                    auto timer=new QTimer(socket);timer->setInterval(response.interval);
                    connect(timer,&QTimer::timeout,socket,[socket,response,offset,timer]{
                        if(socket->state()!=QAbstractSocket::ConnectedState){timer->stop();return;}
                        qint64 length=qMin<qint64>(response.chunk,response.body.size()-*offset);
                        if(response.closeAfter>=0)length=qMin(length,response.closeAfter-*offset);
                        if(length>0){socket->write(response.body.constData()+*offset,length);*offset+=length;}
                        if(*offset>=response.body.size()||(response.closeAfter>=0&&*offset>=response.closeAfter)){timer->stop();socket->disconnectFromHost();}
                    });timer->start();
                });
            }
        });
    }
    QUrl local(const QUrl &logical) const {
        QUrl url=logical;url.setScheme("http");url.setHost("127.0.0.1");url.setPort(server.serverPort());return url;
    }
};
class FixtureTransport : public QNetworkAccessManager {
  public:
    HttpFixture &fixture;
    QList<QUrl> logical;
    explicit FixtureTransport(HttpFixture &f):fixture(f){setProxy(QNetworkProxy::NoProxy);}
  protected:
    QNetworkReply *createRequest(Operation op,const QNetworkRequest &request,QIODevice *data) override {
        logical<<request.url();QNetworkRequest local(request);local.setUrl(fixture.local(request.url()));
        return QNetworkAccessManager::createRequest(op,local,data);
    }
};
class TlsReply final : public QNetworkReply {
  public:
    TlsReply(const QNetworkRequest &r,QObject *p):QNetworkReply(p){setRequest(r);setUrl(r.url());open(ReadOnly);QTimer::singleShot(0,this,[this]{emit sslErrors({QSslError(QSslError::SelfSignedCertificate)});});}
    void abort() override {setFinished(true);setError(OperationCanceledError,"cancelled");emit finished();}
    qint64 readData(char *,qint64) override{return -1;}
};
class TlsTransport final : public QNetworkAccessManager {
    QNetworkReply *createRequest(Operation,const QNetworkRequest &r,QIODevice *) override {return new TlsReply(r,this);}
};
// This reply deliberately emits no readyRead. It exposes bytes only during
// finished(), forcing Client::finished -> drain -> downloadProgress reentry.
class FinishedOnlyReply final : public QNetworkReply {
    QByteArray body;
    qint64 position=0;
    bool visible=false;
  public:
    FinishedOnlyReply(const QNetworkRequest &r,const QByteArray &bytes,QObject *parent):QNetworkReply(parent),body(bytes) {
        setRequest(r);setUrl(r.url());setAttribute(QNetworkRequest::HttpStatusCodeAttribute,200);
        setHeader(QNetworkRequest::ContentLengthHeader,body.size());open(ReadOnly);
        QTimer::singleShot(0,this,[this]{visible=true;setFinished(true);emit finished();});
    }
    void abort() override {setFinished(true);setError(OperationCanceledError,"cancelled");emit finished();}
    qint64 bytesAvailable() const override {return (visible?body.size()-position:0)+QNetworkReply::bytesAvailable();}
    qint64 readData(char *buffer,qint64 max) override {
        if(!visible||position>=body.size())return -1;
        const auto length=qMin(max,body.size()-position);memcpy(buffer,body.constData()+position,size_t(length));position+=length;return length;
    }
};
class FinishedOnlyTransport final : public FixtureTransport {
    QByteArray body;
    bool served=false;
  public:
    FinishedOnlyTransport(HttpFixture &f,const QByteArray &bytes):FixtureTransport(f),body(bytes){}
  protected:
    QNetworkReply *createRequest(Operation op,const QNetworkRequest &request,QIODevice *data) override {
        if(!served&&request.url().path().endsWith("-portable.exe")) {
            served=true;logical<<request.url();return new FinishedOnlyReply(request,body,this);
        }
        return FixtureTransport::createRequest(op,request,data);
    }
};

class ReleaseUpdateTests : public QObject {
    Q_OBJECT
    HttpFixture fixture;
    const Config cfg{"fixture","project","0.1.41","windows","x86_64"};
    const QString metaPath="/repos/fixture/project/releases/latest";
    const QString name="FrogShell-0.1.42-portable.exe";
    const QString assetPath="/fixture/project/releases/download/v0.1.42/FrogShell-0.1.42-portable.exe";
    const QByteArray payload=QByteArray("fixture-data-not-an-executable\n").repeated(8192);
    QByteArray sha(const QByteArray &b)const{return QCryptographicHash::hash(b,QCryptographicHash::Sha256);}
    QJsonObject metadata(const QString &version="0.1.42",bool withDigest=true) const {
        const auto tag="v"+version;const auto filename="FrogShell-"+version+"-portable.exe";
        QJsonObject asset{{"name",filename},{"size",payload.size()},{"state","uploaded"},
            {"browser_download_url","https://github.com/fixture/project/releases/download/"+tag+"/"+filename}};
        if(withDigest)asset["digest"]="sha256:"+QString::fromLatin1(sha(payload).toHex());
        return {{"tag_name",tag},{"draft",false},{"prerelease",false},
            {"html_url","https://github.com/fixture/project/releases/tag/"+tag},{"assets",QJsonArray{asset}}};
    }
    void meta(const QJsonObject &object){fixture.routes[metaPath]=Response{200,QJsonDocument(object).toJson(QJsonDocument::Compact)};}
    void available(Client &c){QSignalSpy done(&c,&Client::checkFinished);c.check();QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QCOMPARE(c.state(),State::Available);}
    static QStringList files(const QString &root){return QDir(root).entryList(QDir::AllEntries|QDir::NoDotAndDotDot);}
  private slots:
    void init(){fixture.routes.clear();fixture.received.clear();meta(metadata());fixture.routes[assetPath]=Response{200,payload};}
    void semver() {
        for(const auto &v:QStringList{"0.0.0","1.20.300","1.0.0-alpha.1+build.01","999999999999999999999.0.0"})QVERIFY(isStrictSemVer(v));
        for(const auto &v:QStringList{"v1.2.3","01.2.3","1.2","1.2.3.4","1.2.3-alpha.01","1.2.3+","1.2.3\n","1.2.3-","1.2.3+空"})QVERIFY2(!isStrictSemVer(v),qPrintable(v));
        bool ok=false;QCOMPARE(compareVersions("1.0.0+1","1.0.0+2",&ok),0);QVERIFY(ok);
        QVERIFY(compareVersions("1.0.0-alpha.2","1.0.0-alpha.10")<0);
        QVERIFY(compareVersions("1.0.0-alpha","1.0.0")<0);
        QVERIFY(compareVersions("999999999999999999999.0.0","2.0.0")>0);
        compareVersions("bad","1.0.0",&ok);QVERIFY(!ok);
    }
    void urlPolicy() {
        QVERIFY(allowedReleaseDownloadUrl(QUrl("https://github.com"+assetPath),cfg));
        for(const auto &url:QStringList{
            "http://github.com"+assetPath,"https://github.com.evil.test"+assetPath,
            "https://github.com:444"+assetPath,"https://user@github.com"+assetPath,
            "https://github.com/other/project/releases/download/v0.1.42/"+name,
            "https://github.com/fixture/project/releases/download/../"+name,
            "https://github.com"+assetPath+"?secret=1","https://github.com"+assetPath+"#fragment"})
            QVERIFY2(!allowedReleaseDownloadUrl(QUrl(url),cfg),qPrintable(url));
        QVERIFY(allowedRedirectUrl(QUrl("https://release-assets.githubusercontent.com/a?token=fixture"),cfg));
        QVERIFY(allowedRedirectUrl(QUrl("https://objects.githubusercontent.com/a"),cfg));
        QVERIFY(!allowedRedirectUrl(QUrl("https://release-assets.githubusercontent.com.evil.test/a"),cfg));
        QVERIFY(!allowedRedirectUrl(QUrl("http://release-assets.githubusercontent.com/a"),cfg));
        auto bad=cfg;bad.owner="fixture\n";QVERIFY(!validConfig(bad));
        QCOMPARE(portableAssetName("0.1.42","windows","x86_64"),name);
        QVERIFY(portableAssetName("0.1.42","windows","arm64")!=name);
    }
    void offlineAndHalfHourSchedule() {
        FixtureTransport transport(fixture);Client empty(Config{},nullptr,&transport);QSignalSpy unconfigured(&empty,&Client::checkFinished);
        empty.check();QCOMPARE(empty.state(),State::Unconfigured);QCOMPARE(unconfigured.size(),1);QVERIFY(transport.logical.isEmpty());
        Client c(cfg,nullptr,&transport);Schedule s;s.nowUtc=QDateTime::fromString("2026-11-11T10:00:00Z",Qt::ISODate);
        c.check(CheckMode::Automatic,s);QCOMPARE(c.state(),State::Disabled);QVERIFY(transport.logical.isEmpty());
        s.automaticEnabled=true;s.lastAutomaticAttemptUtc=s.nowUtc.addSecs(-1799);
        c.check(CheckMode::Automatic,s);QCOMPARE(c.state(),State::Throttled);QVERIFY(transport.logical.isEmpty());
        s.lastAutomaticAttemptUtc=s.nowUtc.addDays(1);c.check(CheckMode::Automatic,s);QCOMPARE(c.state(),State::Throttled);
        s.lastAutomaticAttemptUtc=s.nowUtc.addSecs(-1800);s.lastNotifiedVersion="0.1.42";
        QSignalSpy attempts(&c,&Client::automaticAttemptStarted),done(&c,&Client::checkFinished);
        c.check(CheckMode::Automatic,s);QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QCOMPARE(attempts.size(),1);QCOMPARE(c.state(),State::Available);
        QVERIFY(!qvariant_cast<CheckResult>(done.first().first()).shouldNotify);
        const auto autoRequests=transport.logical.size();c.check(CheckMode::Automatic,s);
        QCOMPARE(c.state(),State::Throttled);QCOMPARE(transport.logical.size(),autoRequests);
        QSignalSpy manual(&c,&Client::checkFinished);c.check(CheckMode::Manual,s);QTRY_COMPARE_WITH_TIMEOUT(manual.size(),1,3000);
        QVERIFY(qvariant_cast<CheckResult>(manual.first().first()).shouldNotify);
    }
    void startupScheduleAndCooldown() {
        FixtureTransport transport(fixture);Schedule s;s.automaticEnabled=true;
        s.nowUtc=QDateTime::fromString("2026-11-11T10:00:00Z",Qt::ISODate);
        s.lastAutomaticAttemptUtc=s.nowUtc.addSecs(-5);s.lastNotifiedVersion="0.1.42";
        Client c(cfg,nullptr,&transport);QSignalSpy attempts(&c,&Client::automaticAttemptStarted),done(&c,&Client::checkFinished);
        c.check(CheckMode::Startup,s);QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);
        QCOMPARE(attempts.size(),1);QCOMPARE(c.state(),State::Available);
        QVERIFY(!qvariant_cast<CheckResult>(done.first().first()).shouldNotify);
        const auto count=transport.logical.size();c.check(CheckMode::Startup,s);QCOMPARE(transport.logical.size(),count);QCOMPARE(c.state(),State::Throttled);
        s.nowUtc=s.nowUtc.addSecs(1799);c.check(CheckMode::Automatic,s);QCOMPARE(transport.logical.size(),count);
        s.nowUtc=s.nowUtc.addSecs(1);meta(metadata("0.1.43"));c.check(CheckMode::Automatic,s);QTRY_COMPARE_WITH_TIMEOUT(attempts.size(),2,3000);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(),3000);QCOMPARE(transport.logical.size(),count+1);
        QCOMPARE(c.release().version,QString("0.1.43"));QVERIFY(qvariant_cast<CheckResult>(done.last().first()).shouldNotify);
        Client disabled(cfg,nullptr,&transport);s.automaticEnabled=false;const auto after=transport.logical.size();
        disabled.check(CheckMode::Startup,s);QCOMPARE(disabled.state(),State::Disabled);QCOMPARE(transport.logical.size(),after);
        Client cooled(cfg,nullptr,&transport);s.automaticEnabled=true;s.rateLimitUntilUtc=s.nowUtc.addSecs(120);
        cooled.check(CheckMode::Startup,s);QCOMPARE(cooled.state(),State::Throttled);QCOMPARE(cooled.lastError().code,ErrorCode::RateLimited);QCOMPARE(transport.logical.size(),after);
        cooled.check(CheckMode::Manual,s);QCOMPARE(cooled.state(),State::Throttled);QCOMPARE(transport.logical.size(),after);
        Client sameProcess(cfg,nullptr,&transport);s.rateLimitUntilUtc={};s.lastAutomaticAttemptUtc={};
        QSignalSpy first(&sameProcess,&Client::checkFinished);sameProcess.check(CheckMode::Automatic,s);
        QTRY_COMPARE_WITH_TIMEOUT(first.size(),1,3000);const auto started=transport.logical.size();
        sameProcess.check(CheckMode::Startup,s);QCOMPARE(sameProcess.state(),State::Throttled);QCOMPARE(transport.logical.size(),started);
    }
    void portableChosenAlongsideSetup() {
        const auto setup=QStringLiteral("FrogShell-0.1.42-setup.exe");
        const auto list=sha(QByteArray("setup")).toHex()+"  "+setup.toUtf8()+"\n"+sha(payload).toHex()+"  "+name.toUtf8()+"\n";
        const QString path="/fixture/project/releases/download/v0.1.42/SHA256SUMS";
        for(bool digest:{true,false}) {
            auto o=metadata("0.1.42",digest);auto assets=o["assets"].toArray();
            assets.prepend(QJsonObject{{"name",setup},{"size",5},{"digest","sha256:"+QString::fromLatin1(sha(QByteArray("setup")).toHex())},
                {"browser_download_url","https://github.com/fixture/project/releases/download/v0.1.42/"+setup}});
            assets.append(QJsonObject{{"name","SHA256SUMS"},{"size",list.size()},{"browser_download_url","https://github.com"+path}});
            o["assets"]=assets;meta(o);fixture.routes[path]=Response{200,list};
            FixtureTransport transport(fixture);Client c(cfg,nullptr,&transport);available(c);
            QCOMPARE(c.release().assetName,name);QCOMPARE(c.release().downloadUrl.path(),assetPath);QCOMPARE(c.release().sha256,sha(payload));
            QCOMPARE(transport.logical.size(),digest?1:2);
        }
    }
    void metadataCases_data() {
        QTest::addColumn<QString>("kind");QTest::addColumn<int>("state");QTest::addColumn<int>("code");
        auto add=[](const char *k,State s,ErrorCode e=ErrorCode::None){QTest::newRow(k)<<QString(k)<<int(s)<<int(e);};
        add("same",State::UpToDate);add("old",State::UpToDate);add("draft",State::UpToDate);add("prerelease",State::UpToDate);
        add("semver-prerelease",State::UpToDate);add("bad-version",State::Failed,ErrorCode::InvalidVersion);
        add("empty-assets",State::ManualDownload,ErrorCode::MissingDigest);add("wrong-arch",State::ManualDownload,ErrorCode::MissingDigest);
        add("no-digest",State::ManualDownload,ErrorCode::MissingDigest);add("bad-digest",State::Failed,ErrorCode::InvalidMetadata);
        add("wrong-url",State::Failed,ErrorCode::UnsafeUrl);add("wrong-page",State::Failed,ErrorCode::UnsafeUrl);
        add("wrong-tag-url",State::Failed,ErrorCode::UnsafeUrl);add("size",State::Failed,ErrorCode::SizeLimit);
        add("duplicate",State::Failed,ErrorCode::InvalidMetadata);add("invalid-type",State::Failed,ErrorCode::InvalidMetadata);
    }
    void metadataCases() {
        QFETCH(QString,kind);QFETCH(int,state);QFETCH(int,code);
        auto o=metadata();auto assets=o["assets"].toArray();auto a=assets.first().toObject();
        if(kind=="same")o=metadata("0.1.41");else if(kind=="old")o=metadata("0.1.40");
        else if(kind=="draft")o["draft"]=true;else if(kind=="prerelease")o["prerelease"]=true;
        else if(kind=="semver-prerelease")o["tag_name"]="v0.1.42-rc.1";
        else if(kind=="bad-version")o["tag_name"]="v0.01.42";
        else if(kind=="empty-assets")o["assets"]=QJsonArray{};
        else if(kind=="wrong-arch"){a["name"]="FrogShell-0.1.42-windows-arm64-portable.exe";o["assets"]=QJsonArray{a};}
        else if(kind=="no-digest"){a.remove("digest");o["assets"]=QJsonArray{a};}
        else if(kind=="bad-digest"){a["digest"]="sha256:abcd";o["assets"]=QJsonArray{a};}
        else if(kind=="wrong-url"){a["browser_download_url"]="https://evil.test/"+name;o["assets"]=QJsonArray{a};}
        else if(kind=="wrong-page")o["html_url"]="https://github.com/other/project/releases/tag/v0.1.42";
        else if(kind=="wrong-tag-url"){a["browser_download_url"]="https://github.com/fixture/project/releases/download/v0.1.43/"+name;o["assets"]=QJsonArray{a};}
        else if(kind=="size"){a["size"]=-1;o["assets"]=QJsonArray{a};}
        else if(kind=="duplicate")o["assets"]=QJsonArray{a,a};
        else if(kind=="invalid-type")o["draft"]="false";
        meta(o);FixtureTransport transport(fixture);Client c(cfg,nullptr,&transport);QSignalSpy done(&c,&Client::checkFinished);c.check();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QCOMPARE(int(c.state()),state);QCOMPARE(int(c.lastError().code),code);
        QCOMPARE(transport.logical.size(),1);
    }
    void arm64NeverUsesLegacy() {
        auto arm=cfg;arm.architecture="arm64";FixtureTransport transport(fixture);Client c(arm,nullptr,&transport);QSignalSpy done(&c,&Client::checkFinished);
        c.check();QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QCOMPARE(c.state(),State::ManualDownload);
        auto o=metadata();auto a=o["assets"].toArray().first().toObject();const auto n=portableAssetName("0.1.42","windows","arm64");
        a["name"]=n;a["browser_download_url"]="https://github.com/fixture/project/releases/download/v0.1.42/"+n;
        auto assets=o["assets"].toArray();assets.append(a);o["assets"]=assets;meta(o);available(c);QCOMPARE(c.release().assetName,n);
    }
    void checksumFallback() {
        auto o=metadata("0.1.42",false);const auto list=sha(payload).toHex()+"  "+name.toUtf8()+"\r\n";
        const QString path="/fixture/project/releases/download/v0.1.42/SHA256SUMS";
        auto assets=o["assets"].toArray();assets.append(QJsonObject{{"name","SHA256SUMS"},{"size",list.size()},{"browser_download_url","https://github.com"+path}});o["assets"]=assets;meta(o);
        fixture.routes[path]=Response{200,list};FixtureTransport transport(fixture);Client c(cfg,nullptr,&transport);available(c);QCOMPARE(c.release().sha256,sha(payload));QCOMPARE(transport.logical.size(),2);
        auto duplicate=list+list;assets[1]=QJsonObject{{"name","SHA256SUMS"},{"size",duplicate.size()},{"browser_download_url","https://github.com"+path}};o["assets"]=assets;meta(o);fixture.routes[path]=Response{200,duplicate};
        QSignalSpy done(&c,&Client::checkFinished);c.check();QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QCOMPARE(c.state(),State::ManualDownload);QVERIFY(c.release().sha256.isEmpty());
    }
    void mixedCaseAndNotes() {
        auto mixed=cfg;mixed.owner="FiXtUrE";mixed.repository="ProJect";
        const auto prefix=QStringLiteral("<b>plain fixture</b> https://example.invalid/\n");
        const auto notes=prefix+QString::fromUtf8("中文😀").repeated(12000);
        auto o=metadata();o["body"]=notes;
        fixture.routes["/repos/FiXtUrE/ProJect/releases/latest"]=Response{200,QJsonDocument(o).toJson(QJsonDocument::Compact)};
        FixtureTransport transport(fixture);Client c(mixed,nullptr,&transport);available(c);
        QVERIFY(c.release().notes.startsWith(prefix));QVERIFY(c.release().notes.toUtf8().size()<=65536);
        QVERIFY(!c.release().notes.contains(QChar(0xfffd)));
        auto a=o["assets"].toArray().first().toObject();a.remove("digest");
        const auto list=sha(payload).toHex()+"  "+name.toUtf8()+"\n";
        const QString path="/fixture/project/releases/download/v0.1.42/SHA256SUMS";
        o["assets"]=QJsonArray{a,QJsonObject{{"name","SHA256SUMS"},{"size",list.size()},{"browser_download_url","https://github.com"+path}}};
        fixture.routes["/repos/FiXtUrE/ProJect/releases/latest"]=Response{200,QJsonDocument(o).toJson(QJsonDocument::Compact)};
        fixture.routes[path]=Response{200,list};available(c);QCOMPARE(c.release().sha256,sha(payload));
    }
    void streamedDownload() {
        FixtureTransport transport(fixture);Client c(cfg,nullptr,&transport);available(c);QTemporaryDir dir;QVERIFY(dir.isValid());
        QSignalSpy ready(&c,&Client::downloadVerified),progress(&c,&Client::downloadProgress);c.download(dir.path());
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,5000);QCOMPARE(c.state(),State::Ready);QVERIFY(progress.size()>2);
        const auto result=qvariant_cast<VerifiedDownload>(ready.first().first());QFile file(result.path);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),payload);QCOMPARE(result.sha256,sha(payload));QCOMPARE(result.size,payload.size());
        qint64 previous=-1;for(const auto &row:progress){const auto n=row[0].toLongLong();QVERIFY(n>=previous);QVERIFY(n<=payload.size());previous=n;QCOMPARE(row[1].toLongLong(),payload.size());}
        QCOMPARE(previous,payload.size());
    }
    void cancelAndStale() {
        FixtureTransport transport(fixture);Client c(cfg,nullptr,&transport);available(c);QTemporaryDir dir;
        QSignalSpy ready(&c,&Client::downloadVerified);
        connect(&c,&Client::downloadProgress,&c,[&c](qint64 n,qint64){if(n>0)c.cancel();});c.download(dir.path());
        QTRY_COMPARE_WITH_TIMEOUT(c.state(),State::Cancelled,3000);QVERIFY(files(dir.path()).isEmpty());QCOMPARE(ready.size(),0);QTest::qWait(100);QCOMPARE(ready.size(),0);
        Response slow{200,QJsonDocument(metadata()).toJson()};slow.stall=true;fixture.routes[metaPath]=slow;
        c.check();QVERIFY(c.busy());auto changed=cfg;changed.repository="other";c.setConfig(changed);QCOMPARE(c.state(),State::Idle);QVERIFY(!c.busy());QTest::qWait(50);QCOMPARE(c.state(),State::Idle);
    }
    void downloadFailures_data() {
        QTest::addColumn<QString>("kind");QTest::addColumn<int>("code");
        QTest::newRow("digest")<<QString("digest")<<int(ErrorCode::DigestMismatch);
        QTest::newRow("oversize")<<QString("oversize")<<int(ErrorCode::SizeLimit);
        QTest::newRow("truncated")<<QString("truncated")<<int(ErrorCode::Network);
        QTest::newRow("encoding")<<QString("encoding")<<int(ErrorCode::InvalidMetadata);
    }
    void downloadFailures() {
        QFETCH(QString,kind);QFETCH(int,code);FixtureTransport transport(fixture);Client c(cfg,nullptr,&transport);available(c);QTemporaryDir dir;Response r{200,payload};
        if(kind=="digest")r.body[0]='X';else if(kind=="oversize")r.body+='x';else if(kind=="truncated")r.closeAfter=32;
        else if(kind=="encoding")r.headers.append(qMakePair(QByteArray("Content-Encoding"),QByteArray("gzip")));fixture.routes[assetPath]=r;
        QSignalSpy ready(&c,&Client::downloadVerified);c.download(dir.path());QTRY_COMPARE_WITH_TIMEOUT(c.state(),State::Failed,5000);
        QCOMPARE(int(c.lastError().code),code);QVERIFY(files(dir.path()).isEmpty());QCOMPARE(ready.size(),0);
    }
    void redirects_data() {
        QTest::addColumn<QString>("target");QTest::addColumn<bool>("allowed");
        QTest::newRow("github-cdn")<<QString("https://release-assets.githubusercontent.com/dummy?token=fixture")<<true;
        QTest::newRow("http-downgrade")<<QString("http://release-assets.githubusercontent.com/dummy")<<false;
        QTest::newRow("evil")<<QString("https://evil.test/dummy")<<false;
        QTest::newRow("suffix")<<QString("https://release-assets.githubusercontent.com.evil.test/dummy")<<false;
    }
    void redirects() {
        QFETCH(QString,target);QFETCH(bool,allowed);FixtureTransport transport(fixture);Client c(cfg,nullptr,&transport);available(c);QTemporaryDir dir;
        Response redirect;redirect.status=302;redirect.headers.append(qMakePair(QByteArray("Location"),target.toUtf8()));fixture.routes[assetPath]=redirect;fixture.routes["/dummy?token=fixture"]=Response{200,payload};
        QSignalSpy ready(&c,&Client::downloadVerified);c.download(dir.path());QTRY_VERIFY_WITH_TIMEOUT(!c.busy(),5000);
        if(allowed){QCOMPARE(c.state(),State::Ready);QCOMPARE(ready.size(),1);QCOMPARE(transport.logical.size(),3);}
        else{QCOMPARE(c.lastError().code,ErrorCode::UnsafeUrl);QCOMPARE(ready.size(),0);QCOMPARE(transport.logical.size(),2);QVERIFY(files(dir.path()).isEmpty());}
    }
    void errorsAndBounds() {
        FixtureTransport transport(fixture);Limits limits;limits.metadataBytes=128;Client c(cfg,nullptr,&transport,limits);QSignalSpy done(&c,&Client::checkFinished);c.check();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QCOMPARE(c.lastError().code,ErrorCode::SizeLimit);
        Client missing(cfg,nullptr,&transport);fixture.routes[metaPath]=Response{404,"missing"};QSignalSpy missingDone(&missing,&Client::checkFinished);missing.check();QTRY_COMPARE_WITH_TIMEOUT(missingDone.size(),1,3000);QCOMPARE(missing.lastError().code,ErrorCode::Http);QCOMPARE(missing.lastError().httpStatus,404);
        Client rate(cfg,nullptr,&transport);Response limited{429,"limited"};limited.headers.append(qMakePair(QByteArray("Retry-After"),QByteArray("120")));fixture.routes[metaPath]=limited;
        Schedule s;s.nowUtc=QDateTime::fromString("2026-11-11T10:00:00Z",Qt::ISODate);QSignalSpy rateDone(&rate,&Client::checkFinished);rate.check(CheckMode::Manual,s);QTRY_COMPARE_WITH_TIMEOUT(rateDone.size(),1,3000);QCOMPARE(rate.lastError().code,ErrorCode::RateLimited);QCOMPARE(rate.lastError().retryAfterUtc,s.nowUtc.addSecs(120));
        const auto count=transport.logical.size();rate.check(CheckMode::Manual,s);QCOMPARE(rate.state(),State::Throttled);QCOMPARE(transport.logical.size(),count);
        meta(metadata());s.nowUtc=s.nowUtc.addSecs(121);rate.check(CheckMode::Manual,s);QTRY_COMPARE_WITH_TIMEOUT(rate.state(),State::Available,3000);
    }
    void timeoutsAndTls() {
        FixtureTransport transport(fixture);Limits limits;limits.inactivityTimeoutMs=50;limits.requestTimeoutMs=200;
        Response stall{200,"x"};stall.stall=true;fixture.routes[metaPath]=stall;Client c(cfg,nullptr,&transport,limits);QSignalSpy done(&c,&Client::checkFinished);c.check();QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QCOMPARE(c.lastError().code,ErrorCode::Timeout);
        TlsTransport tls;Client secureClient(cfg,nullptr,&tls);QSignalSpy tlsDone(&secureClient,&Client::checkFinished);secureClient.check();QTRY_COMPARE_WITH_TIMEOUT(tlsDone.size(),1,3000);QCOMPARE(secureClient.lastError().code,ErrorCode::Tls);
    }
    void noDigestCannotDownload() {
        meta(metadata("0.1.42",false));FixtureTransport transport(fixture);Client c(cfg,nullptr,&transport);QTemporaryDir dir;
        QSignalSpy done(&c,&Client::checkFinished),ready(&c,&Client::downloadVerified);c.check();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QCOMPARE(c.state(),State::ManualDownload);
        c.download(dir.path());QCOMPARE(c.lastError().code,ErrorCode::MissingDigest);
        QCOMPARE(transport.logical.size(),1);QCOMPARE(ready.size(),0);QVERIFY(files(dir.path()).isEmpty());
    }
    void downloadDeadlineAndDestruction() {
        FixtureTransport transport(fixture);Limits limits;limits.inactivityTimeoutMs=1000;limits.downloadTimeoutMs=70;
        Client c(cfg,nullptr,&transport,limits);available(c);QTemporaryDir dir;
        Response slow{200,payload};slow.chunk=1024;slow.interval=10;fixture.routes[assetPath]=slow;
        QSignalSpy ready(&c,&Client::downloadVerified),progress(&c,&Client::downloadProgress);c.download(dir.path());
        QTRY_COMPARE_WITH_TIMEOUT(c.state(),State::Failed,3000);QCOMPARE(c.lastError().code,ErrorCode::Timeout);
        QVERIFY(progress.size()>1);QVERIFY(files(dir.path()).isEmpty());QCOMPARE(ready.size(),0);
        auto destroyed=new Client(cfg,nullptr,&transport);available(*destroyed);
        connect(destroyed,&Client::downloadProgress,destroyed,[destroyed](qint64 n,qint64){if(n>0)delete destroyed;});
        QPointer<Client> guard(destroyed);destroyed->download(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(),3000);QVERIFY(files(dir.path()).isEmpty());
    }
    void redirectLoopAndMetadataOrigin() {
        FixtureTransport transport(fixture);Limits limits;limits.redirects=2;Client c(cfg,nullptr,&transport,limits);available(c);QTemporaryDir dir;
        Response loop;loop.status=302;loop.headers.append(qMakePair(QByteArray("Location"),QByteArray("https://release-assets.githubusercontent.com/loop")));
        fixture.routes[assetPath]=loop;fixture.routes["/loop"]=loop;
        c.download(dir.path());QTRY_COMPARE_WITH_TIMEOUT(c.state(),State::Failed,3000);QCOMPARE(c.lastError().code,ErrorCode::UnsafeUrl);
        QCOMPARE(transport.logical.size(),4);QVERIFY(files(dir.path()).isEmpty());
        Response redirect;redirect.status=302;redirect.headers.append(qMakePair(QByteArray("Location"),QByteArray("https://api.github.com/repos/other/project/releases/latest")));
        fixture.routes[metaPath]=redirect;QSignalSpy done(&c,&Client::checkFinished);const auto count=transport.logical.size();c.check();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QCOMPARE(c.lastError().code,ErrorCode::UnsafeUrl);QCOMPARE(transport.logical.size(),count+1);
    }
    void assetSpecificChecksumAndLimit() {
        auto o=metadata("0.1.42",false);auto assets=o["assets"].toArray();
        const auto checksumName=name+".sha256";const auto path="/fixture/project/releases/download/v0.1.42/"+checksumName;
        const auto list=sha(payload).toHex()+" *"+name.toUtf8()+"\n";
        assets.append(QJsonObject{{"name",checksumName},{"size",list.size()},{"browser_download_url","https://github.com"+path}});
        o["assets"]=assets;meta(o);fixture.routes[path]=Response{200,list};FixtureTransport transport(fixture);Client c(cfg,nullptr,&transport);available(c);
        Limits limits;limits.checksumBytes=16;Client bounded(cfg,nullptr,&transport,limits);QSignalSpy done(&bounded,&Client::checkFinished);bounded.check();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QCOMPARE(bounded.lastError().code,ErrorCode::SizeLimit);
        auto checksum=assets[1].toObject();checksum["browser_download_url"]="https://evil.test/SHA256SUMS";assets[1]=checksum;o["assets"]=assets;meta(o);
        const auto count=transport.logical.size();QSignalSpy rejected(&c,&Client::checkFinished);c.check();
        QTRY_COMPARE_WITH_TIMEOUT(rejected.size(),1,3000);QCOMPARE(c.lastError().code,ErrorCode::UnsafeUrl);QCOMPARE(transport.logical.size(),count+1);
    }
    void invalidConfigurationAndReentrantCancel() {
        FixtureTransport transport(fixture);auto bad=cfg;bad.repository="project/../../other";Client invalid(bad,nullptr,&transport);
        QSignalSpy done(&invalid,&Client::checkFinished);invalid.check();QCOMPARE(done.size(),1);QCOMPARE(invalid.lastError().code,ErrorCode::InvalidConfiguration);QVERIFY(transport.logical.isEmpty());
        Client c(cfg,nullptr,&transport);connect(&c,&Client::stateChanged,&c,[&c](State state,const QString &){if(state==State::Checking)c.cancel();});
        c.check();QCOMPARE(c.state(),State::Cancelled);QVERIFY(transport.logical.isEmpty());
    }
    void finishedProgressCancelThenCheck() {
        FinishedOnlyTransport transport(fixture,payload);Client c(cfg,nullptr,&transport);available(c);QTemporaryDir dir;
        QSignalSpy checked(&c,&Client::checkFinished),ready(&c,&Client::downloadVerified);
        bool reentered=false;
        connect(&c,&Client::downloadProgress,&c,[&](qint64 n,qint64){if(n>0&&!reentered){reentered=true;c.cancel();c.check();}});
        c.download(dir.path());QTRY_COMPARE_WITH_TIMEOUT(checked.size(),1,3000);
        QVERIFY(reentered);QCOMPARE(c.state(),State::Available);
        QCOMPARE(qvariant_cast<CheckResult>(checked.first().first()).state,State::Available);
        QCOMPARE(transport.logical.size(),3);QCOMPARE(ready.size(),0);QVERIFY(files(dir.path()).isEmpty());
    }
};
QTEST_GUILESS_MAIN(ReleaseUpdateTests)
#include "test_releaseupdate.moc"
