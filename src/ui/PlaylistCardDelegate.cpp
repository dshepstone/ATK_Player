#include "ui/PlaylistCardDelegate.h"

#include "project/Project.h"
#include "ui/IconProvider.h"
#include "ui/ReviewHeaderBar.h"
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

int PlaylistCardDelegate::numberGutterWidth(const QFont& font)
{
    QFont bold = font;
    bold.setBold(true);
    return std::max(26, QFontMetrics(bold).horizontalAdvance(QStringLiteral("000")) + 10);
}

QSize PlaylistCardDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const bool heading = !index.data(playlistrole::GroupHeading).toString().isEmpty();
    return QSize(option.rect.width() > 0 ? option.rect.width() : 240, kCardHeight + (heading ? kHeadingHeight : 0));
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

    // A category group starts here: its name and clip count, with a rule,
    // above the card.
    QRect cardRect = option.rect;
    if (const QString heading = index.data(playlistrole::GroupHeading).toString(); !heading.isEmpty()) {
        const QRect band(option.rect.left() + 8, option.rect.top(), option.rect.width() - 16, kHeadingHeight);
        cardRect.setTop(option.rect.top() + kHeadingHeight);
        QFont headingFont = option.font;
        headingFont.setPixelSize(11);
        headingFont.setBold(true);
        headingFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
        const QFontMetrics metrics(headingFont);
        const int count = index.data(playlistrole::GroupCount).toInt();
        const QString countText = count > 0 ? QStringLiteral("  %1").arg(count) : QString();
        const int countWidth = countText.isEmpty() ? 0 : metrics.horizontalAdvance(countText);
        const QString name = metrics.elidedText(heading.toUpper(), Qt::ElideRight, band.width() - countWidth - 24);
        const int nameWidth = metrics.horizontalAdvance(name);
        const int baseline = band.top() + (band.height() + metrics.ascent() - metrics.descent()) / 2 + 2;
        painter->setFont(headingFont);
        painter->setPen(theme::textPrimary());
        painter->drawText(QPoint(band.left(), baseline), name);
        painter->setPen(theme::textSecondary());
        painter->drawText(QPoint(band.left() + nameWidth, baseline), countText);
        const int ruleLeft = band.left() + nameWidth + countWidth + 8;
        if (ruleLeft < band.right()) {
            painter->setPen(QPen(QColor(255, 255, 255, 30), 1.0));
            const qreal y = baseline - metrics.ascent() / 2.0 + 0.5;
            painter->drawLine(QPointF(ruleLeft, y), QPointF(band.right(), y));
        }
    }

    // Card.
    const QRectF card = QRectF(cardRect).adjusted(4, 2, -4, -2);
    QPainterPath shape;
    shape.addRoundedRect(card, theme::metrics::radiusMedium, theme::metrics::radiusMedium);
    painter->fillPath(shape, selected ? theme::cardSelected() : hovered ? theme::cardHover() : theme::cardBackground());
    // Order number, in a gutter down the card's left edge: darker than the
    // card, and the accent colour for the clip in the viewer.
    QFont numberFont = option.font;
    numberFont.setBold(true);
    const QRectF numberRect(card.left(), card.top(), numberGutterWidth(numberFont), card.height());
    painter->save();
    painter->setClipPath(shape);
    painter->fillRect(numberRect, active ? theme::accent() : QColor(0, 0, 0, 70));
    painter->restore();
    painter->setFont(numberFont);
    painter->setPen(active ? QColor(255, 255, 255) : unavailable ? theme::textDisabled() : theme::textPrimary());
    painter->drawText(numberRect, Qt::AlignCenter, QString::number(index.data(playlistrole::Order).toInt()));

    // Thumbnail: 16:9, letterboxed, with the frame count over its corner.
    const qreal thumbHeight = card.height() - 12;
    const qreal thumbWidth = std::min(thumbHeight * 16.0 / 9.0, std::max<qreal>(48.0, (card.width() - 26) * 0.45));
    const QRectF thumb(numberRect.right() + 6, card.top() + 6, thumbWidth, thumbHeight);
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
    if (drawn > 0) badge.rx() += drawBadge(painter, badge, QStringLiteral("pencil"), drawn, detailFont);

    // Review status, right-aligned on the badge line: a dot and its name, so
    // it never relies on colour alone. "Not reviewed" is the default: no chip.
    const auto status = project::ClipStatus(index.data(playlistrole::Status).toInt());
    qreal statusLeft = text.right();
    if (status != project::ClipStatus::NotReviewed) {
        const QString label = clipStatusLabel(status);
        const QFontMetrics metrics(detailFont);
        const qreal width = metrics.horizontalAdvance(label) + 18;
        const QRectF chip(std::max<qreal>(badge.x(), text.right() - width), text.bottom() - 13, width, 15);
        statusLeft = chip.left();
        QColor fill = clipStatusColor(status);
        fill.setAlpha(45);
        QPainterPath pill;
        pill.addRoundedRect(chip, 7.5, 7.5);
        painter->fillPath(pill, fill);
        painter->setPen(Qt::NoPen);
        painter->setBrush(clipStatusColor(status));
        painter->drawEllipse(QPointF(chip.left() + 7.5, chip.center().y()), 3.0, 3.0);
        painter->setFont(detailFont);
        painter->setPen(theme::textPrimary());
        painter->drawText(chip.adjusted(13, 0, -3, 0), Qt::AlignLeft | Qt::AlignVCenter,
                          metrics.elidedText(label, Qt::ElideRight, int(chip.width() - 16)));
    }

    // Category, between the badges and the status: a tag and its name,
    // shortened to the room there is.
    const QString category = index.data(playlistrole::Category).toString();
    const qreal room = statusLeft - 6 - badge.x();
    if (!category.isEmpty() && room > 30) {
        const QFontMetrics metrics(detailFont);
        const int iconSize = 11;
        const qreal width = std::min<qreal>(room, metrics.horizontalAdvance(category) + iconSize + 12);
        const QRectF chip(badge.x(), text.bottom() - 13, width, 15);
        QPainterPath pill;
        pill.addRoundedRect(chip, 4, 4);
        painter->fillPath(pill, QColor(255, 255, 255, 22));
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(QPointF(chip.left() + 4, chip.center().y() - iconSize / 2.0),
                            icons::pixmap(QStringLiteral("tag"), QSize(iconSize, iconSize), theme::textSecondary(), dpr));
        painter->setFont(detailFont);
        painter->setPen(theme::textPrimary());
        painter->drawText(chip.adjusted(iconSize + 7, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter,
                          metrics.elidedText(category, Qt::ElideRight, int(chip.width() - iconSize - 11)));
    }

    if (option.state & QStyle::State_HasFocus && !(option.state & QStyle::State_Selected)) {
        painter->setPen(QPen(theme::accent(), 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), theme::metrics::radiusMedium,
                                 theme::metrics::radiusMedium);
    }
    painter->restore();
}

} // namespace atk::ui
