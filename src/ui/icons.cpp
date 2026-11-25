#include "icons.h"
#include "theme.h"
#include <QIconEngine>
#include <cmath>
namespace rs {
namespace {
class VectorIconEngine final : public QIconEngine {
  public:
    explicit VectorIconEngine(Icon glyph,bool resourceMenu=false) : glyph(glyph),resourceMenu(resourceMenu) {}
    QIconEngine *clone() const override { return new VectorIconEngine(glyph,resourceMenu); }
    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State state) override {
        const auto c = colors();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const qreal scale = glyph == Icon::Grip
            ? qMin(rect.width() / 8.0, rect.height() / 16.0)
            : qMin(rect.width(), rect.height()) / 24.0;
        p->translate(rect.center().x() - 12 * scale + .5, rect.center().y() - 12 * scale + .5);
        p->scale(scale, scale);
        QColor ink = mode == QIcon::Disabled ? c.muted
                     : state == QIcon::On && glyph != Icon::PanelMaximize ? c.accent
                     : resourceMenu ? QColor(lightTheme()?"#1f2328":"#f8f8f2") : c.text;
        if (mode == QIcon::Disabled)
            ink.setAlphaF(.5);
        p->setPen(QPen(ink, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        auto line = [&](qreal a, qreal b, qreal x, qreal y) { p->drawLine(QPointF(a,b), QPointF(x,y)); };
        auto path = [&](std::initializer_list<QPointF> points) {
            QPainterPath shape;
            bool first = true;
            for (auto point : points) { if (first) shape.moveTo(point); else shape.lineTo(point); first = false; }
            p->drawPath(shape);
        };
        auto plus = [&](qreal x, qreal y) { line(x-3,y,x+3,y); line(x,y-3,x,y+3); };
        switch (glyph) {
        case Icon::Add: line(3,12,21,12); line(12,3,12,21); break;
        case Icon::Save:
            path({{3,3},{17,3},{21,7},{21,21},{3,21},{3,3}});
            p->drawRect(QRectF(7,3,9,6));p->drawRect(QRectF(7,14,10,7));break;
        case Icon::More:
            for (int x : {5,12,19}) { p->setBrush(ink); p->drawEllipse(QPointF(x,12),1,1); } break;
        case Icon::Grip:
            p->setPen(Qt::NoPen);
            p->setBrush(mode == QIcon::Disabled ? ink : c.muted);
            for (qreal y : {7.2, 10.4, 13.6, 16.8})
                for (qreal x : {10.35, 13.65})
                    p->drawEllipse(QPointF(x, y), 0.9, 0.9);
            break;
        case Icon::NewFile:
            path({{13,3},{5,3},{5,21},{19,21},{19,9},{13,3},{13,9},{19,9}}); plus(12,15); break;
        case Icon::NewFolder:
            path({{3,7},{3,4},{9,4},{12,7},{21,7},{21,20},{3,20},{3,7}}); plus(12,13); break;
        case Icon::Upload:
            path({{3,15},{3,21},{21,21},{21,15}}); line(12,3,12,15); path({{7,8},{12,3},{17,8}}); break;
        case Icon::DownloadFile:
            path({{13,3},{5,3},{5,21},{19,21},{19,9},{13,3},{13,9},{19,9}});
            line(12,12,12,18); path({{9,15},{12,18},{15,15}}); break;
        case Icon::DownloadFolder:
            path({{3,7},{3,4},{9,4},{12,7},{21,7},{21,20},{3,20},{3,7}});
            line(12,10,12,17); path({{9,14},{12,17},{15,14}}); break;
        case Icon::Rename:
            path({{9,4},{12,4},{15,4}}); line(12,4,12,20); line(9,20,15,20);
            path({{7,8},{3,8},{3,16},{7,16}}); path({{17,8},{21,8},{21,16},{17,16}}); break;
        case Icon::Info:
            p->drawEllipse(QPointF(12,12),9,9); line(12,11,12,17); line(12,7,12,7.2); break;
        case Icon::Eye: {
            QPainterPath eye;eye.moveTo(2,12);eye.cubicTo(7,3,17,3,22,12);eye.cubicTo(17,21,7,21,2,12);
            p->drawPath(eye);p->drawEllipse(QPointF(12,12),3,3);break;
        }
        case Icon::CalendarClock:
            path({{10,21},{3,21},{3,5},{21,5},{21,10}});line(7,2,7,8);line(17,2,17,8);line(3,10,12,10);
            p->drawEllipse(QPointF(17,17),5,5);path({{17,14},{17,17},{19,18}});break;
        case Icon::Cut:
            p->drawEllipse(QPointF(5,6),3,3);p->drawEllipse(QPointF(5,18),3,3);
            line(7.5,7.5,21,21);line(7.5,16.5,21,3);break;
        case Icon::Copy:
            p->drawRoundedRect(QRectF(8,8,13,13),1,1);path({{5,16},{3,16},{3,3},{16,3},{16,5}});break;
        case Icon::Paste:
            path({{8,5},{4,5},{4,21},{20,21},{20,5},{16,5}});p->drawRoundedRect(QRectF(8,2,8,6),1,1);break;
        case Icon::Undo: {
            path({{8,3},{3,8},{8,13}});QPainterPath undo;undo.moveTo(3,8);undo.lineTo(14,8);
            undo.cubicTo(23,8,23,21,14,21);p->drawPath(undo);break;
        }
        case Icon::BookmarkPlus:
            path({{19,12},{19,3},{5,3},{5,21},{12,17}});plus(18,18);break;
        case Icon::Back: path({{13,5},{6,12},{13,19}}); line(6,12,21,12); break;
        case Icon::Forward: path({{11,5},{18,12},{11,19}}); line(3,12,18,12); break;
        case Icon::ChevronRight: path({{9,5},{16,12},{9,19}}); break;
        case Icon::ChevronDown: path({{5,9},{12,16},{19,9}}); break;
        case Icon::ReplaceAll:
            path({{3,6},{6,3},{9,6}});path({{6,3},{6,14},{9,17},{17,17}});path({{14,14},{17,17},{14,20}});
            line(11,4,21,4);line(11,8,19,8);line(11,12,17,12);break;
        case Icon::Up: path({{5,13},{12,6},{19,13}}); line(12,6,12,21); break;
        case Icon::Down: path({{5,11},{12,18},{19,11}}); line(12,3,12,18); break;
        case Icon::Refresh:
            p->drawArc(QRectF(4,4,16,16), 35*16, 285*16); path({{20,3},{20,9},{14,9}}); break;
        case Icon::Home:
            path({{2,11},{12,3},{22,11}}); path({{5,10},{5,21},{10,21},{10,14},{14,14},{14,21},{19,21},{19,10}}); break;
        case Icon::Path:
            path({{8,5},{2,12},{8,19}}); path({{16,5},{22,12},{16,19}}); line(14,3,10,21); break;
        case Icon::SplitHorizontal:
        case Icon::SplitVertical:
            p->drawRoundedRect(QRectF(3,4,18,16),1,1);
            if (glyph == Icon::SplitHorizontal) line(12,4,12,20); else line(3,12,21,12); break;
        case Icon::Merge:
            p->drawRoundedRect(QRectF(2,4,20,16),1,1);
            path({{7,9},{10,12},{7,15}}); path({{17,9},{14,12},{17,15}}); break;
        case Icon::Terminal:
            p->drawRoundedRect(QRectF(2,4,20,16),1,1); path({{6,9},{9,12},{6,15}}); line(12,16,17,16); break;
        case Icon::Stop: p->drawRoundedRect(QRectF(5,5,14,14),1,1); break;
        case Icon::Collapse: path({{5,8},{12,15},{19,8}}); break;
        case Icon::Transfers:
            line(7,3,7,21); path({{3,7},{7,3},{11,7}});
            line(17,3,17,21); path({{13,17},{17,21},{21,17}}); break;
        case Icon::Settings: {
            QPainterPath gear;
            for (int i=0;i<32;++i) {
                qreal angle = i * 2 * 3.141592653589793 / 32;
                qreal radius = (i%4==0 || i%4==3) ? 10 : 7.8;
                QPointF v(12 + radius * std::cos(angle), 12 + radius * std::sin(angle));
                if (i==0) gear.moveTo(v); else gear.lineTo(v);
            }
            gear.closeSubpath(); p->drawPath(gear); p->drawEllipse(QPointF(12,12),3,3); break;
        }
        case Icon::Status:
            p->drawRoundedRect(QRectF(3,3,18,18),2,2); path({{5,13},{8,13},{10,7},{14,17},{16,11},{19,11}}); break;
        case Icon::Explorer:
            path({{8,3},{3,3},{3,17},{6,17}}); p->drawRoundedRect(QRectF(7,7,14,14),1,1);
            line(10,11,18,11); line(10,15,18,15); break;
        case Icon::Close: line(3,3,21,21); line(21,3,3,21); break;
        case Icon::Trash:
            path({{5,7},{6,21},{18,21},{19,7}}); line(3,6,21,6);
            path({{9,6},{9,3},{15,3},{15,6}}); line(10,10,10,17); line(14,10,14,17); break;
        case Icon::PanelMaximize:
            path({{3,9},{3,3},{9,3}}); path({{15,3},{21,3},{21,9}});
            path({{21,15},{21,21},{15,21}}); path({{9,21},{3,21},{3,15}}); break;
        case Icon::PanelRestore:
            path({{3,9},{9,9},{9,3}}); path({{15,3},{15,9},{21,9}});
            path({{21,15},{15,15},{15,21}}); path({{9,21},{9,15},{3,15}}); break;
        case Icon::Return:
            path({{9,5},{3,11},{9,17}}); path({{3,11},{16,11},{20,15},{20,20}}); break;
        case Icon::Expand:
            path({{14,3},{21,3},{21,10}}); line(21,3,10,14); path({{10,3},{3,3},{3,21},{21,21},{21,14}}); break;
        case Icon::Restore:
            path({{8,8},{8,3},{21,3},{21,16},{16,16}}); p->drawRect(QRectF(3,8,13,13)); break;
        case Icon::Replace:
            path({{4,7},{7,4},{10,7}}); path({{7,4},{7,16},{10,19},{20,19}});
            path({{17,16},{20,19},{17,22}}); line(12,5,20,5); line(12,9,17,9); break;
        case Icon::Import:
            path({{3,15},{3,21},{21,21},{21,15}}); line(12,2,12,15); path({{7,10},{12,15},{17,10}}); break;
        case Icon::Lock:
            p->drawRoundedRect(QRectF(5,10,14,11),2,2);p->drawArc(QRectF(8,2,8,14),0,180*16);line(12,14,12,17);break;
        case Icon::Connection:
            p->drawRoundedRect(QRectF(3,3,18,7),1,1); p->drawRoundedRect(QRectF(3,14,18,7),1,1);
            line(6,6.5,7,6.5); line(6,17.5,7,17.5); break;
        }
        p->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal dpr) override {
        QPixmap result(size * dpr);
        result.setDevicePixelRatio(dpr);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }
  private:
    Icon glyph;
    bool resourceMenu;
};
}
QIcon icon(Icon glyph) { return QIcon(new VectorIconEngine(glyph)); }
QIcon resourceMenuIcon(Icon glyph) { return QIcon(new VectorIconEngine(glyph,true)); }
void replaceTabButton(QTabBar *bar, int index, QTabBar::ButtonPosition side, QWidget *replacement) {
    QPointer<QWidget> previous = bar->tabButton(index, side);
    bar->setTabButton(index, side, replacement);
    // Qt only hides the replaced widget; removeTab can no longer reach it.
    if (previous && previous != replacement)
        previous->deleteLater();
}
QToolButton *iconButton(Icon glyph, const QString &name, QWidget *parent, IconSize size, const QString &shortcut) {
    auto b = new QToolButton(parent);
    b->setIcon(icon(glyph));
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setAccessibleName(name);
    b->setToolTip(shortcut.isEmpty() ? name : name + " · " + shortcut);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::StrongFocus);
    b->setProperty("iconButton", true);
    const int hit = size == IconSize::Activity ? 40 : size == IconSize::TabClose ? 24 : 28;
    const int image = size == IconSize::Activity ? 22 : size == IconSize::TabClose ? 16 : 18;
    b->setProperty("activity", size == IconSize::Activity);
    uiIconSize(b,image,image);
    uiFixedSize(b,hit,hit);
    return b;
}
} // namespace rs
