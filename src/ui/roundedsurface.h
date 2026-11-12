#pragma once
#include <QtWidgets>
namespace rs {
// Radius describes the outside contour in logical pixels; the stroke is inside it.
struct RoundedGeometry {
    QPainterPath outside, stroke;
    qreal width = 0;
};
RoundedGeometry roundedGeometry(QRectF bounds, qreal radius, qreal penWidth);
void paintRoundedSurface(QPainter &, QRectF, qreal radius, QColor fill, QColor border,
                         qreal penWidth);
enum class RoundedRole { Card, Transfer, Find, Process, Path, Menu, ComboPopup };
// One transparent, input-pass-through child per opted-in surface. No polling/cache.
void installRoundedSurface(QWidget *, RoundedRole);
void installRoundedMenus();
void prepareRoundedPopup(QWidget *);
// An alpha contour's conservative raster envelope, used only as a native fallback.
QRegion roundedPopupRegion(QRect bounds, qreal radius, qreal dpr);
}
