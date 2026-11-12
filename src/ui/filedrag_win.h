#pragma once
#include "filedrag.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <oleidl.h>
#include <shobjidl.h>
namespace rs {
HRESULT initializeFileDragImage(IDataObject *,const QPixmap &);
IDataObject *createFileDataObject(const std::shared_ptr<FileDragBatch> &);
IDropSource *createFileDropSource(const std::shared_ptr<FileDragBatch> &);
DWORD fileDragAllowedEffects(const QByteArray &internalMoveToken);
}
#endif
