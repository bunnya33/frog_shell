#include "uploadconflictdialog.h"
#include "theme.h"
#include "promptstyle.h"
#include <QtMath>

namespace rs {
namespace {
class ConflictIcon final : public QWidget {
  public:
    explicit ConflictIcon(QWidget *parent) : QWidget(parent) {
        setObjectName("conflictIcon");
        uiFixedSize(this,40,40);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.scale(width()/40.0,height()/40.0);
        p.setPen(QPen(colors().orange,2.4,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        QPainterPath triangle; triangle.moveTo(20,4); triangle.lineTo(37,34);
        triangle.quadTo(38,37,35,37); triangle.lineTo(5,37);
        triangle.quadTo(2,37,3,34); triangle.closeSubpath(); p.drawPath(triangle);
        p.drawLine(QPointF(20,14),QPointF(20,24)); p.drawPoint(QPointF(20,30));
    }
};
class PathLabel final : public QTextEdit {
  public:
    explicit PathLabel(const QString &text,QWidget *parent):QTextEdit(parent) {
        setObjectName("conflictPath");setReadOnly(true);setFrameShape(QFrame::NoFrame);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setLineWrapMode(QTextEdit::WidgetWidth);setWordWrapMode(QTextOption::WrapAnywhere);
        document()->setDocumentMargin(0);setPlainText(text);
        document()->setUndoRedoEnabled(false);stylePromptText(this);
        setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        QSizePolicy policy(QSizePolicy::Ignored,QSizePolicy::Preferred);policy.setHeightForWidth(true);setSizePolicy(policy);
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        QTextDocument measured;measured.setDocumentMargin(0);measured.setDefaultFont(font());
        QTextOption option;option.setWrapMode(QTextOption::WrapAnywhere);measured.setDefaultTextOption(option);
        measured.setPlainText(toPlainText());measured.setTextWidth(qMax(1,width));
        promptDocumentSpacing(&measured);
        return qCeil(measured.size().height());
    }
    QSize minimumSizeHint() const override { return {0,fontMetrics().height()}; }
    QSize sizeHint() const override { return minimumSizeHint(); }
};
class ConflictCheckBox final : public QCheckBox {
  public:
    explicit ConflictCheckBox(QWidget *parent):QCheckBox("对本次其余冲突执行相同操作",parent) {}
  protected:
    void paintEvent(QPaintEvent *event) override {
        QCheckBox::paintEvent(event);
        if(isChecked())return;
        QStyleOptionButton option;initStyleOption(&option);
        const auto indicator=style()->subElementRect(QStyle::SE_CheckBoxIndicator,&option,this);
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(colors().muted,uiReal(1)));p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(indicator).adjusted(0.5,0.5,-0.5,-0.5),uiReal(2),uiReal(2));
    }
};
QLabel *plain(const QString &text, QWidget *parent) {
    auto label=new QLabel(text,parent);label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    stylePromptText(label);
    return label;
}
}
UploadConflictDialog::UploadConflictDialog(const QString &title,const QString &path,bool moving,
                                           bool canOverwrite,QWidget *parent) : QDialog(parent) {
    setWindowTitle(title);setObjectName("uploadCollision");setAttribute(Qt::WA_DeleteOnClose);
    bindUiFont(this,14);
    auto outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);
    auto scroll=new QScrollArea(this);scroll->setObjectName("conflictScroll");scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto body=new QWidget;body->setObjectName("conflictBody");
    auto row=new QHBoxLayout(body);uiMargins(row,20,19,20,20);uiSpacing(row,14);
    auto warning=new ConflictIcon(body);row->addWidget(warning,0,Qt::AlignVCenter);
    auto details=new QWidget(body);details->setObjectName("conflictDetails");
    auto column=new QVBoxLayout(details);column->setContentsMargins(0,0,0,0);uiSpacing(column,8);
    column->addWidget(plain("目标位置已存在同名文件：",details));
    auto target=new PathLabel(path,details);
    column->addWidget(target);
    auto description=plain(moving ? "移动不会替换已有项目。保留两者会自动序号重命名。" :
        canOverwrite ? "替换现有文件，或保留两者并自动序号重命名。" :
        "目标类型不一致或是链接，不能替换。保留两者会自动序号重命名。",details);
    description->setProperty("muted",true);column->addWidget(description);
    all=new ConflictCheckBox(details);all->setVisible(!moving);column->addWidget(all);
    row->addWidget(details,1);scroll->setWidget(body);outer->addWidget(scroll,1);
    auto footer=new QWidget(this);footer->setObjectName("conflictFooter");
    auto actions=new QHBoxLayout(footer);uiMargins(actions,20,12,20,12);uiSpacing(actions,8);actions->addStretch();
    overwrite=new QPushButton("替换文件",footer);keep=new QPushButton("保留两者",footer);cancel=new QPushButton("取消",footer);
    for(auto b:{overwrite,keep,cancel}) {
        stylePromptButton(b);b->setAutoDefault(false);
        actions->addWidget(b);connect(b,&QPushButton::clicked,this,[this,b]{clicked=b;done(b==cancel?QDialog::Rejected:QDialog::Accepted);});
    }
    overwrite->setEnabled(canOverwrite);cancel->setDefault(true);setFocusPolicy(Qt::StrongFocus);setFocus(Qt::OtherFocusReason);
    outer->addWidget(footer);
    UiZoom::instance().bind(this,"conflictGeometry",[this,scroll,footer]{
        const auto screen=parentWidget() && parentWidget()->screen()?parentWidget()->screen():QGuiApplication::primaryScreen();
        const QSize available=screen?screen->availableGeometry().size():QSize(uiPx(800),uiPx(600));
        const int width=qMax(1,qMin(uiPx(448),available.width()-uiPx(24)));
        setMinimumWidth(width);setMaximumWidth(width);
        // Limit the scrollable body, leaving the footer and platform caption accessible.
        scroll->setMaximumHeight(qMax(uiPx(100),available.height()-footer->sizeHint().height()-uiPx(80)));
        adjustSize();
    });
}
}
