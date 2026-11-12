#pragma once
#include "../core/listeningports.h"
#include <QtWidgets>
#include <functional>
namespace rs {
class ListeningTable final : public QTableWidget {
  public:
    explicit ListeningTable(bool compact,QWidget *parent=nullptr);
    ~ListeningTable() override;
    void applySnapshot(const listening::Snapshot &snapshot);
    void setStatus(const QString &status);
    void setFilterText(const QString &text);
    std::function<void(int,int)> countsChanged;
  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
  private:
    bool compact;
    QString status="等待采集";
    QString filter;
    QList<qreal> baseWidths;
    QTimer widthSaveTimer;
    void applyFilter();
    void saveWidths();
};
}
