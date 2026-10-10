#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QReadWriteLock>
#include <QString>
#include <QVector>

namespace atk::annotation {

/// One imported brush tip: a greyscale stamp (255 = full ink) plus the few
/// preset settings that carry over from Photoshop.
///
/// Tips are identified by a stable `id` (the Photoshop sample UUID when the
/// file has one, otherwise a hash of the image), which is what annotation
/// items reference and what a project embeds, so a drawing made with a tip
/// renders the same on a machine that never imported the set.
struct BrushTip {
    QString id;
    QString name;
    QImage mask;          // Format_Grayscale8
    qreal spacing = 0.25; // fraction of the dab diameter between dabs
    qreal angle = 0.0;    // degrees, counter-clockwise like Photoshop
    int diameter = 0;     // the preset's own diameter in pixels, if known

    bool isValid() const { return !id.isEmpty() && !mask.isNull(); }

    /// Lossless PNG bytes of the mask, used for the library and projects.
    QByteArray maskPng() const;
    static QImage maskFromPng(const QByteArray& png);
    /// A stable id derived from the mask pixels, for tips without a UUID.
    static QString idForMask(const QImage& mask);
};

/// Process-wide set of tips the renderer can draw with, keyed by id.
///
/// The UI registers the library's tips and a loaded project's embedded tips;
/// the renderer only reads. Tips are immutable once registered and QImage is
/// implicitly shared, so export threads can render concurrently.
class BrushTipRegistry {
public:
    static BrushTipRegistry& instance();

    void add(const BrushTip& tip);
    BrushTip find(const QString& id) const; // invalid tip when unknown
    bool contains(const QString& id) const;
    void clear();

private:
    mutable QReadWriteLock m_lock;
    QHash<QString, BrushTip> m_tips;
};

} // namespace atk::annotation
