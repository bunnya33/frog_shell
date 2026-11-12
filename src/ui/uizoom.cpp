#include "../core/operationgate.h"
#include "uizoom.h"
#include "theme.h"
#include <cmath>
namespace rs {
UiZoomStyle::UiZoomStyle():QProxyStyle(QStyleFactory::create("Fusion")) {}
int UiZoomStyle::styleHint(StyleHint hint,const QStyleOption *option,const QWidget *widget,QStyleHintReturn *data) const {
    if(hint==SH_Menu_Scrollable && widget && widget->property("resourceFileMenu").toBool())return 1;
    return QProxyStyle::styleHint(hint,option,widget,data);
}
int UiZoomStyle::pixelMetric(PixelMetric metric,const QStyleOption *option,const QWidget *widget) const {
    if((metric==PM_IndicatorWidth || metric==PM_IndicatorHeight) && qobject_cast<const QCheckBox *>(widget))return uiPx(18);
    // PanelTabs reserves its own shared edge; native tab-base overlap would
    // shorten and vertically centre its corner actions against another base.
    if(metric==PM_TabBarBaseHeight&&widget&&widget->property("panelTabs").toBool())return 0;
    if(metric==PM_SmallIconSize && widget && widget->property("resourceFileMenu").toBool())return uiPx(18);
    if(metric==PM_MenuScrollerHeight && widget && widget->property("resourceFileMenu").toBool())return uiPx(QProxyStyle::pixelMetric(metric,option,widget));
    if(readingMetric)return QProxyStyle::pixelMetric(metric,option,widget);
    QScopedValueRollback<bool> guard(readingMetric,true);
    const int base=QProxyStyle::pixelMetric(metric,option,widget);
    switch(metric) {
    case PM_DefaultFrameWidth:case PM_ButtonMargin:case PM_ButtonDefaultIndicator:
    case PM_ScrollBarExtent:case PM_ScrollBarSliderMin:case PM_SplitterWidth:
    case PM_IndicatorWidth:case PM_IndicatorHeight:case PM_ExclusiveIndicatorWidth:case PM_ExclusiveIndicatorHeight:
    case PM_SmallIconSize:case PM_LargeIconSize:case PM_ButtonIconSize:case PM_ToolBarIconSize:
    case PM_TabBarIconSize:case PM_TabCloseIndicatorWidth:case PM_TabCloseIndicatorHeight:
    case PM_FocusFrameHMargin:case PM_FocusFrameVMargin:case PM_TreeViewIndentation:
    case PM_LayoutLeftMargin:case PM_LayoutTopMargin:case PM_LayoutRightMargin:case PM_LayoutBottomMargin:
    case PM_LayoutHorizontalSpacing:case PM_LayoutVerticalSpacing:
    case PM_HeaderMargin:case PM_HeaderMarkSize:case PM_MenuPanelWidth:case PM_MenuHMargin:case PM_MenuVMargin:
        return base<0?base:uiPx(base);
    default:return base;
    }
}
void UiZoomStyle::drawPrimitive(PrimitiveElement element,const QStyleOption *option,QPainter *painter,const QWidget *widget) const {
    if(element!=PE_IndicatorCheckBox || !qobject_cast<const QCheckBox *>(widget)) {
        QProxyStyle::drawPrimitive(element,option,painter,widget);return;
    }
    const bool enabled=option->state & State_Enabled;
    const bool checked=option->state & (State_On | State_NoChange);
    const bool hover=option->state & State_MouseOver;
    const bool pressed=option->state & State_Sunken;
    const bool focus=option->state & State_HasFocus;
    const auto c=colors();
    auto mix=[](QColor a,QColor b,qreal amount){return QColor::fromRgbF(a.redF()*(1-amount)+b.redF()*amount,a.greenF()*(1-amount)+b.greenF()*amount,a.blueF()*(1-amount)+b.blueF()*amount);};
    QColor fill=checked?c.accent:c.side;
    QColor border=checked?c.accent:c.muted;
    if(enabled && hover)fill=mix(fill,checked?c.text:c.muted,pressed?.27:.13);
    if(enabled && pressed && !hover)fill=mix(fill,c.text,.22);
    if(!enabled){fill=mix(fill,c.background,.55);border=mix(border,c.background,.4);}
    if(enabled && focus)border=checked?(lightTheme()?QColor("#003c86"):QColor("#f8f8f2")):c.accent;
    const qreal penWidth=uiReal(focus && enabled?2:1.5);
    QRectF rect=QRectF(option->rect).adjusted(penWidth/2,penWidth/2,-penWidth/2,-penWidth/2);
    painter->save();painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(border,penWidth));painter->setBrush(fill);
    painter->drawRoundedRect(rect,uiReal(3),uiReal(3));
    if(checked){
        QColor ink=lightTheme()?QColor("#ffffff"):QColor("#282a36");
        if(!enabled)ink=mix(ink,fill,.35);
        painter->setPen(QPen(ink,uiReal(2),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        const QRectF r(option->rect);
        if(option->state & State_NoChange)painter->drawLine(QPointF(r.left()+r.width()*.27,r.center().y()),QPointF(r.left()+r.width()*.73,r.center().y()));
        else {QPainterPath tick;tick.moveTo(r.left()+r.width()*.23,r.top()+r.height()*.51);tick.lineTo(r.left()+r.width()*.43,r.top()+r.height()*.70);tick.lineTo(r.left()+r.width()*.77,r.top()+r.height()*.31);painter->drawPath(tick);}
    }
    painter->restore();
}
UiZoom::UiZoom(QObject *parent):QObject(parent) {
    bool ok=false;const int saved=QSettings().value("appearance/uiZoomLevel",0).toInt(&ok);
    current=ok?qBound(-4,saved,4):0;
    qApp->installEventFilter(this);
}
UiZoom &UiZoom::instance(){static auto zoom=new UiZoom(qApp);return *zoom;}
qreal UiZoom::factor() const{return std::pow(1.2,current);}
int uiPx(qreal base){const int value=qRound(base*UiZoom::instance().factor());return base>0?qMax(1,value):value;}
qreal uiReal(qreal base){return base*UiZoom::instance().factor();}
qreal uiBase(qreal displayed){return displayed/UiZoom::instance().factor();}
QSize uiSize(int width,int height){return {uiPx(width),uiPx(height)};}
QFont uiFont(int basePx,QFont::Weight weight){auto f=qApp->font();f.setPixelSize(uiPx(basePx));f.setWeight(weight);return f;}
void UiZoom::ensureStyle(){if(!installedStyle){installedStyle=new UiZoomStyle;qApp->setStyle(installedStyle);}}
void UiZoom::setLevel(int value){value=qBound(-4,value,4);if(value==current)return;current=value;QSettings().setValue("appearance/uiZoomLevel",current);apply();}
void UiZoom::reload(){bool ok=false;int saved=QSettings().value("appearance/uiZoomLevel",0).toInt(&ok);saved=ok?qBound(-4,saved,4):0;if(current!=saved){current=saved;apply();}}
QString uiQss(const QString &canonical) {
    static const QRegularExpression pixels(R"((-?\d+(?:\.\d+)?)px\b)");
    QString result;qsizetype offset=0;auto matches=pixels.globalMatch(canonical);
    while(matches.hasNext()){const auto m=matches.next();result+=QStringView(canonical).mid(offset,m.capturedStart()-offset);result+=QString::number(uiPx(m.captured(1).toDouble()))+"px";offset=m.capturedEnd();}
    result+=QStringView(canonical).mid(offset);return result;
}
void UiZoom::applicationSheet(const QString &canonical){canonicalSheet=canonical;apply();}
void UiZoom::bind(QObject *context,const QString &key,std::function<void()> callback) {
    if(!bindings.contains(context))connect(context,&QObject::destroyed,this,[this,context]{bindings.remove(context);});
    bindings[context][key]=callback;callback();
}
void UiZoom::apply() {
    QScopedValueRollback<bool> guard(applying,true);emit aboutToChange();ensureStyle();
    qApp->setFont(uiFont());QToolTip::setFont(uiFont());if(!canonicalSheet.isEmpty())qApp->setStyleSheet(uiQss(canonicalSheet));
    // Only explicitly registered canonical values; never capture laid-out geometry.
    QList<QPointer<QObject>> contexts;for(auto it=bindings.cbegin();it!=bindings.cend();++it)contexts.append(it.key());
    for(const auto &context:contexts)if(context){const auto callbacks=bindings.value(context);for(const auto &callback:callbacks){if(!context)break;callback();}}
    emit changed();
}
bool UiZoom::eventFilter(QObject *object,QEvent *event) {
    if(OperationGate::locked())return false;
    if(event->type()!=QEvent::ShortcutOverride && event->type()!=QEvent::KeyPress)return false;
    auto widget=qobject_cast<QWidget *>(object);if(!widget)return false;
    bool rabbit=false;for(auto parent=widget;parent;parent=parent->parentWidget())if(parent->property("rabbitUi").toBool()){rabbit=true;break;}
    // Parentless menus/popups still belong to this QApplication. No OS/global hook.
    rabbit |= qobject_cast<QMenu *>(widget)!=nullptr || widget->windowType()==Qt::Popup;
    if(!rabbit)return false;
    const auto key=static_cast<QKeyEvent *>(event);
    const auto mods=key->modifiers()&~Qt::KeypadModifier;
    const bool exact=mods==physicalControlModifier;
    const bool plus=(key->key()==Qt::Key_Plus || key->key()==Qt::Key_Equal) && (exact || mods==(physicalControlModifier|Qt::ShiftModifier));
    const bool minus=key->key()==Qt::Key_Minus && exact;
    const bool reset=key->key()==Qt::Key_0 && exact;
    if(!plus&&!minus&&!reset)return false;
    event->accept();if(event->type()==QEvent::KeyPress)setLevel(reset?0:current+(plus?1:-1));return true;
}
void setUiStyleSheet(QWidget *widget,const QString &canonical){UiZoom::instance().bind(widget,"qss",[widget,canonical]{widget->setStyleSheet(uiQss(canonical));});}
void bindUiFont(QWidget *widget,int px,QFont::Weight weight){UiZoom::instance().bind(widget,"font",[widget,px,weight]{widget->setFont(uiFont(px,weight));});}
void uiMargins(QLayout *layout,int l,int t,int r,int b){UiZoom::instance().bind(layout,"margins",[=]{layout->setContentsMargins(uiPx(l),uiPx(t),uiPx(r),uiPx(b));});}
void uiSpacing(QLayout *layout,int base){UiZoom::instance().bind(layout,"spacing",[=]{layout->setSpacing(base<0?base:uiPx(base));});}
#define RS_UI_DIMENSION(name,method) void name(QWidget *widget,int base){UiZoom::instance().bind(widget,#method,[=]{widget->method(uiPx(base));});}
RS_UI_DIMENSION(uiFixedWidth,setFixedWidth)
RS_UI_DIMENSION(uiFixedHeight,setFixedHeight)
RS_UI_DIMENSION(uiMinimumWidth,setMinimumWidth)
RS_UI_DIMENSION(uiMinimumHeight,setMinimumHeight)
RS_UI_DIMENSION(uiMaximumWidth,setMaximumWidth)
RS_UI_DIMENSION(uiMaximumHeight,setMaximumHeight)
#undef RS_UI_DIMENSION
void uiFixedSize(QWidget *widget,int w,int h){UiZoom::instance().bind(widget,"fixedSize",[=]{widget->setFixedSize(uiSize(w,h));});}
void uiIconSize(QAbstractButton *widget,int w,int h){UiZoom::instance().bind(widget,"iconSize",[=]{widget->setIconSize(uiSize(w,h));});}
void uiIconSize(QAbstractItemView *widget,int w,int h){UiZoom::instance().bind(widget,"iconSize",[=]{widget->setIconSize(uiSize(w,h));});}
void uiHandleWidth(QSplitter *widget,int base){UiZoom::instance().bind(widget,"handleWidth",[=]{widget->setHandleWidth(uiPx(base));});}
}
