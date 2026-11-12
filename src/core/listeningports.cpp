#include "listeningports.h"
#include <QHostAddress>
namespace rs::listening {
services::Command command() {
    services::Command c;c.timeoutMs=2000;c.outputLimit=outputLimit;
    // No sudo, process scan, remote file or dependency installation. A failing
    // tool may have printed partial data; only a completed framed block is used.
    c.text="LC_ALL=C; export LC_ALL; "
           "if command -v ss >/dev/null 2>&1; then printf 'RS_SS\\n'; "
           "if ss -H -lntup 2>/dev/null; then printf '\\nRS_END\\n'; exit 0; fi; fi; "
           "if command -v netstat >/dev/null 2>&1; then printf '\\nRS_NETSTAT\\n'; "
           "if netstat -lntup 2>/dev/null; then printf '\\nRS_END\\n'; exit 0; fi; fi; "
           "printf '\\nRS_UNAVAILABLE\\n'; exit 0";
    return c;
}
namespace {
bool addressPort(const QString &text,QString &address,quint16 &port,bool peer=false) {
    const int colon=text.lastIndexOf(':');if(colon<1)return false;
    address=text.left(colon);if(address.startsWith('[')&&address.endsWith(']'))address=address.mid(1,address.size()-2);
    const auto suffix=text.mid(colon+1);bool ok=false;const uint n=suffix.toUInt(&ok);
    if(peer)return (suffix=="*" || (ok&&n==0))&&(address=="*"||address=="0.0.0.0"||address=="::");
    if(!ok||n==0||n>65535)return false;port=quint16(n);
    if(address=="*")return true;
    // QHostAddress accepts numeric IPv4/IPv6 and scoped IPv6. Never resolve DNS.
    return !QHostAddress(address).isNull();
}
}
Snapshot parse(const services::Result &r) {
    Snapshot s;
    if(r.unknown||r.code!=0||r.out.size()>=outputLimit){s.error="端口采集未完成（超时、输出过大或连接失败）；保留上次结果";return s;}
    const auto lines=QString::fromUtf8(r.out).split('\n');int begin=-1,end=-1;bool ss=false;
    for(int i=0;i<lines.size();++i) {
        if(lines[i]=="RS_SS"||lines[i]=="RS_NETSTAT"){begin=i+1;end=-1;ss=lines[i]=="RS_SS";}
        else if(lines[i]=="RS_END"&&begin>=0){end=i;break;}
        else if(lines[i]=="RS_UNAVAILABLE"){s.complete=true;s.error="监听端口不可用：服务器缺少可用的 ss / netstat 或权限不足";return s;}
    }
    if(begin<0||end<0){s.error="端口采集未完成；保留上次结果";return s;}
    s.source=ss?"ss":"netstat";QHash<QString,int> keys;int malformed=0;
    static const QRegularExpression pidName("\\(\\\"([^\\\"]*)\\\",pid=(\\d+)");
    for(int i=begin;i<end;++i) {
        const auto line=lines[i].trimmed();if(line.isEmpty()||line.startsWith("Active ")||line.startsWith("Proto "))continue;
        const auto p=line.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
        if(p.isEmpty())continue;const auto proto=p[0].toLower();
        if(proto!="tcp"&&proto!="tcp6"&&proto!="udp"&&proto!="udp6")continue;
        if(p.size()<(ss?6:5)){++malformed;continue;}
        const bool tcp=proto.startsWith("tcp");
        if(tcp && (ss?p[1]:p.value(5))!="LISTEN")continue;
        if(!tcp && ss && p[1]!="UNCONN")continue;
        Endpoint e;e.protocol=tcp?"TCP":"UDP";
        if(!addressPort(p[ss?4:3],e.address,e.port)){++malformed;continue;}
        if(!tcp){QString a;quint16 n=0;if(!addressPort(p[ss?5:4],a,n,true))continue;}
        if(ss){const auto match=pidName.match(line);if(match.hasMatch()){e.process=match.captured(1);e.pid=match.captured(2).toLongLong();}}
        else {const auto owner=p.value(tcp?6:5);const int slash=owner.indexOf('/');if(slash>0){bool ok=false;e.pid=owner.left(slash).toLongLong(&ok);if(!ok)e.pid=0;else e.process=owner.mid(slash+1);}}
        if(keys.contains(e.key())){auto &prior=s.endpoints[keys[e.key()]];if(!prior.pid&&e.pid){prior.pid=e.pid;prior.process=e.process;}continue;}
        if(s.endpoints.size()==maxRows){s.truncated=true;continue;}
        keys[e.key()]=int(s.endpoints.size());s.endpoints.append(e);
    }
    if(malformed&&s.endpoints.isEmpty()){s.error="端口输出格式无法识别；保留上次结果";return s;}
    s.complete=true;s.available=true;
    if(malformed)s.error="部分端口行格式无法识别";
    if(s.truncated)s.error="仅显示前 4096 个端点";
    return s;
}
}
