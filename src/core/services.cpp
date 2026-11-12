#include "services.h"
#include "profile.h"
#include <algorithm>
namespace rs::services {
namespace {
void check(bool ok, const QString &s) { if (!ok) throw s; }
QString clean(QString s, int max = 4096) {
    check(s.size() <= max, "输入超过长度上限");
    for (auto c : s) check(!c.isNull() && c.unicode() >= 32 && c.unicode() != 127 &&
                            c.category() != QChar::Other_Control && c != QChar(0x2028) && c != QChar(0x2029),
                            "字段不允许换行或控制字符");
    return s;
}
QString path(QString s) { clean(s); check(s.startsWith('/') && !s.endsWith('/') && !s.contains("//") &&
    !s.split('/').contains("..") && !s.split('/').contains("."), "请输入规范的远端绝对路径"); return s; }
QString quoted(QString s, bool dollar = false) {
    clean(s); s.replace('\\', "\\\\"); s.replace('"', "\\\""); s.replace('%', "%%");
    if (dollar) s.replace('$', "$$");
    return '"' + s + '"';
}
QString scalar(QString s) {
    clean(s);
    check(s==s.trimmed() && !s.endsWith('\\'),"标量字段不能以空白或反斜杠结尾，也不能以空白开头");
    return s.replace('%',"%%");
}
QString unitName(const QVariantMap &f) {
    auto n = f.value("name").toString();
    if (!n.endsWith(".service")) n += ".service";
    check(!isTemplate(n), templateRestriction());
    check(QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.-]{0,199}\\.service$").match(n).hasMatch(), "名称只允许字母、数字、点、短横线和下划线");
    return n;
}
QString command(QString tool, const QStringList &args) {
    QStringList a;
    for (const auto &s : args) a << shellQuote(clean(s, 16384));
    return tool + ' ' + a.join(' ');
}
const QString env = "export LC_ALL=C SYSTEMD_COLORS=0 SYSTEMD_PAGER=cat SYSTEMD_PAGERSECURE=1 SYSTEMD_ASK_PASSWORD=0; ";
QString ctl(const QStringList &args) { return command("systemctl", QStringList{"--system", "--no-pager", "--no-ask-password", "--full"} + args); }
const QString listProps = "Id,LoadState,ActiveState,SubState,UnitFileState,Description,CanStart,CanStop,CanReload";
const QString props = "Id,Names,LoadState,ActiveState,SubState,UnitFileState,Description,MainPID,ActiveEnterTimestamp,CanStart,CanStop,CanReload,FragmentPath,SourcePath,DropInPaths,Transient,TriggeredBy,Triggers,RequiredBy,WantedBy";
struct Flow {
    Runner runner; QStringList steps; bool unknown = false;QString currentStep;
    Result call(const QString &s, QByteArray input = {}, bool mutation = false, int timeout = 15000, int limit = 2*1024*1024, bool allowNonzero = false) {
        auto r = runner({(env + s).toUtf8(), input, timeout, limit, mutation});
        unknown |= r.unknown;
        check(!r.unknown, "结果未知，请刷新确认（超时、断线或输出超限）；变更不会自动重试");
        check(allowNonzero || r.code == 0, QString("命令失败（exit %1）：%2").arg(r.code).arg(QString::fromUtf8(r.err.isEmpty()?r.out:r.err).left(6000)));
        return r;
    }
    QVariantMap show(const QString &u) { return properties(call(ctl({"show", "--property="+props, "--", u})).out); }
    void step(const QString &label, const QString &s, QByteArray in = {}) {
        currentStep=label; call(s, in, true, 90000); steps << label;currentStep.clear();
    }
    QString fingerprint(const QString &u) {
        const auto p = "/etc/systemd/system/"+u;
        // Root-owned, non-shared directory/file required. Never follow a unit symlink.
        auto s = QString("p=%1; test ! -L /etc && test ! -L /etc/systemd && test ! -L /etc/systemd/system && "
            "test -f \"$p\" && test ! -L \"$p\" && test -w /etc/systemd/system && "
            "test \"$(stat -c %%u /etc/systemd/system)\" = 0 && "
            "test \"$(stat -c %%a /etc/systemd/system)\" = 755 && "
            "test \"$(stat -c %%u \"$p\")\" = 0 && test \"$(stat -c %%h \"$p\")\" = 1 && "
            "case \"$(stat -c %%a \"$p\")\" in 644|640|600|444|440|400) true;; *) false;; esac && "
            "stat -c '%%d:%%i:%%f:%%u:%%g:%%h:%%s:%%Y:%%Z' -- \"$p\" && sha256sum < \"$p\"").arg(shellQuote(p));
        s.replace("%%", "%");
        auto out = QString::fromUtf8(call(s, {}, false, 15000, 8192).out).trimmed();
        check(out.split('\n').size() == 2 && QRegularExpression("[a-fA-F0-9]{64}").match(out.section('\n',1)).hasMatch(), "无法可靠核对配置文件身份/内容");
        return out;
    }
    QVariantMap inspect(const QString &u) {
        auto d = show(u); auto why = deletionRestriction(d);
        if (why.isEmpty()) { d["fingerprint"] = fingerprint(u); }
        d["deleteReason"] = why; return d;
    }
};
bool running(const QVariantMap &d) { return d["ActiveState"] == "active" || d["ActiveState"] == "activating" || d["ActiveState"] == "deactivating" || d["ActiveState"] == "reloading"; }
void creationChecks(Flow &f, const QVariantMap &form, const QString &u) {
    auto d = f.show(u);
    check(d.value("LoadState") == "not-found" && d.value("FragmentPath").toString().isEmpty() &&
          d.value("Names").toString().split(' ',Qt::SkipEmptyParts).size() <= 1, "同名服务、别名或屏蔽项已存在，拒绝覆盖");
    auto installed = f.call(ctl({"list-unit-files", "--type=service", "--no-legend", "--", u})).out.trimmed();
    check(installed.isEmpty(), "已安装同名 unit，拒绝覆盖");
    const auto user = clean(form["user"].toString(), 128);
    check(QRegularExpression("^[A-Za-z_][A-Za-z0-9_.-]*[$]?$").match(user).hasMatch(), "必须明确指定已有运行用户");
    const auto account = f.call(command("getent", {"passwd", user})).out.trimmed().split(':');
    check(account.size() >= 7 && account[0] == user.toUtf8(), "运行账号不存在或无法检查");
    bool uidOk=false,gidOk=false;
    const auto uid = account[2].toUInt(&uidOk); account[3].toUInt(&gidOk);
    check(uidOk&&gidOk,"运行账号 UID/GID 无效");
    const auto groups = QString::fromUtf8(f.call(command("id", {"-G", "--", user})).out).trimmed().split(' ');
    for(const auto &g:groups){bool ok=false;g.toUInt(&ok);check(ok,"运行账号组 ID 无效");}
    QStringList paths{path(form["program"].toString())};
    const auto interpreter = form["interpreter"].toString();
    if (!interpreter.isEmpty()) paths << path(interpreter);
    const auto wd = form["directory"].toString();
    if (!wd.isEmpty() && wd != "/") paths << path(wd);
    QSet<QString> checked;
    for (int i=0; i<paths.size(); ++i) {
        QString p=paths[i]; bool leaf=true;
        while (!p.isEmpty()) {
            if (!checked.contains(p) || leaf) {
                const auto bits = QString::fromUtf8(f.call(command("stat", {"-L", "-c", "%f:%u:%g", "--", p})).out).trimmed().split(':');
                check(bits.size()==3, "无法读取路径权限"); bool ok=false;
                auto mode=bits[0].toUInt(&ok,16); check(ok, "无效权限数据");
                bool ownerOk=false,groupOk=false;bits[1].toUInt(&ownerOk);bits[2].toUInt(&groupOk);check(ownerOk&&groupOk,"无效属主/组数据");
                const bool dir = !leaf || (i==paths.size()-1 && !wd.isEmpty() && paths[i]==wd);
                check((mode & 0170000)==(dir?0040000:0100000), "程序必须是普通文件，工作目录必须是目录");
                unsigned need = (leaf && i==0 && !interpreter.isEmpty())?4:1;
                unsigned shift = uid==bits[1].toUInt()?6:groups.contains(bits[2])?3:0;
                check(uid==0 ? (need!=1 || dir || (mode & 0111)) : ((mode>>shift)&need)==need,
                      "运行用户缺少路径所需的读取/执行/目录搜索权限："+p);
                checked.insert(p);
            }
            leaf=false;
            if (p=="/") break;
            p=p.left(p.lastIndexOf('/')); if(p.isEmpty())p="/";
        }
    }
}
}
bool validUnit(const QString &u) {
    return u.size()<=255 && QRegularExpression("^[A-Za-z0-9_:][A-Za-z0-9_: .@\\\\-]*\\.service$").match(u).hasMatch() && !u.contains(' ');
}
bool isTemplate(const QString &u) { return validUnit(u) && u.endsWith("@.service"); }
QString templateRestriction() { return "模板（需指定实例）：请选择具体实例服务；本版不直接控制模板、读取模板日志或修改/删除共享模板，也不自动创建默认实例。"; }
QStringList arguments(const QString &s) {
    clean(s,16384); QStringList out; QString token; QChar quote; bool escape=false, started=false;
    for(auto c:s) {
        if(escape){token+=c;escape=false;started=true;continue;}
        if(c=='\\'){escape=true;started=true;continue;}
        if(!quote.isNull()){if(c==quote)quote=QChar();else token+=c;started=true;continue;}
        if(c=='\'' || c=='"'){quote=c;started=true;continue;}
        if(c==' '){if(started){out<<token;token.clear();started=false;}continue;}
        token+=c;started=true;
    }
    check(quote.isNull() && !escape,"参数引号或反斜杠未闭合"); if(started)out<<token;
    check(out.size()<=128,"参数最多 128 项"); return out;
}
QByteArray configuration(const QVariantMap &f) {
    unitName(f); auto user=clean(f["user"].toString(),128);
    check(QRegularExpression("^[A-Za-z_][A-Za-z0-9_.-]*[$]?$").match(user).hasMatch(),"必须指定已有运行用户");
    auto program=path(f["program"].toString()), interpreter=f["interpreter"].toString();
    auto executable=interpreter.isEmpty()?program:path(interpreter);
    // systemd forbids '$' in the executable token even though arguments support '$$'.
    check(!executable.contains('$'),"systemd 可执行路径不支持 $；请选择不含 $ 的程序/解释器路径");
    QStringList args=arguments(f["arguments"].toString()); if(!interpreter.isEmpty())args.prepend(program);
    QString exec=quoted(executable); for(const auto &a:args)exec+=' '+quoted(a,true);
    auto wd=f["directory"].toString(); if(!wd.isEmpty() && wd!="/")path(wd);
    QString s="# Created by RabbitShell; program and business data are never removed.\n[Unit]\nDescription="+scalar(f["description"].toString())+
        "\n\n[Service]\nType=simple\nUser="+user+"\nExecStart="+exec+"\n";
    if(!wd.isEmpty())s+="WorkingDirectory="+scalar(wd)+"\n";
    s+=QString("Restart=%1\nRestartSec=3s\n\n[Install]\nWantedBy=multi-user.target\n").arg(f["restart"].toBool()?"on-failure":"no");
    check(s.toUtf8().size()<=32768,"配置超过 32 KiB 上限");return s.toUtf8();
}
QVariantMap properties(const QByteArray &text) {
    QVariantMap r; for(const auto &line:QString::fromUtf8(text).split('\n')){auto p=line.indexOf('=');if(p>0)r[line.left(p)]=line.mid(p+1);}return r;
}
QString stateText(const QVariantMap &d) {
    if (isTemplate(d["Id"].toString())) return "模板（需指定实例）";
    if (!d["QueryError"].toString().isEmpty()) return "未知 / 读取失败";
    auto a=d["ActiveState"].toString(),s=d["SubState"].toString();
    if(a=="active" && s=="exited")return "已完成 / 退出";
    if(a=="active" && s=="running")return "运行中";
    if(a=="inactive")return "已停止";if(a=="failed")return "失败";
    if(a=="activating")return "启动中";if(a=="deactivating")return "停止中";
    if(a.isEmpty())return "未知";return a+" ("+s+")";
}
QString bootText(const QString &s) {
    static const QHash<QString,QString> map{{"enabled","已启用"},{"disabled","已禁用"},{"static","静态"},{"masked","已屏蔽"},{"indirect","间接"},{"enabled-runtime","临时启用"},{"linked","外部链接"},{"alias","别名"}};
    return map.value(s,s.isEmpty()?"未知":s);
}
QString deletionRestriction(const QVariantMap &d) {
    for(const auto &key:QStringList{"Id","Names","LoadState","ActiveState","SubState","UnitFileState","FragmentPath","Transient","SourcePath","DropInPaths","TriggeredBy"})if(!d.contains(key))return "来源/状态属性不完整，不能直接删除";
    const auto u=d["Id"].toString(); const auto names=d["Names"].toString().split(' ',Qt::SkipEmptyParts);
    if(!validUnit(u) || u.contains('@'))return "模板及实例不能直接删除";
    if(d["FragmentPath"].toString()!="/etc/systemd/system/"+u)return "仅支持 /etc/systemd/system 下独立普通文件；供应商或来源不确定的服务应按安装来源处理";
    if(d["LoadState"]!="loaded" || d["Transient"]=="yes" || !d["SourcePath"].toString().isEmpty() || !d["DropInPaths"].toString().isEmpty() || names.size()!=1 || names[0]!=u)
        return "别名、屏蔽、生成、瞬态或带 drop-in 的服务不能直接删除";
    if(!d["TriggeredBy"].toString().isEmpty())return "存在激活来源（"+d["TriggeredBy"].toString()+"）；请先处理 timer/socket 等关联";
    return {};
}
QVariantMap run(const QString &action,const QVariantMap &req,const Runner &runner) {
    Flow f{runner,{},false}; QVariantMap result{{"ok",false}};
    try {
        auto u=req["unit"].toString(); if(action!="list" && action!="create" && action!="check-create")check(validUnit(u),"无效 service unit 名称");
        if (isTemplate(u)) {
            check(action=="detail",templateRestriction());
            result["detail"]=QVariantMap{{"Id",u},{"Description",templateRestriction()}};
            result["ok"]=true;return result;
        }
        if(action=="list") {
            check(!f.call("command -v systemctl >/dev/null && "+ctl({"show","--property=Version"})).out.trimmed().isEmpty(),"无法确认实际 system manager；此服务器不支持服务管理");
            QMap<QString,QVariantMap> rows;
            auto installed=f.call(ctl({"list-unit-files","--type=service","--no-legend"})).out;
            for(const auto &line:QString::fromUtf8(installed).split('\n')) {
                auto cols=line.simplified().split(' '); if(cols.size()<2 || !validUnit(cols[0]))continue;
                rows[cols[0]]={{"Id",cols[0]},{"UnitFileState",cols[1]},{"Description",""}};
            }
            auto loaded=f.call(ctl({"list-units","--all","--type=service","--plain","--no-legend"})).out;
            for(const auto &line:QString::fromUtf8(loaded).split('\n')) {auto cols=line.simplified().split(' ');if(!cols.isEmpty() && validUnit(cols[0]))rows[cols[0]]["Id"]=cols[0];}
            check(rows.size()<=5000,"服务超过 5000 项上限");
            QStringList keys;
            for (auto it=rows.begin();it!=rows.end();++it) {
                if (isTemplate(it.key())) it.value()["Description"]=templateRestriction();
                else keys << it.key();
            }
            int successful=0, unreadable=0;
            QStringList errors;
            for(int i=0;i<keys.size();i+=96) {
                const auto batch=keys.mid(i,96);
                const QSet<QString> requested(batch.cbegin(),batch.cend());
                auto response=f.call(ctl(QStringList{"show","--property="+listProps,"--"}+batch),{},false,15000,2*1024*1024,true);
                const auto error=response.code==0?QString("未返回完整运行属性，服务可能已消失或不可查询"):
                    QString("属性查询失败（exit %1）：%2").arg(response.code).arg(QString::fromUtf8(response.err.isEmpty()?response.out:response.err).left(1500));
                QSet<QString> received;
                for(auto block:QString::fromUtf8(response.out).split("\n\n",Qt::SkipEmptyParts)) {
                    auto d=properties(block.toUtf8());auto id=d["Id"].toString();
                    if(!requested.contains(id) || received.contains(id) || d["LoadState"].toString().isEmpty() || d["LoadState"]=="not-found" ||
                       d["ActiveState"].toString().isEmpty() || d["SubState"].toString().isEmpty())continue;
                    if(d["UnitFileState"].toString().isEmpty())d["UnitFileState"]=rows[id]["UnitFileState"];
                    rows[id]=d;received.insert(id);++successful;
                }
                for (const auto &id:batch) if(!received.contains(id)) {rows[id]["QueryError"]=error;++unreadable;}
                if(response.code!=0 || received.size()!=batch.size())errors<<error;
            }
            check(keys.isEmpty() || successful>0,"全部具体服务的运行属性读取失败；"+errors.join('\n').left(4000));
            if(!errors.isEmpty())result["warning"]=QString("部分服务属性查询未完成 · %1 项运行状态未知；没有自动重试。\n").arg(unreadable)+errors.join('\n').left(4000);
            QVariantList list;for(auto d:rows)list<<d; result["rows"]=list;
        } else if(action=="detail" || action=="inspect-delete") {
            result["detail"]=action=="detail"?f.show(u):f.inspect(u);
        } else if(action=="logs") {
            result["text"]=QString::fromUtf8(f.call(command("journalctl",{"--system","--no-pager","--quiet","--output=short-iso","--lines=200","--unit="+u}),{},false,15000,256*1024).out);
        } else if(action=="create" || action=="check-create") {
            const auto bytes=configuration(req);u=unitName(req);creationChecks(f,req,u);
            if(action=="create") {
                const auto p="/etc/systemd/system/"+u;
                f.step("已创建配置 "+p, "p="+shellQuote(p)+"; test ! -e \"$p\" && test ! -L \"$p\" && (set -C; umask 022; cat > \"$p\")",bytes);
                f.step("已重新加载 system manager",ctl({"daemon-reload"}));
                auto d=f.show(u); check(d["Id"]==u && d["LoadState"]=="loaded" && d["FragmentPath"]==p,"配置已创建，但 manager 未按预期加载；请检查");
                if(req["enable"].toBool())f.step("已启用自启",ctl({"enable","--",u}));
                if(req["start"].toBool())f.step("已提交启动并完成等待",ctl({"start","--",u}));
                result["detail"]=f.show(u);
            } else result["text"]="已检查账号、路径、Unix 权限和名称冲突；ACL、安全策略、脚本 shebang/依赖与实际可启动性仍由服务器判定。未执行所选程序。";
        } else if(action=="delete") {
            auto d=f.inspect(u), expected=req["expected"].toMap();
            check(d["deleteReason"].toString().isEmpty(),d["deleteReason"].toString());
            check(!expected.isEmpty() && d==expected,"服务身份、来源、内容或状态已变化，请重新确认");
            if(running(d)) {
                f.step("已停止服务",ctl({"stop","--",u}));auto stopped=f.show(u);
                check(stopped["ActiveState"]=="inactive" || stopped["ActiveState"]=="failed","停止后服务仍活动，已中止删除");
            }
            auto before=f.inspect(u);check(before["fingerprint"]==expected["fingerprint"] && before["deleteReason"].toString().isEmpty(),"停止后来源或文件变化，已中止");
            f.step("已取消自启",ctl({"disable","--",u}));
            auto after=f.inspect(u);check(after["fingerprint"]==expected["fingerprint"] && after["deleteReason"].toString().isEmpty() && !running(after),"取消自启后来源、文件或运行状态变化，已中止");
            auto p="/etc/systemd/system/"+u;
            // Recheck stat and digest within the same shell invocation immediately before unlink.
            auto sig=expected["fingerprint"].toString().section('\n',0,0), hash=expected["fingerprint"].toString().section('\n',1).section(' ',0,0);
            f.step("已删除核对的 unit 文件（保留程序和业务数据）", "p="+shellQuote(p)+"; test -f \"$p\" && test ! -L \"$p\" && test \"$(stat -c '%d:%i:%f:%u:%g:%h:%s:%Y:%Z' -- \"$p\")\" = "+shellQuote(sig)+" && test \"$(sha256sum < \"$p\")\" = "+shellQuote(hash+"  -")+" && rm -- \"$p\"");
            f.step("已重新加载 system manager",ctl({"daemon-reload"}));result["detail"]=f.show(u);
        } else {
            check(QStringList{"start","stop","restart","reload","enable","disable"}.contains(action),"不允许的服务动作");
            auto d=f.show(u);check(d["Id"]==u,"服务身份变化");
            check(d["LoadState"]=="loaded" || action=="enable" || action=="disable","服务未加载或已屏蔽");
            if(action=="reload")check(d["CanReload"]=="yes","服务不支持 reload");
            if(action=="start" || action=="restart")check(d["CanStart"]!="no","服务不允许启动");
            if(action=="stop")check(d["CanStop"]!="no","服务不允许停止");
            if(action=="enable" || action=="disable")check(QStringList{"enabled","enabled-runtime","disabled"}.contains(d["UnitFileState"].toString()),"当前自启状态不支持直接切换");
            f.step("已完成 "+action,ctl({action,"--",u}));result["detail"]=f.show(u);
        }
        result["ok"]=true;
    } catch(const QString &why) { result["error"]=f.currentStep.isEmpty()?why:("未确认完成的步骤："+f.currentStep+"\n"+why+"\n该步骤可能部分完成，请检查实际状态；不会继续或自动重放。"); }
    result["steps"]=f.steps;result["unknown"]=f.unknown;
    return result;
}
}
