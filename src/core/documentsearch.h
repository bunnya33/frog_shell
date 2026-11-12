#pragma once
#include <QtCore>
namespace rs {
struct SearchOptions {
    QString query;
    bool caseSensitive = false, wholeWord = false, regularExpression = false;
};
struct SearchSpan { int start = 0, length = 0; };
struct SearchLine {
    int line = 1, offset = 0;
    QString text;
    QVector<SearchSpan> spans;
};
struct SearchOutcome {
    QVector<SearchLine> lines;
    int matchCount = 0;
    QString error, notice;
};
// All offsets are UTF-16 QTextDocument positions. The worker has no configuration,
// credentials, network, or widget access and communicates only through stdio.
inline constexpr int searchTextLimit = 8 * 1024 * 1024;
inline constexpr int searchRegexTextLimit = 256 * 1024;
inline constexpr int searchMatchLimit = 10000;
inline constexpr int searchLineLimit = 3000;
inline constexpr int searchContextLimit = 1024 * 1024;
inline constexpr int searchWireLimit = 20 * 1024 * 1024;
inline constexpr int searchDeadlineMs = 2500;
SearchOutcome searchDocument(const QString &text, const SearchOptions &options);
QByteArray searchRequest(const QString &text, const SearchOptions &options);
bool readSearchOutcome(const QByteArray &bytes, SearchOutcome *outcome);
int documentSearchWorker();
}
