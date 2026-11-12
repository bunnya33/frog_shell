#pragma once
#include <QtWidgets>
namespace rs {
// Native widgets only. Hidden or inactive workspaces have no busy animation.
class ConnectionNotice final : public QFrame {
  public:
    explicit ConnectionNotice(bool compact,QWidget *parent=nullptr);
    void display(const QString &phase,bool pending,bool animate);
    void refreshTheme();
    QPushButton *retry,*cancel;
    QProgressBar *progress;
  private:
    QLabel *glyph,*message;
    bool compact;
};
}
