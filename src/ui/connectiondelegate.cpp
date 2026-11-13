#include "connectiondelegate.h"
#include "theme.h"

namespace rs {
namespace {
struct BadgeColors { QColor ink, fill; };
BadgeColors environmentColors(const QString &name) {
    const auto value = name.trimmed().toLower();
    const bool light = lightTheme();
    if (value == "生产" || value == "production" || value == "prod")
        return {QColor(light ? "#9f1239" : "#ffb0b8"), QColor(light ? "#ffe3e7" : "#5c2d3a")};
    if (value == "测试" || value == "test" || value == "staging")
        return {QColor(light ? "#845400" : "#ffd69a"), QColor(light ? "#fff0cf" : "#4d3c25")};
    if (value == "个人" || value == "personal")
        return {QColor(light ? "#6135a5" : "#d6bbff"), QColor(light ? "#ede5ff" : "#3e3459")};
    return {QColor(light ? "#15558c" : "#a5dfff"), QColor(light ? "#e1f0ff" : "#273f51")};
}
} // namespace

void ConnectionDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                               const QModelIndex &index) const {
    if (index.parent().isValid() || (index.column() != 3 && index.column() != 4)) {
        DensityDelegate::paint(painter, option, index);
        return;
    }
    QStyleOptionViewItem cell(option);
    initStyleOption(&cell, index);
    const auto text = cell.text;
    // Let Qt retain the row selection, hover and focus under the decoration.
    cell.text.clear();
    cell.icon = {};
    cell.features &= ~(QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration);
    const auto style = cell.widget ? cell.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &cell, painter, cell.widget);
    if (text.isEmpty()) return;

    painter->save();
    painter->setClipRect(option.rect, Qt::IntersectClip);
    painter->setRenderHint(QPainter::Antialiasing);
    const auto available = option.rect.adjusted(uiPx(8), uiPx(3), -uiPx(8), -uiPx(3));
    if (available.isEmpty()) { painter->restore(); return; }
    if (index.column() == 3) {
        const auto badge = environmentColors(text);
        auto font = cell.font;
        font.setWeight(QFont::DemiBold);
        painter->setFont(font);
        const QFontMetrics metrics(font);
        const int width = qMin(available.width(), metrics.horizontalAdvance(text) + uiPx(18));
        const int height = qMin(available.height(), qMax(uiPx(24), metrics.height() + uiPx(6)));
        const QRectF bounds(available.center().x() - width / 2.0,
                            available.center().y() - height / 2.0, width, height);
        auto edge = badge.ink;
        edge.setAlphaF(.32f);
        painter->setPen(QPen(edge, qMax(1, uiPx(1))));
        painter->setBrush(badge.fill);
        painter->drawRoundedRect(bounds.adjusted(.5, .5, -.5, -.5), uiPx(5), uiPx(5));
        painter->setPen(badge.ink);
        painter->drawText(bounds.adjusted(uiPx(6), 0, -uiPx(6), 0), Qt::AlignCenter,
                          metrics.elidedText(text, Qt::ElideRight, qMax(0, width - uiPx(12))));
    } else {
        const auto state = ConnectionState(index.data(ConnectionStateRole).toInt());
        const auto ink = state == ConnectionState::Connected ? (lightTheme() ? QColor("#176c31") : colors().green)
                       : state == ConnectionState::Connecting ? QColor(lightTheme() ? "#805500" : "#ffb86c")
                       : QColor(lightTheme() ? "#4e596c" : "#b6bfd3");
        const int size = qMin(uiPx(16), available.height());
        const QPointF origin(available.left(), available.center().y() - size / 2.0);
        painter->save();
        painter->translate(origin);
        painter->scale(size / 16.0, size / 16.0);
        painter->setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(QRectF(1.5, 1.5, 13, 13));
        QPainterPath mark;
        if (state == ConnectionState::Connected) {
            mark.moveTo(4.5, 8); mark.lineTo(7, 10.5); mark.lineTo(11.5, 5.5);
        } else if (state == ConnectionState::Connecting) {
            mark.moveTo(8, 4); mark.lineTo(8, 8); mark.lineTo(10.5, 9.5);
        } else {
            mark.moveTo(4, 12); mark.lineTo(12, 4);
        }
        painter->drawPath(mark);
        painter->restore();
        painter->setFont(cell.font);
        painter->setPen(ink);
        const auto label = available.adjusted(size + uiPx(6), 0, 0, 0);
        painter->drawText(label, Qt::AlignLeft | Qt::AlignVCenter,
                          cell.fontMetrics.elidedText(text, Qt::ElideRight, qMax(0, label.width())));
    }
    painter->restore();
}
} // namespace rs
