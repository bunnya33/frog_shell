#include "fontsettings.h"
#include "contentfont.h"
#include "styledcombobox.h"
#include "theme.h"
#include "icons.h"
#include <cmath>
#include <QPointer>
#include <QWheelEvent>
namespace rs {
namespace {
// Keep QSpinBox hit testing, keyboard, accessibility and auto-repeat semantics.
// Only replace the platform/QSS arrows with the application's theme icons.
class FontSizeSpinBox final : public QSpinBox {
  public:
    FontSizeSpinBox() {
        setUiStyleSheet(this,"QSpinBox {padding-right:28px;} QSpinBox::up-button {subcontrol-origin:border;subcontrol-position:top right;width:24px;border:0;} QSpinBox::down-button {subcontrol-origin:border;subcontrol-position:bottom right;width:24px;border:0;} QSpinBox::up-arrow,QSpinBox::down-arrow {image:none;width:0;height:0;}");
    }
  protected:
    void paintEvent(QPaintEvent *event) override {
        QSpinBox::paintEvent(event);
        QStyleOptionSpinBox option;initStyleOption(&option);QPainter p(this);
        const auto enabled=stepEnabled();
        for(const auto control:{QStyle::SC_SpinBoxUp,QStyle::SC_SpinBoxDown}) {
            const auto rect=style()->subControlRect(QStyle::CC_SpinBox,&option,control,this);
            const bool up=control==QStyle::SC_SpinBoxUp;
            const auto active=isEnabled() && (enabled & (up?StepUpEnabled:StepDownEnabled));
            icon(up?Icon::Up:Icon::Down).paint(&p,QRect(rect.center()-QPoint(uiPx(7),uiPx(7)),uiSize(14,14)),Qt::AlignCenter,active?QIcon::Normal:QIcon::Disabled);
        }
    }
};
}
FontSettings::FontSettings(QWidget *parent) : QWidget(parent) {
    setProperty("rabbitUi",true);
    setObjectName("fontSettings");
    auto outer = new QVBoxLayout(this); uiMargins(outer,0,0,0,0);
    fontPage=new QWidget(this); zoomPage=new QWidget(this);outer->addWidget(fontPage);outer->addWidget(zoomPage);
    auto layout = new QVBoxLayout(fontPage); uiMargins(layout,0,0,0,0);
    auto zoomLayout = new QVBoxLayout(zoomPage); uiMargins(zoomLayout,0,0,0,0);
    auto title = new QLabel("通用内容字体"); title->setProperty("heading", true); layout->addWidget(title);
    auto fontCard=new QWidget;setFrameRole(fontCard,"card");auto fontCardLayout=new QVBoxLayout(fontCard);uiMargins(fontCardLayout,16,16,16,16);layout->addWidget(fontCard);
    auto form = new QFormLayout;form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);uiSpacing(form,12);fontCardLayout->addLayout(form);
    auto family = new StyledComboBox; family->setObjectName("contentFontFamily");
    family->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); family->setMinimumContentsLength(16);
    for (const auto &name : ContentFonts::availableFamilies())
        family->addItem(name == contentFontFamily() ? name + "（内置）" : name, name);
    form->addRow("等宽字体", family);
    auto weight = new StyledComboBox; weight->setObjectName("contentFontWeight");
    for (const auto &[label, value] : QList<QPair<QString, int>>{{"常规 · 400",400},{"中等 · 500",500},{"半粗 · 600",600},{"粗体 · 700",700}})
        weight->addItem(label, value);
    form->addRow("字重", weight);
    auto editor = new FontSizeSpinBox, terminal = new FontSizeSpinBox, list = new FontSizeSpinBox;
    editor->setObjectName("editorFontPx"); terminal->setObjectName("terminalFontPx"); list->setObjectName("listFontPx");
    for (auto spin : {editor, terminal, list}) { spin->setRange(8,40); spin->setSuffix(" px"); spin->setKeyboardTracking(false); }
    form->addRow("编辑器字号", editor); form->addRow("终端字号", terminal); form->addRow("服务 / 端口字号", list);
    auto zoomTitle=new QLabel("界面缩放");zoomTitle->setProperty("heading",true);zoomLayout->addWidget(zoomTitle);
    auto zoomCard=new QWidget;setFrameRole(zoomCard,"card");auto zoomCardLayout=new QVBoxLayout(zoomCard);uiMargins(zoomCardLayout,16,16,16,16);uiSpacing(zoomCardLayout,12);zoomLayout->addWidget(zoomCard);
    auto zoomRow=new QHBoxLayout;auto smaller=button("−"),larger=button("+"),normal=button("恢复 100%");
    auto scale=new StyledComboBox;scale->setObjectName("uiZoomLevel");
    smaller->setObjectName("uiZoomOut");larger->setObjectName("uiZoomIn");normal->setObjectName("uiZoomReset");
    for(int level=-4;level<=4;++level)scale->addItem(QString::number(qRound(std::pow(1.2,level)*100))+"%",level);
    zoomRow->addWidget(smaller);zoomRow->addWidget(scale,1);zoomRow->addWidget(larger);zoomRow->addWidget(normal);zoomCardLayout->addLayout(zoomRow);
    auto syncZoom=[=]{const QSignalBlocker blocked(scale);const int level=UiZoom::instance().level();scale->setCurrentIndex(scale->findData(level));smaller->setEnabled(level>-4);larger->setEnabled(level<4);};syncZoom();
    connect(&UiZoom::instance(),&UiZoom::changed,this,syncZoom);
    connect(scale,&QComboBox::currentIndexChanged,this,[scale]{UiZoom::instance().setLevel(scale->currentData().toInt());});
    connect(smaller,&QPushButton::clicked,this,[]{auto &z=UiZoom::instance();z.setLevel(z.level()-1);});
    connect(larger,&QPushButton::clicked,this,[]{auto &z=UiZoom::instance();z.setLevel(z.level()+1);});
    connect(normal,&QPushButton::clicked,this,[]{UiZoom::instance().setLevel(0);});
    auto info = new QLabel("基础字号分别记忆；界面缩放同时放大文字、图标和控件，基础字号不变。\n"
                          "Ctrl + + / − 缩放整个应用，Ctrl + 0 恢复 100%。Ctrl + ↑/↓ 保留编辑和终端原有操作。\n"
                          "Ctrl + 滚轮不改变字号。macOS 使用物理 Control 键。");
    info->setWordWrap(true); info->setProperty("muted",true); zoomCardLayout->addWidget(info);zoomLayout->addStretch();
    auto preview = new QWidget; auto samples = new QVBoxLayout(preview); setFrameRole(preview,"card");
    const QList<QPair<QString,ContentFontRole>> roles{{"编辑器",ContentFontRole::Editor},{"终端",ContentFontRole::Terminal},{"服务 / 端口",ContentFontRole::List}};
    for (const auto &[label, role] : roles) {
        samples->addWidget(new QLabel(label));
        auto sample = new QLabel("Aa 0123 你好 é  "); sample->setWordWrap(true); bindContentFont(sample,role); samples->addWidget(sample);
    }
    layout->addWidget(preview);
    auto reset = button("恢复字体默认"); reset->setObjectName("resetContentFonts"); layout->addWidget(reset,0,Qt::AlignLeft);
    auto &fonts = ContentFonts::instance();
    auto sync = [family,weight,editor,terminal,list] {
        const auto value = ContentFonts::instance().preferences();
        const QSignalBlocker a(family),b(weight),c(editor),d(terminal),e(list);
        family->setCurrentIndex(family->findData(value.family)); weight->setCurrentIndex(weight->findData(value.weight));
        editor->setValue(value.editorPx); terminal->setValue(value.terminalPx); list->setValue(value.listPx);
    };
    sync();
    auto update = [family,weight,editor,terminal,list] {
        ContentFonts::instance().setPreferences({family->currentData().toString(),weight->currentData().toInt(),editor->value(),terminal->value(),list->value()});
    };
    connect(family,&QComboBox::currentIndexChanged,this,update); connect(weight,&QComboBox::currentIndexChanged,this,update);
    for (auto spin : {editor,terminal,list}) connect(spin,&QSpinBox::valueChanged,this,update);
    connect(&fonts,&ContentFonts::changed,this,[sync](ContentFontRole){sync();});
    connect(reset,&QPushButton::clicked,this,[]{ContentFonts::instance().resetDefaults();});
}
void FontSettings::showZoomCategory(bool zoom) { fontPage->setVisible(!zoom);zoomPage->setVisible(zoom); }
}
