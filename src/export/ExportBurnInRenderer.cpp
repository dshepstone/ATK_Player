#include "export/ExportBurnInRenderer.h"
#include "timeline/Bookmark.h"
#include <QFontMetrics>
#include <QPainter>
#include <algorithm>

namespace atk::exporter {

QString ExportBurnInRenderer::frameNumberText(qint64 frame)
{
    return QStringLiteral("FRAME %1").arg(frame + 1);
}

QString ExportBurnInRenderer::bookmarkTitle(const ExportBookmark& bookmark)
{
    return bookmark.name.trimmed().isEmpty() ? QStringLiteral("Bookmark") : bookmark.name.trimmed();
}

QString ExportBurnInRenderer::bookmarkFrameText(const ExportBookmark& bookmark)
{
    return bookmark.range && bookmark.endFrame > bookmark.startFrame
        ? QStringLiteral("Frames %1–%2").arg(bookmark.startFrame + 1).arg(bookmark.endFrame + 1)
        : QStringLiteral("Frame %1").arg(bookmark.startFrame + 1);
}

QVector<ExportBookmark> ExportBurnInRenderer::activeBookmarks(const ExportSpec& spec, qint64 frame)
{
    QVector<ExportBookmark> active;
    for (const auto& bookmark : spec.bookmarks) {
        const bool applies = bookmark.range
            ? frame >= bookmark.startFrame && frame <= bookmark.endFrame
            : frame == bookmark.startFrame;
        if (applies) active.push_back(bookmark);
    }
    std::sort(active.begin(), active.end(), [](const auto& left, const auto& right) {
        return left.startFrame != right.startFrame ? left.startFrame < right.startFrame : left.id < right.id;
    });
    active.erase(std::unique(active.begin(), active.end(), [](const auto& a, const auto& b) {
        return a.id == b.id;
    }), active.end());
    return active;
}

QRect ExportBurnInRenderer::frameNumberBounds(const QSize& size)
{
    const int margin = std::max(8, size.height() / 54);
    const int availableWidth = std::max(1, size.width() - margin * 2);
    const int availableHeight = std::max(1, size.height() - margin * 2);
    return {margin, margin, std::min(availableWidth, std::max(130, size.height() / 5)),
            std::min(availableHeight, std::max(34, size.height() / 18))};
}

QRect ExportBurnInRenderer::bookmarkBounds(const QSize& size)
{
    const int margin = std::max(8, size.height() / 54);
    return {margin, size.height() * 2 / 3,
            std::min(std::max(1, size.width() - margin * 2), std::max(280, size.width() * 2 / 5)),
            std::max(1, size.height() / 3 - margin)};
}

void ExportBurnInRenderer::apply(QImage& image, const ExportSpec& spec, qint64 frame)
{
    if (!spec.burnIns.enabled() || image.isNull()) return;
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const int unit = std::max(4, image.height() / 180);
    QFont font = painter.font();
    font.setPixelSize(std::max(14, image.height() / 42));
    painter.setFont(font);
    if (spec.burnIns.frameNumber) {
        const QRect badge = frameNumberBounds(image.size());
        painter.setPen(Qt::NoPen); painter.setBrush(QColor(0, 0, 0, 185));
        painter.drawRoundedRect(badge, unit * 2, unit * 2);
        painter.setPen(Qt::white);
        painter.drawText(badge.adjusted(unit * 3, 0, -unit * 3, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         frameNumberText(frame));
    }
    if (!spec.burnIns.bookmarkLabels && !spec.burnIns.bookmarkNotes) return;
    const auto active = activeBookmarks(spec, frame);
    QRect safe = bookmarkBounds(image.size());
    int bottom = safe.bottom();
    int hidden = 0;
    for (auto it = active.crbegin(); it != active.crend(); ++it) {
        QStringList lines;
        if (spec.burnIns.bookmarkLabels)
            lines << bookmarkTitle(*it) << bookmarkFrameText(*it);
        if (spec.burnIns.bookmarkNotes && !it->note.trimmed().isEmpty()) lines << it->note.trimmed();
        if (lines.isEmpty()) continue;
        const int cardHeight = std::min(safe.height(), std::max(unit * 12,
            QFontMetrics(font).boundingRect(QRect(0, 0, safe.width() - unit * 8, safe.height()),
                Qt::TextWordWrap, lines.join(QLatin1Char('\n'))).height() + unit * 6));
        if (bottom - cardHeight + 1 < safe.top()) { ++hidden; continue; }
        QRect card(safe.left(), bottom - cardHeight + 1, safe.width(), cardHeight);
        painter.setPen(Qt::NoPen); painter.setBrush(QColor(0, 0, 0, 190));
        painter.drawRoundedRect(card, unit * 2, unit * 2);
        QColor accent = timeline::bookmarkColor(it->colorIndex);
        if (!accent.isValid()) accent = QColor(150, 160, 175);
        painter.fillRect(QRect(card.left(), card.top() + unit * 2, unit, card.height() - unit * 4), accent);
        painter.setPen(Qt::white);
        painter.drawText(card.adjusted(unit * 4, unit * 2, -unit * 3, -unit * 2),
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                         lines.join(QLatin1Char('\n')));
        bottom = card.top() - unit * 2;
    }
    if (hidden > 0) {
        painter.setPen(Qt::white);
        painter.drawText(QRect(safe.left(), safe.top(), safe.width(), unit * 5), Qt::AlignLeft,
                         QStringLiteral("+%1 more").arg(hidden));
    }
}

} // namespace atk::exporter
