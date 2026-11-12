#include "filecontextmenu.h"
#include "theme.h"

namespace rs {
void FileContextMenu::showEvent(QShowEvent *event) {
    QMenu::showEvent(event);
    if(property("applicationMenu").toBool()&&QApplication::isEffectEnabled(Qt::UI_FadeMenu)) {
        if(!fade)fade=new QPropertyAnimation(this,"windowOpacity",this);
        fade->stop();fade->setDuration(110);fade->setStartValue(.15);fade->setEndValue(1.);fade->setEasingCurve(QEasingCurve::OutCubic);fade->start();
    }
}
void FileContextMenu::hideEvent(QHideEvent *event) {
    if(fade)fade->stop();setWindowOpacity(1.);QMenu::hideEvent(event);
}
FileContextMenu::FileContextMenu(QWidget *parent):QMenu(parent) {
    setProperty("resourceFileMenu",true);
    UiZoom::instance().bind(this,"resourceMenuMetrics",[this]{syncMetrics();});
    connect(this,&QMenu::aboutToShow,this,&FileContextMenu::syncMetrics);
    // QMenu allocates its native scroller on StyleChange after this local
    // property is available; its base constructor could not see the property.
    QEvent styleChange(QEvent::StyleChange);QApplication::sendEvent(this,&styleChange);
}
void FileContextMenu::syncMetrics() {
    auto f=uiFont(13);f.setFamilies({"Microsoft YaHei UI","Segoe UI"});setFont(f);
    const auto bg=QColor(lightTheme()?"#ffffff":"#21222c");
    const auto ink=QColor(lightTheme()?"#1f2328":"#f8f8f2");
    auto pal=palette();
    for(auto role:{QPalette::Window,QPalette::Base,QPalette::Button})pal.setColor(role,bg);
    for(auto role:{QPalette::WindowText,QPalette::Text,QPalette::ButtonText})pal.setColor(role,ink);
    setPalette(pal);
    QFontMetrics fm(f);int label=0,right=uiPx(18);
    for(auto action:actions())if(action->isVisible()&&!action->isSeparator()) {
        const auto text=action->text().split('\t');label=qMax(label,fm.horizontalAdvance(text[0]));
        if(text.size()>1)right=qMax(right,fm.horizontalAdvance(text[1]));
    }
    setMinimumWidth(qMax(uiPx(248),uiPx(10+10+20+9+12+10)+label+right));
}
void FileContextMenu::initStyleOption(QStyleOptionMenuItem *option,const QAction *action) const {
    QMenu::initStyleOption(option,action);
    option->checkType=QStyleOptionMenuItem::NotCheckable;
    option->checked=false;
    option->menuHasCheckableItems=false;
}
QRect FileContextMenu::iconRect(QAction *action) const {
    const auto row=actionGeometry(action);
    return QStyle::visualRect(layoutDirection(),row,QRect(row.left()+uiPx(11),row.top()+(row.height()-uiPx(18))/2,uiPx(18),uiPx(18)));
}
QRect FileContextMenu::checkRect(QAction *action) const {
    const auto row=actionGeometry(action);const int size=uiPx(14);
    return QStyle::visualRect(layoutDirection(),row,QRect(row.right()-uiPx(10)-uiPx(18)+1+uiPx(2),row.top()+(row.height()-size)/2,size,size));
}
QRect FileContextMenu::itemViewport() const {
    QRect area=rect().adjusted(uiPx(5),uiPx(5),-uiPx(5),-uiPx(5));
    QRect first,last;
    for(auto action:actions())if(action->isVisible()) {
        const auto row=actionGeometry(action);if(row.isEmpty())continue;
        if(first.isEmpty())first=row;last=row;
    }
    const int scroller=style()->pixelMetric(QStyle::PM_MenuScrollerHeight,nullptr,this);
    if(!first.isEmpty()&&first.top()<area.top())area.setTop(area.top()+scroller);
    if(!last.isEmpty()&&last.bottom()>area.bottom())area.setBottom(area.bottom()-scroller);
    return area;
}
void FileContextMenu::paintEvent(QPaintEvent *event) {
    // QMenu still paints/owns scroll arrows and all input/accessible actions.
    // Only action contents are repainted to avoid the platform icon gutter.
    QMenu::paintEvent(event);
    QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipRect(itemViewport());
    const QColor bg(lightTheme()?"#ffffff":"#21222c"),ink(lightTheme()?"#1f2328":"#f8f8f2");
    const QColor hover(lightTheme()?"#eaeef2":"#44475a"),divider(lightTheme()?"#d0d7de":"#44475a");
    for(auto action:actions()) {
        const auto row=actionGeometry(action);
        if(!action->isVisible()||row.isEmpty()||!event->region().intersects(row))continue;
        painter.save();painter.setClipRect(row,Qt::IntersectClip);painter.fillRect(row,bg);
        if(action->isSeparator()) {
            painter.fillRect(QRectF(row.left()+uiPx(6),row.center().y(),row.width()-uiPx(12),uiReal(1)),divider);
            painter.restore();continue;
        }
        if(activeAction()==action&&action->isEnabled()) {
            painter.setPen(Qt::NoPen);painter.setBrush(hover);painter.drawRoundedRect(QRectF(row),uiReal(2),uiReal(2));
        }
        painter.setOpacity(.88);
        action->icon().paint(&painter,iconRect(action),Qt::AlignCenter,action->isEnabled()?QIcon::Normal:QIcon::Disabled,QIcon::Off);
        painter.setOpacity(1);painter.setPen(action->isEnabled()?ink:colors().muted);painter.setFont(font());
        const auto text=action->text().split('\t');const auto fm=painter.fontMetrics();
        const int right=row.right()-uiPx(10)+1;
        const int suffix=text.size()>1?fm.horizontalAdvance(text[1]):uiPx(18);
        const QRect label(row.left()+uiPx(39),row.top(),right-suffix-uiPx(12)-(row.left()+uiPx(39)),row.height());
        painter.drawText(label,Qt::AlignVCenter|Qt::AlignLeft|Qt::TextSingleLine,text[0]);
        if(text.size()>1) {
            painter.setOpacity(.72);painter.drawText(QRect(right-suffix,row.top(),suffix,row.height()),Qt::AlignVCenter|Qt::AlignRight|Qt::TextSingleLine,text[1]);painter.setOpacity(1);
        }
        if(action->isCheckable()&&action->isChecked()) {
            const auto mark=checkRect(action);
            painter.setPen(QPen(action->isEnabled()?ink:colors().muted,uiReal(2),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            QPainterPath path;path.moveTo(mark.left()+mark.width()*.2,mark.top()+mark.height()*.5);
            path.lineTo(mark.left()+mark.width()*.42,mark.top()+mark.height()*.72);
            path.lineTo(mark.left()+mark.width()*.82,mark.top()+mark.height()*.26);painter.drawPath(path);
        }
        painter.restore();
    }
}
}
