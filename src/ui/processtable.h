#pragma once
#include <QtWidgets>
#include <functional>
namespace rs {
// Identity is deliberately separate from Qt::UserRole, which stores numeric sort values.
inline constexpr int ProcessIdRole = Qt::UserRole + 1;
struct ProcessInfo {
    qint64 pid;
    QString name, user, state;
    double cpu;
    quint64 rss;
};
class ProcessTable final : public QTableWidget {
  public:
    explicit ProcessTable(bool compact, QWidget *parent = nullptr);
    ~ProcessTable() override;
    void applySnapshot(const QVector<ProcessInfo> &snapshot, quint64 memoryTotal);
    void setFilterText(const QString &text);
    int visibleProcessCount() const;
    std::function<void(int,int)> countsChanged;

  protected:
    void resizeEvent(QResizeEvent *) override;
    void paintEvent(QPaintEvent *) override;

  private:
    friend class AppearanceTests;
    bool compact;
    bool adjustingWidths = false, customWidths = false;
    QTimer widthSaveTimer;
    QList<qreal> baseWidths;
    QString widthSettingsKey() const;
    void saveWidths();
    QString filter;
    void applyFilter();
};
QWidget *processCard(QTableView *table,QWidget *header=nullptr);
QHeaderView *processHeader(bool compact,QWidget *parent);
} // namespace rs
