#pragma once
#include "theme.h"
#include "layoutmetrics.h"
#include "roundedsurface.h"
#include "../core/profile.h"
#include <optional>
namespace rs {
// Actual monitor values, independently optional. Missing data never becomes zero.
class ResourceSummary : public QWidget {
  public:
    explicit ResourceSummary(QWidget *parent=nullptr):QWidget(parent){
        setObjectName("resourceSummary");setAccessibleName("服务器资源使用");uiMinimumHeight(this,150);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Minimum);
    }
    void setMetrics(std::optional<double> cpu,quint64 used,quint64 total,
                    quint64 diskUsedBytes,quint64 diskTotalBytes,std::optional<double> uptime) {
        usage=cpu;memoryUsed=used;memoryTotal=total;diskUsed=diskUsedBytes;diskTotal=diskTotalBytes;seconds=uptime;
        const auto summary=QString("CPU %1；内存 %2；磁盘 / %3；运行时间 %4")
            .arg(usage?QString::number(*usage,'f',1)+"%":"采样中",volume(used,total),volume(diskUsedBytes,diskTotalBytes),
                 uptime?QString("%1 天 %2 小时").arg(qint64(*uptime/86400)).arg(qint64(*uptime/3600)%24):QString("—"));
        setAccessibleDescription(summary);setToolTip(summary);update();
    }
    void clear(){setMetrics({},0,0,0,0,{});}
    std::optional<double> cpuUsage() const{return usage;}
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);auto c=colors();
        paintRoundedSurface(p,rect(),uiReal(8),c.background,c.cardBorder,uiReal(1));
        const int left=uiPx(12),right=width()-uiPx(12),w=right-left;
        auto text=[&](QRect r,const QString &s,QColor ink,int align,int px,bool strong=false){p.setFont(uiFont(px,strong?QFont::Medium:QFont::Normal));p.setPen(ink);p.drawText(r,align|Qt::AlignVCenter,p.fontMetrics().elidedText(s,Qt::ElideRight,r.width()));};
        auto metric=[&](int y,const QString &name,const QString &value,std::optional<double> percent,QColor color){
            text(QRect(left,uiPx(y),uiPx(45),uiPx(22)),name,c.muted,Qt::AlignLeft,12);
            text(QRect(left+uiPx(42),uiPx(y),w-uiPx(42),uiPx(22)),value,color,Qt::AlignRight,13,true);
            const QRectF track(left,uiPx(y+29),w,uiPx(3));p.fillRect(track,c.line);
            if(percent)p.fillRect(QRectF(track.topLeft(),QSizeF(track.width()*qBound(0.,*percent,100.)/100.,track.height())),color);
        };
        metric(29,"CPU",usage?QString::number(*usage,'f',1)+"%":"—",usage,cpuColor());
        const auto memory=memoryTotal?std::optional<double>(100.*memoryUsed/memoryTotal):std::nullopt;
        const auto disk=diskTotal?std::optional<double>(100.*diskUsed/diskTotal):std::nullopt;
        metric(69,"内存",volume(memoryUsed,memoryTotal),memory,memoryColor());
        metric(109,"磁盘 /",volume(diskUsed,diskTotal),disk,diskColor());
        text(QRect(left,uiPx(3),uiPx(70),uiPx(22)),"运行时间",c.muted,Qt::AlignLeft,12);
        text(QRect(left+uiPx(70),uiPx(3),w-uiPx(70),uiPx(22)),seconds?QString("%1 天 %2 小时").arg(qint64(*seconds/86400)).arg(qint64(*seconds/3600)%24):QString("—"),c.text,Qt::AlignRight,13);
    }
  private:
    static QString volume(quint64 used,quint64 total){
        if(!total)return "—";double divisor=1;QStringList units{"B","KiB","MiB","GiB","TiB"};int unit=0;
        while(double(total)/divisor>=1024&&unit<4){divisor*=1024;++unit;}
        return QString("%1 / %2 %3 · %4%").arg(QString::number(used/divisor,'f',unit?1:0),QString::number(total/divisor,'f',unit?1:0),units[unit],QString::number(100.*used/total,'f',1));
    }
    std::optional<double> usage,seconds;
    quint64 memoryUsed=0,memoryTotal=0,diskUsed=0,diskTotal=0;
};
class NetworkRateSummary : public QWidget {
  public:
    explicit NetworkRateSummary(QWidget *parent=nullptr):QWidget(parent){bandHeight(this,40);setObjectName("networkRates");setAccessibleName("网卡传输速率");}
    void setRates(std::optional<QPair<double,double>> values){rates=values;setAccessibleDescription(values?QString("接收 RX %1/s；发送 TX %2/s").arg(humanSize(quint64(values->first)),humanSize(quint64(values->second))):QString("接收和发送速率采样中"));update();}
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);auto c=colors();
        const int gap=qMin(width(),uiPx(8)), leftWidth=qMax(0,(width()-gap)/2), rightX=leftWidth+gap;
        const int rightWidth=qMax(0,width()-rightX);
        auto draw=[&](QRect rect,const QString &value,QColor color) {
            p.save();p.setClipRect(rect);p.setPen(color);
            p.drawText(rect,Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(value,Qt::ElideRight,rect.width()));
            p.restore();
        };
        p.setFont(uiFont(12));
        draw(QRect(0,0,leftWidth,uiPx(18)),"接收 RX",c.muted);
        draw(QRect(rightX,0,rightWidth,uiPx(18)),"发送 TX",c.muted);
        p.setFont(uiFont(13,QFont::Medium));
        draw(QRect(0,uiPx(19),leftWidth,uiPx(20)),rates?humanSize(quint64(rates->first))+"/s":"—",networkReceiveColor());
        draw(QRect(rightX,uiPx(19),rightWidth,uiPx(20)),rates?humanSize(quint64(rates->second))+"/s":"—",networkSendColor());
    }
  private:
    std::optional<QPair<double,double>> rates;
};
}
