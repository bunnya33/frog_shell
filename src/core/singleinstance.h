#pragma once
#include <QtCore>
#include <QtNetwork>
namespace rs {
class SingleInstance final : public QObject {
    Q_OBJECT
  public:
    explicit SingleInstance(const QString &directory,QObject *parent=nullptr);
    enum Result { Primary, Activated, Failed };
    Result acquire();
    QString error() const {return problem;}
  signals:
    void activationRequested();
  private:
    QString key,problem;
    QLockFile file;
    QLocalServer server;
};
}
