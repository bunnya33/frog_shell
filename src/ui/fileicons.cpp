#include "fileicons.h"
#include "theme.h"
#include <QIconEngine>

static void initializeFileIcons() { Q_INIT_RESOURCE(fileicons); }

namespace rs {
namespace {
QJsonObject readObject(const char *name) {
    QFile file(QString(":/fileicons/") + name);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}
class FileIconEngine final : public QIconEngine {
  public:
    FileIconEngine(QPainterPath shape, QColor color, QPainterPath expanded = {})
        : shape(std::move(shape)), expanded(std::move(expanded)), ink(color) {}
    QIconEngine *clone() const override { return new FileIconEngine(shape, ink, expanded); }
    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State state) override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        auto color = ink;
        if (mode == QIcon::Disabled) color.setAlphaF(.45f);
        p->translate(rect.center().x() + .5, rect.center().y() + .5);
        const qreal scale = qMin(rect.width(), rect.height()) / 24.0;
        p->scale(scale, scale);
        p->fillPath(state == QIcon::On && !expanded.isEmpty() ? expanded : shape, color);
        p->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal dpr) override {
        QPixmap image(size * dpr); image.setDevicePixelRatio(dpr); image.fill(Qt::transparent);
        QPainter painter(&image); paint(&painter, QRect(QPoint(), size), mode, state); return image;
    }
  private:
    QPainterPath shape, expanded;
    QColor ink;
};
struct LanguagePattern { QRegularExpression pattern; QString language; };
struct Seti {
    QJsonObject theme, light;
    QHash<QString, QPainterPath> outlines;
    QHash<QString, QString> languageNames, languageExtensions;
    QList<LanguagePattern> languagePatterns;
    QHash<QString, QIcon> icons;
    Seti() {
        initializeFileIcons();
        theme = readObject("vs-seti-icon-theme.json"); light = theme["light"].toObject();
        const auto source = readObject("outlines.json");
        for (auto i = source.begin(); i != source.end(); ++i) {
            QPainterPath path; path.setFillRule(Qt::WindingFill);
            for (const auto &value : i.value().toArray()) {
                const auto c = value.toArray(); const auto type = c[0].toString();
                auto n = [&](int index) { return c[index].toDouble(); };
                if (type == "M") path.moveTo(n(1), -n(2));
                else if (type == "L") path.lineTo(n(1), -n(2));
                else if (type == "Q") path.quadTo(n(1), -n(2), n(3), -n(4));
                else if (type == "C") path.cubicTo(n(1), -n(2), n(3), -n(4), n(5), -n(6));
                else if (type == "Z") path.closeSubpath();
            }
            const auto bounds = path.boundingRect();
            if (bounds.isEmpty()) continue;
            const qreal scale = 21.0 / qMax(bounds.width(), bounds.height());
            QTransform transform; transform.scale(scale, scale); transform.translate(-bounds.center().x(), -bounds.center().y());
            outlines.insert(i.key(), transform.map(path));
        }
        QFile file(":/fileicons/languages.json");
        if (file.open(QIODevice::ReadOnly)) for (const auto &value : QJsonDocument::fromJson(file.readAll()).array()) {
            const auto language = value.toObject(); const auto id = language["id"].toString();
            for (const auto &name : language["filenames"].toArray()) languageNames[name.toString().toLower()] = id;
            for (const auto &ext : language["extensions"].toArray()) {
                auto suffix = ext.toString();
                if (suffix.startsWith('.')) suffix.remove(0,1);
                languageExtensions[suffix.toLower()] = id;
            }
            for (const auto &pattern : language["filenamePatterns"].toArray())
                languagePatterns.append({QRegularExpression(QRegularExpression::wildcardToRegularExpression(pattern.toString()),
                                                           QRegularExpression::CaseInsensitiveOption), id});
        }
    }
    QString association(const char *group, const QString &key) const {
        auto value = lightTheme() ? light[group].toObject().value(key).toString() : QString();
        return value.isEmpty() ? theme[group].toObject().value(key).toString() : value;
    }
    QString resolve(const QString &path) const {
        const auto normalized = QDir::fromNativeSeparators(path).toLower();
        const auto name = normalized.mid(normalized.lastIndexOf('/') + 1);
        if (const auto value = association("fileNames", name); !value.isEmpty()) return value;
        // Iterate suffixes from left to right: spec.ts wins over ts.
        QStringList suffixes;
        for (int dot = name.indexOf('.'); dot >= 0; dot = name.indexOf('.', dot + 1)) suffixes << name.mid(dot + 1);
        for (const auto &suffix : suffixes)
            if (const auto value = association("fileExtensions", suffix); !value.isEmpty()) return value;
        auto language = languageNames.value(name);
        if (language.isEmpty()) for (const auto &pattern : languagePatterns)
            if (pattern.pattern.match(name).hasMatch() || pattern.pattern.match(normalized).hasMatch()) { language = pattern.language; break; }
        if (language.isEmpty()) language = languageExtensions.value(name);
        if (language.isEmpty()) for (const auto &suffix : suffixes)
            if (languageExtensions.contains(suffix)) { language = languageExtensions.value(suffix); break; }
        if (const auto value = association("languageIds", language); !value.isEmpty()) return value;
        const auto fallback = lightTheme() ? light["file"].toString() : QString();
        return fallback.isEmpty() ? theme["file"].toString("_default") : fallback;
    }
    QIcon get(const QString &id) {
        if (icons.contains(id)) return icons.value(id);
        const auto definition = theme["iconDefinitions"].toObject()[id].toObject();
        auto shape = outlines.value(definition["fontCharacter"].toString());
        auto ink = QColor(definition["fontColor"].toString());
        if (!ink.isValid()) ink = colors().text;
        // Seti's pale default is unreadable on GitHub Light; only the unknown fallback changes ink.
        if (id == "_default_light") ink = QColor("#57606a");
        if (shape.isEmpty()) {
            shape.moveTo(-7, -10); shape.lineTo(3, -10); shape.lineTo(8, -5); shape.lineTo(8, 10);
            shape.lineTo(-7, 10); shape.closeSubpath();
        }
        auto result = QIcon(new FileIconEngine(shape, ink));
        // id comes only from the finite embedded theme, never from a filename.
        icons.insert(id, result); return result;
    }
};
Seti &seti() { static Seti data; return data; }
} // namespace
QString FileIconProvider::definition(const QString &path) { return seti().resolve(path); }
QIcon FileIconProvider::file(const QString &path) { return seti().get(definition(path)); }
bool FileIconProvider::resourcesAvailable() { return seti().outlines.size() > 100 && !seti().theme.isEmpty(); }
QIcon FileIconProvider::directory() {
    // Two vector engines total, one per palette, with a distinct expanded state.
    static QIcon darkClosed, lightClosed;
    auto &cached = lightTheme() ? lightClosed : darkClosed;
    if (!cached.isNull()) return cached;
    QPainterPath closed; closed.moveTo(-10,-7); closed.lineTo(-3,-7); closed.lineTo(0,-4);
    closed.lineTo(10,-4); closed.lineTo(10,8); closed.lineTo(-10,8); closed.closeSubpath();
    QPainterPath opened; opened.moveTo(-10,-7); opened.lineTo(-3,-7); opened.lineTo(0,-4);
    opened.lineTo(9,-4); opened.lineTo(9,-1); opened.lineTo(-7,-1); opened.lineTo(-10,8);
    opened.lineTo(7,8); opened.lineTo(11,-1); opened.lineTo(-7,-1); opened.lineTo(-10,8); opened.closeSubpath();
    // Vector directory outlines use existing theme ink and explicit QIcon::On for expanded trees.
    QPainterPathStroker stroke; stroke.setWidth(1.6); stroke.setJoinStyle(Qt::RoundJoin);
    cached = QIcon(new FileIconEngine(stroke.createStroke(closed), colors().muted, stroke.createStroke(opened)));
    return cached;
}
} // namespace rs
