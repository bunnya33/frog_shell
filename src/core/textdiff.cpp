#include "textdiff.h"
#include <algorithm>

namespace rs {
namespace {
using Lines = QList<QStringView>;
using Pair = QPair<int,int>;

// Extract equal anchors inside a small changed region. A shared work budget
// bounds both the number of cells visited and the largest allocation.
QList<Pair> common(const Lines &a, int a0, int a1, const Lines &b, int b0, int b1, int &budget) {
    const int n = a1-a0, m = b1-b0;
    if (!n || !m || qint64(n+1)*(m+1) > budget) return {};
    const int stride = m+1;
    budget -= (n+1)*stride;
    QList<int> lengths((n+1)*stride,0);
    for (int i=n-1; i>=0; --i)
        for (int j=m-1; j>=0; --j)
            lengths[i*stride+j] = a[a0+i] == b[b0+j] ?
                1 + lengths[(i+1)*stride+j+1] : qMax(lengths[(i+1)*stride+j],lengths[i*stride+j+1]);
    QList<Pair> anchors;
    for (int i=0,j=0; i<n && j<m;) {
        if (a[a0+i] == b[b0+j]) { anchors.append({a0+i,b0+j}); ++i; ++j; }
        else if (lengths[(i+1)*stride+j] >= lengths[i*stride+j+1]) ++i;
        else ++j;
    }
    return anchors;
}

// Patience anchors give identical distinctive lines stable positions even
// when a large section was inserted. Take the longest ordered set of anchors.
QList<Pair> uniqueAnchors(const Lines &left, int l0, int l1, const Lines &right, int r0, int r1) {
    struct Occurrence { int left = -1, right = -1; bool repeated = false; };
    QHash<QStringView,Occurrence> occurrences;
    for (int i=l0; i<l1; ++i) {
        auto &entry = occurrences[left[i]];
        entry.repeated |= entry.left >= 0;
        entry.left = i;
    }
    for (int i=r0; i<r1; ++i) {
        auto it = occurrences.find(right[i]);
        if (it == occurrences.end()) continue;
        it->repeated |= it->right >= 0;
        it->right = i;
    }
    QList<Pair> candidates;
    QList<int> tails, predecessors;
    for (int i=l0; i<l1; ++i) {
        const auto entry = occurrences.value(left[i]);
        if (entry.repeated || entry.right < 0) continue;
        auto tail = std::lower_bound(tails.begin(),tails.end(),entry.right,[&](int index,int row) {
            return candidates[index].second < row;
        });
        predecessors.append(tail == tails.begin() ? -1 : *(tail-1));
        const int index = int(candidates.size());
        if (tail == tails.end()) tails.append(index);
        else *tail = index;
        candidates.append({i,entry.right});
    }
    QList<Pair> anchors;
    for (int i=tails.value(tails.size()-1,-1); i>=0; i=predecessors[i]) anchors.append(candidates[i]);
    std::reverse(anchors.begin(),anchors.end());
    return anchors;
}

Lines graphemes(QStringView line) {
    Lines pieces;
    QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme,line.data(),line.size());
    for (qsizetype start=0, end=boundaries.toNextBoundary(); end>=0; start=end,end=boundaries.toNextBoundary())
        pieces.append(line.mid(start,end-start));
    return pieces;
}

void inlineChanges(QStringView a, QStringView b, DiffRow &row, int &budget) {
    // Very long lines receive the row tint. Avoid indexing megabytes of text
    // just to decorate a single line.
    if (a.size() > 4096 || b.size() > 4096) return;
    const auto left = graphemes(a), right = graphemes(b);
    int start=0, lEnd=int(left.size()), rEnd=int(right.size());
    while (start<lEnd && start<rEnd && left[start]==right[start]) ++start;
    while (lEnd>start && rEnd>start && left[lEnd-1]==right[rEnd-1]) { --lEnd; --rEnd; }
    auto anchors = common(left,start,lEnd,right,start,rEnd,budget);
    anchors.append({lEnd,rEnd});
    auto addSpan = [](QStringView line, const Lines &pieces, int first, int end, QList<DiffSpan> &spans) {
        if (first>=end) return;
        const int begin = int(pieces[first].data()-line.data());
        const int finish = int(pieces[end-1].data()-line.data()+pieces[end-1].size());
        spans.append({begin,finish-begin});
    };
    int li=start, ri=start;
    for (const auto &[l,r] : anchors) {
        addSpan(a,left,li,l,row.leftSpans);
        addSpan(b,right,ri,r,row.rightSpans);
        li=l+1; ri=r+1;
    }
}

Lines keys(const Lines &lines, QList<QString> &storage) {
    storage.reserve(lines.size());
    for (auto line : lines) {
        QString squeezed;
        squeezed.reserve(line.size());
        for (QChar c : line) if (!c.isSpace()) squeezed.append(c);
        storage.append(std::move(squeezed));
    }
    Lines result;
    result.reserve(lines.size());
    for (const auto &key : std::as_const(storage)) result.append(QStringView(key));
    return result;
}

int similarity(QStringView a, QStringView b) {
    a = a.trimmed(); b = b.trimmed();
    const int span = int(qMin(a.size(),b.size()));
    int prefix=0, suffix=0;
    while (prefix<qMin(span,80) && a[prefix]==b[prefix]) ++prefix;
    while (suffix<qMin(span-prefix,80) && a[a.size()-suffix-1]==b[b.size()-suffix-1]) ++suffix;
    return 1 + prefix + suffix;
}

// Prefer pairing similar changed lines when an insertion/deletion shares
// their hunk. Otherwise changed lines align in document order.
QList<Pair> changedPairs(const Lines &a, int a0, int a1, const Lines &b, int b0, int b1, int &budget) {
    const int n=a1-a0,m=b1-b0;
    const int stride=m+1;
    if (!n || !m || n==m || qint64(n+1)*stride>qMin(budget,16384)) return {};
    budget -= (n+1)*stride;
    QList<int> scores((n+1)*stride,0);
    QList<char> direction(n*m,0);
    for (int i=n-1; i>=0; --i) {
        for (int j=m-1; j>=0; --j) {
            const int pair = similarity(a[a0+i],b[b0+j]) + scores[(i+1)*stride+j+1];
            const int del = scores[(i+1)*stride+j], add = scores[i*stride+j+1];
            scores[i*stride+j] = qMax(pair,qMax(del,add));
            direction[i*m+j] = pair>=del && pair>=add ? 0 : del>=add ? 1 : 2;
        }
    }
    QList<Pair> result;
    for (int i=0,j=0; i<n && j<m;) {
        switch (direction[i*m+j]) {
        case 0: result.append({a0+i,b0+j}); ++i; ++j; break;
        case 1: ++i; break;
        default: ++j; break;
        }
    }
    return result;
}
}

TextDiff compareText(const QString &left, const QString &right, bool ignoreWhitespace) {
    TextDiff result;
    // Reject before splitting so a million empty lines cannot allocate a
    // million view objects or native margin decorations.
    if (left.size()>diffTextLimit || right.size()>diffTextLimit ||
        left.count(u'\n')>=40000 || right.count(u'\n')>=40000) {
        result.tooLarge = true;
        return result;
    }
    const auto leftLines = QStringView(left).split(u'\n');
    const auto rightLines = QStringView(right).split(u'\n');
    QList<QString> leftStorage,rightStorage;
    const auto a = ignoreWhitespace ? keys(leftLines,leftStorage) : leftLines;
    const auto b = ignoreWhitespace ? keys(rightLines,rightStorage) : rightLines;
    int lineBudget=1500000, characterBudget=250000, pairingBudget=80000;
    auto append = [&](int l,int r) {
        DiffRow row{l,r,l<0 || r<0 || a[l]!=b[r],{},{}};
        if (row.changed) {
            if (result.rows.isEmpty() || !result.rows.last().changed)
                result.hunks.append(int(result.rows.size()));
            if (l>=0 && r>=0) inlineChanges(leftLines[l],rightLines[r],row,characterBudget);
        }
        result.rows.append(std::move(row));
    };
    int l0=0,r0=0,l1=int(a.size()),r1=int(b.size());
    while (l0<l1 && r0<r1 && a[l0]==b[r0]) append(l0++,r0++);
    int suffix=0;
    while (l0<l1 && r0<r1 && a[l1-1]==b[r1-1]) { --l1; --r1; ++suffix; }
    auto anchors = uniqueAnchors(a,l0,l1,b,r0,r1);
    anchors.append({l1,r1});
    auto appendRun = [&](int l,int r) {
        while (l0<l || r0<r) append(l0<l ? l0++ : -1,r0<r ? r0++ : -1);
    };
    auto appendChanged = [&](int l,int r) {
        for (const auto &[pairL,pairR] : changedPairs(a,l0,l,b,r0,r,pairingBudget)) {
            appendRun(pairL,pairR);
            append(l0++,r0++);
        }
        appendRun(l,r);
    };
    for (const auto &[l,r] : anchors) {
        // Trim each region's equal edges before using the bounded LCS.
        while (l0<l && r0<r && a[l0]==b[r0]) append(l0++,r0++);
        int le=l, re=r;
        while (l0<le && r0<re && a[le-1]==b[re-1]) { --le; --re; }
        if (l0<le && r0<re && qint64(le-l0+1)*(re-r0+1)>lineBudget) result.coarse = true;
        for (const auto &[matchL,matchR] : common(a,l0,le,b,r0,re,lineBudget)) {
            appendChanged(matchL,matchR);
            append(l0++,r0++);
        }
        appendChanged(le,re);
        while (l0<l) append(l0++,r0++);
        if (l<l1) append(l0++,r0++);
    }
    for (int i=0; i<suffix; ++i) append(l1+i,r1+i);
    return result;
}
}
