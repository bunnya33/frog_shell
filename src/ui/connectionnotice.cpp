#include "connectionnotice.h"
#include "icons.h"
#include "theme.h"
#include "windowstyle.h"
namespace rs {
ConnectionNotice::ConnectionNotice(bool small,QWidget *parent):QFrame(parent),compact(small) {
    setObjectName(small?"connectionBar":"connectionLoading");setFrameRole(this,"card");
    setAttribute(Qt::WA_StyledBackground);
    auto outer=new QVBoxLayout(this);uiMargins(outer,16,12,16,12);
    if(!small)outer->addStretch();
    auto row=new QBoxLayout(small?QBoxLayout::LeftToRight:QBoxLayout::TopToBottom);
    uiSpacing(row,12);outer->addLayout(row);
    glyph=new QLabel;glyph->setObjectName("connectionLogo");glyph->setAccessibleName("青蛙应用图标");row->addWidget(glyph,0,Qt::AlignCenter);
    message=small?new ElidedLabel:new QLabel;message->setTextFormat(Qt::PlainText);
    message->setAlignment(small?Qt::AlignLeft|Qt::AlignVCenter:Qt::AlignCenter);
    row->addWidget(message,small?1:0);
    progress=new QProgressBar;progress->setObjectName("connectionProgress");progress->setTextVisible(false);
    uiFixedHeight(progress,3);uiFixedWidth(progress,small?100:220);row->addWidget(progress,0,Qt::AlignCenter);
    auto buttons=new QHBoxLayout;uiSpacing(buttons,8);if(!small)buttons->addStretch();
    retry=button("重新连接");cancel=button("取消");buttons->addWidget(retry);buttons->addWidget(cancel);
    if(!small)buttons->addStretch();row->addLayout(buttons);
    if(!small)outer->addStretch();
    UiZoom::instance().bind(this,"icon",[this]{glyph->setPixmap(applicationIcon().pixmap(uiSize(compact?20:64,compact?20:64),glyph->devicePixelRatioF()));});
    refreshTheme();hide();
}
void ConnectionNotice::display(const QString &phase,bool pending,bool animate){
    message->setText(phase);message->setToolTip(phase);
    retry->setVisible(!pending);cancel->setText(pending?"取消":"收起");
    progress->setRange(0,pending&&animate?0:1);progress->setVisible(pending&&animate);
}
void ConnectionNotice::refreshTheme(){
    const auto c=colors();
    setUiStyleSheet(progress,QString("QProgressBar{border:0;background:%1;border-radius:1px;}QProgressBar::chunk{background:%2;}").arg(c.line.name(),c.accent.name()));
    glyph->setPixmap(applicationIcon().pixmap(uiSize(compact?20:64,compact?20:64),glyph->devicePixelRatioF()));
}
}
