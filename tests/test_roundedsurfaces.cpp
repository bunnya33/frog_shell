#include "../src/ui/roundedsurface.h"
#include "../src/ui/theme.h"
#include "../src/ui/processtable.h"
#include "../src/ui/services.h"
#include "../src/ui/ports.h"
#include "../src/ui/styledcombobox.h"
#include "../src/ui/windowstyle.h"
#include "../src/ui/filecontextmenu.h"
#include <QtTest>
#include <cmath>
#include <numbers>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif
namespace rs {
class HiddenWindows final : public QObject {
    bool eventFilter(QObject *o,QEvent *e) override {
        if(e->type()==QEvent::Polish)if(auto w=qobject_cast<QWidget *>(o);w&&w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
class RoundedTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    QString report;
    void settle(){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();}
    QImage render(QWidget &w,qreal dpr) {
        QImage image(QSize(qCeil(w.width()*dpr),qCeil(w.height()*dpr)),QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);image.fill(Qt::transparent);w.render(&image);return image;
    }
    void corners(const QImage &image,qreal radius,qreal width,QColor color,QColor leftStrip = {}) {
        // Probe all four quarter arcs, including their joins to straight edges.
        // Search neighboring raster samples to allow subpixel/DPR antialiasing.
        const auto dpr=image.devicePixelRatio();
        const QSizeF size(image.width()/dpr,image.height()/dpr);
        for(int corner=0;corner<4;++corner)for(int step=0;step<=8;++step) {
            const qreal angle=step*std::numbers::pi/16;
            QPointF p(radius-(radius-width/2)*std::cos(angle),radius-(radius-width/2)*std::sin(angle));
            if(corner&1)p.setX(size.width()-p.x());if(corner&2)p.setY(size.height()-p.y());
            const QColor expected = leftStrip.isValid() && p.x() < uiReal(2) ? leftStrip : color;
            int best=1000;
            for(int y=qRound(p.y()*dpr)-1;y<=qRound(p.y()*dpr)+1;++y)
                for(int x=qRound(p.x()*dpr)-1;x<=qRound(p.x()*dpr)+1;++x) {
                    if(!image.rect().contains(x,y))continue;const auto c=image.pixelColor(x,y);if(c.alpha()<100)continue;
                    best=qMin(best,qAbs(c.red()-expected.red())+qAbs(c.green()-expected.green())+qAbs(c.blue()-expected.blue()));
                }
            // A <1 physical-pixel stroke has lower maximum raster coverage.
            const auto tolerance=qCeil(72/qMin(qreal(1),width*dpr));
            QVERIFY2(best<tolerance,qPrintable(QString("arc missing corner=%1 sample=%2 error=%3 tolerance=%4").arg(corner).arg(step).arg(best).arg(tolerance)));
        }
    }
  private slots:
    void nativeCompositedMenuCorners(){
#ifdef Q_OS_WIN
        if(QGuiApplication::platformName()!="windows"||!qEnvironmentVariableIsSet("RABBITSHELL_COMPOSITE_PROBE"))QSKIP("Explicit isolated Windows compositing probe required");
        const auto prior=GetForegroundWindow();
        struct Restore{HWND prior;~Restore(){if(prior&&IsWindow(prior))SetForegroundWindow(prior);}} restore{prior};
        for(bool light:{false,true}){
            applyTheme(light);UiZoom::instance().setLevel(0);
            class FixedUnderlay final:public QWidget {void paintEvent(QPaintEvent *) override {QPainter p(this);p.fillRect(rect(),QColor("#90c8e0"));}} host;
            host.setWindowTitle("RabbitShell isolated corner probe");host.resize(480,320);
            const auto screen=QGuiApplication::primaryScreen();host.move(screen->availableGeometry().center()-QPoint(240,160));host.show();host.raise();host.activateWindow();settle();
            const bool priorEffect=QApplication::isEffectEnabled(Qt::UI_FadeMenu);QApplication::setEffectEnabled(Qt::UI_FadeMenu,true);
            FileContextMenu menu(&host);menu.setProperty("applicationMenu",true);menu.addAction("复制文件");menu.addAction("上传文件…");menu.addSeparator();menu.addAction("删除…");menu.ensurePolished();menu.adjustSize();
            const auto popupPoint=host.mapToGlobal(QPoint(100,80));QTest::qWait(100);DwmFlush();
            auto under=screen->grabWindow(0,popupPoint.x(),popupPoint.y(),menu.width(),menu.height()).toImage();QVERIFY(!under.isNull());
            const QColor back("#90c8e0");auto delta=[&](QColor c){return qMax(qAbs(c.red()-back.red()),qMax(qAbs(c.green()-back.green()),qAbs(c.blue()-back.blue())));};
            for(auto p:{under.rect().topLeft(),under.rect().topRight(),under.rect().bottomLeft(),under.rect().bottomRight(),under.rect().center()})QVERIFY2(delta(under.pixelColor(p))<=2,qPrintable(under.pixelColor(p).name()));
            QVERIFY(under.save(report+QString("/menu-composited-underlay-%1.png").arg(light?"light":"dark")));
            menu.popup(popupPoint);settle();QTest::qWait(40);DwmFlush();
            const auto during=menu.mapToGlobal(QPoint());const auto mid=screen->grabWindow(0,during.x(),during.y(),menu.width(),menu.height()).toImage();QVERIFY(!mid.isNull());
            for(auto point:{mid.rect().topLeft(),mid.rect().topRight(),mid.rect().bottomLeft(),mid.rect().bottomRight()}){const auto c=mid.pixelColor(point);QVERIFY2(delta(c)<=8,qPrintable(c.name()));}
            QVERIFY(mid.save(report+QString("/menu-composited-animation-%1.png").arg(light?"light":"dark")));QTest::qWait(130);DwmFlush();
            const auto p=menu.mapToGlobal(QPoint());auto image=screen->grabWindow(0,p.x(),p.y(),menu.width(),menu.height()).toImage();QVERIFY(!image.isNull());
            QVERIFY(menu.windowFlags().testFlag(Qt::FramelessWindowHint));QVERIFY(menu.testAttribute(Qt::WA_TranslucentBackground));
            const auto dpr=image.devicePixelRatio();
            for(auto point:{QPoint(0,0),QPoint(menu.width()-1,0),QPoint(0,menu.height()-1),QPoint(menu.width()-1,menu.height()-1)}){
                const auto c=image.pixelColor(qFloor(point.x()*dpr),qFloor(point.y()*dpr));
                // Native shadows can darken the underlay slightly. An opaque
                // black rectangular corner or menu-colored fill cannot pass.
                QVERIFY2(delta(c)<=8,qPrintable(c.name()));QVERIFY(c!=colors().side);
            }
            QVERIFY(image.save(report+QString("/menu-composited-%1.png").arg(light?"light":"dark")));
            QJsonObject result{{"platform","windows"},{"desktopComposite",true},{"opaqueBlackCorners",false},{"alphaReady",menu.property("roundedNativeAlphaReady").toBool()},{"regionFallback",menu.property("roundedPopupFallback").toBool()}};
            result["animationTestEnabledInFixture"]=true;result["validatedUnderlay"]=back.name();result["cornerMaxChannelDelta"]=8;QFile file(report+QString("/menu-composited-%1.json").arg(light?"light":"dark"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(result).toJson());menu.close();host.close();QApplication::setEffectEnabled(Qt::UI_FadeMenu,priorEffect);
        }
#else
        QSKIP("Windows only");
#endif
    }
    void nativeMenuBarHover(){
#ifdef Q_OS_WIN
        if(QGuiApplication::platformName()!="windows"||!qEnvironmentVariableIsSet("RABBITSHELL_COMPOSITE_PROBE"))QSKIP("Explicit isolated Windows menu-bar probe required");
        const auto prior=GetForegroundWindow();struct Restore{HWND h;~Restore(){if(h&&IsWindow(h))SetForegroundWindow(h);}} restore{prior};
        QWidget host;host.setWindowTitle("RabbitShell isolated menu input probe");host.resize(480,320);auto layout=new QVBoxLayout(&host);auto bar=new QMenuBar;bar->setNativeMenuBar(false);layout->setMenuBar(bar);layout->addStretch();
        FileContextMenu first(&host),second(&host);first.setTitle("连接");second.setTitle("文件");first.addAction("新建连接");second.addAction("新建文件");bar->addMenu(&first);bar->addMenu(&second);
        host.show();host.raise();host.activateWindow();QTRY_COMPARE(qApp->focusWindow(),host.windowHandle());
        auto point=[&](int i){return bar->mapTo(&host,bar->actionGeometry(bar->actions()[i]).center());};
        QTest::mouseClick(host.windowHandle(),Qt::LeftButton,{},point(0));QTRY_VERIFY(first.isVisible());
        QTest::mouseMove(host.windowHandle(),point(1));QTRY_VERIFY(second.isVisible());QVERIFY(!first.isVisible());
        QTest::keyClick(second.windowHandle(),Qt::Key_Escape);QTRY_VERIFY(!second.isVisible());host.close();
#else
        QSKIP("Windows only");
#endif
    }
    void initTestCase() {
        QVERIFY(data.isValid());qputenv("RABBITSHELL_DATA_DIR",data.path().toUtf8());QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellRoundedTests");QCoreApplication::setApplicationName("Private");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,data.path());
        QApplication::setStyle("Fusion");installWindowStyle();
        if(QFileInfo::exists("C:/Windows/Fonts/msyh.ttc"))QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
        QApplication::setFont(QFont("Microsoft YaHei UI",10));report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",data.path());
    }
    void surfaces_data() {
        QTest::addColumn<bool>("light");QTest::addColumn<int>("zoom");QTest::addColumn<qreal>("dpr");
        for(bool light:{false,true})for(int zoom:{-1,0,1})for(qreal dpr:{1.,1.5,2.})
            QTest::newRow(qPrintable(QString("%1-z%2-dpr%3").arg(light?"light":"dark").arg(zoom).arg(dpr)))<<light<<zoom<<dpr;
    }
    void surfaces() {
        QFETCH(bool,light);QFETCH(int,zoom);QFETCH(qreal,dpr);applyTheme(light);UiZoom::instance().setLevel(zoom);
        QWidget parent;parent.resize(400,250);parent.show();
        QTreeWidget table(&parent);table.setGeometry(20,20,220,150);table.setHeaderLabels({"名称","路径"});
        new QTreeWidgetItem(&table,{"目录","/中文 folder"});setFrameRole(&table,"card");table.show();settle();
        auto frame=table.findChild<QWidget *>("roundedSurfaceBorder",Qt::FindDirectChildrenOnly);QVERIFY(frame);QVERIFY(frame->testAttribute(Qt::WA_TransparentForMouseEvents));
        auto image=render(table,dpr);corners(image,uiReal(8),uiReal(1),colors().cardBorder);
        QCOMPARE(table.mask(),QRegion());QCOMPARE(table.findChildren<QWidget *>("roundedSurfaceBorder").size(),1);
        setFrameRole(&table,"transfer");settle();
        corners(render(table,dpr),uiReal(5),uiReal(1),colors().panelBorder);
        setFrameRole(&table,"find");settle();
        corners(render(table,dpr),uiReal(8),uiReal(1),colors().panelBorder,QColor(light?"#afb8c1":"#494b55"));
        table.setObjectName("breadcrumbs");installRoundedSurface(&table,RoundedRole::Path);table.setProperty("uploadTarget",true);settle();
        corners(render(table,dpr),uiReal(5),uiReal(2),colors().accent);
        table.setProperty("uploadTarget",false);settle();corners(render(table,dpr),uiReal(5),uiReal(1),colors().line);
        QMenu menu;menu.addAction("目录操作");menu.addSeparator();menu.addAction("删除…");menu.popup(QPoint(30,30));settle();
        corners(render(menu,dpr),uiReal(6),uiReal(1),colors().line);
        StyledComboBox combo(&parent);combo.addItems({"默认选项","中文目录"});combo.setGeometry(20,180,220,40);combo.show();combo.showPopup();settle();
        auto popup=combo.view()->window();QVERIFY(popup!=parent.window());QCOMPARE(popup->windowType(),Qt::Popup);
        QVERIFY(popup->testAttribute(Qt::WA_TranslucentBackground));QVERIFY(!parent.testAttribute(Qt::WA_TranslucentBackground));
        corners(render(*popup,dpr),uiReal(6),uiReal(1),colors().cardBorder);
        if(dpr==1.5) {
            const auto suffix=QString("%1-z%2-dpr1.5.png").arg(light?"light":"dark").arg(zoom);
            QVERIFY(render(menu,dpr).save(report+"/rounded-menu-"+suffix));QVERIFY(render(*popup,dpr).save(report+"/rounded-combo-"+suffix));
        }
        combo.hidePopup();menu.close();
    }
    void fallbackContourLifecycle() {
        applyTheme(false);UiZoom::instance().setLevel(0);QWidget popup(nullptr,Qt::Popup);popup.setAttribute(Qt::WA_TranslucentBackground);
        installRoundedSurface(&popup,RoundedRole::ComboPopup);popup.resize(170,100);popup.show();settle();
        const auto unmasked=render(popup,1);popup.setProperty("roundedPopupFallback",true);settle();QVERIFY(!popup.mask().isEmpty());
        const auto masked=render(popup,1);
        // The conservative region must preserve every substantially painted client pixel.
        for(int y=0;y<unmasked.height();++y)for(int x=0;x<unmasked.width();++x)
            if(unmasked.pixelColor(x,y).alpha()>128)QVERIFY(masked.pixelColor(x,y).alpha()>100);
        const auto old=popup.mask();popup.resize(240,140);settle();QVERIFY(popup.mask()!=old);
        UiZoom::instance().setLevel(1);settle();QCOMPARE(popup.mask(),roundedPopupRegion(popup.rect(),uiReal(6),popup.devicePixelRatioF()));
        popup.setProperty("roundedPopupFallback",false);settle();QVERIFY(popup.mask().isEmpty());
        UiZoom::instance().setLevel(0);
    }
    void actualCardsAndPopup() {
        UiZoom::instance().setLevel(0);
        for(bool light:{false,true}) {
            applyTheme(light);QWidget gallery;gallery.resize(1000,650);auto grid=new QGridLayout(&gallery);
            auto process=new ProcessTable(false);grid->addWidget(processCard(process),0,0);
            auto service=new ServicesPanel("合成服务",true);grid->addWidget(service,0,1);
            auto ports=new PortsPanel("rounding-fixture",true);grid->addWidget(ports,1,0,1,2);gallery.show();settle();
            auto processFrame=process->parentWidget();corners(render(*processFrame,2),uiReal(8),uiReal(1),colors().cardBorder);
            auto serviceTable=service->findChild<QTreeWidget *>("serviceTable");QVERIFY(serviceTable);
            corners(render(*serviceTable->parentWidget(),2),uiReal(8),uiReal(1),colors().cardBorder);
            QVERIFY(gallery.grab().save(report+QString("/rounded-cards-%1.png").arg(light?"light":"dark")));
            QMenu menu;menu.addAction("打开文件");menu.addSeparator();auto sub=menu.addMenu("更多操作");sub->addAction("子菜单项");menu.addAction("删除…");
            if(QGuiApplication::platformName()=="windows"){menu.ensurePolished();menu.winId();}
            menu.popup(QPoint(40,40));settle();QVERIFY(menu.testAttribute(Qt::WA_TranslucentBackground));
            corners(menu.grab().toImage(),uiReal(6),uiReal(1),colors().line);
            QVERIFY(menu.grab().save(report+QString("/rounded-menu-%1-%2.png").arg(light?"light":"dark",QGuiApplication::platformName())));
            QTest::keyClick(&menu,Qt::Key_Down);QVERIFY(menu.activeAction());
            menu.setActiveAction(sub->menuAction());QTest::keyClick(&menu,Qt::Key_Right);settle();QVERIFY(sub->isVisible());
            QTest::keyClick(sub,Qt::Key_Escape);QVERIFY(!sub->isVisible());QVERIFY(menu.isVisible());
            QTest::keyClick(&menu,Qt::Key_Escape);QVERIFY(!menu.isVisible());
            StyledComboBox combo;combo.addItems({"默认选项","中文目录","第三项"});combo.resize(220,40);combo.show();settle();combo.showPopup();settle();
            auto popup=combo.view()->window();QVERIFY(popup->isVisible());corners(popup->grab().toImage(),uiReal(6),uiReal(1),colors().cardBorder);
            QVERIFY(popup->grab().save(report+QString("/rounded-combo-%1-%2.png").arg(light?"light":"dark",QGuiApplication::platformName())));
            QTest::keyClick(combo.view(),Qt::Key_Down);QTest::keyClick(combo.view(),Qt::Key_Escape);QCOMPARE(combo.currentIndex(),0);
            if(QGuiApplication::platformName()=="windows") {
                menu.winId();prepareRoundedPopup(&menu);
                QJsonObject native{{"platform","windows"},{"hidden",menu.testAttribute(Qt::WA_DontShowOnScreen)},
                    {"client_grab_only",true},{"dwm_composited_desktop_capture",false},
                    {"combo_hidden",popup->testAttribute(Qt::WA_DontShowOnScreen)},
                    {"combo_corner_set_result",QJsonValue::fromVariant(popup->property("roundedNativeCornerResult"))},
                    {"corner_set_result",QJsonValue::fromVariant(menu.property("roundedNativeCornerResult"))}};
#ifdef Q_OS_WIN
                DWORD preference=0;const auto hr=DwmGetWindowAttribute(reinterpret_cast<HWND>(menu.winId()),33,&preference,sizeof(preference));
                native["corner_get_result"]=qint64(hr);native["corner_preference"]=int(preference);
                bool numeric=false;const auto result=menu.property("roundedNativeCornerResult").toLongLong(&numeric);
                if(numeric&&result==0){QVERIFY(SUCCEEDED(hr));QCOMPARE(preference,DWORD(1));}
#endif
                QFile f(report+QString("/rounded-native-%1.json").arg(light?"light":"dark"));QVERIFY(f.open(QIODevice::WriteOnly));f.write(QJsonDocument(native).toJson());
                QVERIFY(menu.testAttribute(Qt::WA_DontShowOnScreen));
            }
        }
    }
};
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);rs::HiddenWindows hidden;
    if(QGuiApplication::platformName()=="windows"&&!qEnvironmentVariableIsSet("RABBITSHELL_COMPOSITE_PROBE"))app.installEventFilter(&hidden);
    rs::RoundedTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "test_roundedsurfaces.moc"
