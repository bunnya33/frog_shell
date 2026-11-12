#pragma once
#include "../core/tunnel.h"
#include "theme.h"
#include <optional>
namespace rs {
class PortsModel;
class PortsDelegate;
// Owns configuration and its single edit row. Runtime ownership stays with Workspace.
class PortsPanel : public QWidget {
    Q_OBJECT
  public:
    explicit PortsPanel(const QString &profileId, bool offline = false, QWidget *parent = nullptr);
    ~PortsPanel() override;
    QVector<TunnelRule> rules() const;
    std::optional<TunnelRule> rule(const QString &id) const;
    QString state(const QString &id) const;
    int activeCount() const;
    bool hasDraft() const { return draft.has_value(); }
    std::function<QString()> startGuard;
    void setRuleState(const QString &id, const QString &state, const QString &reason = {});
    bool rememberEnabled(const QString &id, bool enabled);
    void setRuleTraffic(const QString &id, quint64 uploadPerSecond, quint64 downloadPerSecond);
    bool ensureMapping(const QString &remoteAddress, quint16 remotePort);
    void refreshTheme();
    void beginEdit(const QString &id = {}, int column = 1);
    bool commitDraft(bool start = false);
    void cancelDraft();
  signals:
    void startRequested(const rs::TunnelRule &rule);
    void stopRequested(const QString &id);
    void restartRequested(const rs::TunnelRule &rule);
    void configurationChanged();
  protected:
    bool eventFilter(QObject *, QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
  private:
    friend class PortsModel;
    friend class PortsDelegate;
    friend class PortsTests;
    friend class MenuTests;
    friend class AppearanceTests;
    struct Runtime { QString state = "stopped", reason, intentError; quint64 upload = 0, download = 0; };
    struct Draft { QString id, local, remote; bool manualLocal = false; };
    QString profileId;
    bool offline = false, loaded = true, dialogOpen = false, committing = false;
    bool fontLayoutPending = false;
    PortsModel *model;
    QTableView *table;
    QWidget *tableCard = nullptr;
    QLabel *empty, *storageError;
    QPushButton *emptyAdd;
    QWidget *draftRow = nullptr;
    QLineEdit *localInput = nullptr, *remoteInput = nullptr;
    QLabel *localError = nullptr, *remoteError = nullptr, *draftMessage = nullptr;
    Toggle *draftToggle = nullptr;
    QBoxLayout *draftActions = nullptr;
    std::optional<Draft> draft;
    QHash<QString, Runtime> runtime;
    QSet<QString> pendingDelete;
    bool save(const QVector<TunnelRule> &candidate);
    bool validateDraft();
    bool busy(const QString &id) const;
    bool running(const QString &id) const;
    int rowFor(const QString &id) const;
    void refreshRows();
    void positionDraft();
    void sizeColumns();
    void scheduleFontLayout();
    void syncDraftState();
    void toggleRule(const QString &id);
    void showMenu(const QString &id, QPoint global);
    void advanced(const QString &id);
    void remove(const QString &id);
    void reportDraft(const QString &message);
    static bool validPort(const QString &value);
};
QString tunnelEndpoint(const QString &address, quint16 port);
} // namespace rs
