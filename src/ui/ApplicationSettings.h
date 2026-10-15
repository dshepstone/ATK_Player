#pragma once

#include "annotation/AnnotationGhosting.h"
#include "annotation/AnnotationHold.h"
#include "annotation/AnnotationToolSettings.h"
#include "media/ImageSource.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <memory>

class QSettings;

namespace atk::ui {

/// Single owner of machine-local application preferences. Media/session state
/// deliberately does not belong here; M3 project serialization owns that.
class ApplicationSettings {
public:
    ApplicationSettings();
    explicit ApplicationSettings(const QString& iniFilePath);
    ~ApplicationSettings();

    static constexpr bool defaultRestoreWindowLayout() { return true; }
    static constexpr bool defaultAudioScrubEnabled() { return true; }
    static constexpr bool defaultFrameStepAudioEnabled() { return false; }
    static constexpr bool defaultBookmarkSnapEnabled() { return true; }
    static constexpr double defaultVolume() { return 1.0; }
    static constexpr bool defaultMuted() { return false; }
    static constexpr bool defaultReopenLastProject() { return false; }
    static constexpr bool defaultShowLibraryAtStartup() { return true; }
    static constexpr int maximumRecentProjects() { return 10; }
    static constexpr bool defaultApiEnabled() { return false; }
    static constexpr int defaultApiPort() { return 45571; }
    /// Hold given to newly added still images. Existing sources keep the hold
    /// they were added with; see media::ImageSourceOptions.
    static constexpr int defaultStillImageHoldFrames()
    {
        return static_cast<int>(media::ImageSourceOptions::kDefaultHoldFrames);
    }
    static constexpr int minimumStillImageHoldFrames()
    {
        return static_cast<int>(media::ImageSourceOptions::kMinimumHoldFrames);
    }
    static constexpr int maximumStillImageHoldFrames()
    {
        return static_cast<int>(media::ImageSourceOptions::kMaximumHoldFrames);
    }

    bool restoreWindowLayout() const;
    bool audioScrubEnabled() const;
    bool frameStepAudioEnabled() const;
    bool bookmarkSnapEnabled() const;
    double volume() const;
    bool muted() const;
    bool reopenLastProject() const;
    /// A launch without a file opens the Review Library (else the empty player).
    bool showLibraryAtStartup() const;
    QStringList recentProjects() const;
    QString lastProjectPath() const;
    /// Rename from Team List: the last list used and the name pattern.
    QString lastRosterPath() const;
    QString renameTemplate() const;
    /// Playlist playback: with loop off, stop after each clip instead of
    /// going on to the next; with loop on, loop the whole playlist instead
    /// of the current clip. Both default to false.
    bool stopAfterEachClip() const;
    bool loopWholePlaylist() const;
    bool apiEnabled() const;
    int apiPort() const;
    int stillImageHoldFrames() const;
    /// Rate given to newly opened image sequences (and new stills). Existing
    /// sources keep their own rate. Default 24/1.
    media::FrameRate imageSequenceFrameRate() const;
    /// Version string the welcome dialog was last shown for. Empty until the
    /// first launch after installation has shown it.
    QString welcomeShownVersion() const;
    /// The drawing tool, colour, size and options last used. Invalid or
    /// missing values fall back to the defaults field by field.
    annotation::AnnotationToolSettings annotationTool() const;
    /// Name written on review notes; defaults to the operating-system user.
    QString reviewerName() const;
    /// Tracing paper opacity in percent (0 = off), and the level the toggle
    /// turns it on at (25, 50, 75 or 100; default 50).
    int tracingPaper() const;
    int tracingPaperLevel() const;
    /// Open a finished PDF or video export in the system viewer (default on).
    bool openExportsWhenDone() const;
    /// Notes PDF: include the note text (off = images only).
    bool reviewNotesIncludeText() const;
    /// Ghosting (onion skin) of nearby frames' drawings.
    annotation::GhostSettings ghostSettings() const;
    /// Hold drawings until the next drawing (default off), and the mode the
    /// toggle turns it on with (default Add to Current Drawing).
    annotation::HoldMode holdDrawings() const;
    annotation::HoldMode holdDrawingsOnMode() const;
    /// Drawing markers under the timeline can be dragged to another frame
    /// (default off; a click still goes to the frame).
    bool dragMarkersEnabled() const;

    void setRestoreWindowLayout(bool value);
    void setAudioScrubEnabled(bool value);
    void setFrameStepAudioEnabled(bool value);
    void setBookmarkSnapEnabled(bool value);
    void setVolume(double value);
    void setMuted(bool value);
    void setReopenLastProject(bool value);
    void setShowLibraryAtStartup(bool value);
    void addRecentProject(const QString& path);
    void clearRecentProjects();
    void setLastProjectPath(const QString& path);
    void setLastRosterPath(const QString& path);
    void setRenameTemplate(const QString& nameTemplate);
    void setStopAfterEachClip(bool value);
    void setLoopWholePlaylist(bool value);
    void setApiEnabled(bool value);
    void setApiPort(int value);
    void setStillImageHoldFrames(int value);
    void setImageSequenceFrameRate(const media::FrameRate& rate);
    void setWelcomeShownVersion(const QString& version);
    void setAnnotationTool(const annotation::AnnotationToolSettings& settings);
    void setReviewerName(const QString& name);
    void setTracingPaper(int percent);
    void setTracingPaperLevel(int percent);
    void setOpenExportsWhenDone(bool open);
    void setReviewNotesIncludeText(bool include);
    void setGhostSettings(const annotation::GhostSettings& settings);
    void setHoldDrawings(annotation::HoldMode mode);
    void setDragMarkersEnabled(bool enabled);

    QByteArray windowGeometry() const;
    QByteArray windowState() const;
    void setWindowGeometry(const QByteArray& value);
    void setWindowState(const QByteArray& value);

    /// Overrides are keyed by stable CommandDefinition::key. An empty value is
    /// an intentional cleared shortcut; absence means use the default.
    QHash<QString, QString> shortcutOverrides() const;
    bool setShortcutOverride(const QString& commandKey, const QString& portableShortcut);
    void resetShortcutOverride(const QString& commandKey);
    void resetAllShortcuts();

    void resetAll();
    void sync();

private:
    bool readBool(const char* key, bool fallback) const;
    std::unique_ptr<QSettings> m_settings;
};

} // namespace atk::ui
