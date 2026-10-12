#include "ui/PlaylistCardDelegate.h"

#include "ui/IconProvider.h"
#include "ui/Theme.h"

#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace atk::ui {
namespace {

/// A small icon followed by a number, in the secondary text colour. Returns
/// the width used, so badges can sit side by side.
int drawBadge(QPainter* painter, const QPoint& at, const QString& iconName, int count, const QFont& font)
{
    const int iconSize = 12;
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const QPixmap icon = icons::pixmap(iconName, QSize(iconSize, iconSize), theme::textSecondary(), dpr);
    painter->drawPixmap(QPoint(at.x(), at.y() - iconSize + 2), icon);
    painter->setFont(font);
    painter->setPen(theme::textSecondary());
    const QString text = QString::number(count);
    painter->drawText(QPoint(at.x() + iconSize + 3, at.y()), text);
    return iconSize + 3 + QFontMetrics(font).horizontalAdvance(text) + 10;
}

} // namespace

QSize PlaylistCardDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    return QSize(option.rect.width() > 0 ? option.rect.width() : 240, kCardHeight);
}

void PlaylistCardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool active = index.data(playlistrole::Active).toBool();
    const bool unavailable = index.data(playlistrole::Unavailable).toBool();

    // Card.
    const QRectF card = QRectF(option.rect).adjusted(4, 2, -4, -2);
    QPainterPath shape;
    shape.addRoundedRect(card, theme::metrics::radiusMedium, theme::metrics::radiusMedium);
    painter->fillPath(shape, selected ? theme::cardSelected() : hovered ? theme::cardHover() : theme::cardBackground());
    if (active) {
        painter->save();
        painter->setClipPath(shape);
        painter->fillRect(QRectF(card.left(), card.top(), 3, card.height()), theme::accent());
        painter->restore();
    }

    // Order number.
    QFont numberFont = option.font;
    numberFont.setBold(true);
    painter->setFont(numberFont);
    painter->setPen(active ? theme::accent() : theme::textSecondary());
    const QRectF numberRect(card.left() + 4, card.top(), 20, card.height());
    painter->drawText(numberRect, Qt::AlignCenter, QString::number(index.data(playlistrole::Order).toInt()));

    // Thumbnail: 16:9, letterboxed, with the frame count over its corner.
    const qreal thumbHeight = card.height() - 12;
    const qreal thumbWidth = std::min(thumbHeight * 16.0 / 9.0, std::max<qreal>(48.0, (card.width() - 26) * 0.45));
    const QRectF thumb(numberRect.right() + 2, card.top() + 6, thumbWidth, thumbHeight);
    QPainterPath thumbShape;
    thumbShape.addRoundedRect(thumb, theme::metrics::radiusSmall, theme::metrics::radiusSmall);
    painter->fillPath(thumbShape, QColor(0, 0, 0));
    const QImage image = index.data(playlistrole::Thumbnail).value<QImage>();
    painter->save();
    painter->setClipPath(thumbShape);
    if (!image.isNull()) {
        const QSizeF fitted = QSizeF(image.size()).scaled(thumb.size(), Qt::KeepAspectRatio);
        const QRectF target(thumb.center() - QPointF(fitted.width() / 2, fitted.height() / 2), fitted);
        if (unavailable) painter->setOpacity(0.35);
        painter->drawImage(target, image);
        painter->setOpacity(1.0);
    } else {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const QSize iconSize(18, 18);
        const QPixmap film = icons::pixmap(unavailable ? QStringLiteral("x") : QStringLiteral("film"), iconSize,
                                           theme::textDisabled(), dpr);
        painter->drawPixmap(QPointF(thumb.center().x() - iconSize.width() / 2.0, thumb.center().y() - iconSize.height() / 2.0),
                            film);
    }
    const qint64 frames = index.data(playlistrole::FrameCount).toLongLong();
    if (frames > 0) {
        QFont small = option.font;
        small.setPixelSize(10);
        small.setBold(true);
        const QString text = QString::number(frames);
        const QFontMetrics metrics(small);
        const QRectF chip(thumb.right() - metrics.horizontalAdvance(text) - 8, thumb.bottom() - 15,
                          metrics.horizontalAdvance(text) + 6, 13);
        painter->fillRect(chip, QColor(0, 0, 0, 150));
        painter->setFont(small);
        painter->setPen(QColor(255, 255, 255, 220));
        painter->drawText(chip, Qt::AlignCenter, text);
    }
    painter->restore();

    // Name, details and badges.
    const QRectF text(thumb.right() + 8, card.top() + 7, card.right() - thumb.right() - 14, card.height() - 14);
    QFont titleFont = option.font;
    titleFont.setBold(true);
    const QFontMetrics titleMetrics(titleFont);
    painter->setFont(titleFont);
    painter->setPen(unavailable ? theme::textDisabled() : theme::textPrimary());
    const QString title = titleMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, int(text.width()));
    painter->drawText(QRectF(text.left(), text.top(), text.width(), titleMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                      title);

    QFont detailFont = option.font;
    detailFont.setPixelSize(11);
    const QFontMetrics detailMetrics(detailFont);
    painter->setFont(detailFont);
    painter->setPen(unavailable ? theme::statusNeedsChanges() : theme::textSecondary());
    const QString subtitle =
        detailMetrics.elidedText(index.data(playlistrole::Subtitle).toString(), Qt::ElideRight, int(text.width()));
    painter->drawText(QRectF(text.left(), text.top() + titleMetrics.height() + 1, text.width(), detailMetrics.height()),
                      Qt::AlignLeft | Qt::AlignVCenter, subtitle);

    const int notes = index.data(playlistrole::NoteCount).toInt();
    const int drawn = index.data(playlistrole::DrawnFrames).toInt();
    QPoint badge(int(text.left()), int(text.bottom()));
    if (notes > 0) badge.rx() += drawBadge(painter, badge, QStringLiteral("message-square"), notes, detailFont);
    if (drawn > 0) drawBadge(painter, badge, QStringLiteral("pencil"), drawn, detailFont);

    if (option.state & QStyle::State_HasFocus && !(option.state & QStyle::State_Selected)) {
        painter->setPen(QPen(theme::accent(), 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), theme::metrics::radiusMedium,
                                 theme::metrics::radiusMedium);
    }
    painter->restore();
}

} // namespace atk::ui
