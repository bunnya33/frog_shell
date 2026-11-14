#include "messagebox.h"
#include "editor.h"
#include "theme.h"
#include "icons.h"
#include "fileicons.h"
#include "layoutmetrics.h"
#include "windowstyle.h"
#include "contentfont.h"
#include "editorlexer.h"
namespace rs {
namespace {
constexpr int kSyntaxLimit = 1024 * 1024;
// A single parse process-wide; a busy editor keeps no queued text snapshot.
struct SyntaxScheduler {
    QObject receiver;
    QThreadPool pool;
    SyntaxScheduler() { pool.setMaxThreadCount(1); }
};
SyntaxScheduler &syntaxScheduler() { static SyntaxScheduler scheduler; return scheduler; }
}
class EditorTabBar final : public PanelTabBar {
  public:
    using PanelTabBar::PanelTabBar;
};
CodeEdit::CodeEdit(QWidget *p) : EditorSurface(p) {
    syntaxTimer = new QTimer(this);
    syntaxTimer->setSingleShot(true);
    syntaxTimer->setInterval(300);
    connect(syntaxTimer, &QTimer::timeout, this, &CodeEdit::startSyntax);
    connect(document(), &EditorDocument::bytesChanged, this, [this](int position, int removed, int added) {
        // Highlighter/theme format refreshes can emit contentsChanged without
        // changing text. They must not clear or reschedule a valid diagnostic.
        if (removed || added) {
            adjustFindMatches(position, removed, added);
            adjustAdvancedSearch(position, removed, added);
            scheduleSyntax();
        }
    });
    viewport()->installEventFilter(this);
    setAccessibleName("文件编辑器");
    setLineWrapMode(NoWrap);
    bindContentFont(this, ContentFontRole::Editor);
    setTabStopDistance(QFontMetricsF(font(), viewport()).horizontalAdvance(' ') * 4);
    highlighter = new EditorLexer(this);
    updateGutter();
    auto cursorChanged = [this] {
        if (locatingMatch) return;
        pendingFindNavigation = FindNavigation::None;
        selectionOwner = SelectionOwner::None;
        ownedSelection = {};
        if (searchSelection) {
            searchSelection = false;
            updateHighlights();
        } else updateSearchSelectionStyle();
        updateCursorOutline();
    };
    connect(this, &EditorSurface::selectionChanged, this, cursorChanged);
    connect(&UiZoom::instance(), &UiZoom::changed, this, [this]{
        updateGutter();if(findPanel){findPanel->setFixedWidth(qMax(uiPx(120),qMin(uiPx(520),width()-uiPx(24))));findPanel->adjustSize();findPanel->move(qMax(0,width()-findPanel->width()-uiPx(18)),uiPx(4));}
    });
}
void CodeEdit::ensureFindPanel() {
    if (findPanel)
        return;
    findPanel = new QFrame(this);
    // The content font must not leak into find/replace controls or their labels.
    bindUiFont(findPanel);
    findPanel->setObjectName("side");
    findPanel->setFrameShape(QFrame::StyledPanel);
    setFrameRole(findPanel,"find");
    auto l = new QVBoxLayout(findPanel);
    uiMargins(l,6,4,6,4);
    uiSpacing(l,3);
    findTimer=new QTimer(this);findTimer->setSingleShot(true);findTimer->setInterval(100);
    connect(findTimer,&QTimer::timeout,this,&CodeEdit::updateMatches);
    auto row = new QHBoxLayout;
    findToggle=iconButton(Icon::ChevronRight,"显示或隐藏替换");findToggle->setObjectName("findToggleReplace");row->addWidget(findToggle);
    auto inputShell=new QWidget;inputShell->setObjectName("findInputShell");
    auto inputLayout=new QHBoxLayout(inputShell);uiMargins(inputLayout,2,1,2,1);uiSpacing(inputLayout,0);
    query = new QLineEdit;
    query->setObjectName("findQuery");
    query->installEventFilter(this);
    query->setPlaceholderText("查找");
    query->setAccessibleName("查找文本");
    inputLayout->addWidget(query,1);row->addWidget(inputShell,1);
    auto option=[this,inputShell,inputLayout](const QString &text,const QString &name,const QString &tip){auto b=new QToolButton(inputShell);uiFixedSize(b,24,24);b->setText(text);b->setToolButtonStyle(Qt::ToolButtonTextOnly);b->setProperty("findOption",true);b->setCheckable(true);b->setObjectName(name);b->setToolTip(tip);b->setAccessibleName(tip);inputLayout->addWidget(b);connect(b,&QToolButton::toggled,this,[this]{scheduleFind();});return b;};
    findCase=option("Aa","findCaseSensitive","区分大小写 (Alt+C)");
    findWord=option("ab","findWholeWord","全字匹配 (Alt+W)");auto wholeFont=findWord->font();wholeFont.setUnderline(true);findWord->setFont(wholeFont);
    findRegex=option(".*","findRegex","使用正则表达式 (Alt+R)");
    findStatus = new QLabel;findStatus->setObjectName("findStatus");row->addWidget(findStatus);
    auto prev = iconButton(Icon::Up, "上一处", nullptr, IconSize::Toolbar, "Shift+Enter");
    auto next = iconButton(Icon::Down, "下一处", nullptr, IconSize::Toolbar, "Enter");
    auto close = iconButton(Icon::Close, "关闭查找", nullptr, IconSize::Toolbar, "Esc");
    row->addWidget(prev);
    row->addWidget(next);
    row->addWidget(close);
    l->addLayout(row);
    replaceRow = new QWidget;
    auto r = new QHBoxLayout(replaceRow);
    uiMargins(r,0,0,0,0);
    r->addSpacing(uiPx(28));
    replacement = new QLineEdit;
    replacement->setObjectName("findReplacement");
    replacement->installEventFilter(this);
    replacement->setPlaceholderText("替换为");
    r->addWidget(replacement);
    preserveCase=new QToolButton;preserveCase->setObjectName("findPreserveCase");preserveCase->setText("AB");preserveCase->setCheckable(true);preserveCase->setToolTip("保留大小写");r->addWidget(preserveCase);
    auto one = iconButton(Icon::Replace,"替换当前"), all = iconButton(Icon::ReplaceAll,"全部替换");one->setObjectName("findReplaceOne");all->setObjectName("findReplaceAll");
    r->addWidget(one);
    r->addWidget(all);
    l->addWidget(replaceRow);
    replaceRow->hide();
    findPanel->hide();
    connect(close, &QToolButton::clicked, this, [this] {
        closeFind();
        setFocus();
    });
    connect(findToggle, &QToolButton::clicked, this, [this] {
        replaceRow->setVisible(!replaceRow->isVisible());
        findToggle->setIcon(icon(replaceRow->isVisible()?Icon::ChevronDown:Icon::ChevronRight));
        findPanel->adjustSize();
    });
    connect(next, &QToolButton::clicked, this, [this] { locate(); });
    connect(prev, &QToolButton::clicked, this, [this] { locate(true); });
    connect(query, &QLineEdit::returnPressed, this, [this] { locate(); });
    connect(query, &QLineEdit::textChanged, this, [this] { scheduleFind(); });
    connect(document(),&EditorDocument::contentsChanged,this,[this]{if(findPanel->isVisible())scheduleFind(false);});
    connect(one,&QToolButton::clicked,this,[this]{replaceMatches(false);});
    connect(all,&QToolButton::clicked,this,[this]{replaceMatches(true);});
    for(auto pair:QList<QPair<int,QToolButton *>>{{Qt::Key_C,findCase},{Qt::Key_W,findWord},{Qt::Key_R,findRegex}}){auto shortcut=new QShortcut(QKeySequence(Qt::ALT|pair.first),findPanel);connect(shortcut,&QShortcut::activated,pair.second,&QToolButton::toggle);}
    auto esc = new QShortcut(QKeySequence(Qt::Key_Escape), findPanel);
    connect(esc, &QShortcut::activated, this, [this] {
        closeFind();
        setFocus();
    });
    auto previous = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Return), findPanel);
    connect(previous, &QShortcut::activated, this, [this] { locate(true); });
}
void CodeEdit::setSyntaxPath(const QString &path) {
    syntaxFormat = syntaxFormatForPath(path);
    scheduleSyntax();
}
void CodeEdit::setSyntaxResult(const SyntaxDiagnostic &result) {
    syntaxResult = result;
    updateHighlights();
    if (result.error)
        emit syntaxStatusChanged(QStringLiteral("语法错误 · %1:%2").arg(result.line).arg(result.column));
    else emit syntaxStatusChanged(QString());
}
void CodeEdit::scheduleSyntax() {
    ++syntaxRevision;
    syntaxTimer->stop();
    setSyntaxResult({});
    if (syntaxFormat == SyntaxFormat::None) return;
    if (document()->characterCount() - 1 > kSyntaxLimit) {
        emit syntaxStatusChanged(QStringLiteral("语法未检查 · 文件超过 1 Mi 字符"));
        return;
    }
    emit syntaxStatusChanged(QStringLiteral("语法检查中…"));
    syntaxTimer->start(300);
}
void CodeEdit::startSyntax() {
    if (syntaxFormat == SyntaxFormat::None || syntaxRunning) return;
    if (document()->characterCount() - 1 > kSyntaxLimit) { scheduleSyntax(); return; }
    auto &scheduler = syntaxScheduler();
    const auto revision = syntaxRevision;
    const auto format = syntaxFormat;
    const auto input = toPlainText();
    QPointer<CodeEdit> target(this);
    auto task = QRunnable::create([target, revision, format, input, receiver = &scheduler.receiver] {
        const auto result = checkSyntax(format, input);
        QMetaObject::invokeMethod(receiver, [target, revision, result] {
            if (!target) return;
            target->syntaxRunning = false;
            if (revision != target->syntaxRevision) {
                if (target->syntaxFormat != SyntaxFormat::None && !target->syntaxTimer->isActive())
                    target->syntaxTimer->start(0);
                return;
            }
            target->setSyntaxResult(result);
        }, Qt::QueuedConnection);
    });
    if (!scheduler.pool.tryStart(task)) {
        delete task;
        syntaxTimer->start(100);
        return;
    }
    syntaxRunning = true;
}
QRect CodeEdit::syntaxAnchorRect() const {
    EditorCursor cursor(document());
    cursor.setPosition(qBound(0, syntaxResult.offset, document()->characterCount() - 1));
    QRect rect = cursorRect(cursor);
    const auto block = cursor.block();
    if (cursor.position() < block.position() + block.length() - 1) {
        cursor.movePosition(QTextCursor::NextCharacter);
        rect = rect.united(cursorRect(cursor));
    }
    return rect.adjusted(-2, 0, 8, 2);
}
bool CodeEdit::eventFilter(QObject *object,QEvent *event) {
    if (object == viewport() && event->type() == QEvent::ToolTip && syntaxResult.error) {
        auto help = static_cast<QHelpEvent *>(event);
        const int pos = cursorForPosition(help->pos()).position();
        if (pos == syntaxResult.offset || syntaxAnchorRect().contains(help->pos())) {
            QToolTip::showText(help->globalPos(), QStringLiteral("语法错误 %1:%2 · %3")
                .arg(syntaxResult.line).arg(syntaxResult.column).arg(syntaxResult.message), viewport());
            return true;
        }
        QToolTip::hideText();
    }
    if((object==query||object==replacement)&&(event->type()==QEvent::ShortcutOverride||event->type()==QEvent::KeyPress)){
        auto key=static_cast<QKeyEvent *>(event);
        if((key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)&&!(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))){
            // QLineEdit deliberately ignores Return after emitting its signal
            // for dialog defaults. Here that event would bubble into CodeEdit
            // and replace the newly selected match with a newline.
            key->accept();if(event->type()==QEvent::KeyPress&&object==query)locate(key->modifiers().testFlag(Qt::ShiftModifier));return true;
        }
    }
    return EditorSurface::eventFilter(object,event);
}
void CodeEdit::closeFind() {
    if (findPanel) findPanel->hide();
    if (findTimer) findTimer->stop();
    ++findRevision;
    pendingFindNavigation = FindNavigation::None;
    clearOwnedSelection(SelectionOwner::Find);
    currentMatch = -1;
    matches.clear();
    searchSelection = false;
    updateHighlights();
}
void CodeEdit::scheduleFind(bool navigate) {
    ++findRevision;
    // Query changes invalidate results; document edits retain unaffected tracked
    // hits until the new snapshot is ready. Never select text from an edit.
    if (navigate) clearOwnedSelection(SelectionOwner::Find);
    else if (selectionOwner == SelectionOwner::Find) {
        selectionOwner = SelectionOwner::None; ownedSelection = {};
    }
    const bool invalidate = navigate || !query || query->text().isEmpty();
    const bool repaint = invalidate || searchSelection;
    if (invalidate) {
        currentMatch = -1;
        matches.clear();
    }
    searchSelection = false;
    pendingFindNavigation = navigate ? FindNavigation::Forward : FindNavigation::None;
    if (repaint) updateHighlights();
    if (query && query->text().isEmpty()) {
        pendingFindNavigation = FindNavigation::None;
        if (findTimer) findTimer->stop();
        if (findStatus) findStatus->clear();
        return;
    }
    if (findTimer) findTimer->start();
}
void CodeEdit::adjustFindMatches(int position, int removed, int added) {
    const int changeEnd = position + removed;
    int retained = 0, retainedCurrent = -1;
    for (int i = 0; i < matches.size(); ++i) {
        auto &match = matches[i];
        // Drop only touched tokens. Insertions at either boundary are outside
        // the token; word/regex context is checked by the debounced full search.
        if ((removed && position < match.byteEnd && changeEnd > match.byteStart) ||
            (added && position > match.byteStart && position < match.byteEnd) ||
            (match.byteStart == match.byteEnd && position == match.byteStart)) continue;
        if (changeEnd <= match.byteStart) {
            match.byteStart += added - removed;
            match.byteEnd += added - removed;
        }
        // EditorCursor anchors already followed the change. A right-boundary
        // insertion must not expand the painted match to include the new text.
        if (match.cursor.byteAnchor() != match.byteStart || match.cursor.bytePosition() != match.byteEnd) {
            match.cursor = EditorCursor(document());
            match.cursor.setPosition(toUtf16(match.byteStart));
            match.cursor.setPosition(toUtf16(match.byteEnd), QTextCursor::KeepAnchor);
        }
        if (i == currentMatch) retainedCurrent = retained;
        if (retained != i) matches[retained] = std::move(match);
        ++retained;
    }
    matches.resize(retained);
    currentMatch = retainedCurrent;
}
void CodeEdit::clearOwnedSelection(SelectionOwner owner) {
    if (selectionOwner != owner) return;
    const auto current = textCursor();
    const bool owned = current.hasSelection() && current == ownedSelection;
    selectionOwner = SelectionOwner::None; ownedSelection = {};
    if (owned) { auto cursor = current; cursor.clearSelection(); locatingMatch = true; setTextCursor(cursor); locatingMatch = false; updateCursorOutline(); }
}
void CodeEdit::updateMatches(){
    if(!findPanel||!findPanel->isVisible())return;
    if(findRunning)return;
    if(query->text().isEmpty()){findStatus->clear();return;}
    const QString input=toPlainText(), needle=query->text();const bool regex=findRegex->isChecked(),whole=findWord->isChecked(),sensitive=findCase->isChecked();
    if((regex&&input.size()>262144)||needle.size()>512){matches.clear();currentMatch=-1;updateHighlights();findStatus->setText("超出查找限制");return;}
    const quint64 revision=findRevision;findRunning=true;
    if(matches.isEmpty())findStatus->setText("查找中…");
    struct Result {QVector<Match> matches;QString error;};auto result=std::make_shared<Result>();
    auto worker=QThread::create([input,needle,regex,whole,sensitive,result]{
        QString pattern=regex?needle:QRegularExpression::escape(needle);
        if(whole)pattern="(?<![\\p{L}\\p{N}_])(?:"+pattern+")(?![\\p{L}\\p{N}_])";
        pattern="(*LIMIT_MATCH=10000)(*LIMIT_DEPTH=500)"+pattern;
        QRegularExpression::PatternOptions flags=QRegularExpression::UseUnicodePropertiesOption;
        if(!sensitive)flags|=QRegularExpression::CaseInsensitiveOption;
        QRegularExpression expression(pattern,flags);
        if(!expression.isValid()){result->error="正则表达式无效";return;}
        qsizetype offset=0;
        while(offset<=input.size()){
            auto match=expression.match(input,offset);
            if(!match.isValid()){result->error="表达式运算超出限制，无法得出结果";result->matches.clear();return;}
            if(!match.hasMatch())break;
            if(result->matches.size()>=10000){result->error="结果超过 10000 项";result->matches.clear();return;}
            result->matches.append({int(match.capturedStart()),int(match.capturedEnd()),match.capturedTexts()});
            offset=match.capturedEnd();
            if(match.capturedLength()==0){if(offset>=input.size())break;if(input.at(offset).isHighSurrogate()&&offset+1<input.size()&&input.at(offset+1).isLowSurrogate())++offset;++offset;}
        }
    });
    connect(worker,&QThread::finished,this,[this,result,revision]{
        findRunning=false;if(revision!=findRevision){if(findPanel&&findPanel->isVisible())findTimer->start(0);return;}
        if(!findPanel->isVisible())return;
        const int previousStart = currentMatch >= 0 ? matches[currentMatch].byteStart : -1;
        const int previousEnd = currentMatch >= 0 ? matches[currentMatch].byteEnd : -1;
        matches=std::move(result->matches);currentMatch=-1;
        for (int i=0;i<matches.size();++i) {
            auto &match=matches[i];
            match.cursor=EditorCursor(document());match.cursor.setPosition(match.start);
            match.cursor.setPosition(match.end,QTextCursor::KeepAnchor);
            match.byteStart=match.cursor.byteAnchor();match.byteEnd=match.cursor.bytePosition();
            if(match.byteStart==previousStart&&match.byteEnd==previousEnd)currentMatch=i;
        }
        if(!result->error.isEmpty()){updateHighlights();findStatus->setText(result->error);return;}
        if(matches.isEmpty()){updateHighlights();findStatus->setText("无结果");return;}
        const auto navigation = pendingFindNavigation;
        pendingFindNavigation = FindNavigation::None;
        if (navigation != FindNavigation::None) locate(navigation == FindNavigation::Backward);
        else {
            // Keep the current token's identity/color even when new hits appear
            // before it. A removed token falls back to the live caret, without
            // selecting text or scrolling.
            if (currentMatch < 0) {
                currentMatch = 0;
                const int position = textCursor().selectionStart();
                for (int i=0;i<matches.size();++i) if (matches[i].start>=position) { currentMatch=i; break; }
            }
            paintMatches();
        }
    });
    connect(worker,&QThread::finished,worker,&QObject::deleteLater);worker->start();
}
void CodeEdit::paintMatches(){
    updateHighlights();
    findStatus->setText(matches.isEmpty()?QString("无结果"):QString("%1 / %2").arg(currentMatch+1).arg(matches.size()));
}
void CodeEdit::setAdvancedSearch(const SearchOutcome &result) {
    advancedRanges.clear(); advancedCurrent = {}; advancedCurrentRow = -1;
    const int end = document()->characterCount() - 1;
    for (int row = 0; row < result.lines.size(); ++row) {
        const auto &line = result.lines[row];
        for (const auto &span : line.spans) {
            const qint64 start = qint64(line.offset) + span.start, finish = start + span.length;
            if (span.length <= 0 || start < 0 || finish > end || span.start < 0 ||
                qint64(span.start) + span.length > line.text.size()) continue;
            EditorCursor cursor(document()); cursor.setPosition(int(start)); cursor.setPosition(int(finish), QTextCursor::KeepAnchor);
            advancedRanges.append({cursor, line.text.mid(span.start, span.length), cursor.byteAnchor(), cursor.bytePosition(), row});
        }
    }
    updateHighlights();
}
void CodeEdit::clearAdvancedSearch() {
    clearOwnedSelection(SelectionOwner::Advanced); advancedRanges.clear(); advancedCurrent = {}; advancedCurrentRow = -1; updateHighlights();
}
void CodeEdit::setAdvancedSearchCurrent(int row, const EditorCursor &cursor) {
    advancedCurrentRow = row; advancedCurrent = cursor;
    for (auto &range : advancedRanges) range.current = range.row == row && range.start == qMin(cursor.byteAnchor(), cursor.bytePosition()) && range.end == qMax(cursor.byteAnchor(), cursor.bytePosition());
    updateHighlights();
}
void CodeEdit::selectAdvancedSearchCurrent(int row, const EditorCursor &cursor) {
    searchSelection = false;
    setAdvancedSearchCurrent(row, cursor);
    locatingMatch = true; setTextCursor(cursor); locatingMatch = false;
    selectionOwner = SelectionOwner::Advanced; ownedSelection = cursor;
    updateSearchSelectionStyle(); updateCursorOutline();
}
void CodeEdit::adjustAdvancedSearch(int position, int removed, int added) {
    if (advancedRanges.isEmpty() && advancedCurrent.isNull()) return;
    const int changeEnd = position + removed;
    for (int i = advancedRanges.size() - 1; i >= 0; --i) {
        auto &range = advancedRanges[i];
        // A change inside a hit invalidates it, even if the replacement happens
        // to contain the same characters. Boundary insertions belong outside it.
        if ((removed && position < range.end && changeEnd > range.start) ||
            (added && position > range.start && position < range.end)) {
            advancedRanges.removeAt(i); continue;
        }
        if (changeEnd <= range.start) { range.start += added - removed; range.end += added - removed; }
        if (range.start < 0 || range.end > byteLength() || range.start >= range.end) {
            advancedRanges.removeAt(i); continue;
        }
        range.cursor = EditorCursor(document()); range.cursor.setPosition(toUtf16(range.start));
        range.cursor.setPosition(toUtf16(range.end), QTextCursor::KeepAnchor);
        if (range.cursor.selectedText() != range.text) advancedRanges.removeAt(i);
    }
    // A zero-width current result has no painted token; discard its context on
    // edits. Otherwise retain it only while its exact tracked hit still exists.
    bool found = false;
    for (const auto &range : advancedRanges) {
        if (range.current) { advancedCurrent = range.cursor; found = true; break; }
    }
    if (!found) { advancedCurrent = {}; advancedCurrentRow = -1; }
    updateHighlights();
}
void CodeEdit::appendSearchLineHighlights(QList<EditorExtraSelection> &selections, int position) {
    const auto block = document()->findBlock(position);
    if (!block.isValid()) return;
    EditorExtraSelection line; line.cursor = EditorCursor(block);
    line.format.setProperty(QTextFormat::FullWidthSelection, true);
    line.format.setBackground(QColor(lightTheme() ? "#f0edf8" : "#363447")); selections.append(line);
}
void CodeEdit::updateHighlights() {
    ++highlightRebuildCount;
    QList<EditorExtraSelection> selections;
    const bool activeSearch = findPanel && findPanel->isVisible() && searchSelection &&
                              currentMatch >= 0 && currentMatch < matches.size() &&
                              !matches[currentMatch].cursor.isNull();
    const QColor currentColor("#eaa13b");
    if (activeSearch) {
        appendSearchLineHighlights(selections, matches[currentMatch].cursor.selectionStart());
    }
    if (!activeSearch && !advancedCurrent.isNull()) {
        appendSearchLineHighlights(selections, advancedCurrent.selectionStart());
    }
    const QColor advancedColor("#f6d75d"), advancedCurrentColor("#eaa13b"), advancedInk("#211806");
    for (const auto &range : advancedRanges) {
        EditorExtraSelection s; s.cursor = range.cursor;
        s.format.setBackground(range.current ? advancedCurrentColor : advancedColor); s.format.setForeground(advancedInk);
        selections.append(s);
    }
    for(int i=0;i<matches.size();++i){if(matches[i].cursor.isNull()||!matches[i].cursor.hasSelection())continue;EditorExtraSelection s;s.cursor=matches[i].cursor;s.format.setBackground(i==currentMatch?currentColor:QColor("#f6d75d"));s.format.setForeground(QColor("#211806"));selections.append(s);}
    if (syntaxResult.error) {
        const int pos = qBound(0, syntaxResult.offset, document()->characterCount() - 1);
        const auto block = document()->findBlock(pos);
        if (block.isValid() && pos < block.position() + block.length() - 1) {
            EditorExtraSelection error;
            error.cursor = EditorCursor(document());
            error.cursor.setPosition(pos);
            error.cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            error.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            error.format.setUnderlineColor(colors().red);
            selections.append(error);
        }
    }
    setExtraSelections(selections);
    // Native selection remains editable. Give the owned search selection the
    // current-match colors; ordinary user selections retain the theme colors.
    updateSearchSelectionStyle();
    viewport()->update();
}
void CodeEdit::updateSearchSelectionStyle() {
    const int start = int(send(SCI_GETSELECTIONSTART)), end = int(send(SCI_GETSELECTIONEND));
    const bool advancedSelected = !advancedCurrent.isNull() && start != end &&
        start == qMin(advancedCurrent.byteAnchor(), advancedCurrent.bytePosition()) && end == qMax(advancedCurrent.byteAnchor(), advancedCurrent.bytePosition());
    const bool activeSearch = findPanel && findPanel->isVisible() && searchSelection && currentMatch >= 0 && currentMatch < matches.size();
    const bool wanted = advancedSelected || (activeSearch && start != end);
    if (wanted == searchSelectionStyle) return;
    searchSelectionStyle = wanted; setSearchSelectionStyle(wanted);
}
void CodeEdit::updateCursorOutline() {
    setCursorFrameVisible(hasFocus() && send(SCI_GETSELECTIONEMPTY) && !searchSelection);
}
QString CodeEdit::replacementFor(const Match &match) const {
    QString out=replacement->text();
    if(findRegex->isChecked()){
        QString expanded;for(int i=0;i<out.size();++i){if(out[i]=='$'&&i+1<out.size()){
            if(out[i+1]=='$'){expanded+='$';++i;continue;}
            if(out[i+1]=='&'){expanded+=match.captures.value(0);++i;continue;}
            if(out[i+1].isDigit()){int n=out[++i].digitValue();if(i+1<out.size()&&out[i+1].isDigit()){int candidate=n*10+out[i+1].digitValue();if(candidate<match.captures.size()){n=candidate;++i;}}expanded+=match.captures.value(n);continue;}}
            expanded+=out[i];}out=expanded;
    }
    if(preserveCase->isChecked()&&!match.captures.isEmpty()){
        const auto source=match.captures[0];if(source.toUpper()==source&&source.toLower()!=source)out=out.toUpper();
        else if(source.toLower()==source)out=out.toLower();
        else if(!source.isEmpty()&&source.left(1).toUpper()==source.left(1)&&source.mid(1).toLower()==source.mid(1)&&!out.isEmpty())out=out.left(1).toUpper()+out.mid(1).toLower();
    }
    return out;
}
void CodeEdit::replaceMatches(bool all){
    if(isReadOnly()||matches.isEmpty()||findRunning||findTimer->isActive())return;
    const auto selected=matches;const int chosen=currentMatch;if(!all&&(chosen<0||chosen>=selected.size()))return;
    EditorCursor edit(document());edit.beginEditBlock();
    for(int i=selected.size()-1;i>=0;--i)if(all||i==chosen){edit.setPosition(selected[i].start);edit.setPosition(selected[i].end,QTextCursor::KeepAnchor);edit.insertText(replacementFor(selected[i]));}
    edit.endEditBlock();if(!all)setTextCursor(edit);scheduleFind();
}
void CodeEdit::refreshTheme() {
    refreshSurfaceTheme();
    highlighter->rehighlight();
    updateHighlights();
    updateCursorOutline();
}
bool CodeEdit::event(QEvent *event) {
    const bool result = EditorSurface::event(event);
    switch (event->type()) {
    case QEvent::FocusIn:
    case QEvent::FocusOut:
        updateCursorOutline();
        viewport()->update();
        break;
    case QEvent::FontChange:
    case QEvent::ApplicationFontChange:
    case QEvent::ScreenChangeInternal:
    case QEvent::DevicePixelRatioChange:
        updateGutter();
        break;
    default: break;
    }
    return result;
}
void CodeEdit::paintEvent(QPaintEvent *event) {
    EditorSurface::paintEvent(event);
    const auto errorBlock = document()->findBlock(syntaxResult.offset);
    if (syntaxResult.error && errorBlock.isValid() && syntaxResult.offset >= errorBlock.position() + errorBlock.length() - 1) {
        // Native indicators cover characters. Empty lines and EOF need a
        // visible anchor without inserting an artificial character.
        const QRect rect = syntaxAnchorRect();
        QPainter painter(viewport());
        painter.setClipRegion(event->region());
        painter.setPen(QPen(colors().red, 1));
        const int y = rect.bottom() - 3;
        QPolygon wave;
        const int start = rect.left() + 2;
        const int end = qMax(start + 8, rect.right() - 8);
        for (int x = start; x < end; ++x)
            wave << QPoint(x, y + ((x - rect.left()) % 4 < 2 ? 0 : 2));
        painter.drawPolyline(wave);
    }
}
void CodeEdit::wheelEvent(QWheelEvent *event) {
    // EditorSurface has a separate Ctrl-wheel point-size zoom. Never invoke it
    // for hover/Find focus or extra modifiers; our preferences are logical px.
    if (event->modifiers() & (Qt::ControlModifier | contentZoomModifier)) QAbstractScrollArea::wheelEvent(event);
    else EditorSurface::wheelEvent(event);
}
int CodeEdit::gutterWidth() const { return int(send(SCI_GETMARGINWIDTHN, 0)); }
void CodeEdit::updateGutter() { refreshFont(); }
void CodeEdit::showFind(bool replace) {
    ensureFindPanel();
    replaceRow->setVisible(replace);
    findToggle->setIcon(icon(replace?Icon::ChevronDown:Icon::ChevronRight));
    findPanel->show();
    findPanel->setFixedWidth(qMax(uiPx(120),qMin(uiPx(520),width()-uiPx(24))));
    findPanel->adjustSize();
    findPanel->move(qMax(0, width() - findPanel->width() - uiPx(18)), uiPx(4));
    findPanel->raise();
    query->setFocus();
    query->selectAll();
    scheduleFind();
}
void CodeEdit::locate(bool back) {
    if (findRunning || (findTimer && findTimer->isActive())) {
        if (query && !query->text().isEmpty()) pendingFindNavigation = back ? FindNavigation::Backward : FindNavigation::Forward;
        return;
    }
    if(matches.isEmpty())return;
    if (!searchSelection) currentMatch = -1;
    if(currentMatch<0){const int position=textCursor().selectionStart();currentMatch=0;
        if(back){currentMatch=matches.size()-1;for(int i=matches.size()-1;i>=0;--i)if(matches[i].end<=position){currentMatch=i;break;}}
        else for(int i=0;i<matches.size();++i)if(matches[i].start>=position){currentMatch=i;break;}
    }else currentMatch=(currentMatch+(back?-1:1)+matches.size())%matches.size();
    const auto &match=matches[currentMatch];EditorCursor c(document());c.setPosition(match.start);c.setPosition(match.end,QTextCursor::KeepAnchor);locatingMatch=true;setTextCursor(c);locatingMatch=false;selectionOwner=SelectionOwner::Find;ownedSelection=c;searchSelection=true;centerCursor();paintMatches();
}
void CodeEdit::resizeEvent(QResizeEvent *e) {
    EditorSurface::resizeEvent(e);
    updateGutter();
    if (findPanel && findPanel->isVisible()) {
        findPanel->setFixedWidth(qMax(uiPx(120),qMin(uiPx(520),width()-uiPx(24))));
        findPanel->move(qMax(0, width() - findPanel->width() - uiPx(18)), uiPx(4));
    }
    if (lineWrapMode() != NoWrap && (searchSelection || !advancedCurrent.isNull())) updateHighlights();
}
void CodeEdit::keyPressEvent(QKeyEvent *e) {
    if((e->key()==Qt::Key_Return||e->key()==Qt::Key_Enter)&&findPanel&&findPanel->isVisible()&&findPanel->isAncestorOf(QApplication::focusWidget())){
        e->accept();return; // Never turn a find/replace control key into document input.
    }
    if (e->matches(QKeySequence::Find)) {
        showFind();
        return;
    }
    if (e->modifiers() == Qt::ControlModifier && e->key() == Qt::Key_H) {
        showFind(true);
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        if (findPanel) closeFind();
        return;
    }
    EditorSurface::keyPressEvent(e);
}
Editor::Editor(const QString &sid, const QString &p, const QByteArray &raw, const QByteArray &hash,
               QWidget *parent)
    : QWidget(parent), path(p), remoteHash(hash), serverId(sid) {
    bom = raw.startsWith("\xef\xbb\xbf");
    crlf = raw.contains("\r\n");
    QByteArray data = bom ? raw.mid(3) : raw;
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString content = decoder(data);
    bool invalid = decoder.hasError();
    auto l = new QVBoxLayout(this);
    uiMargins(l,0,0,0,0);
    uiSpacing(l,0);
    auto pathBand = new QWidget;
    bandHeight(pathBand,28);
    auto top = new QHBoxLayout(pathBand);
    tools = top;
    uiMargins(top,12,0,8,0);
    uiSpacing(top,6);
    pathIcon = new QLabel(this);
    pathIcon->setObjectName("filePathIcon");
    uiFixedSize(pathIcon,18,18);
    // File type already appears in the tab; retain the icon for detached windows.
    pathIcon->hide();
    pathLabel = new ElidedLabel(path);
    pathLabel->setTextFormat(Qt::PlainText);
    pathLabel->setWordWrap(false);
    pathLabel->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    bindUiFont(pathLabel,12);
    pathLabel->setProperty("muted", true);
    top->addWidget(pathLabel, 1);
    auto save = iconButton(Icon::Save,"保存 · Ctrl+S");
    top->addWidget(save);
    l->addWidget(pathBand);
    code = new CodeEdit;
    code->setPlainText(content);
    code->document()->setModified(false);
    l->addWidget(code, 1);
    formatLabel = new QLabel(invalid ? "非 UTF-8 文件 · 只读预览，请下载后选择编码"
                                     : QString("UTF-8%1 · %2").arg(bom ? " BOM" : "", crlf ? "CRLF" : "LF"));
    UiZoom::instance().bind(formatLabel,"margin",[this]{formatLabel->setContentsMargins(uiPx(12),0,uiPx(10),0);});
    bandHeight(formatLabel,22);bindUiFont(formatLabel,12);
    formatLabel->setProperty("muted", true);
    auto statusBand = new QWidget;
    auto statusLayout = new QHBoxLayout(statusBand);
    uiMargins(statusLayout,0,0,0,0); uiSpacing(statusLayout,0);
    statusLayout->addWidget(formatLabel, 1);
    syntaxLabel = new QLabel;
    syntaxLabel->setObjectName("syntaxStatus");
    bindUiFont(syntaxLabel,12);
    syntaxLabel->setContentsMargins(0,0,uiPx(12),0);
    statusLayout->addWidget(syntaxLabel);
    connect(code, &CodeEdit::syntaxStatusChanged, syntaxLabel, &QLabel::setText);
    l->addWidget(statusBand);
    code->setReadOnly(invalid);
    save->setEnabled(!invalid);
    setPath(path);
    connect(&UiZoom::instance(),&UiZoom::changed,this,[this]{refreshFileIcon();});
    QFile draft(draftPath);
    if (draft.open(QIODevice::ReadOnly)) {
        auto o = QJsonDocument::fromJson(draft.readAll()).object();
        if (o["text"].isString() && o["text"].toString() != code->toPlainText() &&
            AppMessageBox::question(this, "恢复草稿", path + "\n发现上次未保存的草稿，是否恢复？") ==
                QMessageBox::Yes) {
            code->setPlainText(o["text"].toString());
            code->document()->setModified(true);
        }
    }
    connect(save, &QToolButton::clicked, this, [this] { emit saveRequested(this, false); });
    auto shortcut = new QShortcut(QKeySequence::Save, this);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut, &QShortcut::activated, this, [this] { emit saveRequested(this, false); });
    draftTimer.setSingleShot(true);
    draftTimer.setInterval(800);
    connect(&draftTimer, &QTimer::timeout, this, &Editor::persistDraft);
    connect(code, &EditorSurface::textChanged, this, [this] {
        draftTimer.start();
        emit changed();
    });
}
void Editor::setReturnAction(QAction *action) {
    if (!returnButton && action) {
        returnButton = iconButton(Icon::Return, "收回工作区", this);
        tools->addWidget(returnButton);
    }
    if (returnButton) {
        returnButton->setDefaultAction(action);
        returnButton->setVisible(action != nullptr);
    }
}
void Editor::setPath(const QString &p) {
    auto previousDraft = draftPath;
    path = p;
    code->setSyntaxPath(path);
    pathLabel->setText(path);pathLabel->setToolTip(path);pathLabel->setAccessibleName(path);
    refreshFileIcon();
    QDir().mkpath(dataPath() + "/drafts");
    draftPath =
        dataPath() + "/drafts/" +
        QString::fromLatin1(
            QCryptographicHash::hash((serverId + "\n" + path).toUtf8(), QCryptographicHash::Sha256).toHex()) +
        ".json";
    if (!previousDraft.isEmpty() && previousDraft != draftPath && QFileInfo::exists(previousDraft)) {
        if (!QFileInfo::exists(draftPath))
            QFile::rename(previousDraft, draftPath);
        else if (dirty())
            persistDraft();
    }
}
void Editor::refreshFileIcon() {
    const auto image = FileIconProvider::file(path);
    setWindowIcon(image);
    pathIcon->setPixmap(image.pixmap(uiSize(18,18), devicePixelRatioF()));
}
bool Editor::event(QEvent *event) {
    const auto handled = QWidget::event(event);
    if (event->type() == QEvent::DevicePixelRatioChange && pathIcon) refreshFileIcon();
    return handled;
}
bool Editor::dirty() const {
    return code->document()->isModified();
}
QByteArray Editor::bytes() const {
    auto s = code->toPlainText();
    if (crlf)
        s.replace("\n", "\r\n");
    return (bom ? QByteArray("\xef\xbb\xbf") : QByteArray()) + s.toUtf8();
}
void Editor::saved(const QByteArray &sent, const QByteArray &hash) {
    saving = false;
    remoteHash = hash;
    if (bytes() == sent) {
        code->document()->setModified(false);
        discardDraft();
    }
    formatLabel->setText(QString("UTF-8 · %1 · 已保存").arg(crlf ? "CRLF" : "LF"));
    emit changed();
}
void Editor::discardDraft() {
    draftTimer.stop();
    QFile::remove(draftPath);
}
void Editor::reload(const QByteArray &raw, const QByteArray &hash) {
    bom = raw.startsWith("\xef\xbb\xbf");
    crlf = raw.contains("\r\n");
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QByteArray payload = bom ? raw.mid(3) : raw;
    const QString content = decoder(payload);
    code->setPlainText(content);
    code->setReadOnly(decoder.hasError());
    code->document()->setModified(false);
    remoteHash = hash;
    saving = false;
    discardDraft();
    formatLabel->setText(
        decoder.hasError()
            ? "非 UTF-8 文件 · 只读预览"
            : QString("UTF-8%1 · %2 · 已重新载入").arg(bom ? " BOM" : "", crlf ? "CRLF" : "LF"));
    emit changed();
}
void Editor::persistDraft() {
    if (!dirty())
        return;
    QSaveFile f(draftPath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(
            QJsonDocument(QJsonObject{{"server", serverId}, {"path", path}, {"text", code->toPlainText()}})
                .toJson(QJsonDocument::Compact));
        f.commit();
        QFile::setPermissions(draftPath, QFile::ReadOwner | QFile::WriteOwner);
    }
}
EditorTabs::EditorTabs(QWidget *parent) : PanelTabs(parent) {
    setTabBar(new EditorTabBar(this));
    tabBar()->setExpanding(false);
    UiZoom::instance().bind(tabBar(),"icons",[this]{tabBar()->setIconSize(uiSize(18,18));});
    setTabsClosable(true);
    // One native drag path handles both tab order and group placement.
    setMovable(false);
    setAcceptDrops(true);
    setDocumentMode(true);
    setElideMode(Qt::ElideRight);
    setUsesScrollButtons(true);
    tabBar()->setDrawBase(false);
    tabBar()->setAcceptDrops(true);
    tabBar()->installEventFilter(this);
    tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QTabWidget::tabCloseRequested, this, [this](int i) { emit closeEditor(widget(i)); });
    connect(tabBar(), &QWidget::customContextMenuRequested, this, [this](QPoint p) {
        int i = tabBar()->tabAt(p);
        if (i < 0)
            return;
        QPointer<QWidget> page = widget(i);
        QMenu menu;
        menu.addAction("关闭", this, [this, page] {
            if (page)
                emit closeEditor(page);
        });
        auto closeMatching = [this, i](int mode) {
            QList<QPointer<QWidget>> pages;
            for (int n = 0; n < count(); ++n) {
                auto ed = qobject_cast<Editor *>(widget(n));
                if ((mode == 0 && n != i) || (mode == 1 && n > i) || (mode == 2 && ed && !ed->dirty()) ||
                    mode == 3)
                    pages << widget(n);
            }
            for (const auto &p : pages)
                if (p)
                    emit closeEditor(p);
        };
        menu.addAction("关闭其他", this, [closeMatching] { closeMatching(0); });
        menu.addAction("关闭右侧", this, [closeMatching] { closeMatching(1); });
        menu.addAction("关闭已保存", this, [closeMatching] { closeMatching(2); });
        menu.addAction("全部关闭", this, [closeMatching] { closeMatching(3); });
        menu.addSeparator();
        menu.addAction("移动到另一个编辑组", this, [this, page] {
            if (page)
                emit moveEditor(page);
        });
        menu.addAction("移到独立窗口", this, [this, page] {
            if (page)
                emit floatEditor(page);
        });
        menuOpen = true;
        menu.exec(tabBar()->mapToGlobal(p));
        menuOpen = false;
        emit interactionFinished();
    });
}
void EditorTabs::tabInserted(int index) {
    QTabWidget::tabInserted(index);
    auto close = iconButton(Icon::Close, "关闭 " + tabText(index), tabBar(), IconSize::TabClose);
    QPointer<QWidget> page = widget(index);
    close->setAccessibleName("关闭 " + tabText(index));
    connect(close, &QToolButton::clicked, this, [this, page] {
        if (page)
            emit closeEditor(page);
    });
    replaceTabButton(tabBar(), index, QTabBar::LeftSide, nullptr);
    replaceTabButton(tabBar(), index, QTabBar::RightSide, close);
    close->show();
}
bool EditorTabs::eventFilter(QObject *o, QEvent *e) {
    if (o == tabBar()) {
        if (e->type() == QEvent::MouseButtonPress) {
            auto m = static_cast<QMouseEvent *>(e);
            if (m->button() == Qt::LeftButton) {
                dragStart = m->globalPosition().toPoint();
                int i = tabBar()->tabAt(m->position().toPoint());
                dragPage = i >= 0 ? widget(i) : nullptr;
            }
        } else if (e->type() == QEvent::MouseMove) {
            auto m = static_cast<QMouseEvent *>(e);
            auto position = m->globalPosition().toPoint();
            if (dragPage && (m->buttons() & Qt::LeftButton) &&
                (position - dragStart).manhattanLength() >= QApplication::startDragDistance()) {
                auto page = dragPage;
                dragPage.clear();
                emit dragRequested(page);
                return true;
            }
        } else if (e->type() == QEvent::MouseButtonRelease) {
            dragPage.clear();
        }
    }
    return QTabWidget::eventFilter(o, e);
}
void EditorTabs::paintEvent(QPaintEvent *event) {
    PanelTabs::paintEvent(event);
}
} // namespace rs
