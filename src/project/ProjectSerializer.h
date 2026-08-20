#pragma once

#include <QString>

#include <utility>

namespace atk::project {

class Project;

/// Result of a load or save attempt.
struct SerializerResult {
    bool ok = false;
    QString errorMessage;

    static SerializerResult success() { return { true, {} }; }
    static SerializerResult failure(QString message) { return { false, std::move(message) }; }
};

/// Reads and writes the ATK Player project format.
///
/// PHASE 0 STATUS: not implemented. Both methods fail with an explanatory
/// message so callers exercise the error path from day one rather than
/// discovering it in M3.
///
/// PLANNED FORMAT -- ".atkproj", UTF-8 JSON, one object per file:
///
///     {
///       "formatVersion": 1,
///       "application": "ATK Player",
///       "name": "Shot 040 review",
///       "sources": [
///         {
///           "id": "src-0",
///           "path": "shots/sh040_v012.mov",   // relative to the project file
///           "frameOffset": 0,
///           "playbackRange": { "start": 12, "end": 96, "enabled": true },
///           "bookmarks": [
///             { "frame": 24, "name": "contact", "note": "foot slides", "color": 3 }
///           ]
///         }
///       ],
///       "playlist": [ "src-0" ],
///       "compare": { "a": "src-0", "b": null, "layout": "horizontal", "bOffset": 0 }
///     }
///
/// Design rules the implementation must follow:
///   - Media paths are stored relative to the project file when the media sits
///     at or below it, absolute otherwise, so a review folder stays portable
///     between machines.
///   - "formatVersion" is checked on load; a newer version is refused with a
///     clear message rather than partially parsed.
///   - Unknown keys are preserved on round-trip where practical, so an older
///     build does not silently strip a newer build's data.
class ProjectSerializer {
public:
    /// Canonical file extension, without the dot.
    static QString fileExtension();
    /// Filter string for QFileDialog.
    static QString fileDialogFilter();

    /// Writes `project` to `filePath`.
    /// TODO(M3): implement using QJsonDocument, writing to a temporary file and
    ///           renaming into place so an interrupted save cannot corrupt an
    ///           existing project.
    static SerializerResult save(const Project& project, const QString& filePath);

    /// Replaces the contents of `project` with the file at `filePath`.
    /// TODO(M3): implement, validating formatVersion before touching `project`.
    static SerializerResult load(Project& project, const QString& filePath);
};

} // namespace atk::project
