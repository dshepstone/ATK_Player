#pragma once

#include "export/ExportSpec.h"
#include <QImage>
#include <QRect>

namespace atk::exporter {

class ExportBurnInRenderer final {
public:
    static QString frameNumberText(qint64 sourceAFrameIndex);
    static QString bookmarkTitle(const ExportBookmark& bookmark);
    static QString bookmarkFrameText(const ExportBookmark& bookmark);
    static QVector<ExportBookmark> activeBookmarks(const ExportSpec& spec, qint64 sourceAFrameIndex);
    static QRect frameNumberBounds(const QSize& imageSize);
    static QRect bookmarkBounds(const QSize& imageSize);
    static void apply(QImage& image, const ExportSpec& spec, qint64 sourceAFrameIndex);
};

} // namespace atk::exporter
