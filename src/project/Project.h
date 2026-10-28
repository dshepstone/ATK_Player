#pragma once

#include "annotation/AnnotationTypes.h"
#include "media/MediaSource.h"
#include "timeline/Bookmark.h"
#include "timeline/PlaybackRange.h"

#include <QObject>
#include <QString>
#include <QUuid>
#include <QVector>

#include <memory>

namespace atk::project {

enum class SourceAvailability { Unknown, Probing, Ready, Missing, Error };

/// Where a clip stands in the review. Saved with the project; a project with
/// any status other than NotReviewed is written as version 3, so older builds
/// refuse it rather than drop the statuses.
enum class ClipStatus { NotReviewed, InReview, NeedsChanges, Approved };

/// Stable keys used in the project file ("notReviewed", "inReview", ...).
QString clipStatusKey(ClipStatus status);
/// The status for a key; NotReviewed for an empty or unknown one.
ClipStatus clipStatusFromKey(const QString& key);

/// Per-source review state. Bookmarks and the in/out range belong to the source
/// they were made against, not to the application, so switching between two
/// sources in a playlist restores each one's own notes.
struct SourceEntry {
    QUuid id = QUuid::createUuid();
    std::shared_ptr<media::MediaSource> source;
    QString displayName;
    QString storedPath;
    bool missing = false;
    SourceAvailability availability = SourceAvailability::Unknown;
    QString availabilityError;
    quint64 probeToken = 0;
    timeline::PlaybackRange playbackRange;
    QVector<timeline::Bookmark> bookmarks;
    /// Drawn annotations, keyed by zero-based source frame. Review state like
    /// the bookmarks: saved with the project, trimmed when a relink shortens
    /// the media.
    annotation::SourceAnnotations annotations;
    /// Frames added to the master frame number for this source. Used by A/B
    /// comparison to line up takes with different handles.
    int64_t frameOffset = 0;
    /// Review status, set by the reviewer from the header bar.
    ClipStatus status = ClipStatus::NotReviewed;
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

    /// Folder exports of this project are saved to by default, or empty to
    /// save them beside the project (or the clip). Saved in the project file.
    const QString& exportFolder() const { return m_exportFolder; }
    /// Sets the export folder and marks the project modified.
    void setExportFolder(const QString& folder);

    /// True when there are unsaved changes.
    bool isModified() const { return m_modified; }
    void setModified(bool modified);

    /// True when closing the project should offer to save it.
    ///
    /// A saved project asks whenever it is modified. An untitled session asks
    /// only when it holds review work worth keeping -- bookmarks, or a playlist
    /// of more than one source -- so opening and closing a single video to
    /// watch it never prompts.
    bool needsSavePrompt() const;

    // --- Sources / playlist ----------------------------------------------
    const QVector<SourceEntry>& entries() const { return m_entries; }
    QVector<SourceEntry>& mutableEntries() { return m_entries; }
    /// Appends a source and returns its playlist index.
    int addSource(std::shared_ptr<media::MediaSource> source);
    void removeSourceAt(int index);
    void moveSource(int fromIndex, int toIndex);
    void clear();

    /// Index of the entry currently loaded in the main viewer, or -1.
    int activeIndex() const { return m_activeIndex; }
    QUuid currentSourceId() const;
    int indexForId(const QUuid& id) const;
    void setActiveIndex(int index);
    void replace(QString name, QString filePath, QVector<SourceEntry> entries,
                 const QUuid& currentSourceId, QString exportFolder = {});
    /// Replaces only the media descriptor for an existing stable source.
    /// Bookmarks outside the replacement extent are discarded; the review
    /// range is clamped. A still-image source's hold carries over to the
    /// replacement. Returns false without mutation for invalid input.
    bool relinkSource(const QUuid& id, std::shared_ptr<media::MediaSource> source,
                      int64_t replacementFrameCount);
    /// Replaces one source's annotations (the editor writes its result back
    /// here), marks the project modified and emits annotationsChanged. Returns
    /// false for an unknown id; no change is made then.
    bool setAnnotations(const QUuid& id, annotation::SourceAnnotations annotations);
    /// Sets one clip's review status, marks the project modified and emits
    /// clipStatusChanged. Returns false for an unknown id or no change.
    bool setClipStatus(const QUuid& id, ClipStatus status);
    /// The name a clip is shown under (the file is not touched). Marks the
    /// project modified and emits entriesChanged. False for an unknown id or
    /// no change.
    bool setDisplayName(const QUuid& id, const QString& name);
    bool beginProbe(const QUuid& id, const QString& path, quint64 token);
    bool applyProbeResult(const QUuid& id, const QString& path, quint64 token,
                          const media::MediaMetadata& metadata, const QString& error,
                          bool missing);

    // --- A/B comparison ---------------------------------------------------
    /// Playlist index bound to viewer A / B, or -1 when unassigned.
    int compareIndexA() const { return m_compareA; }
    int compareIndexB() const { return m_compareB; }
    void setCompareIndex(CompareSlot slot, int playlistIndex);

signals:
    void nameChanged(const QString& name);
    void modifiedChanged(bool modified);
    void entriesChanged();
    void annotationsChanged(const QUuid& sourceId);
    void clipStatusChanged(const QUuid& sourceId);
    void activeIndexChanged(int index);
    void compareAssignmentChanged();

private:
    bool isValidIndex(int index) const;

    /// What a save would keep that is worth asking about: the videos and
    /// their order, bookmarks, drawings and notes, comparison offsets. Not the
    /// timeline zoom or the frame on screen, which change just by watching.
    struct ReviewContent {
        QString path;
        QVector<timeline::Bookmark> bookmarks;
        annotation::SourceAnnotations annotations;
        int64_t frameOffset = 0;
        friend bool operator==(const ReviewContent&, const ReviewContent&) = default;
    };
    QVector<ReviewContent> reviewContent() const;

    QString m_name;
    QString m_filePath;
    QString m_exportFolder;
    QVector<SourceEntry> m_entries;
    int m_activeIndex = -1;
    int m_compareA = -1;
    int m_compareB = -1;
    bool m_modified = false;
    /// reviewContent() when the project last matched its file (saved, loaded
    /// or new), so a save prompt only appears when something real changed.
    QVector<ReviewContent> m_savedContent;
    QString m_savedExportFolder;
};

} // namespace atk::project
