#include "contentfont.h"
#include "uizoom.h"
#include <QFontDatabase>
#include <QResource>
#include <QApplication>
#include <QSettings>
#include <QKeyEvent>
#include <QWheelEvent>

static void initializeContentFont() { Q_INIT_RESOURCE(contentfont); }

namespace rs {
QString contentFontFamily() {
    static const QString family = [] {
        initializeContentFont();
        const int id = QFontDatabase::addApplicationFont(":/fonts/CascadiaCodeNF.ttf");
        const auto families = QFontDatabase::applicationFontFamilies(id);
        return families.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::FixedFont).family()
                                  : families.first();
    }();
    return family;
}
namespace {
ContentFontPreferences normalized(ContentFontPreferences value, bool checkFamily = true) {
    if (checkFamily && (value.family.isEmpty() || (value.family != contentFontFamily() && !QFontDatabase::isFixedPitch(value.family))))
        value.family = contentFontFamily();
    if (value.weight != 400 && value.weight != 500 && value.weight != 600 && value.weight != 700) value.weight = 500;
    value.editorPx = qBound(8, value.editorPx, 40);
    value.terminalPx = qBound(8, value.terminalPx, 40);
    value.listPx = qBound(8, value.listPx, 40);
    return value;
}
int readSize(const QSettings &settings, const char *key) {
    bool ok = false;
    const int size = settings.value(key, 14).toInt(&ok);
    return ok ? qBound(8, size, 40) : 14;
}
}
ContentFonts::ContentFonts(QObject *parent) : QObject(parent) {
    qRegisterMetaType<ContentFontRole>();
    connect(&UiZoom::instance(), &UiZoom::changed, this, [this]{
        for(auto role:{ContentFontRole::Editor,ContentFontRole::Terminal,ContentFontRole::List})emit changed(role);
    });
    reload(); // First accessed by a content widget/settings, after data-dir setup.
}
ContentFonts &ContentFonts::instance() {
    static auto service = new ContentFonts(qApp);
    return *service;
}
void ContentFonts::reload() {
    QSettings settings;
    ContentFontPreferences value;
    value.family = settings.value("appearance/font/family", contentFontFamily()).toString();
    value.weight = settings.value("appearance/font/weight", 500).toInt();
    value.editorPx = readSize(settings, "appearance/font/editorPx");
    value.terminalPx = readSize(settings, "appearance/font/terminalPx");
    value.listPx = readSize(settings, "appearance/font/listPx");
    apply(value, false);
}
int ContentFonts::size(ContentFontRole role) const {
    return role == ContentFontRole::Editor ? current.editorPx : role == ContentFontRole::Terminal ? current.terminalPx : current.listPx;
}
void ContentFonts::setPreferences(ContentFontPreferences value) { apply(value, true); }
void ContentFonts::apply(ContentFontPreferences value, bool persist) {
    // A wheel step never enumerates families or reloads QSettings.
    value = normalized(value, value.family != current.family || current.family.isEmpty());
    if (value == current) return;
    const auto previous = current;
    current = value;
    if (persist) {
        QSettings settings;
        settings.setValue("appearance/font/family", current.family);
        settings.setValue("appearance/font/weight", current.weight);
        settings.setValue("appearance/font/editorPx", current.editorPx);
        settings.setValue("appearance/font/terminalPx", current.terminalPx);
        settings.setValue("appearance/font/listPx", current.listPx);
    }
    const bool shared = previous.family != current.family || previous.weight != current.weight;
    if (shared || previous.editorPx != current.editorPx) emit changed(ContentFontRole::Editor);
    if (shared || previous.terminalPx != current.terminalPx) emit changed(ContentFontRole::Terminal);
    if (shared || previous.listPx != current.listPx) emit changed(ContentFontRole::List);
}
void ContentFonts::adjustSize(ContentFontRole role, int steps) {
    auto value = current;
    auto &size = role == ContentFontRole::Editor ? value.editorPx : role == ContentFontRole::Terminal ? value.terminalPx : value.listPx;
    size = int(qBound(qint64(8), qint64(size) + steps, qint64(40)));
    setPreferences(value);
}
void ContentFonts::resetDefaults() {
    QSettings settings; settings.remove("appearance/font");
    apply(ContentFontPreferences{contentFontFamily()}, false);
}
QStringList ContentFonts::availableFamilies() {
    QStringList result{contentFontFamily()};
    for (const auto &family : QFontDatabase::families())
        if (family != contentFontFamily() && QFontDatabase::isFixedPitch(family)) result << family;
    return result;
}
QFont ContentFonts::font(ContentFontRole role) const {
    QFont font(current.family);
    font.setPixelSize(uiPx(size(role)));
    font.setWeight(QFont::Weight(current.weight));
    // Qt's style-name matching can keep a variable font at its Regular instance.
    // Set the embedded font's actual axis as well as its semantic QFont weight.
    if (current.family == contentFontFamily())
        font.setVariableAxis(QFont::Tag::fromString("wght").value(), float(current.weight));
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferFullHinting);
    // Keep font merging: this file has no CJK glyphs. Terminal shaping stays per cell.
    if (role == ContentFontRole::Terminal)
        for (const auto tag : {"liga", "clig", "dlig", "calt"})
            font.setFeature(QFont::Tag::fromString(tag).value(), 0);
    return font;
}
QFont contentFont(bool terminal) { return ContentFonts::instance().font(terminal ? ContentFontRole::Terminal : ContentFontRole::Editor); }
void bindContentFont(QWidget *widget, ContentFontRole role) {
    widget->setProperty("contentFont", true);
    widget->setProperty("rabbitUi", true);
    auto &fonts = ContentFonts::instance();
    widget->setFont(fonts.font(role));
    struct InputState {int cursor=0,start=-1,length=0;bool pending=false;};
    auto input=qobject_cast<QLineEdit *>(widget);auto state=std::make_shared<InputState>();
    if(input)QObject::connect(&UiZoom::instance(),&UiZoom::aboutToChange,widget,[input,state]{
        state->cursor=input->cursorPosition();state->start=input->selectionStart();state->length=input->selectedText().size();state->pending=true;
    });
    QObject::connect(&fonts, &ContentFonts::changed, widget, [widget,role,input,state](ContentFontRole changed) {
        if(changed!=role)return;
        const int cursor=input?(state->pending?state->cursor:input->cursorPosition()):0;
        const int start=input?(state->pending?state->start:input->selectionStart()):-1;
        const int length=input?(state->pending?state->length:input->selectedText().size()):0;
        widget->setFont(ContentFonts::instance().font(role));
        if(input){if(start>=0)input->setSelection(cursor==start?start+length:start,cursor==start?-length:length);else input->setCursorPosition(cursor);}state->pending=false;
    });
}
} // namespace rs
