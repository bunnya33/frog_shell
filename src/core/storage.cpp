#include "storage.h"
#include <QSettings>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace rs {
namespace {
QString clean(const QString &p) { return QDir::cleanPath(QFileInfo(p).absoluteFilePath()); }
QString compare(const QString &p) {
    auto v=clean(p);
#ifdef Q_OS_WIN
    v=v.toCaseFolded();
#endif
    return v;
}
bool inside(const QString &p,const QString &base) { const auto a=compare(p),b=compare(base);return a==b||a.startsWith(b+"/"); }
bool noLinks(const QString &path) {
    auto p=clean(path);
    for (;;) {
        QFileInfo i(p);
        if(i.isSymLink() || i.isJunction()) return false;
        auto parent=QFileInfo(p).dir().absolutePath();
        if(parent==p) return true;
        p=parent;
    }
}
bool fail(QString *error,const QString &s) { if(error)*error=s;return false; }
bool copyTree(const QString &src,const QString &dst,QStringList &created,QString *error) {
    QFileInfo info(src);
    if(info.isSymLink()||info.isJunction())return fail(error,"配置中存在链接或联接点，迁移已停止："+src);
    if(info.isDir()) {
        if(!QDir().mkdir(dst))return fail(error,"无法创建目标目录："+dst);
        created.prepend(dst);
        QDir dir(src);
        for(const auto &name:dir.entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot))
            if(!copyTree(dir.filePath(name),QDir(dst).filePath(name),created,error))return false;
        return true;
    }
    if(!info.isFile())return fail(error,"配置包含无法迁移的特殊文件："+src);
#ifdef Q_OS_WIN
    // CopyFileW preserves the source security descriptor and never replaces an existing file.
    if(!CopyFileW(reinterpret_cast<LPCWSTR>(src.utf16()),reinterpret_cast<LPCWSTR>(dst.utf16()),TRUE))return fail(error,"无法复制配置："+src);
#else
    if(!QFile::copy(src,dst))return fail(error,"无法复制配置："+src);
#endif
    created.prepend(dst);
    QFile in(src),check(dst);QCryptographicHash original(QCryptographicHash::Sha256),copied(QCryptographicHash::Sha256);
    if(!in.open(QIODevice::ReadOnly)||!original.addData(&in)||!check.open(QIODevice::ReadOnly)||!copied.addData(&check)||copied.result()!=original.result())return fail(error,"配置校验失败："+dst);
    return true;
}
void rollback(const QStringList &created) { for(const auto &p:created) { QFileInfo i(p);if(i.isDir())QDir().rmdir(p);else QFile::remove(p); } }
const QStringList entries={"connections.json","directory-bookmarks.json","tunnels.json","known_hosts","application-lock.json","application-lock.required","drafts","clipboard-spool"};
const QString markerName=".frogshell-migration.json";
bool digestTree(const QString &path,QCryptographicHash &hash) {
    QFileInfo info(path);if(info.isSymLink()||info.isJunction())return false;
    if(info.isDir()) {
        hash.addData("D",1);QDir d(path);
        for(const auto &name:d.entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDir::Name)){
            const auto n=name.toUtf8();hash.addData(QByteArray::number(n.size()));hash.addData(":",1);hash.addData(n);
            if(!digestTree(d.filePath(name),hash))return false;
        }
        hash.addData("E",1);return true;
    }
    if(!info.isFile())return false;
    hash.addData("F",1);hash.addData(QByteArray::number(info.size()));hash.addData(":",1);
    QFile f(path);return f.open(QIODevice::ReadOnly)&&hash.addData(&f);
}
QByteArray digest(const QString &path){QCryptographicHash hash(QCryptographicHash::Sha256);return digestTree(path,hash)?hash.result().toHex():QByteArray();}
bool inventory(const QString &base,const QString &relative,QJsonObject &items){
    const auto path=QDir(base).filePath(relative);QFileInfo info(path);
    if(info.isSymLink()||info.isJunction())return false;
    if(info.isDir()){
        QDir d(path);for(const auto &name:d.entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot))
            if(!inventory(base,relative+"/"+name,items))return false;
        return true;
    }
    const auto hash=digest(path);if(hash.isEmpty())return false;
    items[relative]=QString::fromLatin1(hash);return true;
}
void cleanupStage(const QString &path,const QJsonObject &items){
    for(auto it=items.begin();it!=items.end();++it){
        const auto rel=QDir::fromNativeSeparators(it.key());
        if(QDir::isAbsolutePath(rel)||QDir::cleanPath(rel)!=rel||rel==".."||rel.startsWith("../"))continue;
        const auto file=QDir(path).filePath(rel);if(!inside(file,path)||compare(file)==compare(path))continue;
        if(noLinks(file)&&QFileInfo(file).isFile()&&digest(file)==it.value().toString().toLatin1())QFile::remove(file);
    }
    // Remove empty directories only; any unfamiliar or modified file remains untouched.
    std::function<void(const QString&)> emptyDirs=[&](const QString &dir){
        QDir d(dir);for(const auto &i:d.entryInfoList(QDir::Dirs|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot))
            if(!i.isSymLink()&&!i.isJunction())emptyDirs(i.absoluteFilePath());
        QDir().rmdir(dir);
    };
    if(noLinks(path)&&QFileInfo(path).isDir())emptyDirs(path);
}
bool saveJournal(const QString &target,const QJsonObject &journal,QString *error){
    QSaveFile f(QDir(target).filePath(markerName));f.setDirectWriteFallback(false);const auto bytes=QJsonDocument(journal).toJson(QJsonDocument::Compact);
    return (f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit())||fail(error,"无法保存迁移恢复记录。");
}
bool inspectTarget(const QString &source,const QString &target,bool locked,QJsonObject *journal,QString *error){
    QDir dir(target);auto names=dir.entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot);
    if(locked&&!names.removeOne("instance.lock"))return fail(error,"目标目录锁不可用。");
    if(names.isEmpty()){*journal={};return true;}
    if(!names.removeOne(markerName))return fail(error,"请选择空目录，已有文件不会被覆盖。");
    // A killed migration can leave its QLockFile; acquire() will check/remove
    // a stale lock after this read-only validation, before any recovery writes.
    if(!locked)names.removeOne("instance.lock");
    QFile f(dir.filePath(markerName));if(!noLinks(f.fileName())||!f.open(QIODevice::ReadOnly)||f.size()>1024*1024)return fail(error,"迁移恢复记录无法读取。");
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(f.readAll(),&parse);const auto j=doc.object();
    if(parse.error!=QJsonParseError::NoError||j.value("version")!=1||compare(j.value("source").toString())!=compare(source)||!j.value("stages").isArray()||!j.value("published").isArray())return fail(error,"迁移恢复记录无效，未改动目标目录。");
    static const QRegularExpression stagePattern("^\\.frogshell-stage-[0-9a-f-]{36}$");
    for(const auto &v:j.value("stages").toArray()){
        const auto n=v.toString();if(!stagePattern.match(n).hasMatch())return fail(error,"迁移恢复目录无效。");
        if(QFileInfo::exists(dir.filePath(n))){if(!noLinks(dir.filePath(n))||!QFileInfo(dir.filePath(n)).isDir())return fail(error,"迁移恢复目录无效。");names.removeOne(n);}
    }
    const auto current=j.value("current").toString();
    if(!j.value("stages").toArray().contains(current))return fail(error,"迁移恢复记录无效。");
    const auto allowed=entries+QStringList{"settings"};
    for(const auto &v:j.value("published").toArray()){
        const auto o=v.toObject();const auto n=o.value("name").toString();const auto expected=o.value("sha256").toString().toLatin1();
        if(!allowed.contains(n)||expected.size()!=64)return fail(error,"迁移恢复记录无效。");
        if(QFileInfo::exists(dir.filePath(n))){if(digest(dir.filePath(n))!=expected)return fail(error,"迁移中的文件已变化，未覆盖或删除。");names.removeOne(n);}
    }
    if(!names.isEmpty())return fail(error,"目标目录包含其他文件，未覆盖或删除。");
    *journal=j;return true;
}
}
QString defaultDataPath(){return clean(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));}
bool storageOverride(){return !qEnvironmentVariable("RABBITSHELL_DATA_DIR").isEmpty();}
QString storagePointerFile(){return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("storage-location.json");}
bool resolveStoragePath(QString *path,QString *error){
    if(storageOverride()){*path=clean(qEnvironmentVariable("RABBITSHELL_DATA_DIR"));return true;}
    QFile file(storagePointerFile());if(!file.exists()){*path=defaultDataPath();return true;}
    if(!file.open(QIODevice::ReadOnly)||file.size()>32768)return fail(error,"无法读取配置目录定位信息，请从备份恢复。");
    QJsonParseError parse;auto doc=QJsonDocument::fromJson(file.readAll(),&parse);
    const auto p=doc.object().value("directory").toString();
    if(parse.error!=QJsonParseError::NoError||!doc.isObject()||!QDir::isAbsolutePath(p)||!QFileInfo(p).isDir()||!noLinks(p))return fail(error,"配置目录定位信息无效或目录不可用，请检查后重试。");
    *path=clean(p);return true;
}
void configureStorageSettings(const QString &path){QSettings::setDefaultFormat(QSettings::IniFormat);const auto dir=QDir(path).filePath("settings");QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir);QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,dir);}
bool validateStorageTarget(const QString &source,const QString &target,QString *error){
    if(target.isEmpty()||!QDir::isAbsolutePath(target))return fail(error,"请选择绝对目录。");
    if(inside(target,source)||inside(source,target))return fail(error,"新目录不能与当前目录相同、嵌套或包含当前目录。");
    if(!QFileInfo(target).isDir()||!noLinks(target)||!noLinks(source))return fail(error,"目录不存在或路径包含链接、联接点。");
    QJsonObject journal;if(!inspectTarget(source,target,false,&journal,error))return false;
    QTemporaryFile probe(QDir(target).filePath(".frogshell-write-XXXXXX"));if(!probe.open())return fail(error,"新目录不可写。");
    return true;
}
bool migrateStorage(const QString &source,const QString &target,QString *error,bool targetLocked,const QString &pointerOverride){
    // Caller holds source and destination instance locks and has destroyed all storage owners.
    if(inside(target,source)||inside(source,target)||!QFileInfo(target).isDir()||!noLinks(target)||!noLinks(source))return fail(error,"目标目录无效或与源目录重叠。");
    QJsonObject journal;if(!inspectTarget(source,target,targetLocked,&journal,error))return false;
    QDir destination(target);
    if(!journal.isEmpty()){
        const auto oldStage=destination.filePath(journal.value("current").toString());
        if(!QFileInfo(oldStage).isDir()&&(!journal.value("published").toArray().isEmpty()||!QDir().mkdir(oldStage)))return fail(error,"迁移恢复目录丢失。");
        for(const auto &v:journal.value("published").toArray()){
            const auto n=v.toObject().value("name").toString();const auto from=destination.filePath(n),to=QDir(oldStage).filePath(n);
            if(QFileInfo::exists(from)&&(QFileInfo::exists(to)||!QDir().rename(from,to)))return fail(error,"无法恢复上次迁移，请保留目标目录。");
        }
    } else journal={{"version",1},{"source",clean(source)},{"stages",QJsonArray{}},{"published",QJsonArray{}}};
    const auto stageName=".frogshell-stage-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto stages=journal.value("stages").toArray();stages.append(stageName);journal["stages"]=stages;journal["current"]=stageName;journal["published"]=QJsonArray{};
    QJsonObject stageItems;
    for(const auto &name:entries){QFileInfo info(QDir(source).filePath(name));if((info.exists()||info.isSymLink()||info.isJunction())&&!inventory(source,name,stageItems))return fail(error,"配置包含无法迁移的链接或文件。");}
    auto inventories=journal.value("inventories").toObject();inventories[stageName]=stageItems;journal["inventories"]=inventories;
    if(!saveJournal(target,journal,error))return false;
    const auto stage=destination.filePath(stageName);if(!QDir().mkdir(stage))return fail(error,"无法创建迁移暂存目录。");
    QSettings old;old.sync();if(old.status()!=QSettings::NoError)return fail(error,"无法同步原界面设置。");
    const auto sourceSettings=old.fileName();
    const auto sourceSettingsHash=QFileInfo(sourceSettings).isFile()?digest(sourceSettings):QByteArray();
    QStringList created;
    for(const auto &name:entries){auto from=QDir(source).filePath(name);QFileInfo info(from);if((info.exists()||info.isSymLink()||info.isJunction())&&!copyTree(from,QDir(stage).filePath(name),created,error))return false;}
    // Export every key through QVariant, preserving lists, byte arrays and typed values.
    auto settingsDir=QDir(stage).filePath("settings");
    if(!QDir().mkpath(settingsDir)){rollback(created);return fail(error,"无法创建界面设置目录。");}
    created.prepend(settingsDir);
    auto org=QDir(settingsDir).filePath(QCoreApplication::organizationName());
    if(!QDir().mkpath(org)){rollback(created);return fail(error,"无法创建界面设置目录。");}
    created.prepend(org);
    const auto ini=QDir(org).filePath(QCoreApplication::applicationName()+".ini");
    // Qt's usual INI writer turns numeric scalars and numeric QVariantLists into
    // strings. Emit Qt's own @Variant representation for every value so the
    // first read in a fresh process retains the registry value's exact type.
    const auto scratch=QDir(stage).filePath("settings-template.ini");
    QHash<QByteArray,QByteArray> replacements;
    {
        QSettings templ(scratch,QSettings::IniFormat);templ.setFallbacksEnabled(false);
        for(const auto &key:old.allKeys()){
            const auto value=old.value(key);QByteArray binary;QDataStream stream(&binary,QIODevice::WriteOnly);
            const bool dateTime=value.metaType().id()==QMetaType::QDateTime;
            stream.setVersion(dateTime?QDataStream::Qt_5_6:QDataStream::Qt_4_0);stream<<value;
            if(stream.status()!=QDataStream::Ok)return fail(error,"无法编码界面设置："+key);
            QByteArray encoded=dateTime?"@DateTime(":"@Variant(";
            for(const auto byte:binary){encoded+="\\x";encoded+=QByteArray(1,byte).toHex();}encoded+=')';
            const auto token=QByteArray("FROGSHELL_")+QUuid::createUuid().toByteArray(QUuid::Id128);
            replacements.insert(token,encoded);templ.setValue(key,QString::fromLatin1(token));
        }
        templ.sync();if(templ.status()!=QSettings::NoError)return fail(error,"无法保存界面设置。");
    }
    QFile templateFile(scratch);QByteArray iniBytes;
    if(templateFile.exists()) {if(!templateFile.open(QIODevice::ReadOnly))return fail(error,"无法读取界面设置暂存文件。");iniBytes=templateFile.readAll();templateFile.close();}
    for(auto it=replacements.cbegin();it!=replacements.cend();++it){if(!iniBytes.contains(it.key()))return fail(error,"界面设置编码失败。");iniBytes.replace(it.key(),it.value());}
    QSaveFile iniFile(ini);iniFile.setDirectWriteFallback(false);
    if(!iniFile.open(QIODevice::WriteOnly)||iniFile.write(iniBytes)!=iniBytes.size()||!iniFile.commit())return fail(error,"无法保存界面设置。");
    QFile::remove(scratch);created.prepend(ini);
    QSettings check(ini,QSettings::IniFormat);check.setFallbacksEnabled(false);
    for(const auto &key:old.allKeys())if(!check.contains(key)||check.value(key).metaType()!=old.value(key).metaType()||check.value(key)!=old.value(key)){rollback(created);return fail(error,"界面设置校验失败："+key);}
    if(QFileInfo::exists(ini)){stageItems[QDir(stage).relativeFilePath(ini)]=QString::fromLatin1(digest(ini));inventories[stageName]=stageItems;journal["inventories"]=inventories;if(!saveJournal(target,journal,error))return false;}
    QJsonArray published;
    for(const auto &name:QDir(stage).entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot)){
        const auto hash=digest(QDir(stage).filePath(name));if(hash.isEmpty())return fail(error,"无法校验迁移暂存内容。");
        published.append(QJsonObject{{"name",name},{"sha256",QString::fromLatin1(hash)}});
    }
    journal["published"]=published;if(!saveJournal(target,journal,error))return false;
    for(const auto &v:published){const auto name=v.toObject().value("name").toString();if(QFileInfo::exists(destination.filePath(name))||!QDir().rename(QDir(stage).filePath(name),destination.filePath(name)))return fail(error,"无法发布迁移文件；可重试，原配置未改动。");}
    const auto pointer=pointerOverride.isEmpty()?storagePointerFile():pointerOverride;
    if(!QDir().mkpath(QFileInfo(pointer).absolutePath()))return fail(error,"无法创建启动定位目录。");
    QSaveFile out(pointer);out.setDirectWriteFallback(false);const auto bytes=QJsonDocument(QJsonObject{{"directory",clean(target)}}).toJson(QJsonDocument::Compact);
    if(!out.open(QIODevice::WriteOnly)||out.write(bytes)!=bytes.size()||!out.commit())return fail(error,"无法提交配置目录定位信息，原配置未改动。");
    for(const auto &v:stages){const auto name=v.toString();cleanupStage(destination.filePath(name),inventories.value(name).toObject());}
    bool stagesRemain=false;for(const auto &v:stages)if(QFileInfo::exists(destination.filePath(v.toString())))stagesRemain=true;
    if(!stagesRemain)QFile::remove(destination.filePath(markerName));
    // Delete only inventoried files whose contents still match the copied data.
    // In particular, do not recursively remove a user-selected directory.
    for(auto it=stageItems.constBegin();it!=stageItems.constEnd();++it){
        const auto rel=it.key();const auto top=rel.section('/',0,0);
        if(!entries.contains(top)||(top=="application-lock.required"&&compare(source)==compare(defaultDataPath())))continue;
        const auto file=QDir(source).filePath(rel);
        if(noLinks(file)&&QFileInfo(file).isFile()&&digest(file)==it.value().toString().toLatin1())QFile::remove(file);
    }
    for(const auto &name:{QString("drafts"),QString("clipboard-spool")}){
        const auto dir=QDir(source).filePath(name);if(noLinks(dir)&&QFileInfo(dir).isDir())cleanupStage(dir,{});
    }
    const auto oldOrg=QDir(source).filePath("settings/"+QCoreApplication::organizationName());
    const auto oldIni=QDir(oldOrg).filePath(QCoreApplication::applicationName()+".ini");
    if(compare(sourceSettings)==compare(oldIni)&&!sourceSettingsHash.isEmpty()&&noLinks(oldIni)&&digest(oldIni)==sourceSettingsHash)QFile::remove(oldIni);
    QDir().rmdir(oldOrg);QDir().rmdir(QDir(source).filePath("settings"));
    return true;
}
}
