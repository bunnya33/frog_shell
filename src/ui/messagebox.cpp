#include "messagebox.h"
#include "theme.h"
#include "layoutmetrics.h"
#include "promptstyle.h"
#include <cmath>
namespace rs {
namespace {
class MessageFooter final:public QWidget {
  public:using QWidget::QWidget;
  protected:void paintEvent(QPaintEvent *) override {QPainter p(this);p.fillRect(rect(),colors().side);p.fillRect(0,0,width(),uiPx(1),colors().divider);}
};
class MessageSymbol final:public QWidget {
  public:explicit MessageSymbol(AppMessageBox *box):QWidget(box),box(box){uiFixedSize(this,40,40);setAttribute(Qt::WA_TransparentForMouseEvents);}
  protected:void paintEvent(QPaintEvent *) override {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.scale(width()/40.,height()/40.);const auto c=colors();
    const auto type=box->icon();p.setPen(QPen(type==QMessageBox::Critical?c.red:type==QMessageBox::Warning?c.orange:c.accent,2.4,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
    if(type==QMessageBox::Warning){QPainterPath path;path.moveTo(20,4);path.lineTo(37,34);path.quadTo(38,37,35,37);path.lineTo(5,37);path.quadTo(2,37,3,34);path.closeSubpath();p.drawPath(path);p.drawLine(QPointF(20,14),QPointF(20,24));p.drawPoint(QPointF(20,30));}
    else {p.drawEllipse(QRectF(3,3,34,34));if(type==QMessageBox::Critical){p.drawLine(14,14,26,26);p.drawLine(26,14,14,26);}else if(type==QMessageBox::Question){QFont f=uiFont(27,QFont::Medium);f.setPixelSize(27);p.setFont(f);p.drawText(QRect(0,0,40,39),Qt::AlignCenter,"?");}else{p.drawPoint(QPointF(20,12));p.drawLine(20,19,20,29);}}
  }
  private:AppMessageBox *box;
};

}
AppMessageBox::AppMessageBox(QWidget *parent):AppMessageBox(NoIcon,{},{},NoButton,parent){}
AppMessageBox::AppMessageBox(Icon icon,const QString &title,const QString &text,StandardButtons buttons,QWidget *parent):QMessageBox(icon,title,text,buttons,parent) {
    setOption(QMessageBox::Option::DontUseNativeDialog);setObjectName("applicationMessage");setTextFormat(Qt::PlainText);
    bindUiFont(this,14);
    localizeButtons();
    // Widgets are laid out after the base showEvent, which sets up Qt's button
    // defaults and Escape detection. Their original objects/connections survive.
    UiZoom::instance().bind(this,"messageMetrics",[this]{queueArrange();});
}
void AppMessageBox::localizeButtons(){
    const struct {StandardButton id;const char *english;const char *chinese;} labels[]={
        {Ok,"OK","确定"},{Open,"Open","打开"},{Save,"Save","保存"},{Cancel,"Cancel","取消"},{Close,"Close","关闭"},
        {Discard,"Discard","不保存"},{Apply,"Apply","应用"},{Reset,"Reset","重置"},{RestoreDefaults,"Restore Defaults","恢复默认"},
        {Help,"Help","帮助"},{SaveAll,"Save All","全部保存"},{Yes,"Yes","是"},{YesToAll,"Yes to All","全部是"},
        {No,"No","否"},{NoToAll,"No to All","全部否"},{Abort,"Abort","中止"},{Retry,"Retry","重试"},{Ignore,"Ignore","忽略"}};
    for(auto b:buttons()){
        if(b->property("messageLocalized").toBool())continue;
        const auto id=standardButton(b);if(id==NoButton)continue;
        for(const auto &label:labels)if(label.id==id){
            auto native=b->text();native.remove('&');
            // Only Qt's unchanged standard caption is translated. A caller's
            // explicit operation label, including custom-role buttons, survives.
            if(native==QString::fromLatin1(label.english))b->setText(QString::fromUtf8(label.chinese));
            break;
        }
        b->setProperty("messageLocalized",true);
    }
}
void AppMessageBox::queueArrange(){if(queued||!isVisible())return;queued=true;QTimer::singleShot(0,this,[this]{queued=false;if(isVisible())arrange();});}
void AppMessageBox::showEvent(QShowEvent *e){QMessageBox::showEvent(e);arrange();}
void AppMessageBox::resizeEvent(QResizeEvent *e){QMessageBox::resizeEvent(e);if(!arranging)queueArrange();}
void AppMessageBox::changeEvent(QEvent *e){QMessageBox::changeEvent(e);if(e->type()==QEvent::StyleChange||e->type()==QEvent::FontChange)queueArrange();}
bool AppMessageBox::event(QEvent *e){
    if(e->type()==QEvent::LayoutRequest&&visualLayout&&layout()==visualLayout){if(!arranging)fit();return true;}
    return QMessageBox::event(e);
}
void AppMessageBox::arrange(){
    if(arranging)return;QScopedValueRollback<bool> guard(arranging,true);
    localizeButtons();
    auto nativeLabel=findChild<QLabel *>("qt_msgbox_label");auto nativeIcon=findChild<QLabel *>("qt_msgboxex_icon_label");
    auto box=findChild<QDialogButtonBox *>();auto grid=qobject_cast<QGridLayout *>(layout());if(!box||!nativeLabel||!grid)return;
    auto nativeInfo=findChild<QLabel *>("qt_msgbox_informativelabel");
    // Detach the Qt-owned controls before discarding only layout items. Qt may
    // recreate its layout on a style/icon/checkbox change; arrange is repeatable.
    box->setParent(this);nativeLabel->setParent(this);if(nativeInfo)nativeInfo->setParent(this);if(checkBox())checkBox()->setParent(this);
    QWidget *details=nullptr;for(auto widget:findChildren<QWidget *>())if(QByteArray(widget->metaObject()->className())=="QMessageBoxDetailsText"){details=widget;details->setObjectName("qt_msgboxex_detailedtext");break;}
    const bool detailsVisible=details&&!details->isHidden();if(details)details->setParent(this);
    // Keep the exact native QGridLayout object. Qt retains and reuses it during
    // setText/updateSize. Discard layout ITEMS, never Qt's layout or controls.
    while(auto item=grid->takeAt(0))delete item;
    for(int r=0;r<grid->rowCount();++r){grid->setRowMinimumHeight(r,0);grid->setRowStretch(r,0);}
    for(int c=0;c<grid->columnCount();++c){grid->setColumnMinimumWidth(c,0);grid->setColumnStretch(c,0);}
    if(scroll){delete scroll;scroll=nullptr;delete footer;footer=nullptr;}
    visualLayout=grid;grid->setContentsMargins(0,0,0,0);grid->setSpacing(0);grid->setSizeConstraint(QLayout::SetNoConstraint);
    scroll=new QScrollArea(this);scroll->setObjectName("messageScroll");scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body=new QWidget;auto row=new QHBoxLayout(body);uiMargins(row,20,20,20,20);uiSpacing(row,14);
    symbol=new MessageSymbol(this);symbol->setObjectName("messageSymbol");symbol->setVisible(icon()!=NoIcon);row->addWidget(symbol,0,Qt::AlignVCenter);
    auto textBody=new QWidget;auto column=new QVBoxLayout(textBody);column->setContentsMargins(0,0,0,0);uiSpacing(column,8);
    message=nativeLabel;message->setWordWrap(true);message->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);message->show();column->addWidget(message);
    stylePromptText(message);
    informationText=nativeInfo;if(informationText){informationText->setWordWrap(true);informationText->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);informationText->setVisible(!informativeText().isEmpty());column->addWidget(informationText);stylePromptText(informationText);}
    if(checkBox()){column->addWidget(checkBox());checkBox()->show();}
    if(details){column->addWidget(details);details->setVisible(detailsVisible);for(auto text:details->findChildren<QTextEdit *>())stylePromptText(text);}
    row->addWidget(textBody,1);scroll->setWidget(body);grid->addWidget(scroll,0,0,1,qMax(1,grid->columnCount()));grid->setRowStretch(0,1);
    footer=new MessageFooter(this);footer->setObjectName("messageFooter");
    auto actions=new QHBoxLayout(footer);uiMargins(actions,20,12,20,12);actions->addStretch();actions->addWidget(box);grid->addWidget(footer,1,0,1,qMax(1,grid->columnCount()));
    box->setCenterButtons(false);if(box->layout())uiSpacing(box->layout(),8);
    for(auto b:buttons())if(auto push=qobject_cast<QPushButton *>(b)){stylePromptButton(push);push->setAutoDefault(false);}
    if(!defaultButton()){
        if(button(Cancel))setDefaultButton(Cancel);else if(button(No))setDefaultButton(No);
        else for(auto b:buttons())if(buttonRole(b)==AcceptRole){if(auto push=qobject_cast<QPushButton *>(b))setDefaultButton(push);break;}
    }
    // Reparenting the native button box can clear QPushButton's default flag
    // while QMessageBox still retains its selected defaultButton pointer.
    if(auto selected=defaultButton())setDefaultButton(selected);
    if(nativeIcon)nativeIcon->hide();
    scroll->show();footer->show();box->show();fit();
}
void AppMessageBox::fit(){
    if(!scroll||!footer)return;
    const auto available=screen()?screen()->availableGeometry().size():QSize(800,600);
    const int w=qMax(1,qMin(qMax(uiPx(448),footer->sizeHint().width()),available.width()-uiPx(24)));
    const int bodyWidth=qMax(1,w-uiPx(40)-(icon()!=NoIcon?uiPx(54):0)-style()->pixelMetric(QStyle::PM_ScrollBarExtent));
    auto measure=[&](QLabel *t){if(!t)return;QTextDocument doc;doc.setDefaultFont(t->font());doc.setDocumentMargin(0);QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);doc.setDefaultTextOption(option);if(t->textFormat()==Qt::RichText||(t->textFormat()==Qt::AutoText&&Qt::mightBeRichText(t->text())))doc.setHtml(t->text());else doc.setPlainText(t->text());doc.setTextWidth(bodyWidth);promptDocumentSpacing(&doc);t->setWordWrap(true);
        // Keep Qt's original label/text/accessibility object, but configure its
        // real text document too: measuring a wrapped copy alone would still
        // let QLabel clip a long path without spaces during painting.
        t->setTextInteractionFlags(t->textInteractionFlags()|Qt::TextSelectableByMouse);
        if(auto actual=t->findChild<QTextDocument *>()){actual->setDefaultTextOption(option);actual->setDocumentMargin(0);promptDocumentSpacing(actual);}
        t->setFixedHeight(qCeil(doc.size().height()));};measure(message);measure(informationText);
    layout()->activate();const int limit=qMax(uiPx(80),available.height()-footer->sizeHint().height()-uiPx(90));
    scroll->setMaximumHeight(limit);const int bodyHeight=body->layout()->totalSizeHint().height();
    setMinimumSize(0,0);setMaximumSize(QWIDGETSIZE_MAX,QWIDGETSIZE_MAX);resize(w,qMin(limit,bodyHeight)+footer->sizeHint().height());
}
QMessageBox::StandardButton AppMessageBox::showMessage(QWidget *parent,Icon icon,const QString &title,const QString &text,StandardButtons buttons,StandardButton selected){
    AppMessageBox box(icon,title,text,buttons,parent);if(selected!=NoButton)box.setDefaultButton(selected);box.exec();return box.standardButton(box.clickedButton());
}
#define RS_MESSAGE(name,kind) QMessageBox::StandardButton AppMessageBox::name(QWidget *p,const QString &title,const QString &text,StandardButtons buttons,StandardButton selected){return showMessage(p,kind,title,text,buttons,selected);}
RS_MESSAGE(information,Information)
RS_MESSAGE(warning,Warning)
RS_MESSAGE(critical,Critical)
RS_MESSAGE(question,Question)
#undef RS_MESSAGE
}
