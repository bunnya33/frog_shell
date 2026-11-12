#include "../src/ui/filedrag_win.h"
#include "../src/ui/fileicons.h"
#include "../src/ui/workspace.h"
#include <QtTest>
#include <thread>
#include <shlobj.h>
namespace rs {
template<class T> struct Com {
    T *p=nullptr;~Com(){if(p)p->Release();}T *operator->()const{return p;}T **out(){return &p;}
};
class NativeFileDragTests : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    QString report;
    FORMATETC format(const wchar_t *name,LONG index=-1,DWORD tymed=TYMED_HGLOBAL){return {CLIPFORMAT(RegisterClipboardFormatW(name)),nullptr,DVASPECT_CONTENT,index,tymed};}
    std::shared_ptr<FileDragBatch> batch(quint64 bytes=7){return FileDragBatch::create({{"/sample.txt","sample.txt",bytes,1}});}
    void stream(IDataObject *data,int index,Com<IStream> &out){auto f=format(CFSTR_FILECONTENTS,index,TYMED_ISTREAM);STGMEDIUM value{};QCOMPARE(data->GetData(&f,&value),S_OK);out.p=value.pstm;}
    void settle(){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();}
    static QByteArray hashFile(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash hash(QCryptographicHash::Sha256);char bytes[65536];while(auto n=f.read(bytes,sizeof(bytes))){if(n<0)return {};hash.addData(QByteArrayView(bytes,n));}return hash.result();}
  private slots:
    void initTestCase(){
        QVERIFY(root.isValid());qputenv("RABBITSHELL_DATA_DIR",root.path().toUtf8());QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RabbitShellNativeDragTest");QCoreApplication::setApplicationName("RabbitShellNativeDragTest");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());
        for(auto font:{"C:/Windows/Fonts/msyh.ttc","C:/Windows/Fonts/consola.ttf"})QFontDatabase::addApplicationFont(font);
        QApplication::setFont(QFont("Microsoft YaHei UI",10));applyTheme(false);QVERIFY(SUCCEEDED(OleInitialize(nullptr)));
        report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",root.path());QDir().mkpath(report);
    }
    void cleanupTestCase(){OleUninitialize();}
    void namesAndBounds(){
        QList<DragFile> files{{"/a/CON.txt","CON.txt",1},{"/b/CON.txt","CON.txt",2},{"/b/CON.txt","duplicate",2},{"/x/a:b?","../a:b? ",3},{"/x/long",QString(300,QChar(u'中')),4}};
        auto b=FileDragBatch::create(files);QVERIFY(b);QCOMPARE(b->files.size(),4);QSet<QString> names;
        for(auto f:b->files){QVERIFY(f.name.size()<260);QVERIFY(!f.name.contains(QRegularExpression("[<>:\"/\\\\|?*]")));QVERIFY(!f.name.endsWith('.'));QVERIFY(!f.name.endsWith(' '));QVERIFY(!names.contains(f.name.toCaseFolded()));names.insert(f.name.toCaseFolded());}
        QCOMPARE(b->files[0].name,QString("_CON.txt"));QCOMPARE(b->files[1].name,QString("_CON (1).txt"));QVERIFY(!FileDragBatch::create(QList<DragFile>(257,files[0])));
        QVERIFY(!FileDragBatch::create({{"relative","bad",0}}));
        auto duplicate=FileDragBatch::create({{"/a/archive.tar.gz","archive.tar.gz",1},{"/b/archive.tar.gz","archive.tar.gz",2},{"/c/archive.tar.gz","archive.tar.gz",3}});
        QVERIFY(duplicate);QCOMPARE(duplicate->files[1].name,QString("archive (1).tar.gz"));QCOMPARE(duplicate->files[2].name,QString("archive (2).tar.gz"));
    }
    void internalMoveMimeExtraction(){
        const QByteArray token("12345678-90ab-cdef-1234-567890abcdef");
        const QString direct=QString::fromLatin1(internalFileMoveMimeType());
        const QString bridge=QStringLiteral("application/x-qt-windows-mime;value=\"RabbitShell.InternalFileMove.v1\"");
        QCOMPARE(internalFileMoveMimeType(),QByteArray("application/x-rabbitshell-internal-move"));
        QVERIFY(internalFileMoveToken(nullptr).isEmpty());
        QMimeData mime;mime.setUrls({QUrl::fromLocalFile("C:/fake/source.txt")});
        QVERIFY(internalFileMoveToken(&mime).isEmpty());
        mime.setData(direct,token);QCOMPARE(internalFileMoveToken(&mime),token);
        for(const auto &bad:{token.toUpper(),token.left(35),token+QByteArray(1,'\0'),QByteArray("12345678_90ab-cdef-1234-567890abcdef"),QByteArray("12345678-90ab-cdef-1234-567890abcdeg")}){
            mime.setData(direct,bad);QVERIFY(internalFileMoveToken(&mime).isEmpty());
        }
        mime.removeFormat(direct);mime.setData(bridge,token);QCOMPARE(internalFileMoveToken(&mime),token);
        mime.setData(bridge,token+QByteArray(28,'\0'));QCOMPARE(internalFileMoveToken(&mime),token);
        mime.setData(bridge,token+QByteArray(29,'\0'));QVERIFY(internalFileMoveToken(&mime).isEmpty());
        mime.setData(bridge,token+QByteArray("\0x",2));QVERIFY(internalFileMoveToken(&mime).isEmpty());
        auto embedded=token;embedded[5]='\0';mime.setData(bridge,embedded);QVERIFY(internalFileMoveToken(&mime).isEmpty());
    }
    void internalMoveFormatAndEffects(){
        const QByteArray token("12345678-90ab-cdef-1234-567890abcdef");
        QCOMPARE(fileDragAllowedEffects({}),DWORD(DROPEFFECT_COPY));
        QCOMPARE(fileDragAllowedEffects(token.toUpper()),DWORD(DROPEFFECT_COPY));
        QCOMPARE(fileDragAllowedEffects(token),DWORD(DROPEFFECT_COPY|DROPEFFECT_MOVE));
        for(const auto &candidate:{QByteArray(),token.toUpper(),token}){
            auto b=batch();b->internalMoveToken=candidate;int fetches=0;b->fetch=[&](int,const QString &,auto){++fetches;};
            Com<IDataObject> data;data.p=createFileDataObject(b);auto f=format(L"RabbitShell.InternalFileMove.v1");
            const bool valid=candidate==token;
            QCOMPARE(data->QueryGetData(&f),valid?S_OK:DV_E_FORMATETC);
            Com<IEnumFORMATETC> formats;QCOMPARE(data->EnumFormatEtc(DATADIR_GET,formats.out()),S_OK);
            FORMATETC entry{};ULONG fetched=0;int occurrences=0;
            while(formats->Next(1,&entry,&fetched)==S_OK)if(entry.cfFormat==f.cfFormat){++occurrences;QCOMPARE(entry.lindex,LONG(-1));QCOMPARE(entry.tymed,DWORD(TYMED_HGLOBAL));QCOMPARE(data->QueryGetData(&entry),S_OK);}
            QCOMPARE(occurrences,valid?1:0);
            STGMEDIUM out{};QCOMPARE(data->GetData(&f,&out),valid?S_OK:DV_E_FORMATETC);
            if(valid){
                QCOMPARE(out.tymed,DWORD(TYMED_HGLOBAL));const auto size=GlobalSize(out.hGlobal);QVERIFY(size>=size_t(36));QVERIFY(size<=size_t(64));
                auto bytes=static_cast<const char *>(GlobalLock(out.hGlobal));QVERIFY(bytes);QCOMPARE(QByteArray(bytes,36),token);
                for(size_t i=36;i<size;++i)QCOMPARE(bytes[i],'\0');GlobalUnlock(out.hGlobal);ReleaseStgMedium(&out);
                auto wrong=f;wrong.lindex=0;QCOMPARE(data->QueryGetData(&wrong),DV_E_FORMATETC);wrong=f;wrong.tymed=TYMED_ISTREAM;QCOMPARE(data->QueryGetData(&wrong),DV_E_FORMATETC);
            }
            STGMEDIUM replacement{};replacement.tymed=TYMED_HGLOBAL;replacement.hGlobal=GlobalAlloc(GMEM_MOVEABLE,36);QVERIFY(replacement.hGlobal);
            QCOMPARE(data->SetData(&f,&replacement,TRUE),E_ACCESSDENIED);ReleaseStgMedium(&replacement);
            QCOMPARE(data->QueryGetData(&f),valid?S_OK:DV_E_FORMATETC);
            STGMEDIUM after{};QCOMPARE(data->GetData(&f,&after),valid?S_OK:DV_E_FORMATETC);
            if(valid){auto bytes=static_cast<const char *>(GlobalLock(after.hGlobal));QVERIFY(bytes);QCOMPARE(QByteArray(bytes,36),token);GlobalUnlock(after.hGlobal);ReleaseStgMedium(&after);}
            auto effect=format(CFSTR_PREFERREDDROPEFFECT);STGMEDIUM preference{};QCOMPARE(data->GetData(&effect,&preference),S_OK);
            auto value=static_cast<const DWORD *>(GlobalLock(preference.hGlobal));QVERIFY(value);QCOMPARE(*value,DWORD(DROPEFFECT_COPY));GlobalUnlock(preference.hGlobal);ReleaseStgMedium(&preference);
            QCOMPARE(fetches,0);
        }
    }
    void internalMoveAsyncCompletionDoesNotFetch(){
        auto b=batch();b->internalMoveToken="12345678-90ab-cdef-1234-567890abcdef";int fetches=0,cancels=0;bool finished=false;
        b->fetch=[&](int,const QString &,auto){++fetches;};b->cancelJobs=[&]{++cancels;};b->systemFinished=[&](bool accepted){finished=accepted;};
        Com<IDataObject> data;data.p=createFileDataObject(b);Com<IDataObjectAsyncCapability> cap;QCOMPARE(data->QueryInterface(IID_PPV_ARGS(cap.out())),S_OK);
        Com<IDropSource> source;source.p=createFileDropSource(b);QCOMPARE(source->QueryContinueDrag(FALSE,0),DRAGDROP_S_DROP);
        QCOMPARE(cap->StartOperation(nullptr),S_OK);QCOMPARE(cap->EndOperation(S_OK,nullptr,DROPEFFECT_MOVE),S_OK);settle();
        QVERIFY(finished);QVERIFY(!b->cancelled);QCOMPARE(fetches,0);QCOMPARE(cancels,0);
    }
    void metadataAndHoverNeverFetch(){
        auto b=batch();int calls=0;b->fetch=[&](int,const QString &,auto){++calls;};Com<IDataObject> data;data.p=createFileDataObject(b);
        IEnumFORMATETC *enumerator=nullptr;QCOMPARE(data->EnumFormatEtc(DATADIR_GET,&enumerator),S_OK);FORMATETC value{};ULONG count=0;while(enumerator->Next(1,&value,&count)==S_OK)QCOMPARE(data->QueryGetData(&value),S_OK);enumerator->Release();
        auto f=format(CFSTR_FILEDESCRIPTORW);STGMEDIUM out{};QCOMPARE(data->GetData(&f,&out),S_OK);auto d=static_cast<FILEGROUPDESCRIPTORW *>(GlobalLock(out.hGlobal));QCOMPARE(d->cItems,UINT(1));QCOMPARE(QString::fromWCharArray(d->fgd[0].cFileName),QString("sample.txt"));GlobalUnlock(out.hGlobal);ReleaseStgMedium(&out);
        Com<IStream> s;stream(data.p,0,s);STATSTG stat{};QCOMPARE(s->Stat(&stat,STATFLAG_NONAME),S_OK);QCOMPARE(stat.cbSize.QuadPart,ULONGLONG(7));char bytes[10];ULONG read=99;QCOMPARE(s->Read(bytes,10,&read),E_PENDING);QCOMPARE(read,ULONG(0));settle();QCOMPARE(calls,0);
        auto invalid=format(CFSTR_FILECONTENTS,1,TYMED_ISTREAM);QCOMPARE(data->QueryGetData(&invalid),DV_E_LINDEX);
        invalid=format(CFSTR_FILECONTENTS,0,TYMED_HGLOBAL);QCOMPARE(data->QueryGetData(&invalid),DV_E_TYMED);
        Com<IDropSource> source;source.p=createFileDropSource(b);QCOMPARE(source->GiveFeedback(DROPEFFECT_COPY),DRAGDROP_S_USEDEFAULTCURSORS);QCOMPARE(source->QueryContinueDrag(FALSE,MK_LBUTTON),S_OK);QCOMPARE(calls,0);
    }
    void shellHelperAndMediumOwnership(){
        auto b=batch();Com<IDataObject> data;data.p=createFileDataObject(b);
        const auto image=fileDragBadge(FileIconProvider::file("sample.txt"),"sample.txt",2);
        const HRESULT initialized=initializeFileDragImage(data.p,image);QCOMPARE(initialized,S_OK);
        QVERIFY(image.save(report+"/native-file-drag-badge.png"));
        Com<IEnumFORMATETC> formats;QCOMPARE(data->EnumFormatEtc(DATADIR_GET,formats.out()),S_OK);FORMATETC f{};ULONG fetched=0;int total=0;
        while(formats->Next(1,&f,&fetched)==S_OK){++total;STGMEDIUM m{};QCOMPARE(data->GetData(&f,&m),S_OK);ReleaseStgMedium(&m);}QVERIFY(total>3);
        auto extra=format(L"RabbitShell-test-private-format");
        for(BOOL release:{FALSE,TRUE}){STGMEDIUM m{};m.tymed=TYMED_HGLOBAL;m.hGlobal=GlobalAlloc(GMEM_MOVEABLE,4);auto p=GlobalLock(m.hGlobal);memcpy(p,"ABCD",4);GlobalUnlock(m.hGlobal);QCOMPARE(data->SetData(&extra,&m,release),S_OK);if(!release)ReleaseStgMedium(&m);STGMEDIUM read{};QCOMPARE(data->GetData(&extra,&read),S_OK);auto p2=GlobalLock(read.hGlobal);QCOMPARE(QByteArray(static_cast<char *>(p2),4),QByteArray("ABCD"));GlobalUnlock(read.hGlobal);ReleaseStgMedium(&read);}
    }
    void multipleFileIndicesAndIndependentStreams(){
        auto b=FileDragBatch::create({{"/one","same.txt",3},{"/two","same.txt",4}});QVERIFY(b);int calls=0;
        b->fetch=[&](int i,const QString &path,auto done){++calls;QFile f(path);if(!f.open(QIODevice::WriteOnly)){done(f.errorString());return;}f.write(i?"TWO!":"one");f.close();done({});};
        Com<IDataObject> data;data.p=createFileDataObject(b);Com<IDropSource> source;source.p=createFileDropSource(b);QCOMPARE(source->QueryContinueDrag(FALSE,0),DRAGDROP_S_DROP);
        for(int index:{1,0,1}){Com<IStream> s;stream(data.p,index,s);char out[8]{};ULONG n=0;QCOMPARE(s->Read(out,sizeof(out),&n),S_FALSE);QCOMPARE(QByteArray(out,int(n)),index?QByteArray("TWO!"):QByteArray("one"));}
        QCOMPARE(calls,2);QVERIFY(b->files[0].name!=b->files[1].name);
    }
    void synchronousReadPumpsAndRejectsReentry(){
        auto b=batch();b->released=true;Com<IDataObject> data;data.p=createFileDataObject(b);Com<IStream> s;stream(data.p,0,s);int ticks=0;HRESULT seek=0,clone=0;
        QTimer heartbeat;connect(&heartbeat,&QTimer::timeout,this,[&]{++ticks;});heartbeat.start(5);
        b->fetch=[&](int,const QString &path,auto done){QTimer::singleShot(35,this,[&,path,done]{LARGE_INTEGER zero{};seek=s->Seek(zero,STREAM_SEEK_SET,nullptr);IStream *other=nullptr;clone=s->Clone(&other);QFile f(path);f.open(QIODevice::WriteOnly);f.write("payload");f.close();done({});});};
        char out[20]{};ULONG read=0;QCOMPARE(s->Read(out,20,&read),S_FALSE);QCOMPARE(read,ULONG(7));QCOMPARE(QByteArray(out,int(read)),QByteArray("payload"));QVERIFY(ticks>=2);QCOMPARE(seek,STG_E_INUSE);QCOMPARE(clone,STG_E_INUSE);
        LARGE_INTEGER zero{};QCOMPARE(s->Seek(zero,STREAM_SEEK_SET,nullptr),S_OK);Com<IStream> copy;QCOMPARE(s->Clone(copy.out()),S_OK);QCOMPARE(copy->Read(out,3,&read),S_OK);QCOMPARE(QByteArray(out,3),QByteArray("pay"));
    }
    void asyncMarshaledStreamDiskSpoolAndLifetime(){
        constexpr quint64 bytes=20*1024*1024+17;auto b=batch(bytes);auto weak=std::weak_ptr<FileDragBatch>(b);const auto stage=b->stagePath(0);int fetches=0;
        b->fetch=[&](int,const QString &path,auto done){++fetches;QFile f(path);if(!f.open(QIODevice::WriteOnly)){done(f.errorString());return;}QByteArray chunk(65536,'x');quint64 total=0;while(total<bytes){auto n=qMin<quint64>(chunk.size(),bytes-total);if(f.write(chunk.constData(),qint64(n))!=qint64(n)){done(f.errorString());return;}total+=n;}f.close();done({});};
        Com<IDataObject> data;data.p=createFileDataObject(b);Com<IDataObjectAsyncCapability> cap;QCOMPARE(data->QueryInterface(IID_PPV_ARGS(cap.out())),S_OK);QCOMPARE(cap->StartOperation(nullptr),S_OK);
        Com<IDropSource> source;source.p=createFileDropSource(b);QCOMPARE(source->QueryContinueDrag(FALSE,0),DRAGDROP_S_DROP);
        Com<IStream> s;stream(data.p,0,s);IStream *marshaled=nullptr;QCOMPARE(CoMarshalInterThreadInterfaceInStream(IID_IStream,s.p,&marshaled),S_OK);
        s.p->Release();s.p=nullptr;data.p->Release();data.p=nullptr;
        const QString destination=root.filePath("target-large.bin");std::atomic_bool finished=false;HRESULT result=E_FAIL;
        std::thread target([&]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);IStream *stream=nullptr;result=CoGetInterfaceAndReleaseStream(marshaled,IID_PPV_ARGS(&stream));if(SUCCEEDED(result)){QFile out(destination);if(!out.open(QIODevice::WriteOnly))result=E_FAIL;else{char chunk[65536];for(;;){ULONG n=0;result=stream->Read(chunk,sizeof(chunk),&n);if(FAILED(result))break;if(out.write(chunk,n)!=n){result=STG_E_WRITEFAULT;break;}if(result==S_FALSE)break;}}stream->Release();}CoUninitialize();finished=true;});
        QElapsedTimer deadline;deadline.start();while(!finished&&deadline.elapsed()<10000){QCoreApplication::processEvents();QTest::qWait(5);}if(!finished)b->cancel();target.join();QVERIFY(finished);QVERIFY(SUCCEEDED(result));QCOMPARE(fetches,1);QCOMPARE(QFileInfo(destination).size(),qint64(bytes));QCOMPARE(hashFile(stage),hashFile(destination));
        QCOMPARE(cap->EndOperation(S_OK,nullptr,DROPEFFECT_COPY),S_OK);cap.p->Release();cap.p=nullptr;source.p->Release();source.p=nullptr;b.reset();settle();QVERIFY(weak.expired());QVERIFY(!QFileInfo::exists(stage));
    }
    void cancellationAndReadFailure(){
        auto b=batch();Com<IDataObject> data;data.p=createFileDataObject(b);Com<IDropSource> source;source.p=createFileDropSource(b);QCOMPARE(source->QueryContinueDrag(TRUE,MK_LBUTTON),DRAGDROP_S_CANCEL);QVERIFY(b->cancelled);QVERIFY(!b->released);
        auto failed=batch();failed->released=true;failed->fetch=[](int,const QString &,auto done){done("synthetic permission failure");};Com<IDataObject> object;object.p=createFileDataObject(failed);Com<IStream> s;stream(object.p,0,s);char out[8];ULONG count=4;QCOMPARE(s->Read(out,8,&count),STG_E_READFAULT);QCOMPARE(count,ULONG(0));
        auto pending=batch();pending->released=true;pending->fetch=[pendingWeak=std::weak_ptr<FileDragBatch>(pending)](int,const QString &,auto){QTimer::singleShot(20,qApp,[pendingWeak]{if(auto b=pendingWeak.lock())b->cancel();});};Com<IDataObject> waiting;waiting.p=createFileDataObject(pending);Com<IStream> stream2;stream(waiting.p,0,stream2);QCOMPARE(stream2->Read(out,8,&count),STG_E_READFAULT);QVERIFY(pending->cancelled);
    }
    void workspaceQueueIdentityAndSourceDestruction(){
        auto w=new Workspace(Profile{},QByteArray("synthetic"),true);w->demo=false;w->online=true;w->session=new Session(w->profile,{},w);
        const QVariantMap row{{"path","/source.txt"},{"name","source.txt"},{"size",7},{"modified",1},{"permissions",0100644}};
        auto b=w->prepareFileDrag({row});QVERIFY(b);b->released=true;w->transferSession=new Session(w->profile,{},w);w->attachSession(w->transferSession,true);w->transferReady=true;
        Com<IDataObject> object;object.p=createFileDataObject(b);Com<IStream> s;stream(object.p,0,s);
        QTimer::singleShot(30,w,[w,b]{QCOMPARE(w->activeTransfer>0,true);const int id=w->activeTransfer;QFile f(b->stagePath(0));QVERIFY(f.open(QIODevice::WriteOnly));f.write("payload");f.close();w->transferSession->completed("download",id,{{"failed",0},{"completed",1},{"payloadBytes",7},{"activeMs",20}});QVERIFY(w->transferItems[id]->text(1).contains("系统负责"));QVERIFY(w->transferItems[id]->text(1).contains("平均下载"));});
        char out[10];ULONG read=0;QCOMPARE(s->Read(out,10,&read),S_FALSE);QCOMPARE(read,ULONG(7));QVERIFY(!QSettings().contains("downloads/path"));
        QString error;QVERIFY(!w->prepareFileDrag({QVariantMap{{"path","/dir"},{"directory",true}}},&error));
        auto later=w->prepareFileDrag({row});delete w;QVERIFY(later->cancelled);QVERIFY(b->cancelled);
    }
    void isolatedSftpActiveWriterWorkspaceClose(){
        const int port=qEnvironmentVariableIntValue("RABBITSHELL_DRAG_PORT");if(!port)QSKIP("Set RABBITSHELL_DRAG_PORT with run_native_drag_integration.py for isolated SFTP");
        Profile profile;profile.host="127.0.0.1";profile.port=port;profile.user="fixture";
        QPointer<Workspace> owner=new Workspace(profile,QByteArray("fixture-only"),true);owner->demo=false;owner->online=true;owner->session=new Session(profile,"fixture-only",owner);
        QSignalSpy identity(owner->session,&Session::authenticatedHostIdentity),primaryReady(owner->session,&Session::connected);
        connect(owner->session,&Session::hostKey,owner->session,[primary=owner->session](const QString &,bool){primary->trust(true);});owner->session->start();
        QTRY_COMPARE_WITH_TIMEOUT(primaryReady.size(),1,10000);QCOMPARE(identity.size(),1);owner->authenticatedFingerprint=identity[0][0].toString();
        QPointer<Session> writer=new Session(profile,"fixture-only",owner);owner->transferSession=writer;owner->attachSession(writer,true);connect(writer,&Session::hostKey,writer,[writer](const QString &,bool){if(writer)writer->trust(true);});writer->start();QTRY_VERIFY_WITH_TIMEOUT(owner->transferReady,10000);
        QFileInfo slow(qEnvironmentVariable("RABBITSHELL_DRAG_ROOT")+"/meter-slow-drag.bin");QVERIFY(slow.exists());
        const QVariantMap row{{"path","/meter-slow-drag.bin"},{"name","meter-slow-drag.bin"},{"size",slow.size()},{"modified",slow.lastModified().toSecsSinceEpoch()},{"permissions",0100644}};
        auto b=owner->prepareFileDrag({row});QVERIFY(b);b->released=true;const auto path=b->stagePath(0);const auto directory=QFileInfo(path).absolutePath();auto weak=std::weak_ptr<FileDragBatch>(b);
        bool actuallyRead=false;connect(writer,&Session::transferProgress,qApp,[owner,&actuallyRead](int,const QString &,quint64 bytes,quint64){if(bytes&&owner){actuallyRead=true;delete owner.data();}});
        Com<IDataObject> object;object.p=createFileDataObject(b);Com<IStream> s;stream(object.p,0,s);char bytes[64];ULONG n=42;QElapsedTimer deadline;deadline.start();QCOMPARE(s->Read(bytes,sizeof(bytes),&n),STG_E_READFAULT);
        QVERIFY(actuallyRead);QVERIFY(owner.isNull());QVERIFY(writer.isNull());QVERIFY(b->cancelled);QCOMPARE(n,ULONG(0));QVERIFY(deadline.elapsed()<10000);QVERIFY(!QFileInfo::exists(path));
        s.p->Release();s.p=nullptr;object.p->Release();object.p=nullptr;b.reset();settle();QVERIFY(weak.expired());QVERIFY(!QFileInfo::exists(directory));
    }
    void isolatedSftpDropAndChangedSize(){
        const int port=qEnvironmentVariableIntValue("RABBITSHELL_DRAG_PORT");if(!port)QSKIP("Set RABBITSHELL_DRAG_PORT with run_native_drag_integration.py for isolated SFTP");
        Profile p;p.host="127.0.0.1";p.port=port;p.user="fixture";p.directory="/";
        Workspace w(p,QByteArray("fixture-only"),true);w.demo=false;w.online=true;w.session=new Session(p,"fixture-only",&w);
        QSignalSpy identity(w.session,&Session::authenticatedHostIdentity),primaryReady(w.session,&Session::connected);
        connect(w.session,&Session::hostKey,w.session,[primary=w.session](const QString &,bool){primary->trust(true);});w.session->start();
        QTRY_COMPARE_WITH_TIMEOUT(primaryReady.size(),1,10000);QCOMPARE(identity.size(),1);w.authenticatedFingerprint=identity[0][0].toString();
        auto transfer=new Session(p,"fixture-only",&w);w.transferSession=transfer;w.attachSession(transfer,true);connect(transfer,&Session::hostKey,transfer,[transfer](const QString &,bool){transfer->trust(true);});transfer->start();QTRY_VERIFY_WITH_TIMEOUT(w.transferReady,10000);
        const auto remote=qEnvironmentVariable("RABBITSHELL_DRAG_ROOT");QFileInfo file(remote+"/drag-large.bin");QVERIFY(file.exists());
        QVariantMap row{{"path","/drag-large.bin"},{"name","drag-large.bin"},{"size",file.size()},{"modified",file.lastModified().toSecsSinceEpoch()},{"permissions",0100644}};
        auto b=w.prepareFileDrag({row});QVERIFY(b);b->released=true;Com<IDataObject> data;data.p=createFileDataObject(b);Com<IStream> s;stream(data.p,0,s);
        QFile output(root.filePath("sftp-export.bin"));QVERIFY(output.open(QIODevice::WriteOnly));char chunk[65536];for(;;){ULONG n=0;const auto result=s->Read(chunk,sizeof(chunk),&n);QVERIFY(SUCCEEDED(result));QCOMPARE(output.write(chunk,n),qint64(n));if(result==S_FALSE)break;}output.close();QCOMPARE(hashFile(output.fileName()),hashFile(file.absoluteFilePath()));
        row["size"]=file.size()+1;auto changed=w.prepareFileDrag({row});QVERIFY(changed);changed->released=true;Com<IDataObject> wrong;wrong.p=createFileDataObject(changed);Com<IStream> rejected;stream(wrong.p,0,rejected);ULONG n=5;QCOMPARE(rejected->Read(chunk,5,&n),STG_E_READFAULT);QCOMPARE(n,ULONG(0));QVERIFY(!QFileInfo::exists(changed->stagePath(0)));
        QFileInfo slow(remote+"/meter-slow-drag.bin");row["path"]="/meter-slow-drag.bin";row["name"]="meter-slow-drag.bin";row["size"]=slow.size();row["modified"]=slow.lastModified().toSecsSinceEpoch();
        auto cancelled=w.prepareFileDrag({row});QVERIFY(cancelled);cancelled->released=true;Com<IDataObject> slowData;slowData.p=createFileDataObject(cancelled);Com<IStream> slowStream;stream(slowData.p,0,slowStream);
        QTimer::singleShot(80,&w,[cancelled]{cancelled->cancel();});QCOMPARE(slowStream->Read(chunk,sizeof(chunk),&n),STG_E_READFAULT);QVERIFY(cancelled->cancelled);
        QTRY_VERIFY_WITH_TIMEOUT(!w.activeTransfer,5000);QVERIFY(!QFileInfo::exists(cancelled->stagePath(0)));QVERIFY(!QSettings().contains("downloads/path"));
        const auto cancelledDirectory=QFileInfo(cancelled->stagePath(0)).absolutePath();
        slowStream.p->Release();slowStream.p=nullptr;slowData.p->Release();slowData.p=nullptr;cancelled.reset();settle();QVERIFY(!QFileInfo::exists(cancelledDirectory));
        w.cancelFileDrags();transfer->stop();QVERIFY(transfer->wait(5000));
    }
};
}
QTEST_MAIN(rs::NativeFileDragTests)
#include "test_nativefiledrag.moc"
