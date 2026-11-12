#include "../src/ui/workspace.h"
#include <QtTest>
namespace rs {
class ServiceMemorySecrets : public SecretBackend {
  public:
    QMutex mutex;QHash<QString,QByteArray> values;int writes=0,removes=0;
    SecretResult read(const QString &k) override {QMutexLocker l(&mutex);return values.contains(k)?SecretResult{SecretResult::Found,values[k],{}}:SecretResult{};}
    QString write(const QString &k,const QByteArray &v) override {QMutexLocker l(&mutex);values[k]=v;++writes;return {};}
    QString remove(const QString &k) override {QMutexLocker l(&mutex);values.remove(k);++removes;return {};}
};
class ServicesTests : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    Profile profile() {Profile p;p.name="Isolated fixture";p.host="127.0.0.1";p.port=qEnvironmentVariableIntValue("RABBITSHELL_SERVICE_PORT");p.user="fixture";p.directory="/";p.timeout=2;return p;}
    QVariantMap form() {return {{"name","fixture-created"},{"description","中文服务 100% $HOME \"描述\""},{"user","fixture"},{"program","/opt/中文 app/run"},{"directory","/opt/中文 app"},{"arguments","'hello world' '$HOME' '%n' ';' '$(touch bad)' 'a\"b' 'a\\\\b' ''"},{"restart",true}};}
    // Independent subset of systemd's quoted ExecStart tokenizer/escaping for the generated syntax.
    QStringList decodeExec(QString s) {
        QStringList out;QRegularExpression rx("\"((?:\\\\.|[^\"\\\\])*)\"");auto matches=rx.globalMatch(s);
        while(matches.hasNext()){auto token=matches.next().captured(1);QString value;for(int i=0;i<token.size();++i){if(token[i]=='\\'&&i+1<token.size())value+=token[++i];else value+=token[i];}value.replace("%%","%");value.replace("$$","$");out<<value;}return out;
    }
    QVariantMap detail(QString name="demo.service") {return {{"Id",name},{"Names",name},{"LoadState","loaded"},{"ActiveState","active"},{"SubState","running"},{"UnitFileState","enabled"},{"FragmentPath","/etc/systemd/system/"+name},{"Transient","no"},{"SourcePath",""},{"DropInPaths",""},{"TriggeredBy",""},{"CanReload","yes"},{"CanStart","yes"},{"CanStop","yes"}};}
    QByteArray props(const QVariantMap &d) {QByteArray b;for(auto i=d.cbegin();i!=d.cend();++i)b+=i.key().toUtf8()+"="+i.value().toString().toUtf8()+"\n";return b;}
    struct Mock {
        ServicesTests *t; QVector<services::Command> audit;QVariantMap d;QString failAction;bool unknown=false,changed=false;int showCount=0;bool created=false;
        services::Result operator()(const services::Command &c) {
            audit<<c;auto s=QString::fromUtf8(c.text);services::Result r;r.code=0;
            if(!failAction.isEmpty()&&s.contains(failAction)){r.code=1;r.err="permission denied";r.unknown=unknown;return r;}
            if(s.contains("'show'")) {++showCount;auto x=d;if(changed&&showCount>1)x["DropInPaths"]="/etc/systemd/system/demo.service.d/extra.conf";r.out=t->props(x);}
            else if(s.contains("'list-unit-files'"))r.out={};
            else if(s.contains("getent"))r.out="fixture:x:1000:1000:Fixture:/home/fixture:/bin/sh\n";
            else if(s.contains("id '-G'"))r.out="1000\n";
            else if(s.contains("sha256sum"))r.out="1:2:81a4:0:0:1:400:100:100\n"+QByteArray(64,'a')+"  -\n";
            else if(s.contains("stat '-L'"))r.out=s.contains("/run'")?"81ed:1000:1000\n":"41ed:1000:1000\n";
            if(s.contains("set -C")){created=true;d=t->detail("fixture-created.service");}
            if(s.contains("'stop'")){d["ActiveState"]="inactive";d["SubState"]="dead";}
            if(s.contains("'disable'"))d["UnitFileState"]="disabled";
            return r;
        }
    };
    QVariantMap job(Session &s,const QString &action,QVariantMap options={}) {
        QSignalSpy done(&s,&Session::completed);Job j;j.type="service";j.id=99;j.path=action;j.options=options;s.submit(j);
        QElapsedTimer time;time.start();while(done.isEmpty()&&time.elapsed()<22000)QTest::qWait(10);
        if(done.isEmpty())return {{"error","fixture result missing"}};return done.last()[2].toMap();
    }
    void trust(Session &s){connect(&s,&Session::hostKey,&s,[&s](const QString &,bool changed){if(!changed)s.trust(true);});}
  private slots:
    void initTestCase(){QCoreApplication::setOrganizationName("RabbitShellServiceTest");QCoreApplication::setApplicationName("isolated");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());qputenv("RABBITSHELL_DATA_DIR",temp.path().toUtf8());QApplication::setStyle("Fusion");for(auto font:{"C:/Windows/Fonts/msyh.ttc","C:/Windows/Fonts/consola.ttf"})if(QFileInfo::exists(font))QFontDatabase::addApplicationFont(font);QApplication::setFont(QFont("Microsoft YaHei UI",10));}
    void generatedUnitAndInjection() {
        auto f=form();const auto bytes=services::configuration(f);const auto d=services::properties(bytes);
        QCOMPARE(d["User"].toString(),"fixture");QCOMPARE(d["WorkingDirectory"].toString(),"/opt/中文 app");QCOMPARE(d["Description"].toString(),"中文服务 100%% $HOME \"描述\"");QCOMPARE(d["Type"].toString(),"simple");
        QCOMPARE(decodeExec(d["ExecStart"].toString()),QStringList({"/opt/中文 app/run","hello world","$HOME","%n",";","$(touch bad)","a\"b","a\\b",""}));
        f["interpreter"]="/usr/bin/python3";f["program"]="/opt/a $file%.py";auto args=decodeExec(services::properties(services::configuration(f))["ExecStart"].toString());QCOMPARE(args[0],"/usr/bin/python3");QCOMPARE(args[1],"/opt/a $file%.py");
        for(auto key:{"description","program","arguments","directory","user","name","interpreter"}) {auto bad=form();bad[key]="a\n[Service]\nExecStart=/bin/evil";bool rejected=false;try{services::configuration(bad);}catch(const QString &){rejected=true;}QVERIFY2(rejected,key);}
        for(auto v:{QString("/opt/end\\"),QString("/opt/trailing ")}){auto bad=form();bad["directory"]=v;bool rejected=false;try{services::configuration(bad);}catch(const QString &){rejected=true;}QVERIFY(rejected);}
        auto bad=form();bad["program"]="/opt/$bad";bool rejected=false;try{services::configuration(bad);}catch(const QString &){rejected=true;}QVERIFY(rejected);
        QVERIFY(!services::validUnit("bad.service;touch evil"));QVERIFY(!services::validUnit("../bad.service"));QVERIFY(services::validUnit("worker@instance.service"));
    }
    void createSeparateStepsFailureAndConflict() {
        for(bool enable:{false,true})for(bool start:{false,true}){auto f=form();f["enable"]=enable;f["start"]=start;Mock m{this};m.d={{"LoadState","not-found"}};auto r=services::run("create",f,[&](const auto &c){return m(c);});QVERIFY2(r["ok"].toBool(),qPrintable(r["error"].toString()));int writes=0,enables=0,starts=0;for(auto c:m.audit){if(c.text.contains("set -C")){++writes;QCOMPARE(c.input,services::configuration(f));QVERIFY(c.mutation);}enables+=c.text.contains("'enable'");starts+=c.text.contains("'start'");QVERIFY(!c.text.contains("--now"));QVERIFY(!c.text.contains("sudo"));}QCOMPARE(writes,1);QCOMPARE(enables,int(enable));QCOMPARE(starts,int(start));}
        Mock conflict{this};conflict.d=detail("fixture-created.service");auto r=services::run("create",form(),[&](auto c){return conflict(c);});QVERIFY(!r["ok"].toBool());for(auto c:conflict.audit)QVERIFY(!c.mutation);
        Mock partial{this};partial.d={{"LoadState","not-found"}};partial.failAction="'daemon-reload'";r=services::run("create",form(),[&](auto c){return partial(c);});QVERIFY(partial.created);QVERIFY(!r["ok"].toBool());QCOMPARE(r["steps"].toStringList().size(),1);
        Mock unknown{this};unknown.d=detail();unknown.failAction="'restart'";unknown.unknown=true;r=services::run("restart",{{"unit","demo.service"}},[&](auto c){return unknown(c);});QVERIFY(r["unknown"].toBool());QCOMPARE(unknown.audit.size(),2);
    }
    void deleteRestrictionsAndInterruptedSteps() {
        const QStringList fields{"FragmentPath","Names","Transient","SourcePath","DropInPaths","TriggeredBy","Id"};
        const QStringList values{"/usr/lib/systemd/system/demo.service","demo.service alias.service","yes","/run/generated","/etc/override.conf","demo.timer","worker@instance.service"};
        for(int i=0;i<fields.size();++i){auto d=detail();d[fields[i]]=values[i];QVERIFY(!services::deletionRestriction(d).isEmpty());}
        auto inspect=[&](Mock &m){return services::run("inspect-delete",{{"unit","demo.service"}},[&](auto c){return m(c);})["detail"].toMap();};
        Mock m{this};m.d=detail();auto expected=inspect(m);QVERIFY(!expected["fingerprint"].toString().isEmpty());m.audit.clear();auto r=services::run("delete",{{"unit","demo.service"},{"expected",expected}},[&](auto c){return m(c);});QVERIFY2(r["ok"].toBool(),qPrintable(r["error"].toString()));QStringList mutations;for(auto c:m.audit)if(c.mutation){mutations<<QString::fromUtf8(c.text);QVERIFY(!c.text.contains("rm -r"));}QCOMPARE(mutations.size(),4);QVERIFY(mutations[0].contains("'stop'"));QVERIFY(mutations[1].contains("'disable'"));QVERIFY(mutations[2].contains("rm --"));QVERIFY(mutations[2].contains("sha256sum"));QVERIFY(!mutations.join(' ').contains("/opt/"));
        for(auto failed:{QString("'stop'"),QString("'disable'"),QString("rm --"),QString("'daemon-reload'")}){Mock fail{this};fail.d=detail();auto e=inspect(fail);fail.failAction=failed;fail.audit.clear();r=services::run("delete",{{"unit","demo.service"},{"expected",e}},[&](auto c){return fail(c);});QVERIFY(!r["ok"].toBool());QVERIFY(QString::fromUtf8(fail.audit.last().text).contains(failed));}
        Mock change{this};change.d=detail();expected=inspect(change);change.changed=true;change.audit.clear();r=services::run("delete",{{"unit","demo.service"},{"expected",expected}},[&](auto c){return change(c);});QVERIFY(!r["ok"].toBool());for(auto c:change.audit)QVERIFY(!c.mutation);
        Mock link{this};link.d=detail();link.failAction="test -f";r=services::run("inspect-delete",{{"unit","demo.service"}},[&](auto c){return link(c);});QVERIFY(!r["ok"].toBool());
    }
    void qtIdentityHiddenRefreshThemesAndLifetime() {
        for(bool light:{false,true}){applyTheme(light);QWidget host;ServicesPanel p("预览服务器",true);auto l=new QVBoxLayout(&host);l->addWidget(p.toolbar);l->addWidget(&p);host.resize(800,430);host.show();QTest::qWait(20);auto old=p.items["nginx.service"];p.table->setCurrentItem(old);auto rows=QVariantList{old->data(0,Qt::UserRole),p.items["docker.service"]->data(0,Qt::UserRole)};p.snapshot(rows);QCOMPARE(p.items["nginx.service"],old);QCOMPARE(p.table->currentItem(),old);p.table->sortItems(0,Qt::DescendingOrder);p.snapshot(rows);QCOMPARE(p.table->currentItem(),old);p.search->setText("容器");QVERIFY(old->isHidden());p.search->clear();p.demo=false;p.ready=true;p.syncTimer();QVERIFY(p.timer.isActive());p.setActivity(false);QVERIFY(!p.timer.isActive());p.setActivity(true);p.hide();QVERIFY(!p.timer.isActive());p.demo=true;p.show();p.snapshot(rows);p.refreshTheme();auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",temp.path());QDir().mkpath(report);QVERIFY(host.grab().save(report+(light?"/services-light.png":"/services-dark.png")));host.resize(370,380);QTest::qWait(10);QVERIFY(host.grab().save(report+(light?"/services-light-narrow.png":"/services-dark-narrow.png")));l->removeWidget(&p);p.setParent(nullptr);}
        for(int i=0;i<3;++i){applyTheme(i==1);Workspace w(profile(),{},true);w.resize(1280,800);w.show();QTest::qWait(10);QCOMPARE(w.bottomTabs->count(),3);QVERIFY(w.serviceWorkers.isEmpty());w.bottomTabs->setCurrentIndex(2);QTest::qWait(10);if(i<2){auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",temp.path());QVERIFY(w.grab().save(report+(i==1?"/workspace-services-light.png":"/workspace-services-dark.png")));}w.maximizeTerminal->click();QVERIFY(w.terminalMaximized);w.hideTerminalPanel();QVERIFY(!w.servicesPanel->timer.isActive());}
    }
    void confirmationRefreshCannotDropMutation() {
        ServicesPanel p("fixture",true);p.show();p.demo=false;auto s=new Session(profile(),{},&p,Session::Mode::Commands);p.worker=s;p.ready=true;p.show();p.timer.stop();int boxes=0;QTimer responder;connect(&responder,&QTimer::timeout,&p,[&]{if(auto b=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())){++boxes;p.refresh();QVERIFY(!p.busy);b->button(QMessageBox::Yes)->click();}});responder.start(5);p.action("stop","demo.service");responder.stop();QCOMPARE(boxes,1);QVERIFY(p.busy);QCOMPARE(p.pendingUnit,"demo.service");QCOMPARE(p.pendingAction,"stop");QVERIFY(p.refreshAfter);
    }
    void serviceCredentialPersistenceAndStaleAuthentication() {
        auto backend=std::make_shared<ServiceMemorySecrets>();ConnectionRepository repo(nullptr,backend);auto p=profile();p.rememberSecret=true;repo.profiles={p};
        Workspace w(p,QByteArray("fixture-only"),true,nullptr,&repo);w.demo=false;w.persistCredential=true;w.rememberCredential=true;
        auto s=w.serviceSession(&w,Session::Mode::Commands);QVERIFY(s);s->authenticated();QTRY_VERIFY_WITH_TIMEOUT(!repo.pending(),5000);QCOMPARE(backend->writes,1);QVERIFY(repo.find(p.id)->rememberSecret);
        w.persistCredential=true;w.rememberCredential=false;auto forget=w.serviceSession(&w,Session::Mode::Commands);QVERIFY(forget);forget->authenticated();QTRY_VERIFY_WITH_TIMEOUT(!repo.pending(),5000);QCOMPARE(backend->removes,1);QVERIFY(!repo.find(p.id)->rememberSecret);
        w.persistCredential=true;w.rememberCredential=true;auto stale=w.serviceSession(&w,Session::Mode::Commands);QVERIFY(stale);repo.invalidate(p.id);stale->authenticated();QTest::qWait(10);QCOMPARE(backend->writes,1);QVERIFY(!w.servicesPanel->mutationGuard().isEmpty());
        w.stopServices();auto canceled=w.serviceSession(&w,Session::Mode::Commands);QVERIFY(canceled);w.stopServices();canceled->authenticated();QTest::qWait(10);QCOMPARE(backend->writes,1);
    }
    void quietFooterAndRealErrors_data(){QTest::addColumn<bool>("light");QTest::newRow("dark")<<false;QTest::newRow("light")<<true;}
    void quietFooterAndRealErrors(){
        QFETCH(bool,light);applyTheme(light);ServicesPanel p("合成离线服务列表",true);p.resize(1000,470);p.show();QTest::qWait(5);
        QVariantMap unknown{{"Id","unavailable.service"},{"UnitFileState","disabled"},{"QueryError","未返回完整属性"}};
        p.deliver("list",{},{{"ok",true},{"rows",QVariantList{detail(),unknown}},{"warning","46 项运行状态未知；没有自动重试"}});QTest::qWait(5);
        QVERIFY(p.status->isHidden());QVERIFY(p.status->text().isEmpty());QCOMPARE(p.items["unavailable.service"]->text(1),QString("未知 / 读取失败"));QVERIFY(p.items["unavailable.service"]->toolTip(1).contains("未返回完整"));
        const auto normalHeight=p.table->height();auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",temp.path());
        QVERIFY(p.grab().save(report+QString("/services-quiet-%1.png").arg(light?"light":"dark")));
        p.deliver("restart","demo.service",{{"ok",false},{"steps",QStringList{"已检查身份"}},{"error","权限不足，服务未重启；不会自动重试。"}});QTest::qWait(5);
        QVERIFY(!p.status->isHidden());QVERIFY(p.status->text().contains("权限不足"));QVERIFY(p.status->toolTip().contains("已检查身份"));QVERIFY(!p.status->wordWrap());QVERIFY(p.table->height()<normalHeight);
        p.deliver("list",{},{{"ok",true},{"rows",QVariantList{detail(),unknown}}});QVERIFY(!p.status->isHidden());
        QVERIFY(p.grab().save(report+QString("/services-error-%1.png").arg(light?"light":"dark")));
        p.deliver("restart","demo.service",{{"ok",true}});QVERIFY(p.status->isHidden());
        p.deliver("list",{},{{"ok",false},{"error","查询连接失败"}});QVERIFY(!p.status->isHidden());p.deliver("list",{},{{"ok",true},{"rows",QVariantList{detail()}}});QVERIFY(p.status->isHidden());
    }
    void malformedAccountAndQueryRecovery() {
        Mock m{this};m.d={{"LoadState","not-found"}};auto r=services::run("create",form(),[&](auto c){auto out=m(c);if(c.text.contains("getent"))out.out="fixture:x:not-a-uid:1000:Fixture:/home/fixture:/bin/sh\n";return out;});QVERIFY(!r["ok"].toBool());for(auto c:m.audit)QVERIFY(!c.mutation);
        ServicesPanel p("preview",true);p.deliver("list",{},{{"ok",false},{"error","temporary query error"}});QVERIFY(p.status->text().contains("temporary"));p.deliver("list",{},{{"ok",true},{"rows",QVariantList{}}});QVERIFY(!p.status->text().contains("temporary"));
    }
    void commandOnlyAndProtocolWorkflows() {
        if(!profile().port)QSKIP("Set RABBITSHELL_SERVICE_PORT to isolated loopback service fixture");
        Session s(profile(),"fixture-only",nullptr,Session::Mode::Commands);trust(s);QSignalSpy ready(&s,&Session::connected);s.start();QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);
        auto r=job(s,"list");QVERIFY2(r["ok"].toBool(),qPrintable(r["error"].toString()));auto rows=r["rows"].toList();QVERIFY(rows.size()>=3);bool installed=false,exited=false;for(auto v:rows){auto d=v.toMap();installed|=d["Id"]=="installed.service";if(d["Id"]=="oneshot.service")exited=services::stateText(d)=="已完成 / 退出";}QVERIFY(installed&&exited);
        auto f=form();f["enable"]=true;f["start"]=false;r=job(s,"create",f);QVERIFY2(r["ok"].toBool(),qPrintable(r["error"].toString()));QCOMPARE(r["detail"].toMap()["ActiveState"].toString(),"inactive");QCOMPARE(r["detail"].toMap()["UnitFileState"].toString(),"enabled");
        r=job(s,"restart",{{"unit","denied.service"}});QVERIFY(!r["ok"].toBool());QVERIFY(r["error"].toString().contains("exit 1"));
        r=job(s,"inspect-delete",{{"unit","fixture-created.service"}});auto expected=r["detail"].toMap();QVERIFY(r["ok"].toBool());r=job(s,"delete",{{"unit","fixture-created.service"},{"expected",expected}});QVERIFY2(r["ok"].toBool(),qPrintable(r["error"].toString()));s.stop();QVERIFY(s.wait(5000));
        Session files(profile(),"fixture-only");trust(files);QSignalSpy failed(&files,&Session::disconnected);files.start();QTRY_VERIFY_WITH_TIMEOUT(!failed.isEmpty(),10000);QVERIFY(failed[0][0].toString().contains("SFTP"));QVERIFY(files.wait(5000));
    }
    void templatesAndPartialListProtocol() {
        if(!profile().port)QSKIP("Set RABBITSHELL_SERVICE_PORT to isolated loopback service fixture");
        Session s(profile(),"fixture-only",nullptr,Session::Mode::Commands);trust(s);QSignalSpy connected(&s,&Session::connected);s.start();QTRY_COMPARE_WITH_TIMEOUT(connected.size(),1,10000);
        auto r=job(s,"list");QVERIFY2(r["ok"].toBool(),qPrintable(r["error"].toString()));
        QMap<QString,QVariantMap> rows;for(auto v:r["rows"].toList()){auto d=v.toMap();rows[d["Id"].toString()]=d;}
        const auto name=QString("apport-coredump-hook@.service");QVERIFY(rows.contains(name));QCOMPARE(rows[name]["UnitFileState"].toString(),"static");QVERIFY(!rows[name].contains("ActiveState"));QCOMPARE(services::stateText(rows[name]),"模板（需指定实例）");
        QCOMPARE(services::stateText(rows["apport-coredump-hook@worker.service"]),"运行中");QCOMPARE(rows["installed.service"]["ActiveState"].toString(),"inactive");QVERIFY(!rows["vanished.service"].contains("ActiveState"));QVERIFY(!r["warning"].toString().isEmpty());
        for(auto a:{"start","stop","restart","reload","enable","disable","logs","inspect-delete","delete"}){auto rejected=job(s,a,{{"unit",name}});QVERIFY(!rejected["ok"].toBool());QVERIFY(rejected["error"].toString().contains("模板"));}
        auto d=job(s,"detail",{{"unit",name}});QVERIFY(d["ok"].toBool());QCOMPARE(d["detail"].toMap()["Id"].toString(),name);
        r=job(s,"restart",{{"unit","apport-coredump-hook@worker.service"}});QVERIFY2(r["ok"].toBool(),qPrintable(r["error"].toString()));s.stop();QVERIFY(s.wait(5000));
    }
    void globalListFailureProtocol() {
        if(!profile().port)QSKIP("Set RABBITSHELL_SERVICE_PORT to isolated loopback service fixture");
        for(auto user:{"query-denied","no-manager"}){auto p=profile();p.user=user;Session s(p,"fixture-only",nullptr,Session::Mode::Commands);trust(s);QSignalSpy connected(&s,&Session::connected);s.start();QTRY_COMPARE_WITH_TIMEOUT(connected.size(),1,10000);auto r=job(s,"list");QVERIFY(!r["ok"].toBool());QVERIFY(!r.contains("rows"));QVERIFY(!r["error"].toString().isEmpty());s.stop();QVERIFY(s.wait(5000));}
    }
    void partialBatchBoundAndTemplateBackendGuards() {
        const QString name="apport-coredump-hook@.service";
        QVERIFY(services::validUnit(name));QVERIFY(services::isTemplate(name));QVERIFY(!services::isTemplate("apport-coredump-hook@worker.service"));
        for(auto action:{"start","stop","restart","reload","enable","disable","logs","inspect-delete","delete"}){
            int requests=0;auto r=services::run(action,{{"unit",name}},[&](const auto &){++requests;return services::Result{};});QVERIFY(!r["ok"].toBool());QVERIFY(r["error"].toString().contains("模板"));QCOMPARE(requests,0);
        }
        for(auto action:{"create","check-create"}){auto f=form();f["name"]=name;int requests=0;auto r=services::run(action,f,[&](const auto &){++requests;return services::Result{};});QVERIFY(!r["ok"].toBool());QVERIFY(r["error"].toString().contains("模板"));QCOMPARE(requests,0);}
        int details=0;auto d=services::run("detail",{{"unit",name}},[&](const auto &){++details;return services::Result{};});QVERIFY(d["ok"].toBool());QCOMPARE(details,0);
        for(int scenario=0;scenario<4;++scenario){
            int requests=0;auto r=services::run("list",{},[&](const services::Command &c){
                ++requests;services::Result out;out.code=0;
                if(c.text.contains("--property=Version"))out.out="Version=255\n";
                else if(c.text.contains("'list-unit-files'"))out.out="a.service enabled\nmiddle.service disabled\nz.service static\n";
                else if(c.text.contains("'list-units'"))out.out="";
                else {out.code=scenario==1?0:1;out.err="Failed to get properties: Unit middle.service not found.";
                    if(scenario==0 || scenario==1)out.out=props(detail("a.service"));
                    if(scenario==1)out.out+="\nId=middle.service\nLoadState=loaded\n\nId=z.service\nLoadState=not-found\nActiveState=inactive\nSubState=dead\n";
                    if(scenario==3){out.out=props(detail("a.service"));out.unknown=true;}
                }
                return out;
            });
            QCOMPARE(requests,4); // No per-unit retries, even if a failed name prevents reading later names.
            if(scenario>=2){QVERIFY(!r["ok"].toBool());QVERIFY(!r.contains("rows"));QCOMPARE(r["unknown"].toBool(),scenario==3);continue;}
            QVERIFY(r["ok"].toBool());QVERIFY(!r["warning"].toString().isEmpty());QMap<QString,QVariantMap> rows;for(auto v:r["rows"].toList()){auto d=v.toMap();rows[d["Id"].toString()]=d;}
            QCOMPARE(services::stateText(rows["a.service"]),"运行中");for(auto u:{"middle.service","z.service"}){QVERIFY(!rows[u].contains("ActiveState"));QCOMPARE(services::stateText(rows[u]),"未知 / 读取失败");}
        }
    }
    void templatePanelRestrictionsAndPartialWarning() {
        const QString name="apport-coredump-hook@.service";const QVariantMap templ{{"Id",name},{"UnitFileState","static"},{"Description","模板说明"}};
        auto missing=QVariantMap{{"Id","vanished.service"},{"UnitFileState","disabled"},{"QueryError","Unit vanished.service not found."}};
        Workspace w(profile(),{},true);w.resize(1360,800);w.show();w.bottomTabs->setCurrentIndex(2);auto p=w.servicesPanel;p->demo=false;p->snapshot({templ,detail("apport-coredump-hook@worker.service"),missing});
        p->deliver("list",{},{{"ok",true},{"rows",QVariantList{templ,detail("apport-coredump-hook@worker.service"),missing}},{"warning","部分服务属性查询未完成 · 1 项运行状态未知"}});QVERIFY(p->status->isHidden());QVERIFY(p->status->text().isEmpty());
        auto item=p->items[name];p->table->setCurrentItem(item);QCOMPARE(item->text(1),"模板（需指定实例）");QCOMPARE(item->text(2),"静态");QVERIFY(p->items["vanished.service"]->toolTip(1).contains("not found"));
        p->filter->setCurrentIndex(2);QVERIFY(item->isHidden());QVERIFY(p->items["vanished.service"]->isHidden());p->filter->setCurrentIndex(1);QVERIFY(item->isHidden());QVERIFY(!p->items["apport-coredump-hook@worker.service"]->isHidden());p->filter->setCurrentIndex(0);
        for(auto a:{"start","stop","restart","reload","enable","disable","logs","inspect-delete","delete"}){QVERIFY(!p->submit(a,{{"unit",name}}));p->action(a,name);QVERIFY(p->status->text().contains("需指定实例"));QVERIFY(!p->busy);}
        QTimer respond;int menus=0;connect(&respond,&QTimer::timeout,p,[&]{if(auto m=qobject_cast<QMenu *>(QApplication::activePopupWidget())){++menus;for(auto a:m->actions()){if(a->isSeparator())continue;const auto key=a->data().toString();QCOMPARE(a->isEnabled(),key=="detail"||key=="copy");if(!a->isEnabled())QVERIFY(a->toolTip().contains("需指定实例"));}m->close();}});respond.start(5);p->menu(name,p->mapToGlobal(QPoint(10,10)));respond.stop();QCOMPARE(menus,1);
        p->action("detail",name);auto dialogs=p->findChildren<QDialog *>();QVERIFY(!dialogs.isEmpty());auto content=dialogs.last()->findChild<QPlainTextEdit *>();QVERIFY(content&&content->toPlainText().contains("static")&&content->toPlainText().contains(name));dialogs.last()->reject();QVERIFY(!p->busy);
        auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",temp.path());QDir().mkpath(report);for(bool light:{false,true}){applyTheme(light);w.refreshTheme();p->deliver("list",{},{{"ok",true},{"rows",QVariantList{templ,detail("apport-coredump-hook@worker.service"),missing}},{"warning","部分服务属性查询未完成 · 1 项运行状态未知"}});QTest::qWait(10);QVERIFY(w.grab().save(report+(light?"/template-workspace-light.png":"/template-workspace-dark.png")));}
        p->deliver("list",{},{{"ok",true},{"rows",QVariantList{templ}}});QVERIFY(!p->status->text().contains("查询未完成"));
    }
    void remotePickerNavigationAndCancellation() {
        if(!profile().port)QSKIP("Set RABBITSHELL_SERVICE_PORT to isolated loopback service fixture");
        auto factory=[this](QObject *owner,Session::Mode mode){auto p=profile();p.user="files";auto s=new Session(p,"fixture-only",owner,mode);trust(*s);return s;};
        RemoteServicePicker p(factory,"/",false);p.show();QTRY_VERIFY_WITH_TIMEOUT(p.choose->isEnabled(),10000);QVERIFY(p.files->topLevelItemCount()>0);p.navigate("/folder");QVERIFY(!p.choose->isEnabled());QCOMPARE(p.files->topLevelItemCount(),0);QTRY_VERIFY_WITH_TIMEOUT(p.choose->isEnabled(),5000);QCOMPARE(p.current,"/folder");p.files->setCurrentItem(p.files->topLevelItem(0));p.acceptSelection();QCOMPARE(p.selectedPath(),"/folder/program");
        RemoteServicePicker cancel(factory,"/",true);cancel.show();QTRY_VERIFY_WITH_TIMEOUT(cancel.choose->isEnabled(),10000);QTest::mouseClick(cancel.crumbs,Qt::LeftButton,{},QPoint(cancel.crumbs->width()-4,5));QCOMPARE(cancel.address->currentWidget(),cancel.path);cancel.path->setText("/not-submitted");QTest::keyClick(cancel.path,Qt::Key_Escape);QCOMPARE(cancel.path->text(),"/");cancel.reject();QCOMPARE(cancel.selectedPath(),QString());
    }
    void workspaceServiceOnlyCredentialInvalidationAndShutdown() {
        if(!profile().port)QSKIP("Set RABBITSHELL_SERVICE_PORT to isolated loopback service fixture");
        Workspace w(profile(),QByteArray("fixture-only"),true);w.demo=false;w.servicesPanel->demo=false;w.servicesPanel->mutationGuard=[&]{return w.tunnelStartError();};w.show();w.bottomTabs->setCurrentIndex(2);QTRY_VERIFY_WITH_TIMEOUT(w.servicesPanel->ready,10000);QVERIFY(!w.online);QVERIFY(w.hasServiceConnection());QTRY_VERIFY_WITH_TIMEOUT(!w.servicesPanel->busy,10000);auto old=w.serviceWorkers.last();w.updateCredentials(w.profile,QByteArray("new-secret"));QVERIFY(!w.servicesPanel->ready);QTRY_VERIFY_WITH_TIMEOUT(!old||!old->isRunning(),5000);QVERIFY(!w.servicesPanel->submit("restart",{{"unit","demo.service"}}));QSignalSpy done(&w,&Workspace::shutdownComplete);w.shutdown();QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);
    }
    void transportOutputBoundTimeoutAndDisconnect() {
        if(!profile().port)QSKIP("Set RABBITSHELL_SERVICE_PORT to isolated loopback service fixture");
        for(auto unit:{QString("huge.service"),QString("drop.service"),QString("slow.service")}){Session s(profile(),"fixture-only",nullptr,Session::Mode::Commands);trust(s);QSignalSpy ready(&s,&Session::connected);s.start();QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);auto r=job(s,"logs",{{"unit",unit}});QVERIFY2(r["unknown"].toBool(),qPrintable(r["error"].toString()));QVERIFY(s.wait(5000));}
    }
};
}
int main(int argc,char **argv){qputenv("QT_QPA_PLATFORM","offscreen");QApplication app(argc,argv);rs::ServicesTests t;return QTest::qExec(&t,argc,argv);}
#include "test_services.moc"
