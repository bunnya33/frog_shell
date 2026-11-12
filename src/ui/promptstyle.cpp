#include "promptstyle.h"
#include "theme.h"
namespace rs {
void promptDocumentSpacing(QTextDocument *document) {
    const int height=qMax(uiPx(22),QFontMetrics(document->defaultFont()).height()+uiPx(2));
    bool needsSpacing=false;
    for(auto block=document->begin();block.isValid();block=block.next())
        if(block.blockFormat().lineHeightType()!=QTextBlockFormat::FixedHeight||block.blockFormat().lineHeight()!=height){needsSpacing=true;break;}
    if(!needsSpacing)return;
    QTextCursor cursor(document);cursor.select(QTextCursor::Document);
    QTextBlockFormat format;format.setLineHeight(height,QTextBlockFormat::FixedHeight);cursor.mergeBlockFormat(format);
}
namespace {
class PromptLabelLayout final:public QObject {
  public:explicit PromptLabelLayout(QLabel *label):QObject(label){label->installEventFilter(this);}
  protected:bool eventFilter(QObject *object,QEvent *event) override {
    if(event->type()==QEvent::Paint){
        auto label=static_cast<QLabel *>(object);
        // Flush QLabel's native layout first: it restores WordWrap on resize.
        // Keep the original label, document, selection and plain text intact.
        label->heightForWidth(label->width());
        if(auto doc=label->findChild<QTextDocument *>()){
            auto option=doc->defaultTextOption();
            if(option.wrapMode()!=QTextOption::WrapAtWordBoundaryOrAnywhere){option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);doc->setDefaultTextOption(option);}
            promptDocumentSpacing(doc);
        }
    }
    return false;
  }
};
}
void stylePromptText(QLabel *label) {
    if(!label->property("promptTextInstalled").toBool()){label->setProperty("promptTextInstalled",true);label->style()->unpolish(label);label->style()->polish(label);new PromptLabelLayout(label);}
    UiZoom::instance().bind(label,"promptText",[label]{
        label->setFont(uiFont(14));label->setWordWrap(true);
        label->setTextInteractionFlags(label->textInteractionFlags()|Qt::TextSelectableByMouse);
        label->heightForWidth(qMax(1,label->width()));
        if(auto doc=label->findChild<QTextDocument *>())promptDocumentSpacing(doc);
        label->updateGeometry();
    });
}
void stylePromptText(QTextEdit *text) {
    if(!text->property("promptTextInstalled").toBool()){text->setProperty("promptTextInstalled",true);text->style()->unpolish(text);text->style()->polish(text);}
    UiZoom::instance().bind(text,"promptText",[text]{text->setFont(uiFont(14));promptDocumentSpacing(text->document());text->updateGeometry();});
}
void stylePromptButton(QPushButton *button) {
    if(!button->property("promptButton").toBool()){
        button->setProperty("promptButton",true);
        // Qt's native button box may have polished it before arrange assigned
        // this property. Apply the scoped border on the very first display.
        button->style()->unpolish(button);button->style()->polish(button);
    }
    UiZoom::instance().bind(button,"promptButton",[button]{
        button->setFont(uiFont(14));button->setMinimumWidth(uiPx(84));
        button->setMinimumHeight(qMax(uiPx(32),button->fontMetrics().height()+uiPx(12)));button->updateGeometry();
    });
}
}
