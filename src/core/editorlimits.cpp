#include "editorlimits.h"
#include <QSettings>
#include <QRegularExpression>

namespace rs::editorlimits {
int sizeMiB() {
    const auto text = QSettings().value("editor/maxFileMiB", defaultMiB).toString();
    static const QRegularExpression integer(QStringLiteral("^[0-9]+$"));
    bool ok = false;
    const auto value = text.toULongLong(&ok);
    return integer.match(text).hasMatch() && ok && value >= 1 && value <= maximumMiB
               ? int(value) : defaultMiB;
}
quint64 sizeBytes() { return quint64(sizeMiB()) * MiB; }
}
