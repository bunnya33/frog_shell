#pragma once
#include "../core/releaseupdate.h"
#include "../core/selfupdate.h"
#include <QObject>
#include <memory>
class QWidget;
class QNetworkAccessManager;
namespace rs {
// Application-scoped: closing the About window never destroys a download or
// moves installation ownership out of main's normal exit lifecycle.
class UpdateController final : public QObject {
    Q_OBJECT
  public:
    explicit UpdateController(const releaseupdate::Config &runtime,
                              const selfupdate::PortableOrigin &origin,
                              const QString &stagingBase, QObject *parent = nullptr,
                              QNetworkAccessManager *transport = nullptr);
    ~UpdateController() override;
    static releaseupdate::Config runtimeConfig();
    void showAbout(QWidget *parent);
    QWidget *createSettingsPage(QWidget *parent = nullptr);
    bool setAutomaticCheckEnabled(bool automatic, QString *error = nullptr);
    bool hasUpdate() const;
    bool automaticInstallSupported() const;
    bool updateBusy() const;
    QString updateVersion() const;
    QString progressText() const;
    void requestUpdate();
    releaseupdate::Config configuration() const;
    releaseupdate::State state() const;
    QString statusText() const;
    QString installStatusText() const;
    void refreshInstallStatus();
    bool ready() const;
    bool restartPending() const;
    bool preparing() const;
    void check(bool automatic = false);
    void download();
    void cancel();
    void requestRestart();
    void cancelExitIntent();
    void confirmExit();
    void setMigrationPending(bool pending);
    selfupdate::PreparedInstall takePreparedInstall();
  signals:
    void changed();
    void sourceChanged();
    void restartRequested();
    void updateAvailable(const QString &version);
    void settingsRequested();
    void manualUpdateRequested();
  private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
