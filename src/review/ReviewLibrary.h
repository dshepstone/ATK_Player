#pragma once

#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

namespace atk::review {

/// One review in the library: a folder of clips and the project file that
/// holds its order, notes and drawings.
struct ReviewInfo {
    QUuid id;
    QString name;
    QString folder;      ///< where the clips are
    QString projectPath; ///< <folder>/<name>.atkproj by default
    bool includeSubfolders = false;
    bool starred = false;
    bool archived = false;
    qint64 createdMsUtc = 0;
    qint64 openedMsUtc = 0;
    int clipCount = 0;   ///< as of the last open or save, for the list
    /// The first few clips (absolute paths), for the thumbnail strip.
    QStringList previewClips;

    friend bool operator==(const ReviewInfo&, const ReviewInfo&) = default;
};

/// The list of reviews the start screen shows, kept as a small JSON file in
/// the user's application data (not beside the clips: it indexes reviews
/// that live in many folders).
///
/// The reviews themselves are ordinary .atkproj projects saved in their
/// folders, so a review folder copied to another machine still opens there.
/// Not thread-safe; owned by the GUI thread.
class ReviewLibrary {
public:
    explicit ReviewLibrary(QString filePath);

    const QString& filePath() const { return m_filePath; }

    /// Reads the file. A missing file is an empty library, not an error; an
    /// unreadable or malformed one is, and leaves the library empty.
    bool load(QString* error = nullptr);
    /// Writes the whole list atomically (QSaveFile).
    bool save(QString* error = nullptr) const;

    const QVector<ReviewInfo>& reviews() const { return m_reviews; }
    const ReviewInfo* find(const QUuid& id) const;
    /// The review whose project is `projectPath` (compared as absolute paths).
    const ReviewInfo* findByProject(const QString& projectPath) const;

    /// Adds a review (a fresh id when it has none) and returns its id. A
    /// review for the same project file replaces the old entry instead.
    QUuid add(ReviewInfo review);
    bool update(const ReviewInfo& review);
    bool remove(const QUuid& id);

private:
    QString m_filePath;
    QVector<ReviewInfo> m_reviews;
};

} // namespace atk::review
