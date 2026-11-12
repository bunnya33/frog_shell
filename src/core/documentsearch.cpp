#include "documentsearch.h"
#include <cstdio>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif
namespace rs {
namespace {
constexpr quint32 magic = 0x46534731;
void configure(QDataStream &stream) { stream.setVersion(QDataStream::Qt_6_0); }
}
SearchOutcome searchDocument(const QString &text, const SearchOptions &options) {
    SearchOutcome out;
    if (options.query.isEmpty()) { out.error = "请输入搜索内容。"; return out; }
    if (options.query.size() > 512 || text.size() > searchTextLimit ||
        (options.regularExpression && text.size() > searchRegexTextLimit)) {
        out.error = options.regularExpression ? "正则搜索支持不超过 256 Ki 字符的文档。" : "搜索内容或文档超出限制。";
        return out;
    }
    QString pattern = options.regularExpression ? options.query : QRegularExpression::escape(options.query);
    if (options.wholeWord) pattern = "(?<![\\p{L}\\p{N}_])(?:" + pattern + ")(?![\\p{L}\\p{N}_])";
    // A per-match budget supplements the parent process's hard wall-clock deadline.
    pattern = "(*LIMIT_MATCH=10000)(*LIMIT_DEPTH=200)" + pattern;
    auto flags = QRegularExpression::UseUnicodePropertiesOption;
    QRegularExpression::PatternOptions optionsFlags(flags);
    if (!options.caseSensitive) optionsFlags |= QRegularExpression::CaseInsensitiveOption;
    QRegularExpression expression(pattern, optionsFlags);
    if (!expression.isValid()) { out.error = "正则表达式无效，请检查括号或转义字符。"; return out; }
    QElapsedTimer clock; clock.start();
    qsizetype offset = 0, lineStart = 0;
    int line = 1, contextSize = 0;
    auto limited = [&] { out.notice = "已达到结果上限，仅显示部分匹配。请缩小搜索内容。"; };
    while (offset <= text.size()) {
        if (clock.elapsed() > 1500) { out.error = "搜索耗时过长，请简化表达式或缩小文档。"; out.lines.clear(); out.matchCount = 0; return out; }
        const auto match = expression.match(text, offset);
        if (!match.isValid()) { out.error = "表达式运算超出限制，无法得出结果。"; out.lines.clear(); out.matchCount = 0; return out; }
        if (!match.hasMatch()) break;
        if (out.matchCount == searchMatchLimit) { limited(); break; }
        const auto begin = match.capturedStart(), end = match.capturedEnd();
        // Each line is represented once, even when it contains several matches.
        while (true) {
            const auto newline = text.indexOf('\n', lineStart);
            if (newline < 0 || newline >= begin) break;
            lineStart = newline + 1; ++line;
        }
        auto segmentStart = begin, segmentLineStart = lineStart;
        int segmentLine = line;
        bool stop = false;
        do {
            const auto newline = text.indexOf('\n', segmentLineStart);
            const auto lineEnd = newline < 0 ? text.size() : newline;
            const bool add = out.lines.isEmpty() || out.lines.last().line != segmentLine;
            if (add) {
                if (out.lines.size() >= searchLineLimit || lineEnd - segmentLineStart > 16384 ||
                    contextSize + lineEnd - segmentLineStart > searchContextLimit) { limited(); stop = true; break; }
                out.lines.append({segmentLine, int(segmentLineStart), text.mid(segmentLineStart, lineEnd - segmentLineStart), {}});
                contextSize += int(lineEnd - segmentLineStart);
            }
            out.lines.last().spans.append({int(segmentStart - segmentLineStart), int(qMin(end, lineEnd) - segmentStart)});
            if (end <= lineEnd || newline < 0) break;
            segmentLineStart = lineEnd + 1; segmentStart = segmentLineStart; ++segmentLine;
        } while (segmentStart < end);
        if (stop) break;
        ++out.matchCount;
        offset = end;
        if (begin == end) {
            if (offset == text.size()) break;
            if (text.at(offset).isHighSurrogate() && offset + 1 < text.size() && text.at(offset + 1).isLowSurrogate()) ++offset;
            ++offset;
        }
    }
    return out;
}
QByteArray searchRequest(const QString &text, const SearchOptions &options) {
    QByteArray bytes; QDataStream stream(&bytes, QIODevice::WriteOnly); configure(stream);
    stream << magic << text << options.query << options.caseSensitive << options.wholeWord << options.regularExpression;
    return bytes;
}
bool readSearchOutcome(const QByteArray &bytes, SearchOutcome *outcome) {
    if (!outcome || bytes.size() > searchWireLimit) return false;
    QDataStream stream(bytes); configure(stream); quint32 tag; int count;
    SearchOutcome result;
    stream >> tag >> result.error >> result.notice >> result.matchCount >> count;
    if (tag != magic || count < 0 || count > searchLineLimit || result.matchCount < 0 || result.matchCount > searchMatchLimit) return false;
    int context = 0, spans = 0;
    for (int i = 0; i < count; ++i) {
        SearchLine row; int spanCount; stream >> row.line >> row.offset >> row.text >> spanCount;
        context += int(row.text.size()); spans += spanCount;
        if (row.line < 1 || row.offset < 0 || row.offset > searchTextLimit || context > searchContextLimit || spanCount < 1 || spans > searchMatchLimit + searchLineLimit) return false;
        for (int n = 0; n < spanCount; ++n) {
            SearchSpan span; stream >> span.start >> span.length;
            if (span.start < 0 || span.length < 0 || span.start + qint64(span.length) > row.text.size()) return false;
            row.spans.append(span);
        }
        result.lines.append(std::move(row));
    }
    if (stream.status() != QDataStream::Ok || !stream.atEnd()) return false;
    *outcome = std::move(result); return true;
}
int documentSearchWorker() {
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
    QFile input, output;
    if (!input.open(stdin, QIODevice::ReadOnly) || !output.open(stdout, QIODevice::WriteOnly)) return 2;
    const auto bytes = input.read(searchWireLimit + 1);
    if (bytes.size() > searchWireLimit) return 2;
    QDataStream stream(bytes); configure(stream); quint32 tag; QString text; SearchOptions options;
    stream >> tag >> text >> options.query >> options.caseSensitive >> options.wholeWord >> options.regularExpression;
    if (tag != magic || stream.status() != QDataStream::Ok || !stream.atEnd()) return 2;
    const auto result = searchDocument(text, options);
    QByteArray response; QDataStream wire(&response, QIODevice::WriteOnly); configure(wire);
    wire << magic << result.error << result.notice << result.matchCount << int(result.lines.size());
    for (const auto &line : result.lines) {
        wire << line.line << line.offset << line.text << int(line.spans.size());
        for (const auto &span : line.spans) wire << span.start << span.length;
    }
    return output.write(response) == response.size() && output.flush() ? 0 : 2;
}
}
