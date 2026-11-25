#include "roundedsurface.h"
#include "theme.h"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif
namespace rs {
RoundedGeometry roundedGeometry(QRectF bounds, qreal radius, qreal penWidth) {
    RoundedGeometry g;
    if (bounds.isEmpty()) return g;
    radius=qBound(qreal(0),radius,qMin(bounds.width(),bounds.height())/2);
    g.width=qBound(qreal(0),penWidth,qMin(bounds.width(),bounds.height()));
    g.outside.addRoundedRect(bounds,radius,radius);
    const auto inset=g.width/2;
    g.stroke.addRoundedRect(bounds.adjusted(inset,inset,-inset,-inset),
                            qMax(qreal(0),radius-inset),qMax(qreal(0),radius-inset));
    return g;
}
void paintRoundedSurface(QPainter &p,QRectF bounds,qreal radius,QColor fill,QColor border,qreal width) {
    const auto g=roundedGeometry(bounds,radius,width);
    p.save();p.setRenderHint(QPainter::Antialiasing);
    if(fill.alpha())p.fillPath(g.outside,fill);
    if(width>0 && border.alpha()){p.setBrush(Qt::NoBrush);p.setPen(QPen(border,g.width));p.drawPath(g.stroke);}
    p.restore();
}
QRegion roundedPopupRegion(QRect bounds,qreal radius,qreal dpr) {
    const auto g=roundedGeometry(bounds,radius,0);
    QPainterPathStroker envelope;envelope.setWidth(2/qMax(qreal(1),dpr));
    const auto shape=g.outside.united(envelope.createStroke(g.outside));
    return QRegion(shape.toFillPolygon().toPolygon()).intersected(bounds);
}
namespace {
qreal radius(RoundedRole role) {
    if (role == RoundedRole::Process || role == RoundedRole::Card || role == RoundedRole::Find) return uiReal(8);
    if (role == RoundedRole::Menu || role == RoundedRole::ComboPopup) return uiReal(6);
    return uiReal(5);
}
QColor border(RoundedRole role,QWidget *host) {
    const auto c=colors();
    if(role==RoundedRole::Path)return host->property("uploadTarget").toBool()?c.accent:c.line;
    return role==RoundedRole::Card||role==RoundedRole::Process||role==RoundedRole::ComboPopup?c.cardBorder:
           role==RoundedRole::Menu?(host->property("resourceFileMenu").toBool()?QColor(lightTheme()?"#8c959f":"#555c73"):c.line):c.panelBorder;
}
QColor underlay(QWidget *host) {
    const auto c=colors();
    for(auto w=host->parentWidget();w;w=w->parentWidget()) {
        if(w->property("statusCard").toBool())return c.background;
        if(w->property("processCard").toBool())return c.processBody;
        const auto role=w->property("frameRole").toString();
        if(role=="card"||role=="find"||role=="transfer"||role=="footer"||w->objectName()=="side")return c.side;
        if(role=="rail"||w->objectName()=="rail")return c.rail;
    }
    return c.background;
}
class Border final : public QWidget {
  public:
    Border(QWidget *host,RoundedRole role):QWidget(host),host(host),role(role) {
        setObjectName("roundedSurfaceBorder");
        setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);setProperty("roundedBorderOverlay",true);
        host->installEventFilter(this);
        connect(&UiZoom::instance(),&UiZoom::changed,this,[this]{sync();});
        sync();
    }
    RoundedRole role;
    void sync(){
        setGeometry(host->rect());raise();show();update();
        if(host->property("roundedPopupFallback").isValid()) {
            if(host->property("roundedPopupFallback").toBool())
                host->setMask(roundedPopupRegion(host->rect(),radius(role),host->devicePixelRatioF()));
            else host->clearMask();
        }
    }
  protected:
    bool eventFilter(QObject *,QEvent *e) override {
        switch(e->type()) {
        case QEvent::Resize:case QEvent::Show:case QEvent::LayoutRequest:case QEvent::ChildPolished:sync();break;
        case QEvent::DynamicPropertyChange:
            if(static_cast<QDynamicPropertyChangeEvent *>(e)->propertyName()=="roundedPopupFallback")sync();else update();
            break;
        case QEvent::StyleChange:update();break;
        case QEvent::Paint:
            if(role==RoundedRole::Menu||role==RoundedRole::ComboPopup) {
                // QMenu and Qt's private combo container retain all input/selection handling.
                const auto fill=role==RoundedRole::Menu&&host->property("resourceFileMenu").toBool()?QColor(lightTheme()?"#ffffff":"#21222c"):colors().side;
                QPainter p(host);p.setCompositionMode(QPainter::CompositionMode_Source);p.fillRect(host->rect(),Qt::transparent);p.setCompositionMode(QPainter::CompositionMode_SourceOver);
                paintRoundedSurface(p,host->rect(),radius(role),fill,Qt::transparent,0);
            }
            break;
        default:break;
        }
        return false;
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        const qreal width=uiReal(role==RoundedRole::Path&&host->property("uploadTarget").toBool()?2:1);
        const auto g=roundedGeometry(rect(),radius(role),width);
        // Child headers/viewports can paint square corners. Cover only the area outside
        // the contour, then paint the full ring above every child. Never mask the ring.
        if(role!=RoundedRole::Menu&&role!=RoundedRole::ComboPopup) {
            QPainterPath outside;outside.addRect(QRectF(rect()));outside=outside.subtracted(g.outside);
            p.fillPath(outside,underlay(host));
        }
        paintRoundedSurface(p,rect(),radius(role),Qt::transparent,border(role,host),width);
        if (role == RoundedRole::Find) {
            const bool dragging = host->property("findResizeDragging").toBool();
            const bool hovering = host->property("findResizeHovered").toBool();
            const QColor idle = lightTheme() ? QColor("#afb8c1") : QColor("#494b55");
            const QColor hover = lightTheme() ? QColor("#4f84c4") : QColor("#626184");
            const QColor active = lightTheme() ? colors().accent : colors().comment;
            // Paint the strip on the outer contour so its ends follow the corners.
            p.setClipRect(QRectF(0, 0, uiReal(dragging || hovering ? 3 : 2), height()));
            p.fillPath(g.outside, dragging ? active : hovering ? hover : idle);
        }
    }
  private:
    QWidget *host;
};
class MenuInstaller final : public QObject {
  public:
    using QObject::QObject;
    bool eventFilter(QObject *object,QEvent *event) override {
        if(event->type()!=QEvent::Polish&&event->type()!=QEvent::Show&&event->type()!=QEvent::WinIdChange)return false;
        auto menu=qobject_cast<QMenu *>(object);
        if(!menu)return false;
        if(event->type()==QEvent::Polish) {
            // Set alpha before Qt creates its platform window; the client paints its
            // rounded background instead of depending on Windows' unrelated radius.
            // Windows requires a frameless native window for per-pixel alpha.
            // Keep Qt::Popup, its input semantics and native popup shadow.
            menu->setWindowFlag(Qt::FramelessWindowHint,true);
            menu->setAttribute(Qt::WA_TranslucentBackground);
            menu->setAttribute(Qt::WA_OpaquePaintEvent,false);
            installRoundedSurface(menu,RoundedRole::Menu);
        }
        if(event->type()==QEvent::Show||event->type()==QEvent::WinIdChange)prepareRoundedPopup(menu);
        return false;
    }
};
}
void installRoundedSurface(QWidget *host,RoundedRole role) {
    auto overlay=host->findChild<QWidget *>("roundedSurfaceBorder",Qt::FindDirectChildrenOnly);
    if(overlay) {auto frame=static_cast<Border *>(overlay);frame->role=role;frame->sync();return;}
    new Border(host,role);
}
void installRoundedMenus() {
    if(qApp->property("roundedMenusInstalled").toBool())return;
    qApp->setProperty("roundedMenusInstalled",true);
    qApp->installEventFilter(new MenuInstaller(qApp));
}
void prepareRoundedPopup(QWidget *widget) {
#ifdef Q_OS_WIN
    if(QGuiApplication::platformName()!="windows"||!widget->internalWinId())return;
    HIGHCONTRASTW hc{sizeof(HIGHCONTRASTW)};
    if(SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0)&&(hc.dwFlags&HCF_HIGHCONTRASTON)) {
        const DWORD systemPreference=0;
        DwmSetWindowAttribute(reinterpret_cast<HWND>(widget->internalWinId()),33,&systemPreference,sizeof(systemPreference));
        widget->setProperty("roundedPopupFallback",false);
        widget->setProperty("roundedNativeCornerResult","high-contrast-system");return;
    }
    // Public DWMWA_WINDOW_CORNER_PREFERENCE / DWMWCP_DONOTROUND, Windows 11+.
    // Unsupported older systems reject it and keep Qt's client alpha contour.
    const DWORD preference=1;
    const auto hr=DwmSetWindowAttribute(reinterpret_cast<HWND>(widget->internalWinId()),33,&preference,sizeof(preference));
    widget->setProperty("roundedNativeCornerResult",qint64(hr));
    const auto native=reinterpret_cast<HWND>(widget->internalWinId());
    const bool alpha=widget->testAttribute(Qt::WA_TranslucentBackground)&&widget->windowFlags().testFlag(Qt::FramelessWindowHint)&&(GetWindowLongPtrW(native,GWL_EXSTYLE)&WS_EX_LAYERED);
    widget->setProperty("roundedNativeAlphaReady",alpha);
    widget->setProperty("roundedPopupFallback",FAILED(hr)||!alpha);
    if(FAILED(hr)||!alpha) {
        // A region is a safe fallback for a compositor that ignores client alpha.
        // Include the antialias envelope; the centered stroke is already inset.
        widget->setMask(roundedPopupRegion(widget->rect(),uiReal(6),widget->devicePixelRatioF()));
    } else widget->clearMask();
#else
    Q_UNUSED(widget);
#endif
}
}
