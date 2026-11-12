#pragma once
#include "../src/ui/theme.h"
namespace rs::test {
inline QString promptLineError(QTextDocument *doc) {
    if(!doc)return "missing rendered text document";
    QVector<qreal> baselines;
    for(auto block=doc->begin();block.isValid();block=block.next()){
        auto layout=block.layout();const auto top=doc->documentLayout()->blockBoundingRect(block).top();
        for(int n=0;n<layout->lineCount();++n){const auto line=layout->lineAt(n);baselines.append(top+line.y()+line.ascent());}
    }
    for(int n=1;n<baselines.size();++n){const auto step=baselines[n]-baselines[n-1];
        if(step<uiPx(21)||step>uiPx(23))return QString("actual baseline advance %1, expected about %2").arg(step).arg(uiPx(22));}
    return {};
}
inline QString promptBorderError(QPushButton *button,bool light) {
    const bool enabled=button->isEnabled(),hover=button->testAttribute(Qt::WA_UnderMouse),down=button->isDown();
    const QColor expected(light?"#afb8c1":"#687086");QString result;
    for(int state=0;state<4;++state){
        button->setEnabled(state!=3);button->setAttribute(Qt::WA_UnderMouse,state==1||state==2);button->setDown(state==2);
        const auto pix=button->grab();auto image=pix.toImage();
        for(auto p:{QPoint(image.width()/2,0),QPoint(image.width()/2,image.height()-1),QPoint(0,image.height()/2),QPoint(image.width()-1,image.height()/2)}){
            const auto c=image.pixelColor(p);const int delta=qMax(qAbs(c.red()-expected.red()),qMax(qAbs(c.green()-expected.green()),qAbs(c.blue()-expected.blue())));
            if(delta>8){result=QString("state %1 edge %2,%3 is %4, expected %5").arg(state).arg(p.x()).arg(p.y()).arg(c.name(),expected.name());break;}
        }
        if(!result.isEmpty())break;
    }
    button->setEnabled(enabled);button->setAttribute(Qt::WA_UnderMouse,hover);button->setDown(down);return result;
}
}
