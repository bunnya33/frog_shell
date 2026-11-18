#pragma once
#include <QtCore>

namespace rs {
struct DiffSpan { int start, length; };
struct DiffRow {
    int left = -1, right = -1; // Original zero-based line numbers. -1 is an alignment gap.
    bool changed = false;
    QList<DiffSpan> leftSpans, rightSpans;
};
struct TextDiff {
    QList<DiffRow> rows;
    QList<int> hunks;
    bool coarse = false;
    bool tooLarge = false;
};
inline constexpr int diffTextLimit = 8 * 1024 * 1024;
TextDiff compareText(const QString &left, const QString &right, bool ignoreWhitespace);
}
