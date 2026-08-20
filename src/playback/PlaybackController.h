#pragma once

#include "media/AudioBuffer.h"
#include "media/FrameCache.h"
#include "media/MediaMetadata.h"
#include "media/VideoFrame.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>

class QThread;
class QTimer;

namespace atk::audio {
class AudioOutput;
class AudioRingBuffer;
}

namespace atk::media { class DecoderWorker; }
namespace atk::timeline { class TimelineModel; }

namespace atk::playback {

/// Explicit transport state.
///
/// One enum rather than a set of booleans: `m_playing`, `m_loaded`, `m_seeking`
/// and `m_error` can contradict each other, and every combination has to be
/// reasoned about at each call site. A single value cannot be in two states at
/// once, so the UI can switch on it and be exhaustive.
enum class PlayerState {
    Empty,    ///< Nothing to play -- no media and no extent.
    Loading,  ///< An open is in progress.
    Ready,    ///< Positioned and idle, ready to play.
    Playing,
    Paused,
    Seeking,  ///< A seek is in flight; the displayed frame is still the old one.
    Ended,    ///< Playback reached the end with looping disabled.
    Error,    ///< The last operation failed; see errorMessage().
};

/// Drives playback: owns the decode thread, the frame cache, the audio output
/// and the master clock, and decides which frame should be on screen.
///
/// No UI dependency. Widgets observe its signals and call its methods; it never
/// calls into a widget. That is what lets the external API and the DCC
/// integrations drive the same transport as the on-screen buttons.
///
/// SYNCHRONISATION
/// ---------------
/// When the media has audio, the audio device is the master clock: audio cannot
/// be stretched or skipped without being audible, whereas a video frame arriving
/// a moment late is not. Video frames are selected by asking the audio output
/// what media position is currently being heard.
///
/// With no audio, a monotonic clock takes that role. There is never a second
/// independent timer -- one clock decides the position and everything follows it.
///
/// PLACEHOLDER MODE
/// ----------------
/// With no media loaded the timeline carries the clearly-marked placeholder
/// extent from M0, and the transport drives it from the monotonic clock. That
/// keeps the controls demonstrable before a file is opened; `hasMedia()` stays
/// false and the UI labels it.
class PlaybackController : public QObject {
    Q_OBJECT

public:
    /// `timeline` must outlive the controller and is not owned by it.
    explicit PlaybackController(timeline::TimelineModel* timeline, QObject* parent = nullptr);
    ~PlaybackController() override;

    // --- Media ------------------------------------------------------------

    /// Opens a file asynchronously. State becomes Loading, then Ready or Error.
    void openMedia(const QString& filePath);

    /// Unloads the current media and returns to the placeholder empty state.
    void closeMedia();

    bool hasMedia() const { return m_hasMedia; }
    const media::MediaMetadata& metadata() const { return m_metadata; }
    QString errorMessage() const { return m_errorMessage; }

    // --- Transport --------------------------------------------------------
    void play();
    void pause();
    void togglePlayPause();
    void stop();

    void seekFrame(int64_t frame);
    void stepForward();
    void stepBackward();
    void goToStart();
    void goToEnd();

    // --- Options ----------------------------------------------------------
    void setLoopEnabled(bool enabled);
    bool isLoopEnabled() const { return m_loopEnabled; }

    void setPlaybackRange(int64_t startFrame, int64_t endFrame);
    void clearPlaybackRange();

    // --- Audio ------------------------------------------------------------
    void setMuted(bool muted);
    bool isMuted() const;
    void setVolume(qreal volume);
    qreal volume() const;
    /// True when the loaded media has an audio track and a device accepted it.
    bool hasAudioOutput() const;

    // --- State ------------------------------------------------------------
    PlayerState state() const { return m_state; }
    bool isPlaying() const { return m_state == PlayerState::Playing; }
    int64_t currentFrame() const;

    /// The most recently displayed frame. Invalid before the first decode.
    const media::VideoFrame& currentVideoFrame() const { return m_currentFrame; }

    /// Frames the display loop had to skip because they had not decoded in
    /// time. Diagnostic only; never incremented while stepping.
    int64_t droppedFrameCount() const { return m_droppedFrames; }

signals:
    void stateChanged(atk::playback::PlayerState state);
    void loopEnabledChanged(bool enabled);
    void mediaOpened(const atk::media::MediaMetadata& metadata);
    void mediaClosed();
    void errorOccurred(const QString& message);

    /// A new frame should be displayed.
    void frameChanged(const atk::media::VideoFrame& frame);

    /// Emitted while an open is in progress so the viewer can show its state.
    void loadingChanged(bool loading);

private slots:
    void onWorkerMediaOpened(const atk::media::MediaMetadata& metadata);
    void onWorkerMediaOpenFailed(const QString& message);
    void onWorkerFrameReady(const atk::media::VideoFrame& frame);
    void onWorkerFrameFailed(qint64 frameIndex, const QString& message);
    void onWorkerEndOfStream();
    void onWorkerDecodeError(const QString& message);

    /// Chooses and displays the frame for the current master clock position.
    void onDisplayTick();

signals:
    // Requests to the decode thread. Connected to DecoderWorker slots as queued
    // connections, so nothing on the UI thread ever touches an FFmpeg context.
    void requestOpen(const QString& filePath);
    void requestClose();
    void requestFrame(qint64 frameIndex);
    void requestStartPlayback(qint64 fromFrameIndex);
    void requestStopPlayback();
    void requestPlayheadFrame(qint64 frameIndex);
    void requestConfigureAudio(int sampleRate, int channelCount);

private:
    void setState(PlayerState state);
    void setError(const QString& message);

    /// Master media position in microseconds: the audio device when there is
    /// audio, the monotonic clock otherwise.
    int64_t masterPositionUs() const;

    void startDisplayTimer();
    void stopDisplayTimer();
    int displayIntervalMs() const;

    /// Displays `frame` and updates the timeline playhead.
    void presentFrame(const media::VideoFrame& frame);

    /// Moves to `frame` without playing: cache lookup, else a decode request.
    void seekAndShow(int64_t frame, bool keepPlaying);

    media::FrameRate effectiveFrameRate() const;
    int64_t effectiveLastFrame() const;
    bool inPlaceholderMode() const { return !m_hasMedia; }

    timeline::TimelineModel* m_timeline = nullptr;

    // --- Decode thread ----------------------------------------------------
    QThread* m_decodeThread = nullptr;
    media::DecoderWorker* m_worker = nullptr;

    // --- Audio ------------------------------------------------------------
    std::shared_ptr<audio::AudioRingBuffer> m_audioBuffer;
    std::unique_ptr<audio::AudioOutput> m_audioOutput;
    bool m_audioActive = false;

    // --- Video ------------------------------------------------------------
    media::FrameCache m_cache;
    media::VideoFrame m_currentFrame;
    QTimer* m_displayTimer = nullptr;

    // --- Clock ------------------------------------------------------------
    /// Media position the current playback run started from, in microseconds.
    int64_t m_playbackStartUs = 0;
    /// Monotonic reference captured when playback started, in nanoseconds.
    int64_t m_monotonicStartNs = 0;

    media::MediaMetadata m_metadata;
    QString m_errorMessage;
    PlayerState m_state = PlayerState::Empty;
    bool m_hasMedia = false;
    bool m_loopEnabled = false;
    bool m_resumeAfterSeek = false;
    int64_t m_droppedFrames = 0;
    int64_t m_pendingSeekFrame = -1;
};

} // namespace atk::playback

Q_DECLARE_METATYPE(atk::playback::PlayerState)
