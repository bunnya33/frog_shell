#pragma once
#include <QtCore>
#include <functional>
#include <memory>

namespace rs::selfupdate {
inline constexpr auto HelperArgument = "--internal-install-update";
inline constexpr auto ReceiptArgument = "--internal-update-receipt";
inline constexpr auto ReceiptTokenArgument = "--internal-update-token";
enum class Code { Ok, ManualUpdate, InvalidInput, Changed, InUse, Canceled, TimedOut, IoError, RolledBack, RecoveryRequired };
struct Result {
    Code code = Code::InvalidInput;
    QString message;
    bool ok() const { return code == Code::Ok; }
};
struct FileIdentity {
    quint64 volume = 0, index = 0, bytes = 0, modified = 0;
    QByteArray sha256; // Exactly 32 raw bytes, never an unverified digest string.
};
struct ProcessIdentity { quint32 pid = 0; quint64 created = 0; QString imagePath; };
// Launcher must replace/filter ALL RABBITSHELL_PORTABLE_* inherited variables.
// Values are its own observed canonical image path, process creation FILETIME,
// file identity and streamed SHA256; not values supplied by the caller.
struct PortableOrigin {
    bool singleExecutable = false;
    ProcessIdentity launcher;
    FileIdentity originalFile;
};
struct VerifiedUpdate { QString filePath; QByteArray sha256; quint64 bytes = 0; };
using Canceled = std::function<bool()>;
Result originFromEnvironment(const QProcessEnvironment &, PortableOrigin *);
Result currentProcessIdentity(ProcessIdentity *);
Result snapshotFile(const QString &, FileIdentity *, const Canceled & = {});

class PreparedInstall {
  public:
    PreparedInstall();
    ~PreparedInstall();
    PreparedInstall(PreparedInstall &&) noexcept;
    PreparedInstall &operator=(PreparedInstall &&) noexcept;
    PreparedInstall(const PreparedInstall &) = delete;
    PreparedInstall &operator=(const PreparedInstall &) = delete;
    bool valid() const;
    QString planPath() const;
    QString receiptPath() const;
    QString transactionToken() const;
  private:
    struct State;
    std::unique_ptr<State> d;
    friend struct Access;
};
// Blocking disk/hash work: call on a worker. Copies the verified package into
// its own private transaction directory. After success the caller may release
// the original download. No helper starts and no target changes. Main owns the
// moved PreparedInstall until normal exit/cancel, independently of the About UI.
Result prepareInstall(const PortableOrigin &, const VerifiedUpdate &, const QString &privateStagingBase,
                      PreparedInstall *, const Canceled & = {});
// Cancel the intent when requestExit is rejected. No waiting helper exists.
Result discardPreparedInstall(PreparedInstall *);
// MAIN ONLY: after normal requestExit completed, app.exec returned and windows
// were destroyed. Starts THIS runtime Qt EXE with HelperArgument + planPath.
// A bounded native ready/commit handshake opens the actual inherited process
// handles before main returns/exits. No shell, elevation or force termination.
Result startAfterMainLoopExit(PreparedInstall *, quint32 *helperPid = nullptr);
// Earliest main dispatch: exactly argc==3 and argv[1]==HelperArgument, create
// QCoreApplication, call this, return. No normal single-instance/UI/settings.
int runInstallHelper(const QString &planPath);

struct InstallStatus {
    QString phase, message, backupPath;
    Code code = Code::InvalidInput;
};
Result readInstallStatus(const QString &receiptPath, InstallStatus *);
// New outer EXE is started with ReceiptArgument/path + ReceiptTokenArgument/token.
// Recognize these in normal main but DO NOT bypass AppLock/startup protection.
// After normal startup succeeds, validate fresh launcher origin and acknowledge;
// only this removes the verified old sibling backup. No config is touched.
Result acknowledgeNormalStartup(const QString &receiptPath, const QString &token, const PortableOrigin &freshOrigin);

#ifdef SELFUPDATE_FIXTURE_TESTS
namespace fixture {
enum class FailAt { None, AfterBackup, AfterInstall, Launch };
struct Options {
    FailAt failAt = FailAt::None;
    int waitMs = 3000;
    Canceled canceled;
};
// Only compiled by the independent fixture. Still verifies real fixture PID,
// creation time, image path and target identity; permits a fixture process tree.
Result prepare(const PortableOrigin &, const ProcessIdentity &fixtureMain, const VerifiedUpdate &,
               const QString &base, PreparedInstall *, const Canceled & = {});
// Runs the same wait/install/rollback engine, never launches the supplied new EXE.
Result install(PreparedInstall *, const Options & = {});
}
#endif
}
