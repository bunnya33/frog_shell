#pragma once
#include "../core/session.h"
#include "theme.h"
namespace rs {
class RemoteServicePicker : public QDialog {
    Q_OBJECT
  public:
    using Factory = std::function<Session *(QObject *, Session::Mode)>;
    RemoteServicePicker(Factory factory, const QString &start, bool directories, QWidget *parent = nullptr);
    ~RemoteServicePicker() override;
    QString selectedPath() const { return selected; }
  private:
    friend class ServicesTests;
    friend class AppLockTests;
    Session *worker = nullptr;
    bool directories;
    int next = 0, position = -1;
    QString current, selected;
    QStringList history;
    QTreeWidget *files;
    QLineEdit *path;
    QLabel *message;
    QWidget *crumbs;
    QStackedWidget *address;
    QPushButton *choose;
    QHBoxLayout *crumbLayout;
    void navigate(const QString &p, bool historical = false);
    void acceptSelection();
    bool eventFilter(QObject *, QEvent *) override;
};
class ServicesPanel : public QWidget {
    Q_OBJECT
  public:
    explicit ServicesPanel(QString server, bool demo, QWidget *parent = nullptr);
    ~ServicesPanel() override;
    QPointer<QWidget> toolbar;
    std::function<QString()> mutationGuard;
    RemoteServicePicker::Factory sessionFactory;
    std::function<QString()> directory;
    void setSession(Session *s);
    void setActivity(bool active);
    void stopLocal();
    bool hasConnection() const { return worker || connecting; }
    bool pendingConnection() const { return connecting; }
    void connectionError(const QString &reason);
    void refreshTheme();
    void refresh();
    void newService();
  signals:
    void connectionRequested();
  protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
  private:
    friend class ServicesTests;
    friend class AppLockTests;
    friend class MenuTests;
    friend class AppearanceTests;
    QString server;
    bool demo, active = true, connecting = false, busy = false, ready = false, refreshAfter = false;
    int next = 0, requestId = 0;
    QString pendingAction, pendingUnit, pressedUnit;
    bool interacting = false;
    bool fontLayoutPending = false;
    QPointer<Session> worker;
    QTimer timer;
    QLineEdit *search;
    QComboBox *filter;
    QTreeWidget *table;
    QLabel *status;
    QWidget *rowActions;
    QHash<QString,QTreeWidgetItem *> items;
    QPointer<QDialog> form;
    QPointer<QPlainTextEdit> formReport;
    QPointer<QPushButton> saveForm;
    bool submit(const QString &action, QVariantMap req = {});
    void deliver(const QString &action, const QString &unit, const QVariantMap &result);
    void snapshot(const QVariantList &rows);
    void filterRows();
    void menu(const QString &unit, QPoint global);
    void action(const QString &action, const QString &unit);
    void positionActions();
    void scheduleFontLayout();
    void syncTimer();
    void feedback(const QString &error = {});
    void textDialog(const QString &title, const QString &text);
};
}
