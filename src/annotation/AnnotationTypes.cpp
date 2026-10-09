#include "annotation/AnnotationTypes.h"

#include <algorithm>
#include <cmath>

namespace atk::annotation {
namespace {
struct ToolNameEntry {
    AnnotationTool tool;
    const char* name;
};
constexpr ToolNameEntry kToolNames[] = {
    {AnnotationTool::Pencil, "pencil"},       {AnnotationTool::Pen, "pen"},
    {AnnotationTool::Brush, "brush"},         {AnnotationTool::Marker, "marker"},
    {AnnotationTool::Eraser, "eraser"},       {AnnotationTool::Line, "line"},
    {AnnotationTool::Arrow, "arrow"},         {AnnotationTool::Rectangle, "rectangle"},
    {AnnotationTool::Ellipse, "ellipse"},     {AnnotationTool::Text, "text"},
};

bool unit(float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; }
}

QString toolName(AnnotationTool tool)
{
    for (const auto& entry : kToolNames)
        if (entry.tool == tool) return QString::fromLatin1(entry.name);
    return {};
}

bool toolFromName(const QString& name, AnnotationTool* out)
{
    for (const auto& entry : kToolNames) {
        if (name == QLatin1String(entry.name)) {
            *out = entry.tool;
            return true;
        }
    }
    return false;
}

bool isStoredTool(AnnotationTool tool) { return tool != AnnotationTool::Eraser; }

bool isFreehandTool(AnnotationTool tool)
{
    return tool == AnnotationTool::Pencil || tool == AnnotationTool::Pen
        || tool == AnnotationTool::Brush || tool == AnnotationTool::Marker;
}

bool isShapeTool(AnnotationTool tool)
{
    return tool == AnnotationTool::Line || tool == AnnotationTool::Arrow
        || tool == AnnotationTool::Rectangle || tool == AnnotationTool::Ellipse;
}

bool isValidItem(const AnnotationItem& item)
{
    if (!isStoredTool(item.tool) || !item.color.isValid()) return false;
    if (!std::isfinite(item.size) || item.size < AnnotationLimits::kMinSize
        || item.size > AnnotationLimits::kMaxSize)
        return false;
    if (!std::isfinite(item.opacity) || item.opacity < 0.0 || item.opacity > 1.0) return false;
    if (item.text.size() > AnnotationLimits::kMaxTextLength) return false;

    const int count = int(item.points.size());
    if (count > AnnotationLimits::kMaxPointsPerItem) return false;
    if (isFreehandTool(item.tool)) {
        if (count < 1 || !item.text.isEmpty()) return false;
    } else if (isShapeTool(item.tool)) {
        if (count != 2 || !item.text.isEmpty()) return false;
    } else { // Text
        if (count != 1 || item.text.isEmpty()) return false;
    }
    for (const StrokePoint& p : item.points) {
        if (!unit(p.x) || !unit(p.y) || !unit(p.pressure)) return false;
        if (!std::isfinite(p.tiltX) || !std::isfinite(p.tiltY)) return false;
    }
    return true;
}

FrameAnnotation SourceAnnotations::frame(int64_t frame) const
{
    if (const FrameAnnotation* found = find(frame)) return *found;
    FrameAnnotation empty;
    empty.frame = frame;
    return empty;
}

const FrameAnnotation* SourceAnnotations::find(int64_t frame) const
{
    const auto it = m_frames.constFind(frame);
    return it == m_frames.constEnd() ? nullptr : &it.value();
}

QVector<int64_t> SourceAnnotations::annotatedFrames() const
{
    QVector<int64_t> result;
    result.reserve(m_frames.size());
    for (auto it = m_frames.constBegin(); it != m_frames.constEnd(); ++it) result.append(it.key());
    return result;
}

void SourceAnnotations::setFrame(FrameAnnotation annotation)
{
    if (annotation.isEmpty()) {
        m_frames.remove(annotation.frame);
        return;
    }
    const int64_t key = annotation.frame;
    m_frames.insert(key, std::move(annotation));
}

void SourceAnnotations::removeFrame(int64_t frame) { m_frames.remove(frame); }

bool SourceAnnotations::trimTo(int64_t lastFrame)
{
    bool removed = false;
    for (auto it = m_frames.begin(); it != m_frames.end();) {
        if (it.key() > lastFrame) {
            it = m_frames.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    return removed;
}

quint64 SourceAnnotations::maxItemId() const
{
    quint64 highest = 0;
    for (const FrameAnnotation& frame : m_frames)
        for (const AnnotationItem& item : frame.items) highest = std::max(highest, item.id);
    return highest;
}

} // namespace atk::annotation
