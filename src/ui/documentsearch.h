#pragma once
#include "../core/documentsearch.h"
#include "editor.h"
#include <QtWidgets>
namespace rs {
class Workspace;
struct SearchSource {
    QString profileId, connectionKey, server, path;
    QPointer<Editor> editor;
    QPointer<QTextDocument> document;
};
class AdvancedSearchDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit AdvancedSearchDialog(QWidget *parent);
    SearchOptions options() const;
    void setSource(const SearchSource &source);
    void setFeedback(const QString &message, bool error = false);
    void focusQuery();
  signals:
    void findAllRequested();
  protected:
    void resizeEvent(QResizeEvent *event) override;
  private:
    QLineEdit *query;
    QCheckBox *sensitive, *word, *regex;
    QLabel *sourceFile, *sourcePath, *sourceServer, *sourceDirty, *feedback;
};
class DocumentSearchResults final : public QWidget {
    Q_OBJECT
  public:
    explicit DocumentSearchResults(QWidget *parent);
    ~DocumentSearchResults() override;
    void setResult(const SearchSource &source, const SearchOptions &options, SearchOutcome result, bool dirty);
    void setFeedback(const QString &message);
    void setDetached(bool detached);
    const SearchOutcome &outcome() const;
    QListView *view() const { return list; }
  signals:
    void activateRequested(int row);
    void detachRequested();
  private:
    class Model;
    Model *model;
    QListView *list;
    QLabel *queryLabel, *countLabel, *fileLabel, *serverLabel, *pathLabel, *dirtyLabel, *feedback;
    QToolButton *detach;
};
// One controller per main window, one result widget across all of its workspaces.
class DocumentSearchController final : public QObject {
    Q_OBJECT
  public:
    explicit DocumentSearchController(QWidget *owner);
    ~DocumentSearchController() override;
    void registerWorkspace(Workspace *workspace);
    bool canOpen(QWidget *focus) const;
    bool openFor(QWidget *focus);
    bool openFor(Workspace *workspace, Editor *editor);
    AdvancedSearchDialog *parameters() const { return dialog; }
    DocumentSearchResults *results() const { return resultPanel; }
    const SearchSource &source() const { return captured; }
    const SearchSource &resultSource() const { return resultOrigin; }
    bool isDetached() const { return detached; }
    bool searching() const { return process != nullptr; }
    void executeSearch();
    void setDetached(bool value);
    void showResults();
    void activateResult(int row);
    void releaseWorkspace(Workspace *workspace);
  signals:
    void searchStarted();
    void searchFinished(bool success);
    void resultActivated(rs::Editor *editor, int line, int column);
    void workspaceActivationRequested(rs::Workspace *workspace);
    void reopenSourceRequested(const QString &profileId, const QString &connectionKey, const QString &path);
  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
  private:
    QWidget *owner;
    AdvancedSearchDialog *dialog;
    DocumentSearchResults *resultPanel;
    QDialog *resultWindow;
    QList<QPointer<Workspace>> workspaces;
    QPointer<Workspace> host;
    QPointer<Editor> lastEditor;
    SearchSource captured, resultOrigin;
    QByteArray resultDigest;
    QVector<QTextCursor> anchors;
    QPointer<QProcess> process;
    quint64 generation = 0;
    bool detached = false;
    int pendingActivation = -1;
    quint64 pendingGeneration = 0;
    Workspace *workspaceFor(Editor *editor) const;
    Workspace *sourceWorkspace(const SearchSource &source) const;
    Editor *editorFromFocus(QWidget *focus) const;
    void cancelSearch();
    void attachResults(Workspace *workspace);
    void parkResults();
    void navigate(Editor *editor, int row, bool reopened);
    void resumeActivation();
};
}
