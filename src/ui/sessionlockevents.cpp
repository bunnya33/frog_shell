#include "sessionlockevents.h"
#include <QtWidgets>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wtsapi32.h>
#endif
#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif
#ifdef RABBITSHELL_LOCK_DBUS
#include <QtDBus>
#endif
namespace rs {
class SessionLockEvents::Impl final: public QWidget {
  public:
    SessionLockEvents *owner;
    bool supported=false;
    explicit Impl(SessionLockEvents *o):owner(o){
#ifdef Q_OS_WIN
        if(QGuiApplication::platformName()!="offscreen")supported=WTSRegisterSessionNotification(reinterpret_cast<HWND>(winId()),NOTIFY_FOR_THIS_SESSION);
#elif defined(Q_OS_MACOS)
        CFNotificationCenterAddObserver(CFNotificationCenterGetDistributedCenter(),this,[](CFNotificationCenterRef,void *observer,CFStringRef,const void *,CFDictionaryRef){
            auto self=static_cast<Impl *>(observer);QMetaObject::invokeMethod(self->owner,"lockSignal",Qt::QueuedConnection);
        },CFSTR("com.apple.screenIsLocked"),nullptr,CFNotificationSuspensionBehaviorDeliverImmediately);supported=true;
#elif defined(RABBITSHELL_LOCK_DBUS)
        auto bus=QDBusConnection::sessionBus();
        for(const auto &name:{QString("org.freedesktop.ScreenSaver"),QString("org.gnome.ScreenSaver")}){
            if(bus.interface()&&bus.interface()->isServiceRegistered(name))supported|=bus.connect(name,"/"+QString(name).replace('.','/'),name,"ActiveChanged",owner,SLOT(screenSaver(bool)));
        }
        auto system=QDBusConnection::systemBus();
        if(system.interface()&&system.interface()->isServiceRegistered("org.freedesktop.login1")){
            supported|=system.connect("org.freedesktop.login1","/org/freedesktop/login1","org.freedesktop.login1.Manager","PrepareForSleep",owner,SLOT(screenSaver(bool)));
            QDBusInterface manager("org.freedesktop.login1","/org/freedesktop/login1","org.freedesktop.login1.Manager",system);
            const QDBusReply<QDBusObjectPath> session=manager.call("GetSessionByPID",uint(QCoreApplication::applicationPid()));
            if(session.isValid())supported|=system.connect("org.freedesktop.login1",session.value().path(),"org.freedesktop.login1.Session","Lock",owner,SLOT(lockSignal()));
        }
#endif
    }
    ~Impl()override{
#ifdef Q_OS_WIN
        if(supported)WTSUnRegisterSessionNotification(reinterpret_cast<HWND>(winId()));
#elif defined(Q_OS_MACOS)
        CFNotificationCenterRemoveEveryObserver(CFNotificationCenterGetDistributedCenter(),this);
#endif
    }
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &,void *message,qintptr *)override{
        const auto msg=static_cast<MSG *>(message);
        if(msg->message==WM_WTSSESSION_CHANGE&&(msg->wParam==WTS_SESSION_LOCK||msg->wParam==WTS_CONSOLE_DISCONNECT||msg->wParam==WTS_REMOTE_DISCONNECT))owner->lockSignal();
        if(msg->message==WM_POWERBROADCAST&&msg->wParam==PBT_APMSUSPEND)owner->lockSignal();
        return false;
    }
#endif
};
SessionLockEvents::SessionLockEvents(std::function<void()> cb):callback(std::move(cb)),impl(std::make_unique<Impl>(this)){}
SessionLockEvents::~SessionLockEvents()=default;
quintptr SessionLockEvents::nativeWindow()const{return quintptr(impl->winId());}
void SessionLockEvents::screenSaver(bool active){if(active)callback();}
void SessionLockEvents::lockSignal(){callback();}
QString SessionLockEvents::support()const{
#ifdef Q_OS_WIN
    return impl->supported?QString("已启用 Windows 锁屏、会话断连和休眠锁定。"):QString("当前平台未提供 Windows 锁屏通知；可手动锁定应用。");
#elif defined(Q_OS_MACOS)
    return QString("已接入 macOS 锁屏通知；本构建不保证会话断连和休眠通知，请离开前手动锁定。");
#elif defined(RABBITSHELL_LOCK_DBUS)
    return impl->supported?QString("已接入当前桌面的 D-Bus 锁屏/休眠通知；支持范围取决于桌面会话。"):QString("当前桌面未提供兼容的 D-Bus 锁屏通知；请手动锁定应用。");
#else
    return QString("此构建未接入系统锁屏通知；请在离开前手动锁定应用。");
#endif
}
}
