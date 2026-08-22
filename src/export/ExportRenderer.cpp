#include "export/ExportRenderer.h"
#include "media/ffmpeg/FFmpegUtil.h"
#include "playback/ComparisonCompositor.h"
#include "playback/CompareSession.h"
#include <QPainter>

namespace atk::exporter {

bool ExportRenderer::open(const ExportSpec& spec, QString* error)
{
    m_spec = spec;
    if (!m_a.open(spec.sourceA.path, error)) return false;
    if (spec.comparison && !m_b.open(spec.sourceB.path, error)) return false;
    media::VideoFrame first;
    if (!m_a.frameAtIndex(spec.sourceA.rangeStartFrame, first, error)) return false;
    m_rangeStartPtsTicks = first.ptsTicks;
    return true;
}

bool ExportRenderer::render(qint64 index, RenderedExportFrame& out, QString* error,
                            const media::MediaDecoder::CancelPredicate& cancelled)
{
    media::VideoFrame a;
    if (!m_a.frameAtIndex(index, a, error, cancelled)) return false;
    out.sourceAFrameIndex = a.frameIndex;
    out.sourceAPtsTicks = a.ptsTicks;
    out.outputPtsTicks = a.ptsTicks - m_rangeStartPtsTicks;
    QImage image = a.image;
    if (m_spec.comparison) {
        const qint64 bIndex = playback::CompareSession::constantRateFrameForSourcePts(
            a.ptsTicks, m_spec.sourceA.metadata.videoTimeBase,
            m_spec.sourceA.metadata.videoStartTime, m_spec.sourceA.rangeStartFrame,
            m_spec.sourceA.metadata.frameRate, m_spec.sourceB.rangeStartFrame,
            m_spec.sourceB.rangeEndFrame, m_spec.sourceB.metadata.frameRate,
            m_spec.sourceBOffsetUs);
        media::VideoFrame b;
        if (!m_b.frameAtIndex(bIndex, b, error, cancelled)) return false;
        out.sourceBFrameIndex = b.frameIndex;
        const int amount = m_spec.layout == playback::CompareLayout::Blend
            ? m_spec.blendAmount : m_spec.wipePosition;
        image = playback::ComparisonCompositor::compose(a.image, b.image, m_spec.layout, amount);
    }
    const QSize padded = m_spec.outputSize();
    if (image.size() != padded) {
        QImage canvas(padded, QImage::Format_ARGB32);
        canvas.fill(Qt::black);
        QPainter painter(&canvas);
        painter.drawImage(0, 0, image);
        image = std::move(canvas);
    }
    out.image = std::move(image);
    return true;
}

} // namespace atk::exporter
