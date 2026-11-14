#pragma once
#include "../core/profile.h"
#include <QtWidgets>
#include <QTextBlock>
#include "contentfont.h"
#include "../core/syntaxdiagnostic.h"
#include "../core/documentsearch.h"
#include "theme.h"
#include "editorsurface.h"
class EditorHighlightTests;
namespace rs {
class EditorLexer;
class CodeEdit : public EditorSurface {
    Q_OBJECT
  public:
    explicit CodeEdit(QWidget *parent = nullptr);
    void showFind(bool replace = false);
    void refreshTheme();
    void setSyntaxPath(const QString &path);
    void setAdvancedSearch(const SearchOutcome &result);
    void clearAdvancedSearch();
    void setAdvancedSearchCurrent(int row, const EditorCursor &cursor);
    void selectAdvancedSearchCurrent(int row, const EditorCursor &cursor);
  signals:
    void syntaxStatusChanged(const QString &status);

  protected:
    bool event(QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void wheelEvent(QWheelEvent *) override;

  private:
    friend class AppearanceTests;
    friend class SyntaxDiagnosticTests;
    friend class ::EditorHighlightTests;
    SyntaxFormat syntaxFormat = SyntaxFormat::None;
    SyntaxDiagnostic syntaxResult;
    quint64 syntaxRevision = 0;
    bool syntaxRunning = false;
    QTimer *syntaxTimer = nullptr;
    void scheduleSyntax();
    void startSyntax();
    void setSyntaxResult(const SyntaxDiagnostic &result);
    QRect syntaxAnchorRect() const;
    QFrame *findPanel = nullptr;
    QLineEdit *query = nullptr, *replacement = nullptr;
    QWidget *replaceRow = nullptr;
    QLabel *findStatus = nullptr;
    QToolButton *findCase = nullptr, *findWord = nullptr, *findRegex = nullptr, *preserveCase = nullptr;
    QToolButton *findToggle = nullptr;
    struct Match {
        // UTF-16 offsets/captures belong to the last completed search snapshot.
        // Live decorations use tracked byte anchors while a refresh is pending.
        int start=0, end=0;
        QStringList captures;
        EditorCursor cursor;
        int byteStart=0, byteEnd=0;
    };
    QVector<Match> matches;
    quint64 findRevision = 0;
    bool findRunning = false;
    enum class FindNavigation { None, Forward, Backward };
    FindNavigation pendingFindNavigation = FindNavigation::None;
    QTimer *findTimer = nullptr;
    int currentMatch = -1;
    bool searchSelection = false;
    bool locatingMatch = false;
    enum class SelectionOwner { None, Find, Advanced };
    SelectionOwner selectionOwner = SelectionOwner::None;
    EditorCursor ownedSelection;
    void clearOwnedSelection(SelectionOwner owner);
    bool searchSelectionStyle = false;
    EditorBlock outlinedBlock;
    quint64 highlightRebuildCount = 0;
    struct AdvancedRange { EditorCursor cursor; QString text; int start = 0, end = 0, row = -1; bool current = false; };
    QVector<AdvancedRange> advancedRanges;
    EditorCursor advancedCurrent;
    int advancedCurrentRow = -1;
    void adjustAdvancedSearch(int position, int removed, int added);
    EditorLexer *highlighter = nullptr;
    int gutterWidth() const;
    void updateGutter();
    void locate(bool backwards = false);
    void ensureFindPanel();
    void scheduleFind(bool navigate = true);
    void adjustFindMatches(int position, int removed, int added);
    void updateMatches();
    void paintMatches();
    void updateHighlights();
    void appendSearchLineHighlights(QList<EditorExtraSelection> &selections, int position);
    void updateSearchSelectionStyle();
    void updateCursorOutline();
    void closeFind();
    QString replacementFor(const Match &match) const;
    void replaceMatches(bool all);
};
class Editor : public QWidget {
    Q_OBJECT
  public:
    Editor(const QString &serverId, const QString &path, const QByteArray &bytes, const QByteArray &hash,
           QWidget *parent = nullptr);
    QString path;
    QByteArray remoteHash;
    CodeEdit *code;
    bool saving = false;
    QByteArray bytes() const;
    bool dirty() const;
    void saved(const QByteArray &sent, const QByteArray &hash);
    void reload(const QByteArray &raw, const QByteArray &hash);
    void discardDraft();
    void setPath(const QString &path);
    void setReturnAction(QAction *action);
    void refreshFileIcon();
  signals:
    void saveRequested(Editor *editor, bool overwrite);
    void changed();

  protected:
    bool event(QEvent *event) override;

  private:
    QString serverId, draftPath;
    bool crlf = false, bom = false;
    QLabel *pathLabel, *formatLabel, *syntaxLabel = nullptr, *pathIcon = nullptr;
    QHBoxLayout *tools;
    QToolButton *returnButton = nullptr;
    QTimer draftTimer;
    void persistDraft();
};
class EditorTabs : public PanelTabs {
    Q_OBJECT
  public:
    explicit EditorTabs(QWidget *parent = nullptr);
    bool interactionActive() const { return menuOpen || actionMenuOpen; }
  signals:
    void closeEditor(QWidget *page);
    void moveEditor(QWidget *page);
    void floatEditor(QWidget *page);
    void dragRequested(QWidget *page);
    void interactionFinished();

  protected:
    bool eventFilter(QObject *, QEvent *) override;
    void tabInserted(int index) override;
    void paintEvent(QPaintEvent *event) override;

  private:
    QPoint dragStart;
    QPointer<QWidget> dragPage;
    bool menuOpen = false;
    bool actionMenuOpen = false;
    friend class EditorArea;
};
} // namespace rs
