#include "filedrag_win.h"
#ifdef Q_OS_WIN
#include <shlobj.h>
#include <shlwapi.h>
#include <cstring>
#include <climits>
namespace rs {
namespace {
CLIPFORMAT descriptorFormat(){static auto f=CLIPFORMAT(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW));return f;}
CLIPFORMAT contentFormat(){static auto f=CLIPFORMAT(RegisterClipboardFormatW(CFSTR_FILECONTENTS));return f;}
CLIPFORMAT effectFormat(){static auto f=CLIPFORMAT(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT));return f;}
CLIPFORMAT internalMoveFormat(){static auto f=CLIPFORMAT(RegisterClipboardFormatW(L"RabbitShell.InternalFileMove.v1"));return f;}
HRESULT medium(const void *data,size_t bytes,STGMEDIUM *out){
    auto memory=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,bytes);if(!memory)return E_OUTOFMEMORY;
    auto ptr=GlobalLock(memory);if(!ptr){GlobalFree(memory);return E_OUTOFMEMORY;}
    std::memcpy(ptr,data,bytes);GlobalUnlock(memory);out->tymed=TYMED_HGLOBAL;out->hGlobal=memory;out->pUnkForRelease=nullptr;return S_OK;
}
class FileStream final : public IStream {
    std::atomic<ULONG> refs{1};std::shared_ptr<FileDragBatch> batch;int index;
    quint64 offset=0;QMutex cursor;IUnknown *marshaler=nullptr;
  public:
    FileStream(std::shared_ptr<FileDragBatch> b,int i,quint64 pos=0):batch(std::move(b)),index(i),offset(pos){CoCreateFreeThreadedMarshaler(this,&marshaler);}
    ~FileStream(){if(marshaler)marshaler->Release();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **out) override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==IID_IUnknown||iid==IID_ISequentialStream||iid==IID_IStream)*out=static_cast<IStream *>(this);
        else if(iid==IID_IMarshal&&marshaler)return marshaler->QueryInterface(iid,out);else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Read(void *buffer,ULONG bytes,ULONG *read) override{
        if(read)*read=0;if(!buffer&&bytes)return STG_E_INVALIDPOINTER;
        if(!batch->released)return E_PENDING;if(batch->cancelled)return STG_E_REVERTED;
        if(!cursor.tryLock())return STG_E_INUSE;
        struct Unlock{QMutex &m;~Unlock(){m.unlock();}} unlock{cursor};
        if(!bytes)return S_OK;
        if(!batch->awaitFile(index).isEmpty())return STG_E_READFAULT;
        QFile file(batch->stagePath(index));if(!file.open(QIODevice::ReadOnly)||!file.seek(qint64(offset)))return STG_E_READFAULT;
        ULONG total=0;auto out=static_cast<char *>(buffer);
        while(total<bytes){if(batch->cancelled)return STG_E_REVERTED;const auto n=file.read(out+total,qMin<quint64>(65536,bytes-total));if(n<0)return STG_E_READFAULT;if(!n)break;total+=ULONG(n);offset+=quint64(n);}
        if(read)*read=total;return total==bytes?S_OK:S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(const void *,ULONG,ULONG *) override{return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER delta,DWORD origin,ULARGE_INTEGER *result) override{
        if(!cursor.tryLock())return STG_E_INUSE;struct Unlock{QMutex &m;~Unlock(){m.unlock();}} unlock{cursor};qint64 base=0;
        if(origin==STREAM_SEEK_CUR)base=qint64(offset);else if(origin==STREAM_SEEK_END)base=qint64(batch->files[index].size);else if(origin!=STREAM_SEEK_SET)return STG_E_INVALIDFUNCTION;
        if((delta.QuadPart<0&&delta.QuadPart < -base)||(delta.QuadPart>0&&base>LLONG_MAX-delta.QuadPart))return STG_E_INVALIDFUNCTION;
        offset=quint64(base+delta.QuadPart);if(result)result->QuadPart=offset;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override{return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE CopyTo(IStream *target,ULARGE_INTEGER count,ULARGE_INTEGER *read,ULARGE_INTEGER *written) override{
        if(read)read->QuadPart=0;if(written)written->QuadPart=0;if(!target)return STG_E_INVALIDPOINTER;
        char buffer[65536];quint64 done=0,sent=0;
        while(done<count.QuadPart){ULONG got=0;auto hr=Read(buffer,ULONG(qMin<quint64>(sizeof(buffer),count.QuadPart-done)),&got);if(FAILED(hr))return hr;
            done+=got;ULONG out=0;if(got){auto wh=target->Write(buffer,got,&out);sent+=out;if(FAILED(wh)||out!=got){if(read)read->QuadPart=done;if(written)written->QuadPart=sent;return FAILED(wh)?wh:STG_E_WRITEFAULT;}}
            if(read)read->QuadPart=done;if(written)written->QuadPart=sent;if(hr==S_FALSE||!got)return S_FALSE;
        }return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE Revert() override{return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD) override{return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD) override{return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE Stat(STATSTG *s,DWORD flags) override{
        if(!s)return E_POINTER;*s={};s->type=STGTY_STREAM;s->cbSize.QuadPart=batch->files[index].size;s->grfMode=STGM_READ;
        if(!(flags&STATFLAG_NONAME)){const auto name=batch->files[index].name;const size_t bytes=size_t(name.size()+1)*sizeof(wchar_t);s->pwcsName=static_cast<wchar_t *>(CoTaskMemAlloc(bytes));if(!s->pwcsName)return E_OUTOFMEMORY;std::memcpy(s->pwcsName,name.utf16(),bytes);}return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream **out) override{if(!out)return E_POINTER;*out=nullptr;if(!cursor.tryLock())return STG_E_INUSE;struct Unlock{QMutex &m;~Unlock(){m.unlock();}} unlock{cursor};*out=new FileStream(batch,index,offset);return S_OK;}
};
class FileDataObject final : public IDataObject,public IDataObjectAsyncCapability {
    std::atomic<ULONG> refs{1};std::shared_ptr<FileDragBatch> batch;QByteArray moveToken;IUnknown *marshaler=nullptr;
    std::atomic_bool async=true,operation=false;
    struct Extra{DWORD tymed;QByteArray bytes;};
    QMutex extraMutex;QMap<QPair<CLIPFORMAT,LONG>,Extra> extras;
  public:
    explicit FileDataObject(std::shared_ptr<FileDragBatch> b):batch(std::move(b)),moveToken(isValidInternalFileMoveToken(batch->internalMoveToken)?batch->internalMoveToken:QByteArray{}){CoCreateFreeThreadedMarshaler(static_cast<IDataObject *>(this),&marshaler);}
    ~FileDataObject(){if(marshaler)marshaler->Release();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **out) override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==IID_IUnknown||iid==IID_IDataObject)*out=static_cast<IDataObject *>(this);
        else if(iid==__uuidof(IDataObjectAsyncCapability))*out=static_cast<IDataObjectAsyncCapability *>(this);
        else if(iid==IID_IMarshal&&marshaler)return marshaler->QueryInterface(iid,out);else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *f) override{
        if(!f)return E_POINTER;if(f->dwAspect!=DVASPECT_CONTENT)return DV_E_DVASPECT;
        if(f->cfFormat==contentFormat()){if(f->lindex<0||f->lindex>=batch->files.size())return DV_E_LINDEX;return f->tymed&TYMED_ISTREAM?S_OK:DV_E_TYMED;}
        if(f->cfFormat==internalMoveFormat())return !moveToken.isEmpty()&&f->lindex==-1&&(f->tymed&TYMED_HGLOBAL)?S_OK:DV_E_FORMATETC;
        if(f->cfFormat==descriptorFormat()||f->cfFormat==effectFormat())return f->lindex==-1&&(f->tymed&TYMED_HGLOBAL)?S_OK:DV_E_FORMATETC;
        QMutexLocker lock(&extraMutex);auto it=extras.constFind({f->cfFormat,f->lindex});return it!=extras.cend()&&(f->tymed&it->tymed)?S_OK:DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *f,STGMEDIUM *out) override{
        if(!out)return E_POINTER;*out={};auto hr=QueryGetData(f);if(FAILED(hr))return hr;
        if(f->cfFormat==contentFormat()){out->tymed=TYMED_ISTREAM;out->pstm=new FileStream(batch,f->lindex);return S_OK;}
        if(f->cfFormat==internalMoveFormat())return medium(moveToken.constData(),size_t(moveToken.size()),out);
        if(f->cfFormat==effectFormat()){DWORD effect=DROPEFFECT_COPY;return medium(&effect,sizeof(effect),out);}
        if(f->cfFormat!=descriptorFormat()){QMutexLocker lock(&extraMutex);const auto value=extras.value({f->cfFormat,f->lindex});auto result=medium(value.bytes.constData(),size_t(value.bytes.size()),out);
            if(FAILED(result)||value.tymed==TYMED_HGLOBAL)return result;
            IStream *stream=nullptr;result=CreateStreamOnHGlobal(out->hGlobal,TRUE,&stream);if(FAILED(result)){ReleaseStgMedium(out);*out={};return result;}out->tymed=TYMED_ISTREAM;out->pstm=stream;return S_OK;}
        const size_t bytes=sizeof(UINT)+size_t(batch->files.size())*sizeof(FILEDESCRIPTORW);QByteArray data(qsizetype(bytes),0);auto group=reinterpret_cast<FILEGROUPDESCRIPTORW *>(data.data());group->cItems=UINT(batch->files.size());
        for(int i=0;i<batch->files.size();++i){const auto &file=batch->files[i];auto &d=group->fgd[i];d.dwFlags=FD_ATTRIBUTES|FD_FILESIZE|FD_UNICODE;d.dwFileAttributes=FILE_ATTRIBUTE_NORMAL;d.nFileSizeHigh=DWORD(file.size>>32);d.nFileSizeLow=DWORD(file.size);std::memcpy(d.cFileName,file.name.utf16(),size_t(file.name.size())*sizeof(wchar_t));}
        return medium(data.constData(),bytes,out);
    }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,IEnumFORMATETC **out) override{
        if(direction!=DATADIR_GET)return E_NOTIMPL;QList<FORMATETC> formats{{descriptorFormat(),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL},{effectFormat(),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL}};
        if(!moveToken.isEmpty())formats<<FORMATETC{internalMoveFormat(),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
        for(int i=0;i<batch->files.size();++i)formats<<FORMATETC{contentFormat(),nullptr,DVASPECT_CONTENT,i,TYMED_ISTREAM};
        {QMutexLocker lock(&extraMutex);for(auto it=extras.cbegin();it!=extras.cend();++it)formats<<FORMATETC{it.key().first,nullptr,DVASPECT_CONTENT,it.key().second,it->tymed};}
        return SHCreateStdEnumFmtEtc(UINT(formats.size()),formats.data(),out);
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *,STGMEDIUM *) override{return DATA_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *,FORMATETC *out) override{if(out)out->ptd=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *f,STGMEDIUM *value,BOOL release) override{
        if(!f||!value)return E_POINTER;
        if(f->cfFormat==descriptorFormat()||f->cfFormat==contentFormat()||f->cfFormat==effectFormat()||f->cfFormat==internalMoveFormat())return E_ACCESSDENIED;
        if(f->dwAspect!=DVASPECT_CONTENT)return DV_E_DVASPECT;
        QByteArray copy;
        if(value->tymed==TYMED_HGLOBAL&&value->hGlobal){
            const auto bytes=GlobalSize(value->hGlobal);if(!bytes||bytes>4*1024*1024)return STG_E_MEDIUMFULL;
            auto ptr=GlobalLock(value->hGlobal);if(!ptr)return E_OUTOFMEMORY;
            copy=QByteArray(static_cast<const char *>(ptr),qsizetype(bytes));GlobalUnlock(value->hGlobal);
        }else if(value->tymed==TYMED_ISTREAM&&value->pstm){
            ULARGE_INTEGER previous{};LARGE_INTEGER zero{};
            if(FAILED(value->pstm->Seek(zero,STREAM_SEEK_CUR,&previous))||FAILED(value->pstm->Seek(zero,STREAM_SEEK_SET,nullptr)))return STG_E_INVALIDFUNCTION;
            HRESULT result=S_OK;char bytes[65536];
            for(;;){ULONG n=0;result=value->pstm->Read(bytes,sizeof(bytes),&n);if(FAILED(result))break;if(n>sizeof(bytes)||copy.size()+n>4*1024*1024){result=STG_E_MEDIUMFULL;break;}copy.append(bytes,n);if(result==S_FALSE||!n)break;}
            LARGE_INTEGER restore;restore.QuadPart=LONGLONG(previous.QuadPart);value->pstm->Seek(restore,STREAM_SEEK_SET,nullptr);
            if(FAILED(result))return result;
        }else return DV_E_TYMED;
        {QMutexLocker lock(&extraMutex);qsizetype total=copy.size();for(auto it=extras.cbegin();it!=extras.cend();++it)if(it.key()!=qMakePair(f->cfFormat,f->lindex))total+=it->bytes.size();
            if((extras.size()>=16&&!extras.contains({f->cfFormat,f->lindex}))||total>4*1024*1024)return STG_E_MEDIUMFULL;
            extras[{f->cfFormat,f->lindex}]={value->tymed,std::move(copy)};
        }
        // Copy owns no caller handle. On accepted fRelease consume exactly once,
        // including custom pUnkForRelease. Replacement just releases our byte copy.
        if(release)ReleaseStgMedium(value);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *,DWORD,IAdviseSink *,DWORD *) override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE SetAsyncMode(BOOL value) override{async=bool(value);return S_OK;}
    HRESULT STDMETHODCALLTYPE GetAsyncMode(BOOL *out) override{if(!out)return E_POINTER;*out=async;return S_OK;}
    HRESULT STDMETHODCALLTYPE StartOperation(IBindCtx *) override{operation=true;return S_OK;}
    HRESULT STDMETHODCALLTYPE InOperation(BOOL *out) override{if(!out)return E_POINTER;*out=operation;return S_OK;}
    HRESULT STDMETHODCALLTYPE EndOperation(HRESULT result,IBindCtx *,DWORD effect) override{operation=false;batch->finish(SUCCEEDED(result)&&(effect&(DROPEFFECT_COPY|(!moveToken.isEmpty()?DROPEFFECT_MOVE:0))));return S_OK;}
};
class FileDropSource final : public IDropSource {
    std::atomic<ULONG> refs{1};std::shared_ptr<FileDragBatch> batch;
  public:
    explicit FileDropSource(std::shared_ptr<FileDragBatch> b):batch(std::move(b)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **out) override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_IDropSource)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,DWORD keys) override{
        if(escape||batch->cancelled){batch->cancel();return DRAGDROP_S_CANCEL;}
        if(!(keys&MK_LBUTTON)){batch->released=true;return DRAGDROP_S_DROP;}return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override{return DRAGDROP_S_USEDEFAULTCURSORS;}
};
}
HRESULT initializeFileDragImage(IDataObject *data,const QPixmap &badge){
    IDragSourceHelper *helper=nullptr;if(FAILED(CoCreateInstance(CLSID_DragDropHelper,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&helper))))return E_FAIL;
    auto image=badge.toImage().convertToFormat(QImage::Format_ARGB32);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=image.width();info.bmiHeader.biHeight=-image.height();info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void *bits=nullptr;auto bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    HRESULT result=E_OUTOFMEMORY;if(bitmap){std::memcpy(bits,image.constBits(),size_t(image.sizeInBytes()));SHDRAGIMAGE drag{{image.width(),image.height()},{12,12},bitmap,CLR_NONE};result=helper->InitializeFromBitmap(&drag,data);if(FAILED(result))DeleteObject(bitmap);}
    helper->Release();return result;
}
IDataObject *createFileDataObject(const std::shared_ptr<FileDragBatch> &batch){return new FileDataObject(batch);}
IDropSource *createFileDropSource(const std::shared_ptr<FileDragBatch> &batch){return new FileDropSource(batch);}
DWORD fileDragAllowedEffects(const QByteArray &token){return DROPEFFECT_COPY|(isValidInternalFileMoveToken(token)?DROPEFFECT_MOVE:0);}
bool dragRemoteFiles(QWidget *source,const std::shared_ptr<FileDragBatch> &batch,const QPixmap &badge){
    if(!batch)return false;
    batch->performedAction=Qt::IgnoreAction;
    if(!source)return false;
    const auto ole=OleInitialize(nullptr);if(FAILED(ole))return false;
    auto object=createFileDataObject(batch);auto dropSource=createFileDropSource(batch);initializeFileDragImage(object,badge);
    const DWORD allowed=fileDragAllowedEffects(batch->internalMoveToken);
    DWORD effect=0;const auto result=DoDragDrop(object,dropSource,allowed,&effect);
    IDataObjectAsyncCapability *capability=nullptr;BOOL running=false;
    if(SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&capability)))){capability->InOperation(&running);capability->Release();}
    const bool accepted=result==DRAGDROP_S_DROP&&(effect&allowed&(DROPEFFECT_COPY|DROPEFFECT_MOVE));
    if(accepted)batch->performedAction=(effect&allowed&DROPEFFECT_MOVE)?Qt::MoveAction:Qt::CopyAction;
    if(!accepted||!running)batch->finish(accepted);
    dropSource->Release();object->Release();OleUninitialize();return accepted;
}
}
#endif
