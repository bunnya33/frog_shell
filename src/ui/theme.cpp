#include "theme.h"
#include "wheeleditguard.h"
#include "windowstyle.h"
#include "roundedsurface.h"
#include "layoutmetrics.h"
#include <QStyleHints>
namespace rs {
namespace {
// QSS :focus does not distinguish pointer and keyboard focus. Keep that
// distinction on the two standard button types without changing actual focus.
class ButtonFocusVisible final : public QObject {
  public:
    explicit ButtonFocusVisible(QObject *parent) : QObject(parent) { qApp->installEventFilter(this); }
  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        const auto type = event->type();
        if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonDblClick &&
            type != QEvent::KeyPress && type != QEvent::Shortcut && type != QEvent::FocusIn)
            return false;
        auto button = qobject_cast<QAbstractButton *>(object);
        if (!button || (!qobject_cast<QPushButton *>(button) && !qobject_cast<QToolButton *>(button)))
            return false;
        bool keyboard;
        switch (type) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
            keyboard = false;
            break;
        case QEvent::KeyPress:
        case QEvent::Shortcut:
            keyboard = true;
            break;
        case QEvent::FocusIn: {
            const auto reason = static_cast<QFocusEvent *>(event)->reason();
            if (reason == Qt::MouseFocusReason) keyboard = false;
            else if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason || reason == Qt::ShortcutFocusReason)
                keyboard = true;
            else return false; // Popup/window return keeps the opening input mode.
            break;
        }
        default: return false;
        }
        if (button->property("keyboardFocus").toBool() != keyboard) {
            button->setProperty("keyboardFocus", keyboard);
            button->style()->unpolish(button);
            button->style()->polish(button);
            button->update();
        }
        return false;
    }
};
void installButtonFocusVisible() {
    static QPointer<ButtonFocusVisible> filter;
    if (!filter) filter = new ButtonFocusVisible(qApp);
}
} // namespace
static bool isLight = false;
bool lightTheme() {
    return isLight;
}
QColor cpuColor(bool selected) {return isLight?QColor("#6f42c1"):QColor(selected?"#d5baff":"#bd93f9");}
QColor memoryColor() {return isLight?QColor("#005cc5"):QColor("#8be9fd");}
QColor networkReceiveColor() {return isLight?QColor("#005cc5"):QColor("#8be9fd");}
QColor networkSendColor() {return isLight?QColor("#a04100"):QColor("#ffb86c");}
QColor diskColor() {return isLight?QColor("#a04100"):QColor("#ffb86c");}
Colors colors() {
    return isLight ? Colors{"#ffffff", "#f6f8fa", "#ffffff", "#f6f8fa", "#d0d7de", "#24292e",
                            "#6a737d", "#0366d6", "#0366d6", "#e2e5e9", "#22863a", "#a04100",
                            "#cb2431", "#032f62", "#d73a49", "#6a737d",
                            "#d0d7de", "#d0d7de", "#d0d7de", "#0366d6", QColor(3,102,214,42), "#0366d6",
                            "#eef1f4", "#ffffff", "#f6f8fa", "#d6e8fa",
                            "#005cc5", "#6f42c1", "#005cc5"}
                   : Colors{"#282a36", "#21222c", "#343746", "#191a21", "#44475a", "#f8f8f2",
                            "#a5acc6", "#bd93f9", "#8be9fd", "#44475a", "#50fa7b", "#ffb86c",
                            "#ff5555", "#f1fa8c", "#ff79c6", "#6272a4",
                            "#44475a", "#44475a", "#44475a", "#bd93f9", QColor(189,147,249,48), "#bd93f9",
                            "#343746", "#282a36", "#2e303e", "#44475a",
                            "#bd93f9", "#50fa7b", "#8be9fd"};
}
std::array<QColor, 16> terminalAnsiPalette() {
    // Pinned official terminal colors, independent of UI status/accent colors.
    return isLight ? std::array<QColor, 16>{
        "#24292e", "#d73a49", "#28a745", "#dbab09", "#0366d6", "#5a32a3", "#1b7c83", "#6a737d",
        "#959da5", "#cb2431", "#22863a", "#b08800", "#005cc5", "#5a32a3", "#3192aa", "#d1d5da"}
        : std::array<QColor, 16>{
        "#21222c", "#ff5555", "#50fa7b", "#f1fa8c", "#bd93f9", "#ff79c6", "#8be9fd", "#f8f8f2",
        "#6272a4", "#ff6e6e", "#69ff94", "#ffffa5", "#d6acff", "#ff92df", "#a4ffff", "#ffffff"};
}
void applyTheme(bool light) {
    installWheelEditGuard();
    installButtonFocusVisible();
    installRoundedMenus();
    // Qt stylesheet repolish can restore its first cached widget font even when
    // the new sheet has no content font rule. Preserve explicit content fonts,
    // including hidden/floating widgets and runtime font/size changes.
    QList<QPair<QPointer<QWidget>, QFont>> contentFonts;
    for (auto widget : QApplication::allWidgets())
        if (widget->property("contentFont").toBool()) contentFonts.append({widget, widget->font()});
    isLight = light;
    QGuiApplication::styleHints()->setColorScheme(light ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark);
    auto c = colors();
    QPalette p;
    p.setColor(QPalette::Window, c.background);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.background);
    p.setColor(QPalette::AlternateBase, c.side);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.side);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::Highlight, c.selection);
    p.setColor(QPalette::HighlightedText, c.text);
    p.setColor(QPalette::ToolTipBase, c.side);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.muted);
    p.setColor(QPalette::Link, c.link);
    p.setColor(QPalette::Disabled, QPalette::Text, c.muted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.muted);
    qApp->setPalette(p);
    QString sheet = QString(R"(
QMainWindow, QDialog { background:%1; color:%2; }
QMenuBar#applicationMenuBar { background:%3; color:%2; border:0; padding:0; spacing:2px; }
QMenuBar#applicationMenuBar::item { padding:3px 9px; margin:1px 0 2px 0; border:0; border-radius:4px; background:transparent; }
QMenuBar#applicationMenuBar::item:selected { background:%6; }
QMenuBar#applicationMenuBar::item:pressed { background:%6; }
QLabel[heading="true"] { font-size:18px; font-weight:600; }
QFrame#side, QWidget#side { background:%3; }
QFrame#rail { background:%4; }
QLabel[muted="true"] { color:%5; }
QPushButton,QToolButton { border:1px solid transparent; border-radius:5px; padding:6px 9px; background:transparent; color:%2; outline:0; }
QPushButton[breadcrumb="true"] { padding:3px 4px; border-radius:4px; text-transform:none; }
QToolButton[iconButton="true"] { padding:0; border-radius:4px; }
QToolButton[activity="true"] { border:0; border-left:3px solid transparent; border-radius:0; }
QWidget#breadcrumbs { border:1px solid transparent; border-radius:5px; background:%1; }
QWidget#breadcrumbs[uploadTarget="true"] { border:2px solid transparent; background:%1; }
QLineEdit[uploadTarget="true"] { border:2px solid %7; background:%1; }
QLineEdit,QSpinBox,QComboBox { padding:6px 8px; border:1px solid %8; border-radius:5px; background:%1; color:%2; selection-background-color:%6; }
QLineEdit:focus,QSpinBox:focus,QComboBox:focus { border-color:%7; }
QWidget#findInputShell { background:%1; border:1px solid %8; border-radius:5px; }
QWidget#findInputShell QLineEdit { background:transparent; border:0; padding:4px 6px; }
QWidget#findInputShell QToolButton[findOption="true"] { padding:0; border-radius:4px; }
QLineEdit:disabled,QSpinBox:disabled,QComboBox:disabled { color:%5; background:%3; }
QComboBox[styledCombo="true"] { padding:6px 30px 6px 10px; border:1px solid %8; border-radius:6px; background:%1; color:%2; }
QComboBox[styledCombo="true"]:hover { border-color:%5; background:%3; }
QComboBox[styledCombo="true"]:focus { border-color:%7; }
QComboBox[styledCombo="true"]:disabled { border-color:%8; color:%5; background:%3; }
QComboBox[styledCombo="true"]::drop-down { width:28px; border:0; }
QComboBox[styledCombo="true"]::down-arrow { image:none; width:0; height:0; }
QFrame#styledComboPopup { background:transparent; border:1px solid transparent; border-radius:6px; }
QListView#styledComboList { background:%3; color:%2; border:0; outline:0; }
QListView#styledComboList::item { padding:0; border:0; }
QToolButton#splitEditorGroup::menu-button { width:12px; border:0; }
QRadioButton,QCheckBox { spacing:8px; padding:4px; }
QTabWidget::pane { border:0; }
QTabWidget[panelTabs="true"]::pane { border-top:1px solid transparent; }
QTabBar::tab { height:33px; padding:0 12px; background:%3; color:%5; border-top:2px solid transparent; border-bottom:1px solid %8; }
QTabBar[panelTabBar="true"]::tab { border-bottom:0; }
QTabBar::tab:selected { background:%1; color:%2; border-top-color:%7; }
QTabBar::tab:hover { background:%6; }
QTabBar::tab:focus { border-top-color:%7; color:%2; }
QTabBar#bottomPanelTabs::tab { border-top:0; border-bottom:2px solid transparent; height:34px; }
QTabBar#bottomPanelTabs::tab:selected { border-bottom-color:%7; }
QTabBar#serverTabs::tab { border-top:0; border-bottom:2px solid transparent; height:34px; }
QTabBar#serverTabs::tab:selected { border-bottom-color:%7; }
QWidget#editorGroupActions { background:transparent; border:0; }
QHeaderView::section { background:%3; color:%2; padding:8px; border:0; border-bottom:1px solid %8; }
QTableView[processTable="true"] QHeaderView[listeningHeader="true"]::section { padding:0 8px; }
QLabel[promptTextInstalled="true"],QTextEdit[promptTextInstalled="true"],QPushButton[promptButton="true"] { font-size:14px; }
QTreeView,QTableView,QListView { background:%1; alternate-background-color:%3; color:%2; border:0; outline:0; selection-background-color:%6; selection-color:%2; }
QTreeView::item,QListView::item { padding:5px 2px; }
QTreeView::item:selected,QTableView::item:selected,QListView::item:selected { background:%6; color:%2; }
QWidget#settingsSidebar { background:%3; }
QListWidget#settingsCategories { background:transparent; border:0; outline:0; }
QListWidget#settingsCategories::item { height:32px; padding:0 8px; border:0; border-radius:6px; }
QListWidget#settingsCategories::item:selected { background:%6; color:%2; }
QListWidget#settingsCategories::item:hover:!selected { background:%1; }
QTreeWidget#terminalManager::item:selected,
QTreeWidget#terminalManager::item:selected:!active { background:%6; color:%2; }
QMenu { background:transparent; color:%2; border:1px solid transparent; border-radius:6px; padding:4px; }
QMenu::item { padding:6px 26px 6px 12px; }
QMenu::item:selected { background:%6; }
QMenu::item:disabled { color:%5; }
QMenu::separator { height:1px; background:%8; margin:4px 8px; }
QMenu[resourceFileMenu="true"]::item { height:29px; padding:0px 26px 0px 10px; }
QMenu[resourceFileMenu="true"]::separator { margin:3px 6px; }
QProgressBar { border:0; border-radius:2px; background:%6; height:4px; }
QProgressBar::chunk { background:%7; }
QScrollBar:vertical { background:%3; width:10px; }
QScrollBar::handle:vertical { background:%8; min-height:22px; }
QScrollBar:horizontal { background:%3; height:10px; }
QScrollBar::handle:horizontal { background:%8; min-width:22px; }
QScrollBar::add-line,QScrollBar::sub-line { width:0;height:0; }
QStatusBar { background:%3; border-top:1px solid %8; }
QWidget#conflictFooter { background:%3; border-top:1px solid %8; }
QWidget#advancedSearchFooter { background:%3; border-top:1px solid %8; }
QTextEdit#conflictPath { border:0; padding:0; background:%1; color:%2; selection-background-color:%6; }
QPlainTextEdit { border:0; background:%1; color:%2; selection-background-color:%6; }
)")
                            .arg(c.background.name(), c.text.name(), c.side.name(), c.rail.name(),
                                 c.muted.name(), c.selection.name(), c.accent.name(), c.line.name());
    // Keep interaction fills independent of selection: a press must remain
    // distinguishable while hovered, including on property-styled buttons.
    const QColor buttonHover = light ? QColor("#e2e5e9") : QColor("#4b4f60");
    const QColor buttonPressed = light ? QColor("#cbd1d8") : QColor("#606579");
    const QColor primaryHover = light ? QColor("#005cc5") : QColor("#cba8fa");
    const QColor primaryPressed = light ? QColor("#044a9b") : QColor("#a77ae7");
    sheet += QString(R"(
QPushButton:enabled:hover,QToolButton:enabled:hover { background:%1; }
QMenuBar#applicationMenuBar::item:selected { background:%1; }
QMenuBar#applicationMenuBar::item:pressed { background:%2; }
QPushButton:enabled:checked,QToolButton:enabled:checked { background:%3; }
QPushButton:enabled:checked:hover,QToolButton:enabled:checked:hover { background:%1; }
QPushButton:enabled:pressed,QToolButton:enabled:pressed,
QPushButton:enabled:checked:pressed,QToolButton:enabled:checked:pressed { background:%2; }
QPushButton[keyboardFocus="true"]:focus,QToolButton[keyboardFocus="true"]:focus { border-color:%4; }
QPushButton[primary="true"] { background:%4; color:%5; }
QPushButton[primary="true"]:enabled:checked { background:%4; }
QPushButton[primary="true"]:enabled:hover,
QPushButton[primary="true"]:enabled:checked:hover { background:%6; }
QPushButton[primary="true"]:enabled:pressed,
QPushButton[primary="true"]:enabled:checked:pressed { background:%7; }
QPushButton:disabled,QToolButton:disabled,
QPushButton[primary="true"]:disabled { color:%8; background:%9; border-color:transparent; }
QToolButton[activity="true"]:enabled:checked { border-left:3px solid %4; background:%3; }
QToolButton[activity="true"]:enabled:hover,
QToolButton[activity="true"]:enabled:checked:hover { background:%1; }
QToolButton[activity="true"]:enabled:pressed,
QToolButton[activity="true"]:enabled:checked:pressed { background:%2; }
QToolButton[activity="true"][keyboardFocus="true"]:focus { border-top:1px solid %4; border-bottom:1px solid %4; }
)").arg(buttonHover.name(), buttonPressed.name(), c.selection.name(), c.accent.name(),
        c.background.name(), primaryHover.name(), primaryPressed.name(), c.muted.name(), c.side.name());
    // The primary fill is already accent-colored; its keyboard outline needs
    // a contrasting color after disabling the native inner focus rectangle.
    sheet += QString("QPushButton[primary=\"true\"][keyboardFocus=\"true\"]:enabled:focus { border-color:%1; }").arg(c.text.name());
    sheet += QString(R"(
QPushButton[promptButton="true"],QPushButton[promptButton="true"]:enabled,
QPushButton[promptButton="true"]:enabled:hover,QPushButton[promptButton="true"]:enabled:pressed,
QPushButton[promptButton="true"]:disabled { border:1px solid %1; border-radius:5px; padding:2px 9px; background:%3; color:%6; }
QPushButton[promptButton="true"]:enabled:hover { background:%4; }
QPushButton[promptButton="true"]:enabled:pressed { background:%5; }
QPushButton[promptButton="true"]:disabled { color:%7; }
QPushButton[promptButton="true"]:enabled:default,
QPushButton[promptButton="true"][keyboardFocus="true"]:enabled:focus { border-color:%2; }
)").arg(light?"#afb8c1":"#687086",c.accent.name(),c.side.name(),buttonHover.name(),buttonPressed.name(),c.text.name(),c.muted.name());
    sheet += QString(R"(
QWidget[frameRole="rail"] { background:%1; }
QWidget[frameRole="divider"] { background:%3; }
QWidget[frameRole="footer"] { background:%2; }
QWidget[frameRole="card"] { background:%2; border:1px solid transparent; border-radius:8px; }
QWidget[frameRole="transfer"] { background:%2; border:1px solid transparent; border-radius:5px; }
QWidget[frameRole="section"] { border-bottom:1px solid %3; }
QWidget[frameRole="find"] { background:%2; border:1px solid transparent; border-radius:4px; }
)").arg(c.rail.name(),c.side.name(),c.divider.name());
    sheet+=QString("QWidget[statusCard=\"true\"] {background:%1;} QTreeWidget[connectionList=\"true\"] QHeaderView::section {padding:6px 10px;} ").arg(c.background.name());
    sheet+=QString("QTreeView[sideTree=\"true\"] {background:%1;} QTreeView[sideTree=\"true\"] QHeaderView::section {background:%1;}").arg(c.side.name());
    sheet += QString(R"(
QTableView[processTable="true"] { background:%1; alternate-background-color:%2; color:%3; border:0; }
QTableView[processTable="true"]::item { border:0; padding:0 10px; }
QTableView[processTable="true"]::item:selected,
QTableView[processTable="true"]::item:selected:!active { background:%4; }
QTableView[processTable="true"] QHeaderView { background:%5; border:0; }
)").arg(c.processBody.name(), c.processAlternate.name(), c.text.name(), c.processSelection.name(),
        c.processHeader.name());
    UiZoom::instance().applicationSheet(sheet);
    // QIconEngine paints from current semantic colors, so repaint all cached
    // button states after a theme change without replacing any button/action.
    for (auto widget : QApplication::allWidgets()) widget->update();
    QSettings().setValue("appearance/light", light);
    refreshWindowStyles();
    for (const auto &[widget, font] : contentFonts)
        if (widget) widget->setFont(font);
}
void setFrameRole(QWidget *widget, const char *role) {
    widget->setProperty("frameRole",role);
    widget->setAttribute(Qt::WA_StyledBackground,true);
    const auto name=QByteArray(role);
    if(name=="card")installRoundedSurface(widget,RoundedRole::Card);
    else if(name=="transfer")installRoundedSurface(widget,RoundedRole::Transfer);
    else if(name=="find")installRoundedSurface(widget,RoundedRole::Find);
}
PanelBand::PanelBand(int height,bool top,QWidget *parent):QWidget(parent),topEdge(top) {
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);bandHeight(this,height);
}
void PanelBand::paintEvent(QPaintEvent *) {
    QPainter p(this);p.fillRect(rect(),colors().side);
    const int width=uiPx(1);p.fillRect(0,topEdge?0:height()-width,this->width(),width,colors().divider);
}
namespace {
int tabBandHeight(const QWidget *widget) {
    return qMax(uiPx(panelBand),widget->fontMetrics().height()+uiPx(6));
}
int tabContentHeight(const QWidget *widget) {return tabBandHeight(widget)-uiPx(1);}
}
PanelTabBar::PanelTabBar(QWidget *parent):QTabBar(parent) {
    setProperty("panelTabBar",true);
    UiZoom::instance().bind(this,"bandHeight",[this]{setFixedHeight(tabContentHeight(this));});
}
QSize PanelTabBar::sizeHint() const {auto s=QTabBar::sizeHint();s.setHeight(tabContentHeight(this));return s;}
QSize PanelTabBar::minimumSizeHint() const {auto s=QTabBar::minimumSizeHint();s.setHeight(tabContentHeight(this));return s;}
QSize PanelTabBar::tabSizeHint(int index) const {auto s=QTabBar::tabSizeHint(index);s.setHeight(tabContentHeight(this));return s;}
PanelTabActions::PanelTabActions(QWidget *parent):QWidget(parent) {
    UiZoom::instance().bind(this,"bandHeight",[this]{setFixedHeight(tabContentHeight(this));});
}
QSize PanelTabActions::sizeHint() const {auto s=QWidget::sizeHint();s.setHeight(tabContentHeight(this));return s;}
PanelTabs::PanelTabs(QWidget *parent):QTabWidget(parent) {
    setProperty("panelTabs",true);setTabBar(new PanelTabBar(this));
    setDocumentMode(true);tabBar()->setDrawBase(false);
    UiZoom::instance().bind(tabBar(),"icons",[this]{tabBar()->setIconSize(uiSize(18,18));});
}
int PanelTabs::headerHeight() const {return tabBandHeight(tabBar());}
void PanelTabs::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);QPainter p(this);
    const int h=headerHeight();p.fillRect(0,0,width(),h,colors().side);
    p.fillRect(0,h-uiPx(1),width(),uiPx(1),colors().divider);
}
namespace {
class PanelHandle final : public QSplitterHandle {
  public:
    PanelHandle(Qt::Orientation orientation, QSplitter *split) : QSplitterHandle(orientation,split) {
        setAttribute(Qt::WA_Hover);
        setMouseTracking(true);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        const auto c = colors();
        // Qt enlarges the hit area of a <=1px handle into adjacent panes. Its
        // contentsRect remains the allocated visual seam; don't fill the overlap.
        p.fillRect(contentsRect(),underMouse() || isDown ? c.activeGroup : c.divider);
    }
    void enterEvent(QEnterEvent *event) override { QSplitterHandle::enterEvent(event); update(); }
    void leaveEvent(QEvent *event) override { QSplitterHandle::leaveEvent(event); update(); }
    void mousePressEvent(QMouseEvent *event) override { isDown=true; update(); QSplitterHandle::mousePressEvent(event); }
    void mouseReleaseEvent(QMouseEvent *event) override { isDown=false; update(); QSplitterHandle::mouseReleaseEvent(event); }
  private:
    bool isDown=false;
};
}
PanelSplitter::PanelSplitter(QWidget *parent) : PanelSplitter(Qt::Horizontal,parent) {}
PanelSplitter::PanelSplitter(Qt::Orientation orientation, QWidget *parent) : QSplitter(orientation,parent) {
    uiHandleWidth(this,1);
    setChildrenCollapsible(false);
}
QSplitterHandle *PanelSplitter::createHandle() { return new PanelHandle(orientation(),this); }
QPushButton *button(const QString &label, QWidget *parent) {
    auto b = new QPushButton(label, parent);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
void paintToggle(QPainter &p, QRectF rect, qreal progress, bool enabled, bool focus) {
    const auto c = colors();
    auto blend = [progress](QColor a, QColor b) {
        return QColor::fromRgbF(a.redF()+(b.redF()-a.redF())*progress,
                               a.greenF()+(b.greenF()-a.greenF())*progress,
                               a.blueF()+(b.blueF()-a.blueF())*progress);
    };
    p.save(); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(focus ? c.accent : c.muted, uiReal(1)));
    p.setBrush(blend(c.side, enabled ? c.green : c.green.darker(160)));
    p.drawRoundedRect(rect, rect.height()/2, rect.height()/2);
    const qreal diameter = rect.height()-uiReal(6);
    p.setPen(QPen(c.muted, uiReal(.7)));
    p.setBrush(enabled ? (progress > .5 ? c.background : c.text) : c.muted);
    p.drawEllipse(QRectF(rect.left()+uiReal(3)+(rect.width()-diameter-uiReal(6))*progress,
                        rect.top()+uiReal(3), diameter, diameter));
    p.restore();
}
Toggle::Toggle(QWidget *parent) : QAbstractButton(parent), animation(this, "progress") {
    setCheckable(true); setFocusPolicy(Qt::StrongFocus); setCursor(Qt::PointingHandCursor);
    animation.setDuration(180); animation.setEasingCurve(QEasingCurve::InOutCubic);
    connect(this, &QAbstractButton::toggled, this, [this](bool checked) {
        animation.stop();
        if (!isVisible()) { setProgress(checked ? 1 : 0); return; }
        animation.setStartValue(position); animation.setEndValue(checked ? 1.0 : 0.0); animation.start();
    });
}
void Toggle::syncChecked(bool checked) {
    QSignalBlocker block(this); animation.stop(); setChecked(checked); setProgress(checked ? 1 : 0);
}
void Toggle::paintEvent(QPaintEvent *) {
    QPainter p(this); paintToggle(p, QRectF(uiReal(2), (height()-uiReal(20))/2.0, uiReal(40), uiReal(20)), position, isEnabled(), hasFocus());
}
void Toggle::hideEvent(QHideEvent *event) {
    animation.stop(); setProgress(isChecked() ? 1 : 0); QAbstractButton::hideEvent(event);
}
} // namespace rs
