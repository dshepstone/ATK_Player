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
    Status,                   ///< int (project::ClipStatus)
};
} // namespace playlistrole

/// Paints one playlist entry as a card: a numbered gutter, a 16:9 thumbnail
/// with the frame count over it, the name, a details line, and small badges
/// for notes and drawn frames. The loaded clip's gutter takes the accent
/// colour, like SyncSketch; the selected card gets the selection fill.
/// Colours come from the theme.
class PlaylistCardDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    static constexpr int kCardHeight = 74;

    /// Width of the numbered gutter for `font`: room for three digits.
    static int numberGutterWidth(const QFont& font);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

} // namespace atk::ui
