#pragma once

#include <QStyledItemDelegate>

namespace atk::ui {

/// Item data the playlist card reads. Qt::DisplayRole is the clip's name and
/// Qt::UserRole its source id, as before.
namespace playlistrole {
enum : int {
    Order = Qt::UserRole + 1, ///< 1-based position, as shown
    Subtitle,                 ///< resolution, rate and length, or the clip's state
    Thumbnail,                ///< QImage; empty until the thumbnail arrives
    Active,                   ///< bool: the clip loaded in the viewer
    NoteCount,                ///< int: notes written on the clip
    DrawnFrames,              ///< int: frames with drawings
    Unavailable,              ///< bool: missing or unreadable
    FrameCount,               ///< qint64: frames in the clip; 0 when unknown
};
} // namespace playlistrole

/// Paints one playlist entry as a card: number, a 16:9 thumbnail with the
/// frame count over it, the name, a details line, and small badges for notes
/// and drawn frames. The loaded clip gets an accent bar, the selected one the
/// selection fill. Colours come from the theme.
class PlaylistCardDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    static constexpr int kCardHeight = 74;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

} // namespace atk::ui
