#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>
#include <memory>

class QNetworkAccessManager;

namespace rs::releaseupdate {
// Platform/architecture are explicit integration inputs, never inferred from
// an asset filename. The legacy portable.exe name is Windows x86_64 only.
struct Config {
    QString owner, repository, currentVersion;
    QString platform = QStringLiteral("windows");
    QString architecture = QStringLiteral("x86_64");
};
struct Limits {
    qint64 metadataBytes = 1024 * 1024;
    qint64 checksumBytes = 256 * 1024;
    qint64 downloadBytes = 512LL * 1024 * 1024;
    int inactivityTimeoutMs = 15000;
    int requestTimeoutMs = 30000;
    int downloadTimeoutMs = 20 * 60 * 1000;
    int redirects = 5;
};
struct Schedule {
    bool automaticEnabled = false;
    QDateTime lastAutomaticAttemptUtc;
    QString lastNotifiedVersion;
    QDateTime nowUtc; // invalid => currentDateTimeUtc(); deterministic fixture seam
    QDateTime rateLimitUntilUtc; // persisted service cooldown, if any
};
enum class CheckMode { Manual, Automatic, Startup };
enum class State {
    Idle, Unconfigured, Disabled, Throttled, Checking, UpToDate,
    Available, ManualDownload, Downloading, Ready, Cancelled, Failed
};
enum class ErrorCode {
    None, Busy, InvalidConfiguration, InvalidMetadata, InvalidVersion,
    UnsafeUrl, Http, RateLimited, Network, Tls, Timeout, SizeLimit,
    MissingDigest, DigestMismatch, Io, Cancelled
};
struct Error {
    ErrorCode code = ErrorCode::None;
    QString message;
    int httpStatus = 0;
    QDateTime retryAfterUtc;
};
struct Release {
    QString version, tag, assetName;
    QUrl releaseUrl, downloadUrl;
    qint64 size = 0;
    QByteArray sha256; // 32 raw bytes, empty => manual download only
    QString notes; // Release body, <=64 KiB UTF-8; UI must use Qt::PlainText
};
struct CheckResult {
    State state = State::Idle;
    Release release;
    Error error;
    bool shouldNotify = false; // integration persists lastNotifiedVersion
};
struct VerifiedDownload {
    QString path; // new file in a unique child directory of stagingDirectory
    QByteArray sha256;
    qint64 size = 0;
    Release release;
};

// General SemVer validation/comparison; versions must not include a 'v' prefix.
bool isStrictSemVer(const QString &version);
// ok=false if either input is invalid. SemVer build metadata has no precedence.
int compareVersions(const QString &a, const QString &b, bool *ok = nullptr);
QString portableAssetName(const QString &version, const QString &platform,
                          const QString &architecture);
bool validConfig(const Config &config, QString *reason = nullptr);
bool allowedReleaseDownloadUrl(const QUrl &url, const Config &config);
bool allowedRedirectUrl(const QUrl &url, const Config &config);

class Client final : public QObject {
    Q_OBJECT
  public:
    // An injected manager is borrowed, must outlive Client and share its thread.
    // Its createRequest override may route validated logical HTTPS requests to
    // an isolated loopback HTTP fixture. Production uses an owned Qt manager.
    explicit Client(const Config &config, QObject *parent = nullptr,
                    QNetworkAccessManager *transport = nullptr, const Limits &limits = {});
    ~Client() override;
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    void setConfig(const Config &config); // cancels and invalidates any old result
    Config config() const;
    State state() const;
    Release release() const;
    Error lastError() const;
    bool busy() const;
    void check(CheckMode mode = CheckMode::Manual, const Schedule &schedule = {});
    // Uses only the last checked/verified Release. No caller-supplied URL/hash.
    // Success hands ownership of the new child directory/file to integration.
    void download(const QString &stagingDirectory);
    void cancel();
  signals:
    void stateChanged(rs::releaseupdate::State state, const QString &message);
    void automaticAttemptStarted(const QDateTime &utc);
    void checkFinished(const rs::releaseupdate::CheckResult &result);
    void downloadProgress(qint64 received, qint64 total);
    void downloadVerified(const rs::releaseupdate::VerifiedDownload &download);
    void errorOccurred(const rs::releaseupdate::Error &error);
  private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
Q_DECLARE_METATYPE(rs::releaseupdate::State)
Q_DECLARE_METATYPE(rs::releaseupdate::Error)
Q_DECLARE_METATYPE(rs::releaseupdate::CheckResult)
Q_DECLARE_METATYPE(rs::releaseupdate::VerifiedDownload)
