#include "documentsearch.h"
#include "workspace.h"
#include "applock.h"
#include "icons.h"
#include "windowstyle.h"
#include "contentfont.h"
#include "../core/operationgate.h"
namespace rs {
namespace {
QByteArray digest(const QString &text) {
    return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(text.constData()), text.size() * sizeof(QChar)), QCryptographicHash::Sha256);
}
QLabel *label(const QString &name, bool muted = false) {
    auto value = new QLabel; value->setObjectName(name); value->setTextFormat(Qt::PlainText);
    value->setProperty("muted", muted); value->setMinimumWidth(0); return value;
}
QString workerProgram() {
    const auto override = qEnvironmentVariable("RABBITSHELL_SEARCH_EXECUTABLE");
    return override.isEmpty() ? QCoreApplication::applicationFilePath() : override;
}
}
AdvancedSearchDialog::AdvancedSearchDialog(QWidget *parent) : QDialog(parent, Qt::Window) {
    setObjectName("advancedSearchDialog"); setWindowTitle("高级搜索"); setProperty("rabbitUi", true);
    setModal(false); setMinimumWidth(uiPx(300)); resize(uiPx(600), uiPx(280)); bindUiFont(this);
    auto outer = new QVBoxLayout(this); uiMargins(outer,0,0,0,0); uiSpacing(outer,0);
    auto body = new QWidget; body->setObjectName("advancedSearchBody");
    auto layout = new QVBoxLayout(body); uiMargins(layout,18,16,18,14); uiSpacing(layout,9);
    outer->addWidget(body,1);
    layout->addWidget(new QLabel("搜索内容"));
    query = new QLineEdit; query->setObjectName("advancedSearchQuery"); query->setAccessibleName("搜索内容");
    query->setMaxLength(512); query->setPlaceholderText("输入要查找的文本"); bandHeight(query,34); layout->addWidget(query);
    auto options = new QGridLayout;options->setObjectName("advancedSearchOptions"); uiSpacing(options,10);
    sensitive = new QCheckBox("区分大小写"); sensitive->setObjectName("advancedSearchCase");
    word = new QCheckBox("全字匹配"); word->setObjectName("advancedSearchWholeWord");
    regex = new QCheckBox("正则表达式"); regex->setObjectName("advancedSearchRegex");
    options->addWidget(sensitive,0,0); options->addWidget(word,0,1); options->addWidget(regex,0,2); options->setColumnStretch(3,1); layout->addLayout(options);
    feedback = label("advancedSearchFeedback"); feedback->setWordWrap(true); feedback->hide(); layout->addWidget(feedback);
    auto divider = new QFrame; setFrameRole(divider,"divider"); uiFixedHeight(divider,1); layout->addWidget(divider);
    auto source = new QGridLayout; uiSpacing(source,5);
    auto fileHeading = new QLabel("文件"), serverHeading = new QLabel("服务器");
    fileHeading->setProperty("muted",true); serverHeading->setProperty("muted",true);
    sourceFile = label("advancedSearchSourceFile"); sourceDirty = label("advancedSearchSourceDirty",true);
    auto fileRow = new QHBoxLayout; uiSpacing(fileRow,9); fileRow->addWidget(sourceFile); fileRow->addWidget(sourceDirty); fileRow->addStretch();
    sourcePath = new ElidedLabel; sourcePath->setObjectName("advancedSearchSourcePath"); sourcePath->setProperty("muted",true); sourcePath->setMinimumWidth(0);
    sourceServer = new ElidedLabel; sourceServer->setObjectName("advancedSearchSourceServer"); sourceServer->setMinimumWidth(0);
    source->addWidget(fileHeading,0,0); source->addLayout(fileRow,0,1); source->addWidget(sourcePath,1,1);
    source->addWidget(serverHeading,2,0); source->addWidget(sourceServer,2,1); source->setColumnStretch(1,1); layout->addLayout(source);
    layout->addStretch();
    auto footer = new QWidget; footer->setObjectName("advancedSearchFooter");
    footer->setAttribute(Qt::WA_StyledBackground,true);
    auto buttons = new QHBoxLayout(footer); uiMargins(buttons,18,11,18,11); uiSpacing(buttons,8); buttons->addStretch();
    auto find = button("查找全部"), close = button("关闭");
    find->setObjectName("advancedSearchFindAll"); find->setProperty("primary",true); find->setDefault(true);
    close->setObjectName("advancedSearchClose"); close->setAutoDefault(false);
    close->setProperty("promptButton",true);uiMinimumWidth(close,82);
    uiMinimumWidth(find,82);bandHeight(find,31);bandHeight(close,31);
    buttons->addWidget(find); buttons->addWidget(close); outer->addWidget(footer);
    connect(find,&QPushButton::clicked,this,&AdvancedSearchDialog::findAllRequested);
    connect(query,&QLineEdit::returnPressed,this,&AdvancedSearchDialog::findAllRequested);
    connect(close,&QPushButton::clicked,this,&QDialog::reject);
    // QLineEdit handles Return; keep the default button from executing a second search.
    find->setDefault(false); find->setAutoDefault(false);
    connect(query,&QLineEdit::textEdited,this,[this]{setFeedback({});});
}
void AdvancedSearchDialog::resizeEvent(QResizeEvent *event){
    QDialog::resizeEvent(event);
    if(auto options=findChild<QGridLayout *>("advancedSearchOptions")){
        const int available=width()-uiPx(36);
        const int needed=sensitive->sizeHint().width()+word->sizeHint().width()+regex->sizeHint().width()+uiPx(20);
        const bool narrow=available<needed;
        const int desired=narrow?1:0;int row,column,rowSpan,columnSpan;
        options->getItemPosition(options->indexOf(regex),&row,&column,&rowSpan,&columnSpan);
        if(row!=desired){options->removeWidget(regex);options->addWidget(regex,desired,narrow?0:2);}
    }
}
SearchOptions AdvancedSearchDialog::options() const { return {query->text(),sensitive->isChecked(),word->isChecked(),regex->isChecked()}; }
void AdvancedSearchDialog::setSource(const SearchSource &source) {
    sourceFile->setText(QFileInfo(source.path).fileName()); sourceFile->setToolTip(source.path);
    static_cast<ElidedLabel *>(sourcePath)->setText(source.path); sourcePath->setToolTip(source.path);
    static_cast<ElidedLabel *>(sourceServer)->setText(source.server); sourceServer->setToolTip(source.server);
    sourceDirty->setText(source.editor && source.editor->dirty() ? "未保存内容" : QString());
}
void AdvancedSearchDialog::setFeedback(const QString &message, bool error) {
    feedback->setText(message); feedback->setStyleSheet("color:" + (error?colors().red:colors().muted).name()); feedback->setVisible(!message.isEmpty());
    query->setProperty("invalid",error); query->style()->unpolish(query); query->style()->polish(query);
}
void AdvancedSearchDialog::focusQuery() { query->setFocus(); query->selectAll(); }
class DocumentSearchResults::Model final : public QAbstractListModel {
  public:
    using QAbstractListModel::QAbstractListModel;
    SearchOutcome value;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid()?0:int(value.lines.size()); }
    QVariant data(const QModelIndex &index, int role) const override {
        if(!index.isValid()||index.row()>=value.lines.size())return {};
        const auto &row=value.lines[index.row()];
        if(role==Qt::DisplayRole||role==Qt::ToolTipRole)return row.text;
        if(role==Qt::AccessibleTextRole)return QString("第 %1 行，%2").arg(row.line).arg(row.text);
        if(role==Qt::UserRole)return row.line;
        if(role==Qt::UserRole+1){QVariantList spans;for(const auto &span:row.spans)spans.append(QVariantMap{{"start",span.start},{"length",span.length}});return spans;}
        return {};
    }
    void replace(SearchOutcome result) {beginResetModel();value=std::move(result);endResetModel();}
};
class SearchResultsDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        return {uiPx(64)+QFontMetrics(option.font).horizontalAdvance(index.data().toString()),qMax(uiPx(30),QFontMetrics(option.font).height()+uiPx(8))};
    }
    void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        const auto c=colors();const bool selected=option.state.testFlag(QStyle::State_Selected);
        p->save();p->setClipRect(option.rect);
        p->fillRect(option.rect,selected?c.processSelection:option.state.testFlag(QStyle::State_MouseOver)?c.selection:c.background);
        p->setFont(option.font);p->setPen(c.muted);
        const auto rect=option.rect.adjusted(uiPx(10),0,-uiPx(10),0);
        p->drawText(QRect(rect.left(),rect.top(),uiPx(38),rect.height()),Qt::AlignRight|Qt::AlignVCenter,index.data(Qt::UserRole).toString());
        QTextLayout text(index.data().toString(),option.font);
        QList<QTextLayout::FormatRange> formats;
        const QColor matchBackground=lightTheme()?QColor("#ffe39a"):QColor("#e7c56b"),matchText("#35280c");
        for(const auto &item:index.data(Qt::UserRole+1).toList()){
            const auto span=item.toMap();QTextLayout::FormatRange format;
            format.start=span.value("start").toInt();format.length=qMax(1,span.value("length").toInt());
            format.format.setBackground(matchBackground);format.format.setForeground(matchText);formats.append(format);
        }
        text.setFormats(formats);text.beginLayout();auto line=text.createLine();line.setLineWidth(qMax(uiPx(50),rect.width()-uiPx(50)));text.endLayout();
        p->setPen(c.text);text.draw(p,QPointF(rect.left()+uiPx(50),rect.center().y()-line.height()/2));
        p->restore();
    }
};
DocumentSearchResults::DocumentSearchResults(QWidget *parent):QWidget(parent) {
    setObjectName("documentSearchResults");setWindowTitle("搜索结果");setProperty("rabbitUi",true);setProperty("documentSearchResults",true);
    auto layout=new QVBoxLayout(this);uiMargins(layout,0,0,0,0);uiSpacing(layout,0);
    auto header=new QWidget;auto summary=new QGridLayout(header);uiMargins(summary,12,9,8,9);uiSpacing(summary,4);
    queryLabel=new ElidedLabel;queryLabel->setObjectName("documentSearchResultQuery");countLabel=label("documentSearchResultCount",true);
    auto top=new QHBoxLayout;uiSpacing(top,12);top->addWidget(queryLabel);top->addWidget(countLabel);top->addStretch();summary->addLayout(top,0,0);
    detach=iconButton(Icon::Expand,"移到独立窗口");detach->setObjectName("documentSearchDetach");summary->addWidget(detach,0,1);
    fileLabel=new ElidedLabel;fileLabel->setObjectName("documentSearchResultFile");serverLabel=new ElidedLabel;serverLabel->setObjectName("documentSearchResultServer");serverLabel->setProperty("muted",true);pathLabel=new ElidedLabel;pathLabel->setObjectName("documentSearchResultPath");pathLabel->setProperty("muted",true);pathLabel->setMinimumWidth(0);
    dirtyLabel=label("documentSearchResultDirty",true);
    auto origin=new QHBoxLayout;uiSpacing(origin,8);origin->addWidget(fileLabel);origin->addWidget(serverLabel,1);origin->addWidget(dirtyLabel);summary->addLayout(origin,1,0,1,2);summary->addWidget(pathLabel,2,0,1,2);
    layout->addWidget(header);auto divider=new QWidget;setFrameRole(divider,"divider");uiFixedHeight(divider,1);layout->addWidget(divider);
    feedback=label("documentSearchResultFeedback",true);feedback->setWordWrap(true);feedback->setContentsMargins(uiPx(12),uiPx(5),uiPx(12),uiPx(5));feedback->hide();layout->addWidget(feedback);
    list=new QListView;list->setObjectName("documentSearchResultList");list->setMouseTracking(true);list->setFrameShape(QFrame::NoFrame);list->setSelectionMode(QAbstractItemView::SingleSelection);list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    bindContentFont(list,ContentFontRole::List);model=new Model(this);list->setModel(model);list->setItemDelegate(new SearchResultsDelegate(list));layout->addWidget(list,1);
    connect(list,&QListView::doubleClicked,this,[this](const QModelIndex &index){emit activateRequested(index.row());});
    auto activate=new QShortcut(QKeySequence(Qt::Key_Return),list);activate->setContext(Qt::WidgetWithChildrenShortcut);connect(activate,&QShortcut::activated,this,[this]{if(list->currentIndex().isValid())emit activateRequested(list->currentIndex().row());});
    auto enter=new QShortcut(QKeySequence(Qt::Key_Enter),list);enter->setContext(Qt::WidgetWithChildrenShortcut);connect(enter,&QShortcut::activated,activate,&QShortcut::activated);
    connect(detach,&QToolButton::clicked,this,&DocumentSearchResults::detachRequested);
}
DocumentSearchResults::~DocumentSearchResults()=default;
const SearchOutcome &DocumentSearchResults::outcome() const {return model->value;}
void DocumentSearchResults::setResult(const SearchSource &source,const SearchOptions &options,SearchOutcome result,bool dirty) {
    queryLabel->setText(options.query);queryLabel->setToolTip(options.query);
    countLabel->setText(QString("%1 个匹配 · %2 行").arg(result.matchCount).arg(result.lines.size()));
    fileLabel->setText(QFileInfo(source.path).fileName());fileLabel->setToolTip(source.path);
    serverLabel->setText(source.server);serverLabel->setToolTip(source.server);
    static_cast<ElidedLabel *>(pathLabel)->setText(source.path);pathLabel->setToolTip(source.path);
    dirtyLabel->setText(dirty?"未保存内容":QString());
    const auto message=!result.notice.isEmpty()?result.notice:result.lines.isEmpty()?QString("未找到匹配项"):QString();
    model->replace(std::move(result));setFeedback(message);if(model->rowCount())list->setCurrentIndex(model->index(0));
}
void DocumentSearchResults::setFeedback(const QString &message){feedback->setText(message);feedback->setVisible(!message.isEmpty());}
void DocumentSearchResults::setDetached(bool value){detach->setIcon(icon(value?Icon::Return:Icon::Expand));detach->setToolTip(value?"收回编辑页签":"移到独立窗口");setProperty("detached",value);}
DocumentSearchController::DocumentSearchController(QWidget *parent):QObject(parent),owner(parent) {
    setObjectName("documentSearchController");dialog=new AdvancedSearchDialog(owner);resultPanel=new DocumentSearchResults(owner);resultPanel->hide();
    resultWindow=new QDialog(owner,Qt::Window);resultWindow->setObjectName("documentSearchResultsWindow");resultWindow->setWindowTitle("搜索结果");resultWindow->setProperty("rabbitUi",true);resultWindow->resize(uiPx(800),uiPx(450));resultWindow->setMinimumSize(uiSize(300,220));
    auto layout=new QVBoxLayout(resultWindow);uiMargins(layout,0,0,0,0);uiSpacing(layout,0);resultWindow->installEventFilter(this);
    connect(dialog,&AdvancedSearchDialog::findAllRequested,this,&DocumentSearchController::executeSearch);
    connect(dialog,&QDialog::rejected,this,[this]{cancelSearch();clearHighlights();});
    connect(dialog->findChild<QLineEdit *>("advancedSearchQuery"),&QLineEdit::textChanged,this,[this](const QString &value){if(value.isEmpty()){cancelSearch();clearHighlights();}});
    connect(resultPanel,&DocumentSearchResults::detachRequested,this,[this]{setDetached(!detached);});
    connect(resultPanel,&DocumentSearchResults::activateRequested,this,&DocumentSearchController::activateResult);
    connect(qApp,&QApplication::focusChanged,this,[this](QWidget *,QWidget *focus){if(auto editor=editorFromFocus(focus))lastEditor=editor;});
    if(auto lock=AppLock::instance())connect(lock,&AppLock::lockedChanged,this,[this](bool value){if(value){cancelSearch();pendingActivation=-1;}});
}
DocumentSearchController::~DocumentSearchController(){cancelSearch();if(highlightedEditor)highlightedEditor->clearAdvancedSearch();}
void DocumentSearchController::clearHighlights(){pendingActivation=-1;if(highlightedEditor)highlightedEditor->clearAdvancedSearch();highlightedEditor=nullptr;}
void DocumentSearchController::publishHighlights(Editor *editor,const SearchOutcome &result){
    if(highlightedEditor)highlightedEditor->clearAdvancedSearch();
    highlightedEditor=editor?editor->code:nullptr;
    if(highlightedEditor)highlightedEditor->setAdvancedSearch(result);
}
Workspace *DocumentSearchController::workspaceFor(Editor *editor) const {
    if(!editor)return nullptr;
    for(auto workspace:workspaces)if(workspace&&!workspace->shuttingDown&&workspace->editors.value(editor->path)==editor)return workspace;
    return nullptr;
}
Workspace *DocumentSearchController::sourceWorkspace(const SearchSource &source) const {
    for(auto workspace:workspaces)if(workspace&&!workspace->shuttingDown&&workspace->profile.id==source.profileId&&workspace->profile.credentialKey()==source.connectionKey)return workspace;
    return nullptr;
}
Editor *DocumentSearchController::editorFromFocus(QWidget *focus) const {
    for(auto widget=focus;widget;widget=widget->parentWidget())if(auto editor=qobject_cast<Editor *>(widget))return workspaceFor(editor)?editor:nullptr;
    return nullptr;
}
bool DocumentSearchController::canOpen(QWidget *focus) const{return !OperationGate::locked()&&editorFromFocus(focus);}
bool DocumentSearchController::openFor(QWidget *focus){auto editor=editorFromFocus(focus);return editor&&openFor(workspaceFor(editor),editor);}
bool DocumentSearchController::openFor(Workspace *workspace,Editor *editor) {
    if(OperationGate::locked()||!workspace||workspaceFor(editor)!=workspace)return false;
    cancelSearch();captured={workspace->profile.id,workspace->profile.credentialKey(),workspace->profile.identity(),editor->path,editor,editor->code->document()};
    dialog->setSource(captured);dialog->setFeedback({});dialog->show();if(dialog->isMinimized())dialog->showNormal();keepWindowOnScreen(dialog);dialog->raise();dialog->activateWindow();dialog->focusQuery();return true;
}
void DocumentSearchController::registerWorkspace(Workspace *workspace) {
    if(!workspace||workspaces.contains(workspace))return;workspaces.append(workspace);
    connect(workspace,&Workspace::searchDocumentOpened,this,[this](Editor *){resumeActivation();});
    connect(workspace,&Workspace::stateChanged,this,&DocumentSearchController::resumeActivation);
    connect(workspace,&Workspace::aboutToShutdown,this,[this,workspace]{releaseWorkspace(workspace);});
    connect(workspace,&Workspace::searchPageCloseRequested,this,[this]{parkResults();});
    connect(workspace,&Workspace::searchPageFloatRequested,this,[this]{setDetached(true);});
    connect(workspace,&QObject::destroyed,this,[this]{workspaces.removeAll(nullptr);});
    resumeActivation();
}
void DocumentSearchController::cancelSearch(){++generation;if(process){auto old=process.data();process=nullptr;old->disconnect(this);old->kill();connect(old,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),old,&QObject::deleteLater);if(old->state()==QProcess::NotRunning)old->deleteLater();}}
void DocumentSearchController::executeSearch() {
    if(OperationGate::locked())return;
    const auto options=dialog->options();auto workspace=sourceWorkspace(captured);
    if(!workspace||!captured.editor||workspaceFor(captured.editor)!=workspace||captured.editor->code->document()!=captured.document||captured.editor->path!=captured.path){dialog->setFeedback("来源文档已关闭或身份已变化，请从编辑文档重新打开搜索。",true);emit searchFinished(false);return;}
    cancelSearch();
    const auto size=captured.document->characterCount();
    if(options.query.isEmpty()||options.query.size()>512||size>searchTextLimit+1||(options.regularExpression&&size>searchRegexTextLimit+1)){
        dialog->setFeedback(options.query.isEmpty()?"请输入搜索内容。":options.regularExpression?"正则搜索支持不超过 256 Ki 字符的文档。":"文档超出搜索限制。",true);emit searchFinished(false);return;
    }
    const QString text=captured.editor->code->toPlainText();const int revision=captured.document->revision();
    const auto origin=captured;const auto hash=digest(text);const bool dirty=origin.editor->dirty();const auto serial=generation,lockToken=OperationGate::token();
    auto worker=new QProcess(this);process=worker;worker->setProgram(workerProgram());worker->setArguments({"--internal-document-search-worker"});
    auto timer=new QTimer(worker);timer->setSingleShot(true);auto response=std::make_shared<QByteArray>();auto completed=std::make_shared<bool>(false);
    auto fail=[this,worker,serial,completed](const QString &message){if(*completed)return;*completed=true;if(process==worker)process=nullptr;worker->kill();if(serial==generation&&!OperationGate::locked()){dialog->setFeedback(message,true);emit searchFinished(false);}worker->deleteLater();};
    connect(timer,&QTimer::timeout,this,[fail]{fail("搜索耗时过长，已停止。请简化表达式或缩小文档。");});
    connect(worker,&QProcess::errorOccurred,this,[fail](QProcess::ProcessError error){if(error==QProcess::FailedToStart)fail("无法启动本地搜索任务。");});
    connect(worker,&QProcess::readyReadStandardOutput,this,[worker,response,fail]{response->append(worker->readAllStandardOutput());if(response->size()>searchWireLimit)fail("搜索结果超出内存限制。");});
    connect(worker,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,worker,timer,response,completed,fail,serial,lockToken,origin,revision,options,hash,dirty](int code,QProcess::ExitStatus status){
        if(*completed)return;timer->stop();response->append(worker->readAllStandardOutput());
        if(serial!=generation||!OperationGate::accepts(lockToken)){*completed=true;if(process==worker)process=nullptr;worker->deleteLater();return;}
        SearchOutcome result;
        if(code||status!=QProcess::NormalExit||!readSearchOutcome(*response,&result)){fail("本地搜索任务未能完成，请重试。");return;}
        *completed=true;process=nullptr;worker->deleteLater();
        if(!origin.editor||workspaceFor(origin.editor)!=sourceWorkspace(origin)||origin.editor->path!=origin.path||origin.editor->code->document()!=origin.document||origin.document->revision()!=revision){dialog->setFeedback("来源内容已变化，请重新查找。",true);emit searchFinished(false);return;}
        if(!result.error.isEmpty()){dialog->setFeedback(result.error,true);emit searchFinished(false);return;}
        publishHighlights(origin.editor,result);
        resultOrigin=origin;resultDigest=hash;anchors.clear();pendingActivation=-1;
        resultWindow->setWindowTitle("搜索结果 — "+origin.server);
        for(const auto &line:result.lines){QTextCursor anchor(origin.document);const auto span=line.spans.first();anchor.setPosition(line.offset+span.start);anchor.setPosition(line.offset+span.start+span.length,QTextCursor::KeepAnchor);anchors.append(anchor);}
        resultPanel->setResult(origin,options,std::move(result),dirty);dialog->setFeedback(resultPanel->outcome().lines.isEmpty()?"未找到匹配项":QString());
        if(!detached)attachResults(sourceWorkspace(origin));else {resultWindow->show();resultPanel->show();}
        emit searchFinished(true);
    });
    connect(worker,&QProcess::started,this,[worker,text,options]{worker->write(searchRequest(text,options));worker->closeWriteChannel();});
    dialog->setFeedback("正在查找…");emit searchStarted();timer->start(searchDeadlineMs);worker->start();
}
void DocumentSearchController::parkResults(){
    cancelSearch();clearHighlights();pendingActivation=-1;
    if(host){host->editArea->removePage(resultPanel);host=nullptr;}
    resultPanel->hide();resultPanel->setParent(owner);resultWindow->hide();detached=false;resultPanel->setDetached(false);
}
void DocumentSearchController::attachResults(Workspace *workspace){
    if(!workspace)return;
    if(host&&host!=workspace)host->editArea->removePage(resultPanel);
    resultWindow->hide();detached=false;host=workspace;resultPanel->setDetached(false);
    if(workspace->groupFor(resultPanel))workspace->editArea->activatePage(resultPanel);
    else workspace->editArea->addPage(resultPanel,"搜索结果");
    resultPanel->show();emit workspaceActivationRequested(workspace);
}
void DocumentSearchController::setDetached(bool value){
    if(OperationGate::locked()||resultOrigin.profileId.isEmpty())return;
    if(value){if(host)host->editArea->removePage(resultPanel);resultWindow->layout()->addWidget(resultPanel);detached=true;resultPanel->setDetached(true);resultPanel->show();resultWindow->show();keepWindowOnScreen(resultWindow);resultWindow->raise();resultWindow->activateWindow();}
    else if(auto workspace=sourceWorkspace(resultOrigin))attachResults(workspace);
    else {resultPanel->setFeedback("来源服务器已关闭；重新打开来源后可收回页签。");resultWindow->show();}
}
void DocumentSearchController::showResults(){if(OperationGate::locked()||resultOrigin.profileId.isEmpty())return;if(detached){resultWindow->show();resultWindow->raise();resultWindow->activateWindow();}else if(auto workspace=host?host.data():sourceWorkspace(resultOrigin))attachResults(workspace);else setDetached(true);}
void DocumentSearchController::releaseWorkspace(Workspace *workspace){
    if(captured.profileId==workspace->profile.id&&captured.connectionKey==workspace->profile.credentialKey())cancelSearch();
    if(host==workspace){if(detached)host=nullptr;else parkResults();}
    if(pendingActivation>=0&&resultOrigin.profileId==workspace->profile.id)pendingActivation=-1;
}
void DocumentSearchController::activateResult(int row){
    if(OperationGate::locked()||row<0||row>=resultPanel->outcome().lines.size())return;
    if(resultOrigin.editor&&workspaceFor(resultOrigin.editor)==sourceWorkspace(resultOrigin)&&resultOrigin.editor->path==resultOrigin.path&&resultOrigin.editor->code->document()==resultOrigin.document){navigate(resultOrigin.editor,row,false);return;}
    pendingActivation=row;pendingGeneration=generation;
    if(auto workspace=sourceWorkspace(resultOrigin)){
        emit workspaceActivationRequested(workspace);
        if(auto editor=workspace->editors.value(resultOrigin.path)){navigate(editor,row,true);pendingActivation=-1;return;}
        resultPanel->setFeedback("正在重新打开来源文档…");workspace->openFile(resultOrigin.path);
        if(!workspace->online&&!workspace->mainPending)resultPanel->setFeedback("来源服务器未连接，请连接后重试定位。");
    }else {resultPanel->setFeedback("正在打开来源服务器…");emit reopenSourceRequested(resultOrigin.profileId,resultOrigin.connectionKey,resultOrigin.path);}
}
void DocumentSearchController::resumeActivation(){
    if(pendingActivation<0||OperationGate::locked()||pendingGeneration!=generation)return;
    if(auto workspace=sourceWorkspace(resultOrigin)){
        if(auto editor=workspace->editors.value(resultOrigin.path)){const auto row=pendingActivation;pendingActivation=-1;navigate(editor,row,true);}
        else if(workspace->online)workspace->openFile(resultOrigin.path);
    }
}
void DocumentSearchController::navigate(Editor *editor,int row,bool reopened){
    if(OperationGate::locked()||!editor)return;auto workspace=workspaceFor(editor);if(!workspace||workspace!=sourceWorkspace(resultOrigin))return;
    const auto &hit=resultPanel->outcome().lines[row];QTextCursor cursor(editor->code->document());
    if(reopened){if(digest(editor->code->toPlainText())!=resultDigest){resultPanel->setFeedback("搜索结果已过期：来源内容已变化，请重新搜索。");return;}cursor.setPosition(hit.offset+hit.spans.first().start);cursor.setPosition(cursor.position()+hit.spans.first().length,QTextCursor::KeepAnchor);}
    else {if(row>=anchors.size()||anchors[row].isNull()){resultPanel->setFeedback("搜索结果已过期，请重新搜索。");return;}cursor=anchors[row];}
    const auto block=cursor.document()->findBlock(cursor.selectionStart());
    if(block.text()!=hit.text||cursor.selectedText()!=hit.text.mid(hit.spans.first().start,hit.spans.first().length)){
        resultPanel->setFeedback("搜索结果已过期：命中行已修改，请重新搜索。");return;
    }
    if(reopened){
        publishHighlights(editor,resultPanel->outcome());resultOrigin.editor=editor;resultOrigin.document=editor->code->document();anchors.clear();
        for(const auto &line:resultPanel->outcome().lines){QTextCursor anchor(resultOrigin.document);const auto span=line.spans.first();anchor.setPosition(line.offset+span.start);anchor.setPosition(line.offset+span.start+span.length,QTextCursor::KeepAnchor);anchors.append(anchor);}
    }
    emit workspaceActivationRequested(workspace);workspace->openFile(editor->path);
    auto window=editor->window();window->show();if(window->isMinimized())window->showNormal();window->raise();window->activateWindow();
    if(!highlightedEditor && digest(editor->code->toPlainText())==resultDigest)publishHighlights(editor,resultPanel->outcome());
    if(!highlightedEditor)highlightedEditor=editor->code;
    editor->code->selectAdvancedSearchCurrent(row,cursor);editor->code->centerCursor();editor->code->setFocus();
    resultPanel->setFeedback({});emit resultActivated(editor,block.blockNumber()+1,cursor.selectionStart()-block.position()+1);
}
bool DocumentSearchController::eventFilter(QObject *object,QEvent *event){if(object==resultWindow&&event->type()==QEvent::Close){if(!OperationGate::locked())parkResults();static_cast<QCloseEvent *>(event)->ignore();return true;}return QObject::eventFilter(object,event);}
}
