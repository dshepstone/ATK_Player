#include "playback/ComparisonCompositor.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace atk::playback {

QSize ComparisonCompositor::canvasSize(const QSize& a, CompareLayout mode)
{
    if (mode == CompareLayout::SideBySide) return {a.width() * 2, a.height()};
    if (mode == CompareLayout::Stacked) return {a.width(), a.height() * 2};
    return a;
}

QImage ComparisonCompositor::fitToCanvas(const QImage& image, const QSize& canvas)
{
    QImage result(canvas, QImage::Format_ARGB32);
    result.fill(Qt::black);
    if (image.isNull() || canvas.isEmpty()) return result;
    const QSize fitted = image.size().scaled(canvas, Qt::KeepAspectRatio);
    const QRect target((canvas.width() - fitted.width()) / 2,
                       (canvas.height() - fitted.height()) / 2,
                       fitted.width(), fitted.height());
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(target, image);
    return result;
}

QImage ComparisonCompositor::compose(const QImage& a, const QImage& b,
                                     CompareLayout mode, int amount)
{
    const QSize cell = !a.isNull() ? a.size() : b.size();
    if (cell.isEmpty()) return {};
    const QImage ca = fitToCanvas(a, cell);
    const QImage cb = fitToCanvas(b, cell);
    if (mode == CompareLayout::SideBySide || mode == CompareLayout::Stacked) {
        QImage out(canvasSize(cell, mode), QImage::Format_ARGB32);
        out.fill(Qt::black);
        QPainter painter(&out);
        painter.drawImage(0, 0, ca);
        painter.drawImage(mode == CompareLayout::SideBySide ? cell.width() : 0,
                          mode == CompareLayout::Stacked ? cell.height() : 0, cb);
        return out;
    }
    amount = std::clamp(amount, 0, 100);
    if (mode == CompareLayout::Wipe) {
        QImage out = cb.copy();
        const int edge = static_cast<int>((static_cast<qint64>(out.width()) * amount) / 100);
        if (edge > 0) {
            QPainter painter(&out);
            painter.drawImage(QRect(0, 0, edge, out.height()), ca,
                              QRect(0, 0, edge, ca.height()));
        }
        return out;
    }
    if (mode == CompareLayout::Blend) {
        QImage out = ca.copy();
        QPainter painter(&out);
        painter.setOpacity(amount / 100.0);
        painter.drawImage(0, 0, cb);
        return out;
    }
    QImage out(cell, QImage::Format_ARGB32);
    for (int y = 0; y < cell.height(); ++y) {
        const auto* ap = reinterpret_cast<const QRgb*>(ca.constScanLine(y));
        const auto* bp = reinterpret_cast<const QRgb*>(cb.constScanLine(y));
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < cell.width(); ++x) {
            dst[x] = qRgba(std::abs(qRed(ap[x]) - qRed(bp[x])),
                           std::abs(qGreen(ap[x]) - qGreen(bp[x])),
                           std::abs(qBlue(ap[x]) - qBlue(bp[x])), 255);
        }
    }
    return out;
}

} // namespace atk::playback
