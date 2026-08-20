#pragma once

#include "media/MediaSource.h"
#include "timeline/Bookmark.h"
#include "timeline/PlaybackRange.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

namespace atk::project {

/// Per-source review state. Bookmarks and the in/out range belong to the source
/// they were made against, not to the application, so switching between two
/// sources in a playlist restores each one's own notes.
struct SourceEntry {
    std::shared_ptr<media::MediaSource> source;
    timeline::PlaybackRange playbackRange;
    QVector<timeline::Bookmark> bookmarks;
    /// Frames added to the master frame number for this source. Used by A/B
    /// comparison to line up takes with different handles.
    int64_t frameOffset = 0;
};

/// Which viewer a source is bound to during A/B comparison.
enum class CompareSlot {
    None,
    A,
    B,
};

/// A review session that can be saved and reopened: the sources, their order,
/// their review state and the comparison layout.
///
/// PHASE 0 STATUS: the model is real; persistence is not. See ProjectSerializer
/// for the planned .atkproj format.
class Project : public QObject {
    Q_OBJECT

public:
    explicit Project(QObject* parent = nullptr);
    ~Project() override;

    const QString& name() const { return m_name; }
    void setName(const QString& name);

    /// Path this project was loaded from or last saved to. Empty when untitled.
    const QString& filePath() const { return m_filePath; }
    void setFilePath(const QString& path);

    /// True when there are unsaved changes.
    bool isModified() const { return m_modified; }
    void setModified(bool modified);

    // --- Sources / playlist ----------------------------------------------
    const QVector<SourceEntry>& entries() const { return m_entries; }
    /// Appends a source and returns its playlist index.
    int addSource(std::shared_ptr<media::MediaSource> source);
    void removeSourceAt(int index);
    void moveSource(int fromIndex, int toIndex);
    void clear();

    /// Index of the entry currently loaded in the main viewer, or -1.
    int activeIndex() const { return m_activeIndex; }
    void setActiveIndex(int index);

    // --- A/B comparison ---------------------------------------------------
    /// Playlist index bound to viewer A / B, or -1 when unassigned.
    int compareIndexA() const { return m_compareA; }
    int compareIndexB() const { return m_compareB; }
    void setCompareIndex(CompareSlot slot, int playlistIndex);

signals:
    void nameChanged(const QString& name);
    void modifiedChanged(bool modified);
    void entriesChanged();
    void activeIndexChanged(int index);
    void compareAssignmentChanged();

private:
    bool isValidIndex(int index) const;

    QString m_name;
    QString m_filePath;
    QVector<SourceEntry> m_entries;
    int m_activeIndex = -1;
    int m_compareA = -1;
    int m_compareB = -1;
    bool m_modified = false;
};

} // namespace atk::project
