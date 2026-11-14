#include "releaseupdate.h"
#include "diagnostics.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTimeZone>
#include <QUrlQuery>
#include <QUuid>
#include <cmath>

namespace rs::releaseupdate {
namespace {
struct Version { QStringList number, pre; };
bool numeric(const QString &s) {
    if (s.isEmpty()) return false;
    for (auto c : s) if (c < u'0' || c > u'9') return false;
    return true;
}
bool parseVersion(const QString &s, Version *out = nullptr) {
    if (s.size() > 256) return false;
    static const QRegularExpression re(QStringLiteral(
        "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)"
        "(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?"
        "(?:\\+([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?$"));
    const auto match = re.match(s);
    if (!match.hasMatch() || match.capturedLength() != s.size()) return false;
    Version v{{match.captured(1), match.captured(2), match.captured(3)}, {}};
    if (!match.captured(4).isEmpty()) {
        v.pre = match.captured(4).split(u'.');
        for (const auto &part : v.pre) if (numeric(part) && part.size() > 1 && part[0] == u'0') return false;
    }
    if (out) *out = v;
    return true;
}
int compareNumber(const QString &a, const QString &b) {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return QString::compare(a, b, Qt::CaseSensitive);
}
bool secure(const QUrl &url) {
    return url.isValid() && !url.isRelative() && url.scheme() == "https" &&
           !url.host().isEmpty() && url.userInfo().isEmpty() && !url.authority().contains(u'@') && !url.hasFragment() &&
           (url.port(-1) == -1 || url.port() == 443) &&
           !url.path().contains(QChar(0)) && !url.path().contains(u'\\');
}
bool ownedPath(const QUrl &url, const Config &c, const QString &kind) {
    if (!secure(url) || url.host() != "github.com") return false;
    const auto parts = url.path(QUrl::FullyDecoded).split(u'/', Qt::KeepEmptyParts);
    if (parts.size() < 6 || parts[0] != "" ||
        parts[1].compare(c.owner, Qt::CaseInsensitive) != 0 ||
        parts[2].compare(c.repository, Qt::CaseInsensitive) != 0 ||
        parts[3] != "releases" || parts[4] != kind) return false;
    for (int i = 5; i < parts.size(); ++i)
        if (parts[i].isEmpty() || parts[i] == "." || parts[i] == "..") return false;
    return true;
}
bool exactTail(const QUrl &url, const QStringList &tail) {
    return url.path(QUrl::FullyDecoded).split(u'/',Qt::KeepEmptyParts).mid(5)==tail;
}
QString boundedNotes(const QString &text) {
    const auto bytes=text.toUtf8();
    if(bytes.size()<=65536)return text;
    int end=65536;
    while(end>0&&(static_cast<unsigned char>(bytes[end])&0xc0)==0x80)--end;
    return QString::fromUtf8(bytes.constData(),end);
}
Error error(ErrorCode code, const QString &message, int status = 0) { return {code, message, status, {}}; }
bool integerSize(const QJsonValue &v, qint64 maximum, qint64 *value) {
    if (!v.isDouble()) return false;
    const double d = v.toDouble();
    if (!std::isfinite(d) || d <= 0 || d > double(maximum) || std::floor(d) != d) return false;
    *value = qint64(d); return true;
}
QUrl strictUrl(const QJsonValue &value) {
    return value.isString() ? QUrl(value.toString(), QUrl::StrictMode) : QUrl();
}
QDateTime nowFor(const Schedule &s) { return s.nowUtc.isValid() ? s.nowUtc.toUTC() : QDateTime::currentDateTimeUtc(); }
}
bool isStrictSemVer(const QString &version) { return parseVersion(version); }
int compareVersions(const QString &a, const QString &b, bool *ok) {
    Version av, bv;
    const bool valid = parseVersion(a, &av) && parseVersion(b, &bv);
    if (ok) *ok = valid;
    if (!valid) return 0;
    for (int i = 0; i < 3; ++i) { const int c = compareNumber(av.number[i], bv.number[i]); if (c) return c < 0 ? -1 : 1; }
    if (av.pre.isEmpty() != bv.pre.isEmpty()) return av.pre.isEmpty() ? 1 : -1;
    for (int i = 0; i < qMin(av.pre.size(), bv.pre.size()); ++i) {
        const auto &x = av.pre[i]; const auto &y = bv.pre[i];
        const bool xn = numeric(x), yn = numeric(y);
        const int c = xn && yn ? compareNumber(x,y) : xn != yn ? (xn ? -1 : 1) : QString::compare(x,y,Qt::CaseSensitive);
        if (c) return c < 0 ? -1 : 1;
    }
    return av.pre.size() == bv.pre.size() ? 0 : av.pre.size() < bv.pre.size() ? -1 : 1;
}
QString portableAssetName(const QString &version, const QString &platform, const QString &architecture) {
    Version v;
    if (!parseVersion(version, &v) || !v.pre.isEmpty() || platform != "windows") return {};
    if (architecture == "x86_64") return "FrogShell-" + version + "-portable.exe";
    if (architecture == "arm64") return "FrogShell-" + version + "-windows-arm64-portable.exe";
    return {};
}
bool validConfig(const Config &c, QString *reason) {
    static const QRegularExpression owner("^[A-Za-z0-9](?:[A-Za-z0-9-]{0,37}[A-Za-z0-9])?$");
    static const QRegularExpression repo("^[A-Za-z0-9_.-]{1,100}$");
    QString why;
    if (!owner.match(c.owner).hasMatch() || owner.match(c.owner).capturedLength()!=c.owner.size() || c.owner.contains("--") ||
        !repo.match(c.repository).hasMatch() || repo.match(c.repository).capturedLength()!=c.repository.size() || c.repository == "." || c.repository == "..") why = "Invalid public GitHub owner/repository";
    else if (!isStrictSemVer(c.currentVersion)) why = "Current version must be strict SemVer";
    else if (c.platform.isEmpty() || c.architecture.isEmpty()) why = "Platform and architecture are required";
    if (reason) *reason = why;
    return why.isEmpty();
}
bool allowedReleaseDownloadUrl(const QUrl &url, const Config &c) {
    return ownedPath(url,c,"download") && url.path(QUrl::FullyDecoded).split(u'/').size() == 7 && !url.hasQuery();
}
bool allowedRedirectUrl(const QUrl &url, const Config &c) {
    if (!secure(url)) return false;
    return allowedReleaseDownloadUrl(url,c) || url.host() == "release-assets.githubusercontent.com" ||
           url.host() == "objects.githubusercontent.com";
}

struct Client::Impl {
    enum class Phase { None, Metadata, Checksum, Payload };
    Client *q;
    Config config;
    Limits limits;
    QPointer<QNetworkAccessManager> manager;
    QPointer<QNetworkReply> reply;
    QTimer idle, deadline, retryTimer;
    State state = State::Idle;
    Error lastError;
    Release release;
    Phase phase = Phase::None;
    Schedule schedule;
    CheckMode mode = CheckMode::Manual;
    bool startupConsumed = false;
    QDateTime clock, rateUntil, automaticAttempt;
    QUrl logicalUrl, metadataUrl, checksumUrl, assetApiUrl;
    qint64 checksumSize = 0, written = 0;
    int redirectCount = 0;
    int retryCount = 0;
    qint64 requestedOffset = 0;
    bool responseAccepted = false, resumable = false;
    bool compatibleTransport = false, useAssetApi = false, ownsManager = false;
    QByteArray entityTag;
    quint64 operation = 0, request = 0;
    QByteArray buffer;
    std::unique_ptr<QTemporaryDir> temporary;
    std::unique_ptr<QSaveFile> output;
    std::unique_ptr<QCryptographicHash> hash;

    Impl(Client *owner, const Config &c, QNetworkAccessManager *transport, const Limits &l)
        : q(owner), config(c), limits(l), manager(transport ? transport : new QNetworkAccessManager(owner)), ownsManager(!transport) {
        idle.setSingleShot(true); deadline.setSingleShot(true); retryTimer.setSingleShot(true);
        QObject::connect(&idle,&QTimer::timeout,q,[this]{
            if (!busy()) return;
            auto e=error(ErrorCode::Timeout,"Network inactivity timeout");e.timeout=TimeoutKind::Inactivity;
            recover(e);
        });
        QObject::connect(&deadline,&QTimer::timeout,q,[this]{
            if (!busy()) return;
            auto e=error(ErrorCode::Timeout,"Update request deadline exceeded");e.timeout=TimeoutKind::Deadline;fail(e);
        });
    }
    bool busy() const { return phase != Phase::None; }
    bool transition(State next, const QString &message) {
        state = next; QPointer<Client> guard(q); emit q->stateChanged(next,message); return !guard.isNull();
    }
    bool limitsValid() const {
        return limits.metadataBytes > 0 && limits.metadataBytes <= 8LL*1024*1024 &&
               limits.checksumBytes > 0 && limits.checksumBytes <= 1024*1024 &&
               limits.downloadBytes > 0 && limits.downloadBytes <= 2LL*1024*1024*1024 &&
               limits.inactivityTimeoutMs > 0 && limits.inactivityTimeoutMs <= 120000 &&
               limits.downloadInactivityTimeoutMs > 0 && limits.downloadInactivityTimeoutMs <= 300000 &&
               limits.downloadRetries >= 0 && limits.downloadRetries <= 5 &&
               limits.retryDelayMs > 0 && limits.retryDelayMs <= 10000 &&
               limits.requestTimeoutMs > 0 && limits.requestTimeoutMs <= 300000 &&
               limits.downloadTimeoutMs > 0 && limits.downloadTimeoutMs <= 2*60*60*1000 &&
               limits.redirects >= 0 && limits.redirects <= 10;
    }
    void stopReply() {
        ++request;
        if (reply) { auto r = reply.data(); reply = nullptr; QObject::disconnect(r,nullptr,q,nullptr); r->abort(); r->deleteLater(); }
        idle.stop();
    }
    void cleanOutput() {
        if (output) output->cancelWriting();
        output.reset(); hash.reset(); temporary.reset(); written = 0; resumable=false;entityTag.clear();
    }
    void resetNetwork() { stopReply(); deadline.stop(); retryTimer.stop();QObject::disconnect(&retryTimer,nullptr,q,nullptr);buffer.clear(); }
    void fail(const Error &e, bool retainPartial=false) {
        diagnose(DiagEvent::UpdateFailed,quint64(e.code),e.httpStatus,{},written);
        const bool wasCheck = phase == Phase::Metadata || phase == Phase::Checksum;
        ++operation; resetNetwork();
        resumable=retainPartial&&written>0&&written<release.size&&output&&hash&&temporary;
        if(!resumable)cleanOutput();
        phase = Phase::None; lastError = e;
        const auto op=operation;
        const auto result = CheckResult{State::Failed,release,e,false};
        QPointer<Client> guard(q);
        if (!transition(State::Failed,e.message)) return;
        if(operation!=op)return;
        emit q->errorOccurred(e); if (!guard || operation!=op) return;
        if (wasCheck) emit q->checkFinished(result);
    }
    int idleTimeout() const {return phase==Phase::Payload?limits.downloadInactivityTimeoutMs:limits.inactivityTimeoutMs;}
    // Retry only transport interruptions and explicitly temporary HTTP failures.
    // Signed CDN URLs may expire, so every attempt starts at the owned Release URL.
    void recover(const Error &e, int minimumDelay=0) {
        if(phase!=Phase::Payload){fail(e);return;}
        if(written==release.size){stopReply();completeDownload();return;}
        if(retryCount>=limits.downloadRetries){fail(e,true);return;}
        const int delay=qMax(minimumDelay,limits.retryDelayMs*(1<<retryCount));
        if(delay>120000){fail(e,true);return;}
        stopReply();buffer.clear();redirectCount=0;++retryCount;
        compatibleTransport=true;
        useAssetApi=!useAssetApi&&!assetApiUrl.isEmpty();
        if(ownsManager&&manager)manager->clearConnectionCache();
        const auto op=operation;
        QObject::disconnect(&retryTimer,nullptr,q,nullptr);
        QObject::connect(&retryTimer,&QTimer::timeout,q,[this,op]{if(operation==op&&phase==Phase::Payload)startPayloadRequest();});
        retryTimer.start(delay);
        // Deadline is intentionally not restarted: backoff counts toward it.
        diagnose(DiagEvent::UpdateRetry,quint64(e.code),retryCount,{},written);
        emit q->downloadRetry(retryCount,limits.downloadRetries,delay,e);
    }
    void startPayloadRequest() {
        // Only synthesize entry URLs from the checked asset/repository. Keep the
        // public URL validator strict; never accept caller-provided query URLs.
        QUrl entry=useAssetApi&&!assetApiUrl.isEmpty()?assetApiUrl:release.downloadUrl;
        QUrlQuery fresh;
        fresh.addQueryItem("frogshell_download",QUuid::createUuid().toString(QUuid::Id128));
        entry.setQuery(fresh);
        startRequest(entry);
    }
    bool restartOutput() {
        const auto path=output->fileName();output->cancelWriting();output.reset();
        output=std::make_unique<QSaveFile>(path);output->setDirectWriteFallback(false);
        if(!output->open(QIODevice::WriteOnly)){fail(error(ErrorCode::Io,"Unable to restart update staging file"));return false;}
        hash=std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);written=0;entityTag.clear();return true;
    }
    void finishCheck(State next, const Error &e = {}) {
        resetNetwork(); phase = Phase::None; lastError = e;
        const bool notify = next == State::Available && (mode == CheckMode::Manual || schedule.lastNotifiedVersion != release.version);
        const CheckResult result{next,release,e,notify};
        const auto op=operation;
        if (!transition(next,e.message)) return;
        if(operation!=op)return;
        emit q->checkFinished(result);
    }
    bool metadataAllowed(const QUrl &url) const { return secure(url) && url == metadataUrl; }
    void startRequest(const QUrl &url) {
        if (!manager || manager->thread() != q->thread()) { fail(error(ErrorCode::InvalidConfiguration,"Transport must outlive Client and share its thread")); return; }
        stopReply(); logicalUrl = url; buffer.clear();responseAccepted=false;
        requestedOffset=phase==Phase::Payload?written:0;
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        req.setAttribute(QNetworkRequest::AuthenticationReuseAttribute,QNetworkRequest::Manual);
        req.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
        req.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
        req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,QNetworkRequest::AlwaysNetwork);
        req.setAttribute(QNetworkRequest::CacheSaveControlAttribute,false);
        req.setRawHeader("Cache-Control","no-cache, no-store");
        if(phase==Phase::Payload&&compatibleTransport) {
            // Repeating the same HTTP/2 connection can repeat a stalled stream.
            // Recover with a fresh HTTP/1.1 connection, retaining downloaded bytes.
            req.setAttribute(QNetworkRequest::Http2AllowedAttribute,false);
            req.setRawHeader("Connection","close");
        }
        req.setRawHeader("User-Agent","FrogShell-ReleaseUpdate/1");
        req.setRawHeader("Accept",phase == Phase::Metadata ? "application/vnd.github+json" : "application/octet-stream");
        req.setRawHeader("Accept-Encoding","identity");
        if(requestedOffset>0){
            req.setRawHeader("Range","bytes="+QByteArray::number(requestedOffset)+"-");
            if(!entityTag.isEmpty())req.setRawHeader("If-Range",entityTag);
        }
        if (url.host()=="api.github.com") req.setRawHeader("X-GitHub-Api-Version","2022-11-28");
        diagnose(DiagEvent::UpdateRequest,quint64(phase),retryCount,{},written);
        auto r = manager->get(req); reply = r; r->setReadBufferSize(128*1024);
        const quint64 id = request, op = operation;
        auto active = [this,r,id,op]{return reply == r && request == id && operation == op;};
        QObject::connect(r,&QNetworkReply::metaDataChanged,q,[this,active]{if(active()) headers();});
        QObject::connect(r,&QIODevice::readyRead,q,[this,active]{if(active()){idle.start(idleTimeout()); drain();}});
        QObject::connect(r,&QNetworkReply::sslErrors,q,[this,active](const QList<QSslError>&){if(active())fail(error(ErrorCode::Tls,"TLS certificate verification failed"));});
        QObject::connect(r,&QNetworkReply::finished,q,[this,active]{if(active()) finished();});
        idle.start(idleTimeout());
    }
    Error httpError(int status) {
        Error e = error(ErrorCode::Http,QString("GitHub HTTP %1").arg(status),status);
        if (status == 429 || (status == 403 && reply && reply->rawHeader("X-RateLimit-Remaining") == "0") ||
            (status == 403 && reply && !reply->rawHeader("Retry-After").isEmpty())) {
            e.code = ErrorCode::RateLimited; e.message = "GitHub rate limit reached";
            const auto now = nowFor(schedule);
            QDateTime retry;
            const auto after = reply->rawHeader("Retry-After"); bool ok = false;
            const auto seconds = after.toLongLong(&ok);
            if (ok && seconds >= 0) retry = now.addSecs(qMin<qint64>(seconds,86400));
            else if (!after.isEmpty()) retry = QDateTime::fromString(QString::fromLatin1(after),Qt::RFC2822Date).toUTC();
            const auto reset = reply->rawHeader("X-RateLimit-Reset").toLongLong(&ok);
            if (!retry.isValid() && ok && reset > 0) retry = QDateTime::fromSecsSinceEpoch(reset,QTimeZone::UTC);
            if (!retry.isValid() || retry <= now) retry = now.addSecs(60);
            if (retry > now.addDays(1)) retry = now.addDays(1);
            rateUntil = retry; e.retryAfterUtc = retry;
        }
        return e;
    }
    bool headers() {
        if (!reply) return false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (!status) return true;
        if (status==301||status==302||status==303||status==307||status==308) return true;
        if (responseAccepted) return true;
        diagnose(DiagEvent::UpdateResponse,quint64(phase),status,{},written);
        if (status != 200 && !(phase==Phase::Payload&&status==206)) {
            const auto e=httpError(status);
            const bool expiredCdn=(logicalUrl.host()=="release-assets.githubusercontent.com"||logicalUrl.host()=="objects.githubusercontent.com")&&
                (status==400||status==401||status==403||status==404);
            if(phase==Phase::Payload&&(expiredCdn||status==408||status==500||status==502||status==503||status==504)) {
                int delay=0;const auto after=reply->rawHeader("Retry-After");
                if(!after.isEmpty()){
                    bool ok=false;const auto seconds=after.toLongLong(&ok);
                    const auto ms=ok&&seconds>=0?qMin<qint64>(seconds,86400)*1000:QDateTime::currentDateTimeUtc().msecsTo(QDateTime::fromString(QString::fromLatin1(after),Qt::RFC2822Date));
                    delay=int(qBound(qint64(0),ms,qint64(86400000)));
                }
                recover(e,delay);
            } else fail(e);
            return false;
        }
        qint64 expected=release.size;
        if(phase==Phase::Payload&&status==206){
            static const QRegularExpression range(QStringLiteral("^bytes ([0-9]+)-([0-9]+)/([0-9]+)$"));
            const auto m=range.match(QString::fromLatin1(reply->rawHeader("Content-Range")));
            bool a=false,b=false,c=false;
            const auto start=m.captured(1).toLongLong(&a),end=m.captured(2).toLongLong(&b),total=m.captured(3).toLongLong(&c);
            if(!m.hasMatch()||!a||!b||!c||start!=requestedOffset||end!=release.size-1||total!=release.size){
                fail(error(ErrorCode::InvalidMetadata,"Invalid update Content-Range"));return false;
            }
            expected=release.size-requestedOffset;
        }
        const qint64 maximum = phase == Phase::Payload ? qMin(limits.downloadBytes,release.size) :
                               phase == Phase::Metadata ? limits.metadataBytes : limits.checksumBytes;
        const auto rawLength = reply->rawHeader("Content-Length");
        if (!rawLength.isEmpty()) {
            bool ok = false; const qint64 size = rawLength.toLongLong(&ok);
            if (!ok || size < 0) {fail(error(ErrorCode::InvalidMetadata,"Invalid Content-Length")); return false;}
            if (size > maximum) {fail(error(ErrorCode::SizeLimit,"Response exceeds update size limit")); return false;}
            if (phase == Phase::Payload && size != expected) {fail(error(ErrorCode::InvalidMetadata,"Download length differs from Release asset size")); return false;}
        }
        const auto encoding = reply->rawHeader("Content-Encoding").trimmed().toLower();
        if (!encoding.isEmpty() && encoding != "identity") {fail(error(ErrorCode::InvalidMetadata,"Encoded update responses are not accepted"));return false;}
        if(phase==Phase::Payload){
            // A server may ignore Range or reject If-Range by returning 200.
            // Discard the old prefix before accepting its full response.
            if(status==200&&requestedOffset>0&&!restartOutput())return false;
            const auto tag=reply->rawHeader("ETag");
            if(status==206&&!entityTag.isEmpty()&&!tag.isEmpty()&&tag!=entityTag){fail(error(ErrorCode::InvalidMetadata,"Update entity changed during resume"));return false;}
            if(tag.startsWith('"')&&tag.endsWith('"'))entityTag=tag;
        }
        responseAccepted=true;
        return true;
    }
    void drain() {
        if (!headers() || !reply) return;
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status!=200&&status!=206) return;
        const quint64 op = operation, id = request;
        int budget = 256*1024;
        while (reply && reply->bytesAvailable() && budget > 0) {
            const QByteArray chunk = reply->read(qMin<qint64>(64*1024,budget));
            if (chunk.isEmpty()) break;
            budget -= int(chunk.size());
            if (phase == Phase::Payload) {
                if (written + chunk.size() > release.size || written + chunk.size() > limits.downloadBytes) {fail(error(ErrorCode::SizeLimit,"Download exceeds expected size"));return;}
                if (!output || output->write(chunk) != chunk.size()) {fail(error(ErrorCode::Io,"Unable to write update staging file"));return;}
                hash->addData(chunk); written += chunk.size();
                QPointer<Client> guard(q); emit q->downloadProgress(written,release.size);
                if (!guard || operation != op || request != id) return;
            } else {
                const auto maximum = phase == Phase::Metadata ? limits.metadataBytes : limits.checksumBytes;
                if (buffer.size() + chunk.size() > maximum) {fail(error(ErrorCode::SizeLimit,"Response exceeds update size limit"));return;}
                buffer += chunk;
            }
        }
        if (phase==Phase::Payload&&written==release.size) {
            // A complete, hash-verified asset does not need a peer EOF/finished
            // notification. Some proxies keep the completed stream open.
            stopReply(); completeDownload(); return;
        }
        if (reply && reply->bytesAvailable()) QTimer::singleShot(0,q,[this,op,id]{if(operation==op && request==id) drain();});
    }
    void finished() {
        if (!reply) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            const auto target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
            const auto next = logicalUrl.resolved(target);
            if (target.isEmpty() || ++redirectCount > limits.redirects ||
                (phase == Phase::Metadata ? !metadataAllowed(next) : !allowedRedirectUrl(next,config))) {
                fail(error(ErrorCode::UnsafeUrl,"Rejected update redirect")); return;
            }
            startRequest(next); return;
        }
        if (!headers() || !reply) return;
        if(status!=200&&!(phase==Phase::Payload&&status==206)&&reply->error()==QNetworkReply::NoError){fail(error(ErrorCode::Network,"Update response has no successful HTTP status",status));return;}
        const auto savedOperation=operation,savedRequest=request;
        QPointer<Client> guard(q);drain();
        if (!guard || operation!=savedOperation || request!=savedRequest || !reply) return;
        // readBufferSize bounds the final available data; continue asynchronously
        // if an injected transport exposes more than the per-event budget.
        if (reply->bytesAvailable()) {const auto op=operation,id=request;QTimer::singleShot(0,q,[this,op,id]{if(operation==op&&request==id)finished();});return;}
        const auto network=reply->error();
        if(network!=QNetworkReply::NoError){
            auto e=error(network==QNetworkReply::TimeoutError?ErrorCode::Timeout:ErrorCode::Network,"Update network transfer failed",status);
            if(network==QNetworkReply::TimeoutError)e.timeout=TimeoutKind::Inactivity;
            switch(network){
            case QNetworkReply::RemoteHostClosedError:case QNetworkReply::ConnectionRefusedError:
            case QNetworkReply::HostNotFoundError:case QNetworkReply::TimeoutError:
            case QNetworkReply::TemporaryNetworkFailureError:case QNetworkReply::NetworkSessionFailedError:
            case QNetworkReply::ProxyConnectionClosedError:case QNetworkReply::ProxyTimeoutError:
            case QNetworkReply::ProtocolFailure:recover(e);break;
            default:fail(e);break;
            }
            return;
        }
        if(phase==Phase::Payload&&written<release.size){recover(error(ErrorCode::Network,"Update connection ended before all bytes arrived",status));return;}
        stopReply();
        if (phase == Phase::Metadata) parseMetadata();
        else if (phase == Phase::Checksum) parseChecksum();
        else if (phase == Phase::Payload) completeDownload();
    }
    void parseMetadata() {
        QJsonParseError pe;
        const auto doc = QJsonDocument::fromJson(buffer,&pe); buffer.clear();
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {fail(error(ErrorCode::InvalidMetadata,"Invalid GitHub Release JSON"));return;}
        const auto object = doc.object();
        if (!object["draft"].isBool() || !object["prerelease"].isBool() || !object["tag_name"].isString() || !object["assets"].isArray()) {fail(error(ErrorCode::InvalidMetadata,"Incomplete GitHub Release metadata"));return;}
        if (object["draft"].toBool() || object["prerelease"].toBool()) {finishCheck(State::UpToDate);return;}
        const auto tag = object["tag_name"].toString();
        const auto version = tag.startsWith(u'v') ? tag.mid(1) : tag;
        Version parsed;
        if (!parseVersion(version,&parsed)) {fail(error(ErrorCode::InvalidVersion,"Release tag must contain strict SemVer"));return;}
        if (!parsed.pre.isEmpty()) {finishCheck(State::UpToDate);return;}
        if (compareVersions(version,config.currentVersion) <= 0) {finishCheck(State::UpToDate);return;}
        const auto page = strictUrl(object["html_url"]);
        if (!ownedPath(page,config,"tag") || page.hasQuery() || !exactTail(page,{tag})) {fail(error(ErrorCode::UnsafeUrl,"Release page does not belong to configured repository/tag"));return;}
        release = {version,tag,{},page,{},0,{}};
        if(!object["body"].isUndefined()&&!object["body"].isNull()&&!object["body"].isString()){fail(error(ErrorCode::InvalidMetadata,"Invalid Release notes type"));return;}
        release.notes=boundedNotes(object["body"].toString());
        const auto expected = portableAssetName(version,config.platform,config.architecture);
        if (expected.isEmpty()) {finishCheck(State::ManualDownload,error(ErrorCode::MissingDigest,"No portable asset convention for this platform/architecture"));return;}
        QJsonObject chosen; int matches = 0;
        const auto assets = object["assets"].toArray();
        for (const auto &value : assets) {
            if (!value.isObject()) {fail(error(ErrorCode::InvalidMetadata,"Invalid Release asset entry"));return;}
            const auto a = value.toObject();
            if (a["name"].toString() == expected) {chosen=a;++matches;}
        }
        if (matches > 1) {fail(error(ErrorCode::InvalidMetadata,"Ambiguous duplicate portable assets"));return;}
        if (!matches) {finishCheck(State::ManualDownload,error(ErrorCode::MissingDigest,"No exact portable asset for this architecture"));return;}
        qint64 size = 0; const auto url = strictUrl(chosen["browser_download_url"]);
        if (!integerSize(chosen["size"],limits.downloadBytes,&size)) {fail(error(ErrorCode::SizeLimit,"Invalid or oversized portable asset"));return;}
        if (!allowedReleaseDownloadUrl(url,config) || !exactTail(url,{tag,expected})) {fail(error(ErrorCode::UnsafeUrl,"Portable asset URL does not match repository/tag/name"));return;}
        if (chosen.contains("state") && chosen["state"].toString() != "uploaded") {fail(error(ErrorCode::InvalidMetadata,"Portable asset is not uploaded"));return;}
        release.assetName=expected;release.downloadUrl=url;release.size=size;
        // Public asset REST downloads require no token. Derive this exact
        // same-repository endpoint from a numeric ID, never the asset's URL field.
        assetApiUrl={}; qint64 assetId=0;
        if(integerSize(chosen["id"],9007199254740991LL,&assetId))
            assetApiUrl=QUrl("https://api.github.com/repos/"+config.owner+"/"+config.repository+"/releases/assets/"+QString::number(assetId));
        const auto digest = chosen["digest"];
        if (!digest.isUndefined() && !digest.isNull() && (!digest.isString() || !digest.toString().isEmpty())) {
            static const QRegularExpression sha("^sha256:([0-9A-Fa-f]{64})$");
            const auto m = sha.match(digest.toString());
            if (!digest.isString() || !m.hasMatch() || m.capturedLength() != digest.toString().size()) {fail(error(ErrorCode::InvalidMetadata,"Invalid GitHub SHA256 digest"));return;}
            release.sha256=QByteArray::fromHex(m.captured(1).toLatin1());finishCheck(State::Available);return;
        }
        QJsonObject checksum; int count = 0;
        // Prefer the asset-specific checksum; then the exact shared list name.
        for (const auto &name : QStringList{expected+".sha256",QStringLiteral("SHA256SUMS")}) {
            count=0;
            for (const auto &value : assets) if(value.toObject()["name"].toString()==name){checksum=value.toObject();++count;}
            if(count) break;
        }
        if (count > 1) {fail(error(ErrorCode::InvalidMetadata,"Ambiguous checksum assets"));return;}
        if (!count) {finishCheck(State::ManualDownload,error(ErrorCode::MissingDigest,"No verifiable SHA256 digest; use the Release page"));return;}
        checksumUrl = strictUrl(checksum["browser_download_url"]);
        const auto checksumName=checksum["name"].toString();
        if (!allowedReleaseDownloadUrl(checksumUrl,config) || !exactTail(checksumUrl,{tag,checksumName})) {fail(error(ErrorCode::UnsafeUrl,"Checksum URL does not match repository/tag/name"));return;}
        if (!integerSize(checksum["size"],limits.checksumBytes,&checksumSize)) {fail(error(ErrorCode::SizeLimit,"Invalid or oversized checksum list"));return;}
        phase=Phase::Checksum;startRequest(checksumUrl);
    }
    void parseChecksum() {
        if (buffer.size() != checksumSize) {fail(error(ErrorCode::InvalidMetadata,"Checksum list length mismatch"));return;}
        QStringDecoder decode(QStringDecoder::Utf8); const QString text=decode(buffer);buffer.clear();
        if (decode.hasError() || text.contains(QChar(0))) {fail(error(ErrorCode::InvalidMetadata,"Invalid checksum list encoding"));return;}
        static const QRegularExpression line("^([A-Fa-f0-9]{64})[ \\t]+\\*?(.+)$");
        int count=0; QByteArray found;
        for (auto row : text.split(u'\n')) {
            if (row.endsWith(u'\r')) row.chop(1);
            if (row.isEmpty() || row.startsWith(u'#')) continue;
            const auto match=line.match(row);
            if (match.hasMatch() && match.captured(2)==release.assetName) {found=QByteArray::fromHex(match.captured(1).toLatin1());++count;}
        }
        if (count != 1 || found.size()!=32) {finishCheck(State::ManualDownload,error(ErrorCode::MissingDigest,"Checksum list has no unique exact asset entry"));return;}
        release.sha256=found;finishCheck(State::Available);
    }
    void completeDownload() {
        if (written != release.size || !hash || hash->result()!=release.sha256) {fail(error(ErrorCode::DigestMismatch,"Update SHA256/size verification failed"));return;}
        if (!output->commit()) {fail(error(ErrorCode::Io,"Unable to commit verified staging file"));return;}
        const VerifiedDownload result{output->fileName(),release.sha256,written,release};
        output.reset();hash.reset();
        deadline.stop();retryTimer.stop();resumable=false;phase=Phase::None;lastError={};
        const auto op=operation;
        if(!transition(State::Ready,"Update download verified"))return;
        if(operation!=op){temporary.reset();return;}
        temporary->setAutoRemove(false);temporary.reset();
        diagnose(DiagEvent::UpdateDone,0,0,{},written);
        emit q->downloadVerified(result);
    }
};

Client::Client(const Config &c,QObject *parent,QNetworkAccessManager *transport,const Limits &limits)
    : QObject(parent),d(std::make_unique<Impl>(this,c,transport,limits)) {
    qRegisterMetaType<State>();qRegisterMetaType<Error>();qRegisterMetaType<CheckResult>();qRegisterMetaType<VerifiedDownload>();
}
Client::~Client() { d->resetNetwork();d->cleanOutput(); }
Config Client::config() const {return d->config;}
State Client::state() const {return d->state;}
Release Client::release() const {return d->release;}
Error Client::lastError() const {return d->lastError;}
bool Client::busy() const {return d->busy();}
bool Client::canResumeDownload() const {return !d->busy()&&d->resumable;}
bool Client::retryPending() const {return d->retryTimer.isActive();}
void Client::setConfig(const Config &c) {
    QPointer<Client> guard(this);cancel();if(!guard)return;
    const bool changed=d->config.owner.compare(c.owner,Qt::CaseInsensitive)!=0||d->config.repository.compare(c.repository,Qt::CaseInsensitive)!=0;
    ++d->operation;d->resetNetwork();d->cleanOutput();d->release={};d->assetApiUrl={};d->lastError={};if(changed){d->rateUntil={};d->automaticAttempt={};}d->config=c;d->transition(State::Idle,{});
}
void Client::check(CheckMode mode,const Schedule &schedule) {
    if(d->busy()){emit errorOccurred(error(ErrorCode::Busy,"An update operation is already running"));return;}
    ++d->operation;d->resetNetwork();d->cleanOutput();d->release={};d->assetApiUrl={};d->lastError={};d->mode=mode;d->schedule=schedule;d->clock=nowFor(schedule);d->phase=Impl::Phase::Metadata;
    if(d->config.owner.isEmpty()||d->config.repository.isEmpty()){d->finishCheck(State::Unconfigured);return;}
    QString reason;
    if(!validConfig(d->config,&reason)||!d->limitsValid()){d->fail(error(ErrorCode::InvalidConfiguration,reason.isEmpty()?"Invalid update resource limits":reason));return;}
    const bool startup=mode==CheckMode::Startup;
    const bool automatic=mode!=CheckMode::Manual;
    const bool repeatedStartup=startup&&d->startupConsumed;
    if(startup)d->startupConsumed=true;
    if(automatic&&!schedule.automaticEnabled){d->finishCheck(State::Disabled);return;}
    auto previous=schedule.lastAutomaticAttemptUtc.toUTC();
    if(d->automaticAttempt.isValid()&&(!previous.isValid()||previous<d->automaticAttempt))previous=d->automaticAttempt;
    // Startup may ignore a previous process's timestamp, never an attempt
    // already made by this client, and can be requested only once.
    if(startup)previous=d->automaticAttempt;
    if(automatic&&(repeatedStartup||(previous.isValid()&&previous.secsTo(d->clock)<1800))){d->finishCheck(State::Throttled);return;}
    auto rateUntil=d->rateUntil;
    if(schedule.rateLimitUntilUtc.isValid()&&(!rateUntil.isValid()||schedule.rateLimitUntilUtc.toUTC()>rateUntil))rateUntil=schedule.rateLimitUntilUtc.toUTC();
    if(rateUntil.isValid()&&d->clock<rateUntil){auto e=error(ErrorCode::RateLimited,"GitHub rate limit cooldown active");e.retryAfterUtc=rateUntil;d->finishCheck(State::Throttled,e);return;}
    d->phase=Impl::Phase::Metadata;d->redirectCount=0;
    d->metadataUrl=QUrl("https://api.github.com/repos/"+d->config.owner+"/"+d->config.repository+"/releases/latest");
    const auto op=d->operation;
    if(!d->transition(State::Checking,"Checking GitHub Release"))return;
    if(d->operation!=op)return;
    if(automatic){d->automaticAttempt=d->clock;QPointer<Client> guard(this);emit automaticAttemptStarted(d->clock);if(!guard||d->operation!=op)return;}
    d->deadline.start(d->limits.requestTimeoutMs);d->startRequest(d->metadataUrl);
}
void Client::download(const QString &directory) {
    if(d->busy()){emit errorOccurred(error(ErrorCode::Busy,"An update operation is already running"));return;}
    const bool resume=canResumeDownload();
    if((d->state!=State::Available&&!resume)||d->release.sha256.size()!=32||!allowedReleaseDownloadUrl(d->release.downloadUrl,d->config)) {d->fail(error(ErrorCode::MissingDigest,"Check a verifiable update before downloading"));return;}
    const QFileInfo parent(directory);
    if(!parent.isDir()||parent.isSymLink()){d->fail(error(ErrorCode::Io,"Staging directory must be an existing local directory"));return;}
    if(!resume){
    d->compatibleTransport=false;d->useAssetApi=false;
    d->temporary=std::make_unique<QTemporaryDir>(QDir(directory).filePath("frogshell-update-XXXXXX"));
    if(!d->temporary->isValid()){d->fail(error(ErrorCode::Io,"Unable to create private update staging directory"));return;}
    QFile::setPermissions(d->temporary->path(),QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    d->output=std::make_unique<QSaveFile>(QDir(d->temporary->path()).filePath(d->release.assetName));
    d->output->setDirectWriteFallback(false);
    if(!d->output->open(QIODevice::WriteOnly)){d->fail(error(ErrorCode::Io,"Unable to open update staging file"));return;}
    d->hash=std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);d->written=0;
    }
    d->resumable=false;d->retryCount=0;d->phase=Impl::Phase::Payload;
    ++d->operation;const auto op=d->operation;d->redirectCount=0;d->lastError={};
    if(!d->transition(State::Downloading,"Downloading update"))return;
    if(d->operation!=op)return;
    QPointer<Client> guard(this);emit downloadProgress(d->written,d->release.size);if(!guard||d->operation!=op)return;
    d->deadline.start(d->limits.downloadTimeoutMs);d->startPayloadRequest();
}
void Client::cancel() {
    if(!d->busy()&&!d->resumable)return;
    const bool check=d->phase==Impl::Phase::Metadata||d->phase==Impl::Phase::Checksum;
    ++d->operation;d->resetNetwork();d->cleanOutput();d->phase=Impl::Phase::None;d->lastError=error(ErrorCode::Cancelled,"Update operation cancelled");
    const CheckResult result{State::Cancelled,d->release,d->lastError,false};
    const auto op=d->operation;
    if(!d->transition(State::Cancelled,d->lastError.message))return;
    if(d->operation!=op)return;
    if(check)emit checkFinished(result);
}
}
