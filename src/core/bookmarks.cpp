#include "bookmarks.h"
#include "profile.h"
namespace rs {
namespace {
bool safeText(const QString &s,int limit) {
    if(s.size()>limit)return false;
    for(qsizetype i=0;i<s.size();++i){const auto c=s[i];
        if(c.category()==QChar::Other_Control || c==QChar(0x2028) || c==QChar(0x2029))return false;
        if(c.isHighSurrogate()){if(++i==s.size()||!s[i].isLowSurrogate())return false;}
        else if(c.isLowSurrogate())return false;
    }
    return true;
}
}
QString DirectoryBookmarkStore::normalizePath(const QString &path) {
    if(!path.startsWith('/') || !safeText(path,4096))return {};
    QStringList parts;
    for(const auto &part:path.split('/',Qt::SkipEmptyParts)) {
        if(part==".")continue;
        if(part=="..") return {};
        else parts<<part;
    }
    return '/'+parts.join('/');
}
DirectoryBookmarkStore::DirectoryBookmarkStore(const QString &file,QObject *parent):QObject(parent),fileName(file.isEmpty()?dataPath()+"/directory-bookmarks.json":file) {
    QFile f(fileName);
    if(!f.exists())return;
    auto invalid=[this]{writable=false;lastError="目录收藏文件无法读取或格式无效；原文件已保留。";};
    if(!f.open(QIODevice::ReadOnly)||f.size()>4*1024*1024){invalid();return;}
    QJsonParseError error;auto doc=QJsonDocument::fromJson(f.readAll(),&error);
    auto object=doc.object();
    if(error.error!=QJsonParseError::NoError || object["version"]!=1 || !object["profiles"].isObject()){invalid();return;}
    const auto all=object["profiles"].toObject();
    if(all.size()>2048){invalid();return;}
    for(auto it=all.begin();it!=all.end();++it) {
        if(it.key().isEmpty()||!safeText(it.key(),128)||!it.value().isArray()||it.value().toArray().size()>256){invalid();data.clear();return;}
        QSet<QString> ids,paths;
        data.insert(it.key(),{});
        for(auto v:it.value().toArray()) {
            auto o=v.toObject();DirectoryBookmark b{o["id"].toString(),o["path"].toString(),o["alias"].toString()};
            if(!o["alias"].isString()||b.id.isEmpty()||!safeText(b.id,128)||normalizePath(b.path)!=b.path||b.path.isEmpty()||!safeText(b.alias,256)||ids.contains(b.id)||paths.contains(b.path)){invalid();data.clear();return;}
            ids.insert(b.id);paths.insert(b.path);data[it.key()]<<b;
        }
    }
}
QList<DirectoryBookmark> DirectoryBookmarkStore::entries(const QString &profile) const {return accepts(profile)?data.value(profile):QList<DirectoryBookmark>{};}
std::optional<DirectoryBookmark> DirectoryBookmarkStore::entry(const QString &p,const QString &id) const {for(const auto &b:entries(p))if(b.id==id)return b;return {};}
bool DirectoryBookmarkStore::fail(const QString &message){lastError=message;emit saveFailed(message);return false;}
bool DirectoryBookmarkStore::commit(const QString &profile,const QList<DirectoryBookmark> &items) {
    if(!accepts(profile))return fail("连接已删除，无法修改其目录收藏。");
    if(!safeText(profile,128) || (!data.contains(profile) && data.size()>=2048))return fail("连接标识无效或目录收藏连接数已达上限。");
    if(!writable)return fail(lastError);
    auto next=data;next[profile]=items;QJsonObject all;
    for(auto it=next.cbegin();it!=next.cend();++it){QJsonArray a;for(const auto &b:it.value())a<<QJsonObject{{"id",b.id},{"path",b.path},{"alias",b.alias}};all[it.key()]=a;}
    const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"profiles",all}}).toJson();
    if(bytes.size()>4*1024*1024)return fail("目录收藏已达到文件容量上限。");
    QSaveFile f(fileName);
    if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()||!f.commit())return fail("目录收藏保存失败："+f.errorString());
    data=std::move(next);lastError.clear();emit changed(profile);return true;
}
bool DirectoryBookmarkStore::add(const QString &p,const QString &path) {
    const auto normalized=normalizePath(path);if(normalized.isEmpty())return fail("请选择有效的远端绝对目录路径。");
    if(!accepts(p))return fail("连接已删除，无法添加目录收藏。");
    auto items=entries(p);for(const auto &b:items)if(b.path==normalized){lastError.clear();return true;}
    if(items.size()>=256)return fail("每个连接最多保存 256 个目录收藏。");
    items<<DirectoryBookmark{QUuid::createUuid().toString(QUuid::WithoutBraces),normalized,{}};return commit(p,items);
}
bool DirectoryBookmarkStore::rename(const QString &p,const QString &id,const QString &alias) {
    if(!safeText(alias,256))return fail("别名过长或包含控制字符。");
    auto items=entries(p);for(auto &b:items)if(b.id==id){b.alias=alias.trimmed();return commit(p,items);}return fail("目录收藏已不存在。");
}
bool DirectoryBookmarkStore::remove(const QString &p,const QString &id) {auto items=entries(p);for(qsizetype i=0;i<items.size();++i)if(items[i].id==id){items.removeAt(i);return commit(p,items);}return fail("目录收藏已不存在。");}
bool DirectoryBookmarkStore::reorder(const QString &p,const QStringList &ids) {
    auto items=entries(p);QList<DirectoryBookmark> next;QSet<QString> seen;
    if(ids.size()!=items.size())return fail("目录收藏顺序已变化，请重试。");
    for(const auto &id:ids){auto b=entry(p,id);if(!b||seen.contains(id))return fail("目录收藏顺序无效。");seen.insert(id);next<<*b;}
    return commit(p,next);
}
}
