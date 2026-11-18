#pragma once
#include <QObject>
#include <QColor>
#include <array>
namespace rs {
class EditorSurface;
// Container lexer: styles only requested lines. No QTextDocument, per-pointer
// full-text conversion, or parsing from selection/mouse notifications.
class EditorLexer final : public QObject {
    Q_OBJECT
  public:
    explicit EditorLexer(EditorSurface *surface);
    void rehighlight();
    static QByteArray previewStyles(const QByteArray &line);
    static std::array<QColor,7> styleColors();
  private:
    EditorSurface *surface;
    void styleTo(int end);
    void applyColors();
};
}
