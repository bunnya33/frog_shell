#include "serverclock.h"
#include "profile.h"
namespace rs::serverclock {
namespace {
bool fail(QString *error,const QString &s){if(error)*error=s;return false;}
QString ntpValue(const QByteArray &v){const auto s=QString::fromLatin1(v).trimmed();return s=="yes"?"yes":s=="no"?"no":"unknown";}
QString reason(const services::Result &r){
    auto text=QString::fromUtf8(r.err.isEmpty()?r.out:r.err).simplified().left(500);
    return QString("命令失败（退出码 %1）%2").arg(r.code).arg(text.isEmpty()?QString():QString("：%1").arg(text));
}
}
services::Command sampleCommand(){
    services::Command c;c.timeoutMs=8000;c.outputLimit=8192;
    c.text="export LC_ALL=C; n=unknown; "
           "if command -v timedatectl >/dev/null 2>&1; then "
           "x=$(timedatectl show --property=NTP --value 2>/dev/null) && "
           "case \"$x\" in yes|no) n=$x;; esac; fi; "
           "v=$(date '+%s|%Y-%m-%d %H:%M:%S|%z|%Z') || exit 1; "
           "printf 'CLOCK|%s\\n' \"$v\"; printf 'NTP|%s\\n' \"$n\"";
    return c;
}
bool validLocalDateTime(const QString &s){
    static const QRegularExpression rx("^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}$");
    if(!rx.match(s).hasMatch())return false;
    const auto date=QDate::fromString(s.left(10),"yyyy-MM-dd");const auto time=QTime::fromString(s.mid(11),"HH:mm:ss");
    return date.isValid()&&date.year()>=1970&&time.isValid()&&date.toString("yyyy-MM-dd")==s.left(10)&&time.toString("HH:mm:ss")==s.mid(11);
}
QString offsetText(int value){const auto magnitude=qAbs(value);return QString("UTC%1%2:%3").arg(value<0?"-":"+").arg(magnitude/3600,2,10,QLatin1Char('0')).arg((magnitude%3600)/60,2,10,QLatin1Char('0'));}
bool parseSample(const QByteArray &bytes,Sample *out,QString *error){
    if(!out||bytes.size()>8192)return fail(error,"服务器时间输出无效。");
    const auto text=QString::fromUtf8(bytes);if(text.toUtf8()!=bytes)return fail(error,"服务器时间编码无效。");
    const auto lines=text.trimmed().split('\n');if(lines.size()!=2)return fail(error,"服务器时间输出不完整。");
    static const QRegularExpression rx("^CLOCK\\|(-?[0-9]{1,14})\\|([0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2})\\|([+-])([0-9]{2})([0-9]{2})\\|([^\\x00-\\x1f\\x7f|]{1,64})$");
    const auto m=rx.match(lines[0]);if(!m.hasMatch()||!lines[1].startsWith("NTP|"))return fail(error,"服务器时间输出格式不受支持。");
    bool ok=false;const auto epoch=m.captured(1).toLongLong(&ok);const auto value=m.captured(2);const int hours=m.captured(4).toInt(),minutes=m.captured(5).toInt();
    if(!ok||hours>14||minutes>59||(hours==14&&minutes)||!validLocalDateTime(value))return fail(error,"服务器日期或时区偏移无效。");
    const int offset=(hours*3600+minutes*60)*(m.captured(3)=="-"?-1:1);
    const auto local=QDateTime::fromSecsSinceEpoch(epoch,QTimeZone::fromSecondsAheadOfUtc(offset));
    if(!local.isValid()||local.toString("yyyy-MM-dd HH:mm:ss")!=value)return fail(error,"服务器时间与时区偏移不一致。");
    const auto ntp=lines[1].mid(4);if(ntp!="yes"&&ntp!="no"&&ntp!="unknown")return fail(error,"自动校时状态无效。");
    *out={epoch,local,offset,m.captured(6),ntp};return true;
}
QVariantMap run(const QString &action,const QVariantMap &options,const services::Runner &runner,const std::function<bool()> &canWrite){
    QVariantMap result{{"ok",false},{"unknown",false}};
    if(action=="sample"){
        auto r=runner(sampleCommand());if(r.unknown){result["error"]="读取服务器时间超时或连接中断。";result["unknown"]=true;return result;}
        if(r.code!=0){result["error"]=reason(r);return result;}
        Sample sample;QString error;if(!parseSample(r.out,&sample,&error)){result["error"]=error;return result;}
        result["ok"]=true;result["epoch"]=sample.epoch;result["offset"]=sample.offsetSeconds;result["zone"]=sample.zone;result["ntp"]=sample.ntp;return result;
    }
    if(action!="set"){result["error"]="不支持的服务器时钟操作。";return result;}
    const auto value=options.value("local").toString();if(!validLocalDateTime(value)){result["error"]="日期时间无效。";return result;}
    services::Command probe;probe.timeoutMs=8000;probe.outputLimit=4096;
    probe.text="export LC_ALL=C SYSTEMD_PAGER=cat SYSTEMD_ASK_PASSWORD=0; t=no; d=no; n=unknown; "
               "if command -v timedatectl >/dev/null 2>&1; then t=yes; "
               "x=$(timedatectl show --property=NTP --value 2>/dev/null) && case \"$x\" in yes|no) n=$x;; esac; fi; "
               "if command -v date >/dev/null 2>&1; then d=yes; fi; printf 'TOOL|%s|%s\\nNTP|%s\\n' \"$t\" \"$d\" \"$n\"";
    auto state=runner(probe);if(state.unknown){result["error"]="无法确认自动校时状态，未提交修改。";return result;}
    if(state.code!=0){result["error"]="无法检查服务器命令；未提交修改。";return result;}
    const auto lines=state.out.trimmed().split('\n');
    if(lines.size()!=2||!lines[0].startsWith("TOOL|")||!lines[1].startsWith("NTP|")){result["error"]="服务器能力检查结果无效，未提交修改。";return result;}
    const auto tools=lines[0].split('|');if(tools.size()!=3||(tools[1]!="yes"&&tools[1]!="no")||(tools[2]!="yes"&&tools[2]!="no")){result["error"]="服务器能力检查结果无效，未提交修改。";return result;}
    const auto ntpRaw=lines[1].mid(4);if(ntpRaw!="yes"&&ntpRaw!="no"&&ntpRaw!="unknown"){result["error"]="自动校时状态无效，未提交修改。";return result;}
    const auto ntp=ntpValue(ntpRaw);if(ntp=="yes"&&!options.value("allowActiveNtp").toBool()){result["error"]="服务器自动校时已开启；请刷新后重新确认。";return result;}
    if(ntp=="unknown"&&!options.value("allowUnknown").toBool()){result["error"]="无法确认自动校时状态；请刷新后重新确认。";return result;}
    if(!canWrite||!canWrite()){result["error"]="操作已取消或应用已锁定，未提交修改。";return result;}
    if(ntp=="yes"&&tools[2]!="yes"){result["error"]="服务器缺少 date，自动校时开启时未提交修改。";return result;}
    if(tools[1]!="yes"&&tools[2]!="yes"){result["error"]="服务器缺少 timedatectl 和 date，未提交修改。";return result;}
    const auto operation=ntp!="yes"&&tools[1]=="yes"?QString("timedatectl set-time "):QString("date -s ");
    const auto command=operation+shellQuote(value);
    services::Command write;write.timeoutMs=15000;write.outputLimit=4096;write.mutation=true;
    write.text=("export LC_ALL=C SYSTEMD_PAGER=cat SYSTEMD_ASK_PASSWORD=0; if [ \"$(id -u)\" = 0 ]; then exec "+command+"; else exec sudo -n -- "+command+"; fi").toUtf8();
    const auto r=runner(write);if(r.unknown){result["unknown"]=true;result["error"]="修改结果未知（超时或断线），不会自动重试；请刷新后检查。";return result;}
    if(r.code!=0){result["error"]=reason(r)+"。请检查权限或自动校时状态；不会自动重试。";return result;}
    result["ok"]=true;return result;
}
}
