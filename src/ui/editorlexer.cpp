#include "editorlexer.h"
#include "editorsurface.h"
#include "theme.h"
#include <QSet>

namespace rs {
namespace {
int rgb(const QColor &c) { return c.red() | (c.green() << 8) | (c.blue() << 16); }
bool word(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' || uchar(c) >= 0x80; }
enum Style { Plain, Function, Keyword, Number, Comment, String, Property };
}
EditorLexer::EditorLexer(EditorSurface *view) : QObject(view), surface(view) {
    connect(view, &ScintillaEditBase::styleNeeded, this, [this](Scintilla::Position end) { styleTo(int(end)); });
    applyColors();
}
void EditorLexer::applyColors() {
    const auto swatches = styleColors();
    for (int i = Function; i <= Property; ++i) surface->send(SCI_STYLESETFORE, i, rgb(swatches[i]));
}
void EditorLexer::rehighlight() {
    applyColors(); surface->send(SCI_CLEARDOCUMENTSTYLE);
    surface->viewport()->update();
}
namespace {
QByteArray styleLine(const QByteArray &bytes, int &state) {
    QByteArray styles(bytes.size(), char(Plain));
    static const QSet<QByteArray> keywords = {"return","import","from","const","let","var","function","if","else","class","def","for","while","async","await","try","catch","throw","new","export","in","of","pass","break","continue"};
    auto mark = [&](int a, int b, Style style) { for (int i = a; i < b; ++i) styles[i] = char(style); };
    for (int i = 0; i < bytes.size();) {
        const int begin = i;
        if (state) {
            const QByteArray close = state == 1 ? "*/" : state == 2 ? "'''" : "\"\"\"";
            const int at = bytes.indexOf(close, i); i = at < 0 ? bytes.size() : at + close.size();
            mark(begin, i, state == 1 ? Comment : String); if (at >= 0) state = 0;
        } else if (bytes.mid(i, 2) == "//" || bytes[i] == '#') {
            mark(i, bytes.size(), Comment); i = bytes.size();
        } else if (bytes.mid(i, 2) == "/*") {
            const int at = bytes.indexOf("*/", i + 2); i = at < 0 ? bytes.size() : at + 2;
            mark(begin, i, Comment); if (at < 0) state = 1;
        } else if (bytes[i] == '\'' || bytes[i] == '"' || bytes[i] == '`') {
            const char quote = bytes[i]; const QByteArray triple(3, quote);
            if (quote != '`' && bytes.mid(i, 3) == triple) {
                const int at = bytes.indexOf(triple, i + 3); i = at < 0 ? bytes.size() : at + 3;
                mark(begin, i, String); if (at < 0) state = quote == '\'' ? 2 : 3;
            } else {
                bool closed = false;
                for (++i; i < bytes.size();) {
                    if (bytes[i] == '\\') { i = qMin(i + 2, int(bytes.size())); continue; }
                    if (bytes[i++] == quote) { closed = true; break; }
                }
                int next = i; while (next < bytes.size() && (bytes[next] == ' ' || bytes[next] == '\t')) ++next;
                mark(begin, i, quote == '"' && closed && next < bytes.size() && bytes[next] == ':' ? Property : String);
            }
        } else if (word(bytes[i])) {
            while (i < bytes.size() && word(bytes[i])) ++i;
            const auto token = bytes.mid(begin, i - begin);
            if (keywords.contains(token)) mark(begin, i, Keyword);
            else if (token == "true" || token == "false" || token == "null" || token == "None" || token == "True" || token == "False" || (token[0] >= '0' && token[0] <= '9')) mark(begin, i, Number);
            else { int next = i; while (next < bytes.size() && (bytes[next] == ' ' || bytes[next] == '\t')) ++next; if (next < bytes.size() && bytes[next] == '(') mark(begin, i, Function); }
        } else ++i;
    }
    return styles;
}
}
std::array<QColor,7> EditorLexer::styleColors() {
    const auto c = colors();
    return {c.text,c.syntaxFunction,c.keyword,c.syntaxNumber,c.comment,c.string,c.syntaxProperty};
}
QByteArray EditorLexer::previewStyles(const QByteArray &line) {
    int state = 0;
    return styleLine(line,state);
}
void EditorLexer::styleTo(int end) {
    auto &view = *surface;
    const int startLine = int(view.send(SCI_LINEFROMPOSITION, view.send(SCI_GETENDSTYLED)));
    const int endLine = int(view.send(SCI_LINEFROMPOSITION, qMin(end, view.byteLength())));
    int state = startLine ? int(view.send(SCI_GETLINESTATE, startLine - 1)) : 0;
    for (int line = startLine; line <= endLine; ++line) {
        const int start = int(view.send(SCI_POSITIONFROMLINE, line));
        const int finish = line + 1 < view.blockCount() ? int(view.send(SCI_POSITIONFROMLINE, line + 1)) : view.byteLength();
        const auto styles = styleLine(view.byteRange(start,finish),state);
        view.send(SCI_SETLINESTATE, line, state);
        view.send(SCI_STARTSTYLING, start);
        view.sends(SCI_SETSTYLINGEX, styles.size(), styles.constData());
    }
}
}
