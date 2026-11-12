#pragma once
#include <QtWidgets>
#include <array>
#include "uizoom.h"
namespace rs {
struct Colors {
    QColor background, side, rail, tabs, line, text, muted, accent, link, selection, green, orange, red,
        string, keyword, comment;
    QColor panelBorder, cardBorder, divider, activeGroup, dropFill, dropBorder;
    QColor processHeader, processBody, processAlternate, processSelection;
    QColor syntaxNumber, syntaxFunction, syntaxProperty;
};
// Semantic boundaries are opt-in; layout widgets and labels never inherit a frame.
void setFrameRole(QWidget *widget, const char *role);
// A structural band owns its boundary inside its height, rather than adding a row.
class PanelBand : public QWidget {
  public:
    explicit PanelBand(int height=36, bool topEdge=false, QWidget *parent=nullptr);
  protected:
    void paintEvent(QPaintEvent *) override;
  private:
    bool topEdge;
};
// Native tabs/corners occupy only the content band. PanelTabs alone paints the
// reserved bottom edge, and the pane reserves that same strip before its page.
class PanelTabBar : public QTabBar {
  public:
    explicit PanelTabBar(QWidget *parent=nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
  protected:
    QSize tabSizeHint(int index) const override;
};
class PanelTabActions : public QWidget {
  public:
    explicit PanelTabActions(QWidget *parent=nullptr);
    QSize sizeHint() const override;
};
class PanelTabs : public QTabWidget {
  public:
    explicit PanelTabs(QWidget *parent=nullptr);
    int headerHeight() const;
  protected:
    void paintEvent(QPaintEvent *) override;
};
class PanelSplitter : public QSplitter {
  public:
    explicit PanelSplitter(QWidget *parent = nullptr);
    explicit PanelSplitter(Qt::Orientation orientation, QWidget *parent = nullptr);
  protected:
    QSplitterHandle *createHandle() override;
};
Colors colors();
std::array<QColor, 16> terminalAnsiPalette();
void applyTheme(bool light);
bool lightTheme();
QColor cpuColor(bool selected=false);
QColor memoryColor();
QColor networkReceiveColor();
QColor networkSendColor();
QColor diskColor();
void paintToggle(QPainter &painter, QRectF rect, qreal progress, bool enabled, bool focus = false);
class Toggle : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)
  public:
    explicit Toggle(QWidget *parent = nullptr);
    QSize sizeHint() const override { return uiSize(44, 28); }
    qreal progress() const { return position; }
    void setProgress(qreal value) { position = value; update(); }
    void syncChecked(bool checked);
  protected:
    void paintEvent(QPaintEvent *) override;
    void hideEvent(QHideEvent *) override;
  private:
    qreal position = 0;
    QPropertyAnimation animation;
};
QPushButton *button(const QString &label, QWidget *parent = nullptr);
} // namespace rs
