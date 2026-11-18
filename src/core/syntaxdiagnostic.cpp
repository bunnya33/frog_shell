#include "syntaxdiagnostic.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QXmlStreamReader>
#include <yaml.h>
#include <toml++/toml.hpp>

namespace rs {
namespace {
SyntaxDiagnostic at(const QString &text, int offset, const QString &message) {
    offset = qBound(0, offset, int(text.size()));
    int line = 1, col = 1;
    for (int i = 0; i < offset; ++i) {
        if (text[i] == u'\n') { ++line; col = 1; }
        else if (text[i] != u'\r' || i + 1 >= text.size() || text[i + 1] != u'\n') ++col;
    }
    return {true, offset, line, col, message};
}
// Parser columns count Unicode scalar values, while QTextCursor uses UTF-16.
int scalarPosition(const QString &text, int line, int column) {
    int pos = 0;
    for (int n = 1; n < line && pos < text.size(); ++n) {
        auto next = text.indexOf(u'\n', pos);
        if (next < 0) return int(text.size());
        pos = int(next) + 1;
    }
    // Both stream parsers consume an initial UTF-8 BOM before counting columns.
    if (line == 1 && !text.isEmpty() && text[0] == QChar(0xfeff)) pos = 1;
    for (int n = 1; n < column && pos < text.size() && text[pos] != u'\n' && text[pos] != u'\r'; ++n) {
        if (text[pos].isHighSurrogate() && pos + 1 < text.size() && text[pos + 1].isLowSurrogate()) ++pos;
        ++pos;
    }
    return pos;
}
SyntaxDiagnostic json(SyntaxFormat format, const QString &text) {
    QString input = text;
    if (!input.isEmpty() && input[0] == QChar(0xfeff)) input[0] = u' ';
    if (format == SyntaxFormat::Jsonc) {
        bool string = false, escape = false;
        for (int i = 0; i < input.size(); ++i) {
            if (string) {
                if (escape) escape = false;
                else if (input[i] == u'\\') escape = true;
                else if (input[i] == u'"') string = false;
            } else if (input[i] == u'"') string = true;
            else if (input[i] == u'/' && i + 1 < input.size() && (input[i+1] == u'/' || input[i+1] == u'*')) {
                const bool block = input[i+1] == u'*';
                const int begin = i;
                input[i++] = u' '; input[i] = u' ';
                bool closed = !block;
                while (++i < input.size()) {
                    if (block && input[i] == u'*' && i + 1 < input.size() && input[i+1] == u'/') {
                        input[i++] = u' '; input[i] = u' '; closed = true; break;
                    }
                    if (!block && (input[i] == u'\n' || input[i] == u'\r')) break;
                    if (input[i] != u'\n' && input[i] != u'\r') input[i] = u' ';
                }
                if (!closed) return at(text, begin, QStringLiteral("未结束的块注释"));
                if (i >= input.size()) break;
            }
        }
        string = false; escape = false;
        for (int i = 0; i < input.size(); ++i) {
            if (string) {
                if (escape) escape = false;
                else if (input[i] == u'\\') escape = true;
                else if (input[i] == u'"') string = false;
            } else if (input[i] == u'"') string = true;
            else if (input[i] == u',') {
                int j = i + 1; while (j < input.size() && (input[j] == u' ' || input[j] == u'\t' || input[j] == u'\r' || input[j] == u'\n')) ++j;
                int previous = i - 1;
                while (previous >= 0 && (input[previous] == u' ' || input[previous] == u'\t' || input[previous] == u'\r' || input[previous] == u'\n')) --previous;
                if (previous >= 0 && input[previous] != u'[' && input[previous] != u'{' &&
                    input[previous] != u',' && input[previous] != u':' &&
                    j < input.size() && (input[j] == u']' || input[j] == u'}')) input[i] = u' ';
            }
        }
    }
    // QJsonDocument accepts literal control characters inside strings even
    // though RFC 8259 requires them to be escaped.
    bool inString = false, escaped = false;
    int controlOffset = -1;
    for (int i = 0; i < input.size(); ++i) {
        const auto c = input[i];
        if (inString) {
            if (c.unicode() < 0x20) { controlOffset = i; break; }
            if (escaped) escaped = false;
            else if (c == u'\\') escaped = true;
            else if (c == u'"') inString = false;
        } else if (c == u'"') inString = true;
    }
    // Qt's document parser accepts arrays/objects only; the wrapper also accepts
    // RFC 8259 scalar top-level values, without changing the original offsets.
    const QByteArray bytes = input.toUtf8();
    QJsonParseError e;
    const auto doc = QJsonDocument::fromJson(QByteArray("[") + bytes + "]", &e);
    if (e.error == QJsonParseError::NoError) {
        if (doc.array().size() == 1) {
            if (controlOffset >= 0) return at(text, controlOffset, QStringLiteral("字符串中的控制字符必须转义"));
            return {};
        }
        if (doc.array().isEmpty()) return at(text, int(text.size()), QStringLiteral("需要一个 JSON 值"));
        int depth = 0; bool string = false, escape = false;
        for (int i = 0; i < input.size(); ++i) {
            const auto c = input[i];
            if (string) {
                if (escape) escape = false;
                else if (c == u'\\') escape = true;
                else if (c == u'"') string = false;
            } else if (c == u'"') string = true;
            else if (c == u'[' || c == u'{') ++depth;
            else if (c == u']' || c == u'}') --depth;
            else if (c == u',' && depth == 0) {
                if (controlOffset >= 0 && controlOffset < i)
                    return at(text, controlOffset, QStringLiteral("字符串中的控制字符必须转义"));
                return at(text, i, QStringLiteral("只能有一个顶层 JSON 值"));
            }
        }
        return at(text, 0, QStringLiteral("只能有一个顶层 JSON 值"));
    }
    const int byteOffset = qBound(0, int(e.offset) - 1, int(bytes.size()));
    int pos = int(QString::fromUtf8(bytes.constData(), byteOffset).size());
    // Qt reports a trailing-comma error after consuming the closing bracket.
    // Mark the actual comma instead of the following character/EOF.
    if (e.error == QJsonParseError::MissingObject || e.error == QJsonParseError::IllegalValue ||
        e.error == QJsonParseError::UnterminatedObject) {
        int close = pos;
        if (close >= input.size() || (input[close] != u']' && input[close] != u'}')) --close;
        int previous = close - 1;
        while (previous >= 0 && (input[previous] == u' ' || input[previous] == u'\t' ||
                                  input[previous] == u'\r' || input[previous] == u'\n')) --previous;
        if (close >= 0 && close < input.size() && (input[close] == u']' || input[close] == u'}') &&
            previous >= 0 && input[previous] == u',') pos = previous;
    }
    if (controlOffset >= 0 && controlOffset <= pos)
        return at(text, controlOffset, QStringLiteral("字符串中的控制字符必须转义"));
    return at(text, pos, e.errorString());
}
}
JsonFormatResult formatJson(const QString &text, qsizetype sizeLimit) {
    if (text.size()>sizeLimit) return {{},{},true};
    const auto diagnostic = json(SyntaxFormat::Json,text);
    if (diagnostic.error) return {{},diagnostic};

    // Use the parser only for validation. Reserializing its value would sort
    // keys, drop duplicates, and round numbers. Copy every token as written.
    auto space = [](QChar c) {
        return c==u' ' || c==u'\t' || c==u'\r' || c==u'\n';
    };
    QString output;
    output.reserve(qMin(text.size(),sizeLimit));
    qsizetype i=0;
    if (text.startsWith(QChar(0xfeff))) output += text[i++];
    int depth=0;
    auto newline = [&] {
        output += u'\n';
        output += QString(depth*4,u' ');
    };
    auto skipSpace = [&] { while (i<text.size() && space(text[i])) ++i; };
    skipSpace();
    while (i<text.size()) {
        const auto token=text[i++];
        switch (token.unicode()) {
        case u'{':
        case u'[': {
            output += token;
            skipSpace();
            const QChar close = token==u'{' ? u'}' : u']';
            if (i<text.size() && text[i]==close) output += text[i++];
            else { ++depth; newline(); }
            break;
        }
        case u'}':
        case u']':
            --depth;
            newline();
            output += token;
            break;
        case u',':
            output += token;
            newline();
            break;
        case u':':
            output += QStringLiteral(": ");
            break;
        case u'"': {
            const auto start=i-1;
            while (i<text.size()) {
                const auto c=text[i++];
                if (c==u'\\' && i<text.size()) ++i;
                else if (c==u'"') break;
            }
            output += QStringView(text).mid(start,i-start);
            break;
        }
        default:
            output += token;
        }
        if (output.size()>sizeLimit) return {{},{},true};
        skipSpace();
    }
    for (qsizetype tail=text.size(); tail>0 && space(text[tail-1]); --tail) {
        if (text[tail-1]==u'\n') { output += u'\n'; break; }
    }
    if (output.size()>sizeLimit) return {{},{},true};
    return {std::move(output),{}};
}
SyntaxFormat syntaxFormatForPath(const QString &path) {
    const auto ext = QFileInfo(path).suffix().toLower();
    if (ext == "json") return SyntaxFormat::Json;
    if (ext == "jsonc") return SyntaxFormat::Jsonc;
    if (ext == "xml") return SyntaxFormat::Xml;
    if (ext == "yaml" || ext == "yml") return SyntaxFormat::Yaml;
    if (ext == "toml") return SyntaxFormat::Toml;
    return SyntaxFormat::None;
}
SyntaxDiagnostic checkSyntax(SyntaxFormat format, const QString &text) {
    if (format == SyntaxFormat::None) return {};
    if (format == SyntaxFormat::Json || format == SyntaxFormat::Jsonc) return json(format, text);
    if (format == SyntaxFormat::Xml) {
        QXmlStreamReader reader(text);
        reader.setEntityExpansionLimit(4096);
        while (!reader.atEnd()) reader.readNext();
        if (reader.hasError()) {
            // Qt's characterOffset indexes UTF-16; point at the last consumed
            // character, or EOF if nothing was consumed.
            const int pos = qBound(0, int(reader.characterOffset()) - 1, int(text.size()));
            return at(text, pos, reader.errorString());
        }
        return {};
    }
    const QByteArray utf8 = text.toUtf8();
    if (format == SyntaxFormat::Yaml) {
        yaml_parser_t parser;
        if (!yaml_parser_initialize(&parser)) return at(text, 0, QStringLiteral("无法初始化 YAML 解析器"));
        yaml_parser_set_input_string(&parser, reinterpret_cast<const unsigned char *>(utf8.constData()), size_t(utf8.size()));
        bool done = false; SyntaxDiagnostic result;
        while (!done) {
            yaml_event_t event;
            if (!yaml_parser_parse(&parser, &event)) {
                result = at(text, scalarPosition(text, int(parser.problem_mark.line) + 1, int(parser.problem_mark.column) + 1),
                            QString::fromUtf8(parser.problem ? parser.problem : "YAML 语法错误"));
                break;
            }
            done = event.type == YAML_STREAM_END_EVENT;
            yaml_event_delete(&event);
        }
        yaml_parser_delete(&parser);
        return result;
    }
    try {
        (void)toml::parse(std::string_view(utf8.constData(), size_t(utf8.size())));
    } catch (const toml::parse_error &e) {
        const auto source = e.source();
        return at(text, scalarPosition(text, int(source.begin.line), int(source.begin.column)),
                  QString::fromUtf8(e.description().data(), qsizetype(e.description().size())));
    }
    return {};
}
}
