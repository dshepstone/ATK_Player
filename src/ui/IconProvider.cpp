#include "ui/IconProvider.h"

#include "core/Logging.h"
#include "ui/Resources.h"
#include "ui/Theme.h"

#include <QBuffer>
#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPixmapCache>

#include <algorithm>
#include <cmath>

namespace atk::ui::icons {
namespace {

QString resourcePath(const QString& name)
{
    return QStringLiteral(":/icons/lucide/%1.svg").arg(name);
}

/// The SVG source of an icon, read once. Empty for an unknown name.
const QByteArray& svgSource(const QString& name)
{
    static QHash<QString, QByteArray> sources;
    auto it = sources.constFind(name);
    if (it == sources.constEnd()) {
        ensureResourcesInitialized();
        QByteArray data;
        QFile file(resourcePath(name));
        if (file.open(QIODevice::ReadOnly)) data = file.readAll();
        it = sources.insert(name, data);
    }
    return *it;
}

/// Rasterises `name` at exactly `deviceSize` pixels in `color`, through the
/// shared pixmap cache. Null for an unknown name or a missing SVG plugin.
QPixmap render(const QString& name, const QSize& deviceSize, const QColor& color)
{
    if (deviceSize.isEmpty()) return {};
    const QString key = QStringLiteral("atk-icon:%1:%2:%3x%4")
                            .arg(name, color.name(QColor::HexArgb))
                            .arg(deviceSize.width())
                            .arg(deviceSize.height());
    QPixmap cached;
    if (QPixmapCache::find(key, &cached)) return cached;

    const QByteArray& source = svgSource(name);
    if (source.isEmpty()) return {};
    QByteArray tinted = source;
    tinted.replace("currentColor", color.name(QColor::HexRgb).toLatin1());

    QBuffer buffer(&tinted);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "svg");
    reader.setScaledSize(deviceSize);
    QImage image = reader.read();
    if (image.isNull()) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            qCWarning(log::ui) << "Could not render icon" << name << ":" << reader.errorString()
                               << "(is the Qt SVG image-format plugin deployed?)";
        }
        return {};
    }
    // Lucide strokes are fully opaque; carry the colour's own alpha too.
    if (color.alpha() < 255) {
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter p(&image);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.fillRect(image.rect(), QColor(0, 0, 0, color.alpha()));
    }
    QPixmap pixmap = QPixmap::fromImage(std::move(image));
    QPixmapCache::insert(key, pixmap);
    return pixmap;
}

/// Draws an icon at whatever size and scale Qt asks for, choosing the colour
/// from the mode and state, so a checked or disabled button never shows a
/// stale or blurry bitmap.
class TintedSvgIconEngine final : public QIconEngine {
public:
    TintedSvgIconEngine(QString name, IconColors colors) : m_name(std::move(name)), m_colors(std::move(colors)) {}

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return render(m_name, size, colorFor(mode, state));
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        const qreal ratio = std::max<qreal>(1.0, scale);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        // Qt 6.8 and later pass the logical size.
        const QSize deviceSize(qRound(size.width() * ratio), qRound(size.height() * ratio));
#else
        // Before 6.8, QIcon::pixmap() passes the size already in device
        // pixels; scaling it again drew 1.5x too large at 150% and blurred
        // when shrunk back. paint() below passes the logical size.
        const QSize deviceSize = size;
#endif
        QPixmap result = render(m_name, deviceSize, colorFor(mode, state));
        result.setDevicePixelRatio(ratio);
        return result;
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal ratio = std::max<qreal>(1.0, painter->device() ? painter->device()->devicePixelRatioF() : 1.0);
        const QSize deviceSize(qRound(rect.width() * ratio), qRound(rect.height() * ratio));
        QPixmap pm = render(m_name, deviceSize, colorFor(mode, state));
        pm.setDevicePixelRatio(ratio);
        if (!pm.isNull()) painter->drawPixmap(rect, pm);
    }

    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }
    QIconEngine* clone() const override { return new TintedSvgIconEngine(m_name, m_colors); }
    QString key() const override { return QStringLiteral("AtkTintedSvg"); }

private:
    QColor colorFor(QIcon::Mode mode, QIcon::State state) const
    {
        if (mode == QIcon::Disabled) return m_colors.disabled;
        if (state == QIcon::On) return m_colors.checked;
        if (mode == QIcon::Active) return m_colors.hover;
        if (mode == QIcon::Selected) return m_colors.selected;
        return m_colors.normal;
    }

    QString m_name;
    IconColors m_colors;
};

} // namespace

IconColors defaultColors()
{
    return {theme::iconNormal(), theme::iconHover(), theme::textOnAccent(), theme::iconDisabled(),
            theme::textOnAccent()};
}

bool exists(const QString& name)
{
    return !name.isEmpty() && !svgSource(name).isEmpty();
}

QIcon icon(const QString& name)
{
    return icon(name, defaultColors());
}

QIcon icon(const QString& name, const IconColors& colors)
{
    if (!exists(name)) {
        qCWarning(log::ui) << "Unknown icon" << name;
        return {};
    }
    return QIcon(new TintedSvgIconEngine(name, colors));
}

QIcon icon(const QString& name, const QColor& color)
{
    return icon(name, IconColors{color, color, color, color, color});
}

QPixmap pixmap(const QString& name, const QSize& size, const QColor& color, qreal devicePixelRatio)
{
    const qreal ratio = std::max<qreal>(1.0, devicePixelRatio);
    QPixmap result = render(name, QSize(qRound(size.width() * ratio), qRound(size.height() * ratio)), color);
    if (!result.isNull()) result.setDevicePixelRatio(ratio);
    return result;
}

} // namespace atk::ui::icons
