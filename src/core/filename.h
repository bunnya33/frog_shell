#pragma once
#include <QString>
#include <QStringList>

namespace rs {
// A basename only: callers retain their own bounded existence checks and
// no-replace/commit rules. Directory dots never denote a file extension.
inline int fileNameExtensionOffset(const QString &name, bool directory) {
    int split = int(name.size());
    if (!directory) {
        static const QStringList compound{
            ".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tar.lzma", ".tar.lz", ".tar.br", ".tar.Z"};
        for (const auto &suffix : compound)
            if (name.size() > suffix.size() && name.endsWith(suffix, Qt::CaseInsensitive)) {
                split = int(name.size() - suffix.size());
                break;
            }
        if (split == name.size()) {
            const auto dot = name.lastIndexOf('.');
            if (dot > 0 && dot + 1 < name.size()) split = int(dot);
        }
    }
    return split;
}
inline QString numberedFileName(const QString &name, int number, bool directory = false) {
    const int split = fileNameExtensionOffset(name, directory);
    return name.left(split) + QString(" (%1)").arg(number) + name.mid(split);
}
inline bool isNumberedFileName(const QString &original, const QString &candidate, bool directory = false) {
    const int split = fileNameExtensionOffset(original, directory);
    const auto prefix = original.left(split) + " (", suffix = ')' + original.mid(split);
    if (!candidate.startsWith(prefix) || !candidate.endsWith(suffix)) return false;
    const auto digits = candidate.mid(prefix.size(), candidate.size() - prefix.size() - suffix.size());
    if (digits.isEmpty()) return false;
    for (auto c : digits) if (c < u'0' || c > u'9') return false;
    bool ok = false; const int number = digits.toInt(&ok);
    return ok && number > 0 && QString::number(number) == digits;
}
} // namespace rs
