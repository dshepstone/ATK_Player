#pragma once

#include "annotation/BrushTip.h"

#include <QString>
#include <QVector>

namespace atk::annotation {

/// A named group of tips, as imported from one .abr file.
struct BrushSet {
    QString name;
    QVector<BrushTip> tips;
};

/// The user's imported brush sets, kept on disk between sessions.
///
/// Layout under `directory`: one sub-folder per set holding `set.json`
/// (name, and per tip: id, name, spacing, angle, diameter, file) and one PNG
/// mask per tip. Plain files, so a set can be backed up or shared by copying
/// the folder. No UI here; MainWindow decides where the directory is.
class BrushLibrary {
public:
    explicit BrushLibrary(QString directory);

    const QVector<BrushSet>& sets() const { return m_sets; }
    const QString& directory() const { return m_directory; }

    /// Reads every set in the directory; unreadable sets are skipped.
    void load();
    /// Adds a set (replacing one with the same name) and writes it to disk.
    /// Returns false with `error` set if it could not be written.
    bool addSet(const BrushSet& set, QString* error = nullptr);
    bool removeSet(const QString& name);

    BrushTip findTip(const QString& id) const;
    /// Registers every tip with the BrushTipRegistry so the renderer can use it.
    void registerAll() const;

private:
    QString folderFor(const QString& setName) const;

    QString m_directory;
    QVector<BrushSet> m_sets;
};

} // namespace atk::annotation
