#pragma once
#include "editor.h"
namespace rs {
enum class DropZone { Center, Left, Right, Top, Bottom, TabBar };
class EditorDragMime;
class DropOverlay;
class EditorArea : public QWidget {
    Q_OBJECT
  public:
    explicit EditorArea(QWidget *parent = nullptr);
    ~EditorArea() override;
    QList<EditorTabs *> groups() const;
    EditorTabs *activeGroup() const;
    EditorTabs *groupFor(QWidget *page) const;
    void activatePage(QWidget *page);
    void addPage(QWidget *page, const QString &label, EditorTabs *target = nullptr);
    void openComparison(Editor *source);
    void removePage(QWidget *page);
    bool movePage(QWidget *page, EditorTabs *target, DropZone zone = DropZone::Center, int index = -1);
    bool canSplit() const;
    void splitActive(Qt::Orientation orientation);
    void mergeAll();
    void clearPreview();
    bool previewVisible() const;
  signals:
    void closePageRequested(QWidget *page);
    void floatPageRequested(QWidget *page);
    void movePageRequested(QWidget *page);
    void layoutChanged();
    void pagePresented(QWidget *page);
  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    virtual Qt::DropAction executeDrag(QDrag *drag);
  private:
    friend class EditorAreaTests;
    QWidget *root = nullptr;
    QVBoxLayout *rootLayout;
    QPointer<EditorTabs> active;
    QPointer<EditorDragMime> liveMime;
    DropOverlay *overlay;
    bool cleanupQueued = false, dragging = false, mutating = false;
    EditorTabs *makeGroup();
    void setActiveGroup(EditorTabs *group);
    void scheduleCleanup();
    void cleanupEmptyGroups();
    void collapseSplitters();
    EditorTabs *tabsAt(QWidget *widget) const;
    static bool outsideApplication(QPoint global);
    static void dragFeedback(QDrag *,QPoint global);
    void startDrag(EditorTabs *source, QWidget *page);
    QMimeData *beginDrag(EditorTabs *source, QWidget *page);
    bool validDrag(const QMimeData *mime) const;
    bool handleDropEvent(QWidget *receiver, QEvent *event);
    DropZone dropZone(EditorTabs *group, const QPoint &position, int *index) const;
    void showPreview(EditorTabs *group, DropZone zone, int index);
};
} // namespace rs
