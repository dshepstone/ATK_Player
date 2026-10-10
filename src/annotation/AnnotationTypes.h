#pragma once

#include <QColor>
#include <QMap>
#include <QPointF>
#include <QString>
#include <QVector>

#include <QtGlobal>
#include <cstdint>

namespace atk::annotation {

/// What an item is. Pencil, Pen, Brush, Marker and Stamp are freehand strokes
/// that differ only in how the renderer shapes them (Stamp repeats an imported
/// brush tip, such as one from a Photoshop brush set, along the stroke); Line..Ellipse are two-point
/// shapes; Text is an anchor point (point text) or a box (paragraph text that
/// wraps to the box width) plus text. Eraser and Select never appear in stored
/// data -- they act on items rather than being one -- but the tool enum is
/// shared with the UI so a tool choice maps to one value.
enum class AnnotationTool {
    Pencil,
    Pen,
    Brush,
    Marker,
    Eraser,
    Line,
    Arrow,
    Rectangle,
    Ellipse,
    Text,
    Select,
    Stamp,
};

/// Stable names used by the project file. Never reorder or rename: they are
/// persisted. Unknown names fail to parse rather than guessing.
QString toolName(AnnotationTool tool);
bool toolFromName(const QString& name, AnnotationTool* out);

/// Whether items of this tool are stored (everything but Eraser and Select).
bool isStoredTool(AnnotationTool tool);
bool isFreehandTool(AnnotationTool tool);
bool isShapeTool(AnnotationTool tool);

/// One sampled point of a stroke.
///
/// x/y are normalised to the *source image* (0..1 across width and height,
/// before zoom, pan and flip), so an annotation is independent of window size,
/// display scaling and export resolution. Pressure is 0..1; a mouse reports
/// 1.0. Tilt is in degrees and is stored only for future brush shaping.
struct StrokePoint {
    float x = 0.0f;
    float y = 0.0f;
    float pressure = 1.0f;
    float tiltX = 0.0f;
    float tiltY = 0.0f;

    friend bool operator==(const StrokePoint&, const StrokePoint&) = default;
};

/// One undoable unit: a stroke, a shape or a text label.
struct AnnotationItem {
    quint64 id = 0;
    AnnotationTool tool = AnnotationTool::Pen;
    QVector<StrokePoint> points;
    QColor color = QColor(255, 0, 0);
    /// Brush diameter as a fraction of image height, so a stroke keeps its look
    /// at any export resolution.
    qreal size = 0.006;
    qreal opacity = 1.0;
    /// Seeds the pencil grain so the viewer and an export draw identical pixels.
    quint32 textureSeed = 0;
    QString text;
    /// Stamp only: id of the BrushTip to repeat along the stroke.
    QString brushId;

    friend bool operator==(const AnnotationItem&, const AnnotationItem&) = default;
};

/// Limits applied on load and on every edit. They bound memory and file size
/// against a corrupt or hostile project; real drawing is far below them.
struct AnnotationLimits {
    static constexpr int kMaxPointsPerItem = 20000;
    static constexpr int kMaxItemsPerFrame = 2000;
    static constexpr int kMaxTextLength = 2000;
    static constexpr int kMaxAnnotatedFrames = 100000;
    static constexpr qreal kMinSize = 0.0005;
    static constexpr qreal kMaxSize = 0.5;
};

/// True when the item is storable: a stored tool, the right number of finite
/// in-range points for its kind, a valid colour, size and opacity within
/// limits, and text only where the tool uses it.
bool isValidItem(const AnnotationItem& item);

/// True for paragraph text: a Text item with a box, whose lines wrap to the
/// box width. Point text (one anchor) never wraps.
bool isBoxText(const AnnotationItem& item);

/// `item` moved by `delta` (normalised), with the delta clamped so every point
/// stays on the picture. Used by the Select tool.
AnnotationItem translatedItem(const AnnotationItem& item, const QPointF& delta);

/// Everything drawn on one frame.
struct FrameAnnotation {
    /// Zero-based source frame index -- the same index the status bar reports
    /// minus one and that bookmarks use.
    int64_t frame = 0;
    /// Media time of that frame when it was drawn, so a retimed source can be
    /// re-resolved by timestamp. Informational; `frame` is authoritative.
    int64_t mediaTimeUs = 0;
    QVector<AnnotationItem> items;

    bool isEmpty() const { return items.isEmpty(); }

    friend bool operator==(const FrameAnnotation&, const FrameAnnotation&) = default;
};

/// All annotation data for one source, keyed by frame. Empty frames are never
/// stored.
class SourceAnnotations {
public:
    bool isEmpty() const { return m_frames.isEmpty(); }
    int frameCount() const { return int(m_frames.size()); }
    bool hasFrame(int64_t frame) const { return m_frames.contains(frame); }
    /// Returns an empty annotation (frame set, no items) when none exists.
    FrameAnnotation frame(int64_t frame) const;
    const FrameAnnotation* find(int64_t frame) const;
    /// Ascending zero-based indices of frames that have items.
    QVector<int64_t> annotatedFrames() const;
    const QMap<int64_t, FrameAnnotation>& frames() const { return m_frames; }

    /// Replaces a frame's annotation; an empty one removes the frame.
    void setFrame(FrameAnnotation annotation);
    void removeFrame(int64_t frame);
    void clear() { m_frames.clear(); }

    /// Drops frames beyond `lastFrame` (inclusive). Returns true if any went.
    bool trimTo(int64_t lastFrame);

    /// Highest item id in use, so a loaded set can seed id allocation.
    quint64 maxItemId() const;

    friend bool operator==(const SourceAnnotations&, const SourceAnnotations&) = default;

private:
    QMap<int64_t, FrameAnnotation> m_frames;
};

} // namespace atk::annotation
