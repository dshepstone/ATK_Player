#include "annotation/BrushTip.h"

#include <QBuffer>
#include <QCryptographicHash>

namespace atk::annotation {

QByteArray BrushTip::maskPng() const
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    mask.convertToFormat(QImage::Format_Grayscale8).save(&buffer, "PNG");
    return bytes;
}

QImage BrushTip::maskFromPng(const QByteArray& png)
{
    QImage image;
    if (!image.loadFromData(png, "PNG")) return {};
    // Bound what a project or library file can make us allocate.
    if (image.width() <= 0 || image.height() <= 0 || image.width() > 4096 || image.height() > 4096) return {};
    return image.convertToFormat(QImage::Format_Grayscale8);
}

QString BrushTip::idForMask(const QImage& mask)
{
    const QImage grey = mask.convertToFormat(QImage::Format_Grayscale8);
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArray::number(grey.width()) + 'x' + QByteArray::number(grey.height()));
    for (int y = 0; y < grey.height(); ++y)
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(grey.constScanLine(y)), grey.width()));
    return QStringLiteral("sha1-") + QString::fromLatin1(hash.result().toHex());
}

BrushTipRegistry& BrushTipRegistry::instance()
{
    static BrushTipRegistry registry;
    return registry;
}

void BrushTipRegistry::add(const BrushTip& tip)
{
    if (!tip.isValid()) return;
    QWriteLocker locker(&m_lock);
    m_tips.insert(tip.id, tip);
}

BrushTip BrushTipRegistry::find(const QString& id) const
{
    QReadLocker locker(&m_lock);
    return m_tips.value(id);
}

bool BrushTipRegistry::contains(const QString& id) const
{
    QReadLocker locker(&m_lock);
    return m_tips.contains(id);
}

void BrushTipRegistry::clear()
{
    QWriteLocker locker(&m_lock);
    m_tips.clear();
}

} // namespace atk::annotation
