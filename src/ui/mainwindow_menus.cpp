#include "mainwindow.h"
#include "applock.h"
#include "theme.h"
#include "icons.h"
#include "filecontextmenu.h"
#include "layoutmetrics.h"
#include "documentsearch.h"
#include "updatecontroller.h"
#include "../core/operationgate.h"
namespace rs {
void MainWindow::changeTheme(bool light) {applyTheme(light);for(auto w:workspaces)w->refreshTheme();}
namespace {
class ApplicationMenuBar final:public QMenuBar {
  public:explicit ApplicationMenuBar(QWidget *parent):QMenuBar(parent){setNativeMenuBar(false);setMouseTracking(true);setProperty("preservePortDraft",true);setObjectName("applicationMenuBar");bandHeight(this,28);bindUiFont(this);}
  protected:void paintEvent(QPaintEvent *e) override {QMenuBar::paintEvent(e);QPainter p(this);p.fillRect(0,height()-uiPx(1),width(),uiPx(1),colors().divider);}
};
struct MenuContext { QPointer<Workspace> workspace;QPointer<QWidget> focus;quint64 epoch=0; };
}
void MainWindow::buildApplicationMenu() {
    auto bar=new ApplicationMenuBar(this);setMenuBar(bar);
    connect(qApp,&QApplication::focusChanged,this,[this,bar](QWidget *,QWidget *now){
        if(now&&now!=bar&&!qobject_cast<QMenu *>(now)&&!qobject_cast<QMenuBar *>(now)){
            for(auto parent=now;parent;parent=parent->parentWidget())if(parent==this){commandFocus=now;break;}
        }
    });
    auto menu=[&](const QString &name){
        auto m=new FileContextMenu(bar);m->setTitle(name);m->setObjectName("applicationMenu-"+name);m->setProperty("applicationMenu",true);bar->addMenu(m);return m;
    };
    auto connection=menu("连接"),file=menu("文件"),edit=menu("编辑"),view=menu("视图"),terminal=menu("终端"),preferences=menu("设置"),help=menu("帮助");
    using C=Workspace::Command;
    for(auto m:{connection,file,edit,view,terminal,preferences,help}) {
        auto context=std::make_shared<MenuContext>();
        connect(m,&QMenu::aboutToShow,this,[this,m,context]{
            context->workspace=qobject_cast<Workspace *>(tabs->currentWidget());context->focus=commandFocus;context->epoch=OperationGate::token();
            for(auto a:m->actions()){
                if(a->property("workspaceCommand").isValid()) {
                    const auto c=C(a->property("workspaceCommand").toInt());const bool enabled=context->workspace&&context->workspace->commandEnabled(c,context->focus);
                    a->setEnabled(enabled);a->setVisible(c!=C::Paste||enabled);
                }
                if(a->objectName()=="applicationLock")a->setVisible(AppLock::instance()&&AppLock::instance()->security().enabled());
                if(a->objectName()=="advancedSearchAction")a->setEnabled(advancedSearch->canOpen(context->focus));
                if(a->objectName()=="themeDark")a->setChecked(!lightTheme());if(a->objectName()=="themeLight")a->setChecked(lightTheme());
            }
        });
        m->setProperty("contextMenuToken",QVariant::fromValue<qulonglong>(0));
        auto workspaceAction=[this,m,context](Icon glyph,const QString &label,C command){
            auto a=m->addAction(resourceMenuIcon(glyph),label);a->setProperty("workspaceCommand",int(command));
            connect(a,&QAction::triggered,this,[this,context,command]{if(OperationGate::accepts(context->epoch)&&context->workspace&&tabs->currentWidget()==context->workspace)context->workspace->executeCommand(command,context->focus);});return a;
        };
        auto globalAction=[this,m,context](Icon glyph,const QString &label,std::function<void()> run){
            auto a=m->addAction(resourceMenuIcon(glyph),label);
            connect(a,&QAction::triggered,this,[context,run]{if(OperationGate::accepts(context->epoch))run();});return a;
        };
        if(m==connection){
            globalAction(Icon::Connection,"新建连接…",[this]{editProfile();});globalAction(Icon::Explorer,"连接管理",[this]{tabs->setCurrentIndex(0);});m->addSeparator();
            workspaceAction(Icon::Refresh,"重新连接",C::Reconnect);workspaceAction(Icon::Stop,"断开连接…",C::Disconnect);
            m->addSeparator();globalAction(Icon::Close,"退出 FrogShell",[this]{requestExit();});
        }else if(m==file){
            workspaceAction(Icon::NewFile,"新建文件",C::NewFile);workspaceAction(Icon::NewFolder,"新建文件夹",C::NewFolder);m->addSeparator();
            workspaceAction(Icon::Save,"保存\tCtrl+S",C::Save);workspaceAction(Icon::Save,"保存全部",C::SaveAll);m->addSeparator();
            workspaceAction(Icon::Upload,"上传文件…",C::Upload);workspaceAction(Icon::Refresh,"刷新目录",C::Refresh);
        }else if(m==edit){
            workspaceAction(Icon::Undo,"撤销\tCtrl+Z",C::Undo);workspaceAction(Icon::Forward,"重做",C::Redo);m->addSeparator();
            workspaceAction(Icon::Cut,"剪切\tCtrl+X",C::Cut);workspaceAction(Icon::Copy,"复制\tCtrl+C",C::Copy);workspaceAction(Icon::Paste,"粘贴\tCtrl+V",C::Paste);m->addSeparator();workspaceAction(Icon::Eye,"查找\tCtrl+F",C::Find);
            globalAction(Icon::Eye,"高级搜索…\tCtrl+Shift+F",[this,context]{advancedSearch->openFor(context->focus);})->setObjectName("advancedSearchAction");
        }else if(m==view){
            globalAction(Icon::Eye,"搜索结果",[this]{advancedSearch->showResults();});
            workspaceAction(Icon::Status,"服务器状态",C::Status);workspaceAction(Icon::Explorer,"资源管理",C::Explorer);m->addSeparator();
            workspaceAction(Icon::Info,"进程信息",C::Processes);workspaceAction(Icon::Eye,"监听端口",C::Listening);m->addSeparator();
            workspaceAction(Icon::Terminal,"终端面板",C::TerminalPanel);workspaceAction(Icon::Transfers,"端口转发",C::Ports);workspaceAction(Icon::Settings,"服务",C::Services);m->addSeparator();
            globalAction(Icon::Add,"放大\tCtrl++",[]{UiZoom::instance().setLevel(UiZoom::instance().level()+1);});
            globalAction(Icon::Collapse,"缩小\tCtrl+−",[]{UiZoom::instance().setLevel(UiZoom::instance().level()-1);});
            globalAction(Icon::Restore,"恢复 100%\tCtrl+0",[]{UiZoom::instance().setLevel(0);});
        }else if(m==terminal){
            workspaceAction(Icon::Add,"新建终端",C::NewTerminal);workspaceAction(Icon::SplitHorizontal,"拆分终端",C::SplitTerminal);m->addSeparator();
            workspaceAction(Icon::Close,"关闭当前终端",C::CloseTerminal);workspaceAction(Icon::Trash,"清屏",C::ClearTerminal);
        }else if(m==help){
            globalAction(Icon::Info,"关于 FrogShell…",[this]{updates->showAbout(this);})->setObjectName("aboutFrogShellAction");
        }else{
            globalAction(Icon::Settings,"设置…\tCtrl+,",[this]{settings();});m->addSeparator();
            auto themes=new FileContextMenu(m);themes->setTitle("主题");themes->setObjectName("applicationThemeMenu");themes->setProperty("applicationMenu",true);m->addMenu(themes);
            auto dark=themes->addAction("Dracula");dark->setObjectName("themeDark");dark->setCheckable(true);
            auto light=themes->addAction("GitHub Light");light->setObjectName("themeLight");light->setCheckable(true);
            auto choices=new QActionGroup(themes);choices->setExclusive(true);choices->addAction(dark);choices->addAction(light);
            connect(themes,&QMenu::aboutToShow,this,[dark,light]{dark->setChecked(!lightTheme());light->setChecked(lightTheme());});
            connect(dark,&QAction::triggered,this,[this,context]{if(OperationGate::accepts(context->epoch))changeTheme(false);});
            connect(light,&QAction::triggered,this,[this,context]{if(OperationGate::accepts(context->epoch))changeTheme(true);});m->addSeparator();
            globalAction(Icon::Lock,"锁定应用",[]{if(auto lock=AppLock::instance())lock->lock();})->setObjectName("applicationLock");
        }
    }
}
}
