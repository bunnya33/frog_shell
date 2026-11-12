#pragma once
#include <QString>

namespace rs {
enum class SyntaxFormat { None, Json, Jsonc, Xml, Yaml, Toml };
struct SyntaxDiagnostic {
    bool error = false;
    int offset = 0; // UTF-16 code units in the supplied text
    int line = 1, column = 1; // UTF-16 column, one based
    QString message;
};
SyntaxFormat syntaxFormatForPath(const QString &path);
SyntaxDiagnostic checkSyntax(SyntaxFormat format, const QString &text);
}
