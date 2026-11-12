#include "applock.h"
#include "sessionlockevents.h"
#include "theme.h"
#include "icons.h"
#include "mainwindow.h"
namespace rs {
namespace {
QPointer<AppLock> controller;
class PasswordField final: public QLineEdit {
  public:
    explicit PasswordField(QWidget *parent=nullptr):QLineEdit(parent){
        setEchoMode(QLineEdit::Password);setMaxLength(128);setContextMenuPolicy(Qt::NoContextMenu);
        setInputMethodHints(Qt::ImhHiddenText|Qt::ImhSensitiveData|Qt::ImhNoPredictiveText|Qt::ImhNoAutoUppercase);
        setAccessibleName("应用锁密码");uiFixedHeight(this,36);
    }
    void keyPressEvent(QKeyEvent *e)override{
        if(e->matches(QKeySequence::Copy)||e->matches(QKeySequence::Cut)){e->accept();return;}
        QLineEdit::keyPressEvent(e);
    }
};
}
class LockSurface final:public QWidget {
  public:
    QWidget *panel;
    PasswordField *password;
    QLabel *message;
    QPushButton *submit;
    explicit LockSurface(AppLock *owner,QWidget *parent):QWidget(parent){
        setObjectName("applicationLockSurface");setProperty("applicationLockTrusted",true);
        setAttribute(Qt::WA_OpaquePaintEvent);setAutoFillBackground(true);setFocusPolicy(Qt::StrongFocus);
        setAccessibleName("FrogShell 已锁定");
        auto root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);
        auto scroll=new QScrollArea;scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);root->addWidget(scroll);
        auto content=new QWidget;scroll->setWidget(content);auto outer=new QVBoxLayout(content);uiMargins(outer,20,20,20,20);outer->addStretch();
        panel=new QWidget;uiMaximumWidth(panel,420);auto l=new QVBoxLayout(panel);uiMargins(l,24,24,24,24);uiSpacing(l,14);
        auto symbol=new QLabel;symbol->setPixmap(icon(Icon::Lock).pixmap(uiSize(40,40)));symbol->setAlignment(Qt::AlignCenter);l->addWidget(symbol);
        auto heading=new QLabel("FrogShell 已锁定");heading->setAlignment(Qt::AlignCenter);bindUiFont(heading,18,QFont::Medium);l->addWidget(heading);
        auto note=new QLabel("输入应用密码以恢复所有窗口。\n连接、终端和传输继续运行。");note->setWordWrap(true);note->setProperty("muted",true);l->addWidget(note);
        password=new PasswordField;password->setObjectName("applicationUnlockPassword");password->setPlaceholderText("应用密码");l->addWidget(password);
        message=new QLabel;message->setTextFormat(Qt::PlainText);message->setWordWrap(true);message->setAccessibleName("解锁反馈");l->addWidget(message);
        submit=button("解锁");submit->setProperty("primary",true);submit->setObjectName("applicationUnlock");l->addWidget(submit);
        auto exit=button(owner->startup?"退出应用":"解锁后退出");exit->setObjectName("applicationLockedExit");l->addWidget(exit);
        connect(submit,&QPushButton::clicked,owner,[owner,this]{auto bytes=password->text().toUtf8();password->clear();owner->unlock(std::move(bytes));});
        connect(password,&QLineEdit::returnPressed,submit,&QPushButton::click);
        connect(exit,&QPushButton::clicked,owner,[owner,this]{
            if(owner->startup){qApp->quit();return;}
            owner->exitAfterUnlock=true;message->setText("请先解锁，再确认未保存文件和退出操作。");password->setFocus();
        });
        outer->addWidget(panel,0,Qt::AlignHCenter);outer->addStretch();
    }
  protected:
    void resizeEvent(QResizeEvent *event)override{QWidget::resizeEvent(event);panel->setFixedWidth(qMax(1,qMin(uiPx(420),width()-uiPx(40))));}
    void paintEvent(QPaintEvent *)override{QPainter p(this);p.fillRect(rect(),colors().background);}
};
AppLock *AppLock::instance(){return controller;}
AppLock::AppLock(const QString &directory,QObject *parent):QObject(parent),store(directory,this){
    Q_ASSERT(!controller);controller=this;
    qApp->installEventFilter(this);
    systemEvents=std::make_unique<SessionLockEvents>([this]{simulateSystemLock();});
    connect(&store,&SecurityStore::changed,this,[this]{if(store.damaged())lock();});
    connect(&store,&SecurityStore::completed,this,[this](SecurityStore::Action action,bool ok,const QString &error){
        if(action!=SecurityStore::Verify){if(!ok&&store.enabled()&&(store.damaged()||error.startsWith("密码保护已写入")||error.startsWith("应用锁配置已写入"))){lock();notifySurfaces(error);}return;}
        if(!locked())return;
        if(verifyEpoch!=OperationGate::token()){notifySurfaces("锁定状态已更新，请重新输入密码。");return;}
        if(!ok){notifySurfaces(error);return;}
        OperationGate::setLocked(false);restore();emit lockedChanged(false);emit unlocked();
        if(exitAfterUnlock){exitAfterUnlock=false;QTimer::singleShot(0,this,[]{for(auto w:QApplication::topLevelWidgets())if(auto main=qobject_cast<MainWindow *>(w)){main->showFromTray();main->requestExit();break;}});}
    });
    if(store.enabled())OperationGate::lockAgain();
}
AppLock::~AppLock(){qApp->removeEventFilter(this);systemEvents.reset();if(locked()){OperationGate::setLocked(false);restore();}controller=nullptr;}
QString AppLock::platformSupport()const{return systemEvents->support();}
void AppLock::simulateSystemLock(){if(store.enabled())lock();else if(store.busy())store.cancel(store.requestId());}
bool AppLock::trusted(QObject *object)const{
    for(auto w=object;w;w=w->parent())if(w->property("applicationLockTrusted").toBool())return true;
    return false;
}
void AppLock::concealChildren(Shield &shield){
    const auto children=shield.window->findChildren<QWidget *>(QString(),Qt::FindDirectChildrenOnly);
    for(auto child:children){
        if(trusted(child)||child->isWindow())continue;
        bool known=false;for(const auto &saved:shield.children)if(saved.widget==child){known=true;break;}
        if(!known)shield.children.append({child,child->isHidden(),!child->testAttribute(Qt::WA_ForceDisabled)});
        child->setEnabled(false);child->hide();
    }
}
void AppLock::cover(QWidget *window){
    if(!window||!window->isWindow()||trusted(window)||window->windowType()==Qt::ToolTip||window->windowType()==Qt::Popup)return;
    for(auto &shield:shields)if(shield.window==window){concealChildren(shield);shield.surface->setGeometry(window->rect());shield.surface->raise();window->setEnabled(true);return;}
    Shield shield;shield.window=window;shield.title=window->windowTitle();shield.name=window->accessibleName();shield.description=window->accessibleDescription();shield.enabled=!window->testAttribute(Qt::WA_ForceDisabled);
    window->setWindowTitle("FrogShell · 已锁定");window->setAccessibleName("FrogShell 已锁定");window->setAccessibleDescription({});
    concealChildren(shield);shield.surface=new LockSurface(this,window);shield.surface->setGeometry(window->rect());
    shields.append(shield);window->setEnabled(true);shield.surface->show();shield.surface->raise();
    if(store.damaged())shield.surface->message->setText(store.error());
}
void AppLock::lock(){
    if(!store.enabled())return;
    if(!locked())priorFocus=QApplication::focusWidget();
    OperationGate::lockAgain();exitAfterUnlock=false;
    QScopedValueRollback<bool> guard(updating,true);
    QDrag::cancel();
    if(auto popup=QApplication::activePopupWidget())popup->close();
    const auto windows=QApplication::topLevelWidgets();
    for(auto w:windows){
        if(trusted(w))continue;
        if(auto menu=qobject_cast<QMenu *>(w)){menu->close();continue;}
        // Reject native dialogs and modal results; hiding an exec() dialog would leave its operation alive.
        if(auto dialog=qobject_cast<QDialog *>(w);dialog&&(dialog->isModal()||qobject_cast<QFileDialog *>(dialog)))dialog->QDialog::done(QDialog::Rejected);
    }
    for(auto w:QApplication::topLevelWidgets())if(w->isVisible()||qobject_cast<QMainWindow *>(w))cover(w);
    // A parent content widget may also disable a separate Qt::Window.
    // Re-enable only the covered windows after all underlying content is disabled.
    for(const auto &shield:shields)if(shield.window)shield.window->setEnabled(true);
    notifySurfaces();emit lockedChanged(true);activate();
}
void AppLock::notifySurfaces(const QString &message){
    for(auto &shield:shields)if(shield.surface){
        shield.surface->password->clear();shield.surface->submit->setEnabled(!store.busy()&&!store.damaged());
        shield.surface->message->setText(store.damaged()?store.error():message);
    }
}
void AppLock::unlock(QByteArray password){
    if(!locked()){SecurityStore::wipe(password);return;}
    if(store.busy()){SecurityStore::wipe(password);return;}
    verifyEpoch=OperationGate::token();
    store.request(SecurityStore::Verify,std::move(password));
    if(store.busy())notifySurfaces("正在校验…");
}
void AppLock::restore(){
    QScopedValueRollback<bool> guard(updating,true);
    auto saved=std::move(shields);shields.clear();
    for(auto &shield:saved)if(shield.window){
        if(shield.surface){shield.surface->hide();shield.surface->deleteLater();}
        shield.window->setWindowTitle(shield.title);shield.window->setAccessibleName(shield.name);shield.window->setAccessibleDescription(shield.description);
        for(const auto &child:shield.children)if(child.widget){child.widget->setEnabled(child.enabled);if(!child.hidden)child.widget->show();}
    }
    for(const auto &shield:saved)if(shield.window)shield.window->setEnabled(shield.enabled);
    if(startup){startup->hide();startup->deleteLater();startup=nullptr;}
    if(priorFocus)priorFocus->setFocus();
}
void AppLock::showStartup(){
    if(!locked())return;
    startup=new QWidget;startup->setObjectName("applicationLockStartup");startup->resize(720,480);startup->setMinimumSize(320,280);
    startup->setWindowTitle("FrogShell · 已锁定");cover(startup);startup->show();activate();
}
void AppLock::activate(){
    if(locked()){
        QWidget *target=nullptr;for(auto &shield:shields)if(shield.window&&qobject_cast<QMainWindow *>(shield.window)){target=shield.window;break;}
        if(!target)for(auto &shield:shields)if(shield.window){target=shield.window;if(target->isVisible())break;}
        if(!target&&startup)target=startup;
        if(target){target->show();target->raise();target->activateWindow();for(auto &shield:shields)if(shield.window==target&&shield.surface){shield.surface->raise();shield.surface->password->setFocus();}}
    }else for(auto w:QApplication::topLevelWidgets())if(auto main=qobject_cast<MainWindow *>(w)){main->showFromTray();break;}
}
bool AppLock::eventFilter(QObject *object,QEvent *event){
    if(!locked()||updating)return false;
    auto widget=qobject_cast<QWidget *>(object);
    if(widget&&widget->isWindow()&&event->type()==QEvent::Close&&!trusted(widget)){
        if(qobject_cast<MainWindow *>(widget))return false; // MainWindow chooses a reachable tray or keeps the lock visible.
        static_cast<QCloseEvent *>(event)->ignore();widget->hide();return true;
    }
    if(widget&&!trusted(widget)){
        if(event->type()==QEvent::Show){
            QScopedValueRollback<bool> guard(updating,true);
            if(auto popup=qobject_cast<QMenu *>(widget)){popup->close();return true;}
            if(auto dialog=qobject_cast<QDialog *>(widget);dialog&&(dialog->isModal()||qobject_cast<QFileDialog *>(dialog))){
                cover(dialog);dialog->hide();
                // exec() installs its nested loop after show(); reject again once that loop is live.
                QTimer::singleShot(0,dialog,[dialog]{dialog->QDialog::done(QDialog::Rejected);});
                return true;
            }
            if(widget->isWindow())cover(widget);
            else for(auto &shield:shields)if(shield.window==widget->window()){concealChildren(shield);break;}
        }else if(event->type()==QEvent::Resize&&widget->isWindow()){
            QScopedValueRollback<bool> guard(updating,true);cover(widget);
        }else if(event->type()==QEvent::WindowTitleChange&&widget->isWindow()){
            QScopedValueRollback<bool> guard(updating,true);widget->setWindowTitle("FrogShell · 已锁定");
        }
    }
    if(trusted(object)||qobject_cast<QWindow *>(object))return false;
    switch(event->type()){
    case QEvent::KeyPress:case QEvent::KeyRelease:case QEvent::Shortcut:case QEvent::ShortcutOverride:
    case QEvent::MouseButtonPress:case QEvent::MouseButtonRelease:case QEvent::MouseButtonDblClick:case QEvent::MouseMove:
    case QEvent::Wheel:case QEvent::TouchBegin:case QEvent::TouchUpdate:case QEvent::TouchEnd:
    case QEvent::DragEnter:case QEvent::DragMove:case QEvent::Drop:case QEvent::InputMethod:case QEvent::ContextMenu:
        event->accept();return true;
    default:break;
    }
    return false;
}
void AppLock::passwordDialog(SecurityStore::Action action,QWidget *parent){
    if(locked()||store.busy())return;
    const auto epoch=OperationGate::token();quint64 request=0;QDialog dialog(parent);dialog.setWindowTitle(action==SecurityStore::Enable?"启用应用锁":action==SecurityStore::Change?"修改应用密码":"关闭应用锁");
    auto layout=new QVBoxLayout(&dialog);uiMargins(layout,24,20,24,20);uiSpacing(layout,12);
    PasswordField *current=nullptr,*replacement=nullptr,*confirmation=nullptr;
    if(action!=SecurityStore::Enable){layout->addWidget(new QLabel("当前密码"));current=new PasswordField;current->setObjectName("lockCurrentPassword");layout->addWidget(current);}
    if(action!=SecurityStore::Disable){layout->addWidget(new QLabel("新密码（6–128 个字符）"));replacement=new PasswordField;replacement->setObjectName("lockNewPassword");layout->addWidget(replacement);layout->addWidget(new QLabel("确认密码"));confirmation=new PasswordField;confirmation->setObjectName("lockConfirmPassword");layout->addWidget(confirmation);}
    auto status=new QLabel;status->setTextFormat(Qt::PlainText);status->setWordWrap(true);layout->addWidget(status);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);buttons->button(QDialogButtonBox::Ok)->setText("确认");
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&,epoch]{
        if(!OperationGate::accepts(epoch))return;
        if(replacement&&replacement->text()!=confirmation->text()){status->setText("两次输入的密码不一致。");return;}
        auto old=current?current->text().toUtf8():QByteArray();auto next=replacement?replacement->text().toUtf8():QByteArray();
        if(current)current->clear();if(replacement)replacement->clear();if(confirmation)confirmation->clear();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);status->setText("正在校验并保存…");store.request(action,std::move(old),std::move(next));request=store.requestId();
    });
    connect(&store,&SecurityStore::completed,&dialog,[&](SecurityStore::Action completed,bool ok,const QString &error){if(completed!=action)return;if(!OperationGate::accepts(epoch))return;if(ok)dialog.accept();else{status->setText(error);buttons->button(QDialogButtonBox::Ok)->setEnabled(true);}});
    dialog.resize(uiPx(440),dialog.sizeHint().height());dialog.exec();if(request&&store.busy())store.cancel(request);
}
void AppLock::addSettings(QVBoxLayout *layout,QWidget *dialog){
    auto heading=new QLabel("安全 · 应用锁");heading->setProperty("heading",true);layout->addWidget(heading);
    auto row=new QHBoxLayout;auto label=new QLabel("启用应用密码锁");auto toggle=new Toggle;toggle->setObjectName("applicationLockToggle");toggle->setAccessibleName("启用应用密码锁");toggle->syncChecked(store.enabled());row->addWidget(label);row->addStretch();row->addWidget(toggle);layout->addLayout(row);
    auto description=new QLabel("保护应用内的操作；不会加密本地文件或替代系统账户保护。界面重置保留应用锁。");description->setWordWrap(true);description->setProperty("muted",true);layout->addWidget(description);
    auto support=new QLabel(platformSupport());support->setWordWrap(true);support->setProperty("muted",true);layout->addWidget(support);
    auto change=button("修改应用密码");change->setObjectName("changeApplicationPassword");change->setEnabled(store.enabled());layout->addWidget(change,0,Qt::AlignLeft);
    auto sync=[this,toggle,change]{toggle->syncChecked(store.enabled());toggle->setEnabled(!store.busy()&&!store.damaged());change->setEnabled(store.enabled()&&!store.busy()&&!store.damaged());};
    connect(&store,&SecurityStore::changed,toggle,sync);connect(&store,&SecurityStore::completed,toggle,[sync](SecurityStore::Action,bool,const QString &){sync();});
    connect(toggle,&QAbstractButton::clicked,dialog,[this,dialog,sync]{passwordDialog(store.enabled()?SecurityStore::Disable:SecurityStore::Enable,dialog);sync();});
    connect(change,&QPushButton::clicked,dialog,[this,dialog]{passwordDialog(SecurityStore::Change,dialog);});
}
}
