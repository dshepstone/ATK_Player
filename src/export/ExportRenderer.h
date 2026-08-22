#pragma once

#include "export/ExportSpec.h"
#include "media/MediaDecoder.h"

namespace atk::exporter {

struct RenderedExportFrame {
    QImage image;
    qint64 sourceAPtsTicks = 0;
    qint64 outputPtsTicks = 0;
    qint64 sourceAFrameIndex = -1;
    qint64 sourceBFrameIndex = -1;
};

class ExportRenderer final {
public:
    bool open(const ExportSpec& spec, QString* error);
    bool render(qint64 sourceAFrameIndex, RenderedExportFrame& out, QString* error,
                const media::MediaDecoder::CancelPredicate& cancelled = {});
    qint64 rangeStartPtsTicks() const { return m_rangeStartPtsTicks; }
private:
    ExportSpec m_spec;
    media::MediaDecoder m_a;
    media::MediaDecoder m_b;
    qint64 m_rangeStartPtsTicks = 0;
};

} // namespace atk::exporter
