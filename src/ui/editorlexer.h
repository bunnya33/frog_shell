#pragma once
#include <QObject>
namespace rs {
class EditorSurface;
// Container lexer: styles only requested lines. No QTextDocument, per-pointer
// full-text conversion, or parsing from selection/mouse notifications.
class EditorLexer final : public QObject {
    Q_OBJECT
  public:
    explicit EditorLexer(EditorSurface *surface);
    void rehighlight();
  private:
    EditorSurface *surface;
    void styleTo(int end);
    void applyColors();
};
}
