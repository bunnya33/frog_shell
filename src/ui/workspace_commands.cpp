#include "workspace.h"
#include "fileclipboard.h"
#include "../core/operationgate.h"
namespace rs {
namespace {
template<class T> T *ancestor(QWidget *focus) {for(auto w=focus;w;w=w->parentWidget())if(auto result=qobject_cast<T *>(w))return result;return nullptr;}
bool inside(QWidget *root,QWidget *focus){return root&&focus&&(root==focus||root->isAncestorOf(focus));}
}
bool Workspace::commandEnabled(Command c,QWidget *focus) const {
    if(OperationGate::locked()||shuttingDown)return false;
    if(focus&&!isAncestorOf(focus))focus=nullptr;
    auto editor=ancestor<Editor>(focus);if(!editor&&editArea->activeGroup())editor=qobject_cast<Editor *>(editArea->activeGroup()->currentWidget());
    auto code=ancestor<EditorSurface>(focus);auto plain=ancestor<QPlainTextEdit>(focus);auto rich=ancestor<QTextEdit>(focus);auto line=ancestor<QLineEdit>(focus);auto term=ancestor<Terminal>(focus);
    const bool fileFocus=inside(files,focus),remote=online&&!demo;
    switch(c){
    case Command::NewFile:case Command::NewFolder:case Command::Upload:return remote&&!selectedFileTarget().isEmpty();
    case Command::Save:return remote&&editor&&editor->dirty()&&!editor->saving;
    case Command::SaveAll:return remote&&dirty();
    case Command::Refresh:return remote;
    case Command::Undo:return fileFocus?remote:code?code->document()->isUndoAvailable()&&!code->isReadOnly():plain?plain->document()->isUndoAvailable()&&!plain->isReadOnly():rich?rich->document()->isUndoAvailable()&&!rich->isReadOnly():line?line->isUndoAvailable()&&!line->isReadOnly():false;
    case Command::Redo:return code?code->document()->isRedoAvailable()&&!code->isReadOnly():plain?plain->document()->isRedoAvailable()&&!plain->isReadOnly():rich?rich->document()->isRedoAvailable()&&!rich->isReadOnly():line?line->isRedoAvailable()&&!line->isReadOnly():false;
    case Command::Copy:case Command::Cut:
        if(fileFocus){const auto selected=files->selectedItems();return remote&&!selected.isEmpty()&&std::all_of(selected.begin(),selected.end(),[](auto item){const auto info=item->data(0,Qt::UserRole).toMap();const auto path=info.value("path").toString();return !item->data(0,ParentNavigationRole).toBool()&&!item->data(0,FileDraftRole).toBool()&&path.startsWith('/')&&path!="/"&&QDir::cleanPath(path)==path&&!info.value("link").toBool()&&info.contains("directory");});}
        if(term)return c==Command::Copy&&term->hasSelection();
        if(code)return code->textCursor().hasSelection()&&(c==Command::Copy||!code->isReadOnly());
        if(plain)return plain->textCursor().hasSelection()&&(c==Command::Copy||!plain->isReadOnly());
        if(rich)return rich->textCursor().hasSelection()&&(c==Command::Copy||!rich->isReadOnly());
        return line&&line->hasSelectedText()&&(c==Command::Copy||!line->isReadOnly());
    case Command::Paste:
        if(fileFocus)return remote&&filePasteError(selectedFileTarget(),QApplication::clipboard()->mimeData(),FileClipboard::instance().current()).isEmpty();
        return (term&&online)||(code&&!code->isReadOnly()&&code->canPaste())||(plain&&!plain->isReadOnly()&&plain->canPaste())||(rich&&!rich->isReadOnly()&&rich->canPaste())||(line&&!line->isReadOnly()&&QApplication::clipboard()->mimeData()->hasText());
    case Command::Find:return ancestor<CodeEdit>(focus)!=nullptr;
    case Command::NewTerminal:return online||demo;
    case Command::SplitTerminal:return (online||demo)&&terminals.contains(focusedTerm);
    case Command::CloseTerminal:case Command::ClearTerminal:return terminals.contains(focusedTerm);
    case Command::Reconnect:return !demo&&!mainPending;
    case Command::Disconnect:return hasMainConnection();
    default:return true;
    }
}
void Workspace::uploadFilesTo(const QString &target) {
    if(OperationGate::locked()||demo||!online)return;
    const auto epoch=OperationGate::token(),generation=credentialGeneration;QPointer<Workspace> owner=this;QPointer<Session> origin=session;
    const auto paths=QFileDialog::getOpenFileNames(this,"上传文件到 "+target);
    if(owner&&OperationGate::accepts(epoch)&&origin&&session==origin&&online&&generation==credentialGeneration&&!paths.isEmpty())queueTransfers(paths,true,target);
}
void Workspace::executeCommand(Command c,QWidget *focus) {
    if(!commandEnabled(c,focus))return;
    if(focus&&!isAncestorOf(focus))focus=nullptr;
    auto editor=ancestor<Editor>(focus);if(!editor&&editArea->activeGroup())editor=qobject_cast<Editor *>(editArea->activeGroup()->currentWidget());
    switch(c){
    case Command::NewFile:createEntry(false,selectedFileTarget());break;case Command::NewFolder:createEntry(true,selectedFileTarget());break;
    case Command::Save:saveEditor(editor);break;case Command::SaveAll:for(auto e:editors)if(e&&e->dirty()&&!e->saving)saveEditor(e);break;
    case Command::Upload:uploadFilesTo(selectedFileTarget());break;case Command::Refresh:refreshFiles();break;
    case Command::Status:sidebar->setCurrentIndex(0);break;case Command::Explorer:sidebar->setCurrentIndex(1);break;
    case Command::Processes:showProcessPage();break;case Command::Listening:showListeningPage();break;
    case Command::TerminalPanel:case Command::Ports:case Command::Services:terminalPanel->show();bottomTabs->setCurrentIndex(c==Command::TerminalPanel?0:c==Command::Ports?1:2);break;
    case Command::NewTerminal:bottomTabs->setCurrentIndex(0);terminalPanel->show();addTerminal();break;
    case Command::SplitTerminal:bottomTabs->setCurrentIndex(0);terminalPanel->show();addTerminal(terminals[focusedTerm].group);break;
    case Command::CloseTerminal:closeTerminal(focusedTerm);break;case Command::ClearTerminal:bottomTabs->setCurrentIndex(0);terminalPanel->show();terminals[focusedTerm].view->clearOutput();break;
    case Command::Reconnect:reconnect();break;case Command::Disconnect:disconnectHost();break;
    case Command::Find:if(auto code=ancestor<CodeEdit>(focus))code->showFind();break;
    default:
        if(inside(files,focus)){if(c==Command::Copy||c==Command::Cut)copyFileSelection(c==Command::Cut);else if(c==Command::Paste)pasteFiles();else if(c==Command::Undo)undoLastFileOperation();break;}
        if(auto term=ancestor<Terminal>(focus)){if(c==Command::Copy)term->copy();else if(c==Command::Paste)term->paste();break;}
        const char *method=c==Command::Undo?"undo":c==Command::Redo?"redo":c==Command::Cut?"cut":c==Command::Copy?"copy":"paste";
        if(auto code=ancestor<EditorSurface>(focus))QMetaObject::invokeMethod(code,method,Qt::DirectConnection);
        else if(auto plain=ancestor<QPlainTextEdit>(focus))QMetaObject::invokeMethod(plain,method,Qt::DirectConnection);
        else if(auto rich=ancestor<QTextEdit>(focus))QMetaObject::invokeMethod(rich,method,Qt::DirectConnection);
        else if(auto line=ancestor<QLineEdit>(focus))QMetaObject::invokeMethod(line,method,Qt::DirectConnection);
    }
}
}
