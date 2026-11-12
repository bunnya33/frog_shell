#include "singleinstance.h"
namespace rs {
namespace {
QString canonical(const QString &path){
    QDir().mkpath(path);auto value=QFileInfo(path).canonicalFilePath();
    if(value.isEmpty())value=QDir(path).absolutePath();
#ifdef Q_OS_WIN
    value=value.toCaseFolded();
#endif
    return value;
}
}
SingleInstance::SingleInstance(const QString &directory,QObject *parent):QObject(parent),
    key("rabbitshell-"+QString::fromLatin1(QCryptographicHash::hash(canonical(directory).toUtf8(),QCryptographicHash::Sha256).toHex())),
    file(QDir(canonical(directory)).filePath("instance.lock")){
    file.setStaleLockTime(0);server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server,&QLocalServer::newConnection,this,[this]{
        while(server.hasPendingConnections()){
            auto socket=server.nextPendingConnection();socket->setParent(this);
            // Connection alone is the wake request. There is no command or password protocol.
            emit activationRequested();socket->disconnectFromServer();socket->deleteLater();
        }
    });
}
SingleInstance::Result SingleInstance::acquire(){
    if(!file.tryLock(0)){
        QLocalSocket socket;socket.connectToServer(key);
        if(socket.waitForConnected(1500)){socket.disconnectFromServer();return Activated;}
        problem="此数据目录已有实例正在启动或无法响应。请稍后重试；不会绕过应用锁打开另一个实例。";return Failed;
    }
    QLocalServer::removeServer(key); // Only the process holding the data-directory lock may remove a stale endpoint.
    if(!server.listen(key)){problem="无法建立本地单实例唤起通道。";return Failed;}
    return Primary;
}
}
