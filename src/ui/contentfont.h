#pragma once
#include <QFont>
#include <QString>
#include <QObject>
#include <QWidget>

class QEvent;
class QWheelEvent;

namespace rs {
// Lazy process-wide registration, also used by widgets constructed outside main().
QString contentFontFamily();
enum class ContentFontRole { Editor, Terminal, List };
#ifdef Q_OS_MACOS
inline constexpr auto contentZoomModifier = Qt::MetaModifier; // physical Control in Qt/macOS
#else
inline constexpr auto contentZoomModifier = Qt::ControlModifier;
#endif
struct ContentFontPreferences {
    QString family;
    int weight = 500, editorPx = 14, terminalPx = 14, listPx = 14;
    bool operator==(const ContentFontPreferences &) const = default;
};
class ContentFonts final : public QObject {
    Q_OBJECT
  public:
    explicit ContentFonts(QObject *parent = nullptr);
    static ContentFonts &instance();
    ContentFontPreferences preferences() const { return current; }
    QFont font(ContentFontRole role) const;
    int size(ContentFontRole role) const;
    void setPreferences(ContentFontPreferences value);
    void adjustSize(ContentFontRole role, int steps);
    void resetDefaults();
    // Explicit reload also permits isolated configuration/persistence verification.
    void reload();
    static QStringList availableFamilies(); // Enumerated only when opening settings.
  signals:
    void changed(rs::ContentFontRole role);
  private:
    ContentFontPreferences current;
    void apply(ContentFontPreferences value, bool persist);
};
QFont contentFont(bool terminal = false);
void bindContentFont(QWidget *widget, ContentFontRole role);
} // namespace rs
Q_DECLARE_METATYPE(rs::ContentFontRole)
