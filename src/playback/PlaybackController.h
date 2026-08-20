#pragma once

#include "media/AudioBuffer.h"
#include "media/DecodeGeneration.h"
#include "media/FrameCache.h"
#include "media/PlaybackQueue.h"
#include "media/WaveformData.h"
#include "media/MediaMetadata.h"
#include "media/VideoFrame.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <deque>
#include <memory>

class QThread;
class QTimer;

namespace atk::audio {
class AudioOutput;
class AudioRingBuffer;
class ScrubAudioEngine;
}

namespace atk::media {
class DecoderWorker;
class ScrubAudioWorker;
class WaveformWorker;
}
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

    // --- Audio scrubbing and waveform (M2) --------------------------------

    /// Whether dragging the timeline produces audible audio. On by default;
    /// visual scrubbing and the waveform are unaffected when off.
    void setAudioScrubEnabled(bool enabled);
    bool isAudioScrubEnabled() const { return m_audioScrubEnabled; }

    /// Waveform peaks for the open media. Empty until analysis produces some.
    const media::WaveformData& waveform() const { return m_waveform; }

    void beginScrub();
    void scrubToFrame(int64_t frame);
    void endScrub(int64_t frame);
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
    int64_t navigationFrame() const { return m_navigationFrame; }

    /// The most recently displayed frame. Invalid before the first decode.
    const media::VideoFrame& currentVideoFrame() const { return m_currentFrame; }

    /// Frames the display loop had to skip because they had not decoded in
    /// time. Diagnostic only; never incremented while stepping.
    int64_t droppedFrameCount() const { return m_droppedFrames; }
    int64_t reviewCacheBytes() const { return m_cache.usedBytes(); }
    int64_t reviewCacheBudgetBytes() const { return m_cache.budgetBytes(); }
    int64_t reviewCacheHits() const { return m_cache.hitCount(); }
    int64_t reviewCacheMisses() const { return m_cache.missCount(); }
    int64_t reviewCacheEvictions() const { return m_cache.evictionCount(); }
    void resetReviewCacheCounters() { m_cache.resetCounters(); }

signals:
    void stateChanged(atk::playback::PlayerState state);
    void loopEnabledChanged(bool enabled);
    void mediaOpened(const atk::media::MediaMetadata& metadata);
    void mediaClosed();

    /// More waveform peaks are available, or the waveform was cleared.
    void waveformChanged();
    void errorOccurred(const QString& message);

    /// A new frame should be displayed.
    void frameChanged(const atk::media::VideoFrame& frame);

    /// Emitted while an open is in progress so the viewer can show its state.
    void loadingChanged(bool loading);

private slots:
    void onWorkerMediaOpened(const atk::media::MediaMetadata& metadata,
                             quint64 sourceGeneration);
    void onWorkerMediaOpenFailed(const QString& message, quint64 sourceGeneration);
    void onWorkerFrameReady(const atk::media::VideoFrame& frame, quint64 requestGeneration);
    void onWorkerFrameFailed(qint64 frameIndex, const QString& message,
                             quint64 requestGeneration);
    void onWorkerEndOfStream(quint64 requestGeneration);
    void onWorkerDecodeError(const QString& message);
    void onAudioPrimed(int bufferedMs, qint64 mediaOriginUs, quint64 requestGeneration);

    void onWaveformPeaks(const QVector<atk::media::WaveformPeak>& peaks,
                         quint64 sourceGeneration);
    void onWaveformFinished(qint64 totalUs, quint64 sourceGeneration);
    void onWaveformUnavailable(const QString& reason, quint64 sourceGeneration);

    void onScrubGrain(const QByteArray& pcm, qint64 requestedUs, qint64 actualStartUs,
                      quint64 sequence, quint64 sourceGeneration);

    /// Chooses and displays the frame for the current master clock position.
    void onDisplayTick();
    void presentNextNavigationFrame();

signals:
    // Requests to the decode thread. Connected to DecoderWorker slots as queued
    // connections, so nothing on the UI thread ever touches an FFmpeg context.
    void requestOpen(const QString& filePath, quint64 sourceGeneration);
    void requestClose();
    void requestFrame(qint64 frameIndex, quint64 requestGeneration);
    void requestStartPlayback(qint64 fromFrameIndex, quint64 requestGeneration);
    void requestStopPlayback();
    void requestPlayheadFrame(qint64 frameIndex);
    void requestConfigureAudio(int sampleRate, int channelCount);
    void requestLookaheadFrames(int frames);

    // To the waveform thread.
    void requestWaveform(const QString& filePath, quint64 sourceGeneration);

    // To the scrub-audio thread.
    void requestScrubSource(const QString& filePath, int sampleRate, int channelCount,
                            quint64 sourceGeneration);
    void requestScrubGrain(qint64 mediaUs, qint64 durationUs, quint64 sequence,
                           quint64 sourceGeneration);

private:
    void setState(PlayerState state);
    void setError(const QString& message);

    /// Stops the display timer and audio, and tells the worker to stop
    /// producing. Shared by pause, stop, seek, close and shutdown, so the order
    /// is written once rather than repeated slightly differently in five places.
    void haltPlaybackMachinery();

    /// Moves between Empty and Ready as the timeline gains or loses an extent.
    void reconcileIdleState();

    /// Ends the current playback run because the *playhead* reached the end:
    /// loops back if looping is on, otherwise settles on the final frame.
    void finishPlayback();

    /// Emits the periodic performance summary, at most once a second.
    void reportPerformance(bool force);

    /// Lookahead target derived from the frame rate and the decoded frame size.
    int computeLookaheadFrames() const;

    /// True when the audio device is genuinely delivering decoded audio, and is
    /// therefore fit to act as the master clock.
    bool usingAudioClock() const;

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
    void dispatchScrubDecode();

    /// Media time of a frame index, from the real rational frame rate. This is
    /// the single origin both the video preview and the scrub audio derive
    /// their position from, so the two can never drift apart.
    int64_t mediaTimeForFrame(int64_t frame) const;

    /// Asks for a grain at the given scrub position, if audio scrub is on.
    void requestScrubAudioAt(int64_t frame);

    void startWaveformAnalysis(const QString& filePath, quint64 sourceGeneration);
    void finishScrubIfReady();
    void enqueueNavigationTarget(int64_t frame);
    void dispatchNavigationDecode();
    void enqueueNavigationPresentation(const media::VideoFrame& frame);
    void finishNavigationIfReady();
    void cancelNavigation();
    void resetNavigationTarget();

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
    /// Shared with the worker. Bumped here, read there.
    std::shared_ptr<media::DecodeGenerations> m_generations;

    media::FrameCache m_cache;

    /// Frames waiting to be presented. Separate from the cache so LRU eviction
    /// cannot discard the lookahead before playback reaches it -- see
    /// media/PlaybackQueue.h.
    media::PlaybackQueue m_queue;
    media::VideoFrame m_currentFrame;
    QTimer* m_displayTimer = nullptr;
    QTimer* m_navigationTimer = nullptr;

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
    int64_t m_navigationFrame = 0;
    bool m_scrubbing = false;
    bool m_scrubDecodeInFlight = false;
    bool m_scrubFinalPending = false;
    int64_t m_latestScrubFrame = -1;
    quint64 m_scrubRequestGeneration = 0;
    int64_t m_scrubDecodeTarget = -1;
    int64_t m_scrubDecodeStartNs = 0;

    // --- M2: waveform and scrub audio ------------------------------------
    QThread* m_waveformThread = nullptr;
    media::WaveformWorker* m_waveformWorker = nullptr;
    media::WaveformData m_waveform;

    QThread* m_scrubAudioThread = nullptr;
    media::ScrubAudioWorker* m_scrubAudioWorker = nullptr;
    std::unique_ptr<audio::ScrubAudioEngine> m_scrubAudio;

    bool m_audioScrubEnabled = true;
    /// Increments per scrub-audio request so the worker can drop stale ones.
    quint64 m_scrubAudioSequence = 0;
    /// Highest sequence already played, so a late older grain is not heard
    /// after a newer one.
    quint64 m_scrubAudioPlayedSequence = 0;
    /// When the outstanding grain request was issued, for latency reporting.
    int64_t m_scrubAudioRequestNs = 0;
    std::deque<int64_t> m_navigationDecodeTargets;
    std::deque<media::VideoFrame> m_navigationPresentationFrames;
    bool m_navigationDecodeInFlight = false;
    int64_t m_navigationDecodeTarget = -1;
    quint64 m_navigationRequestGeneration = 0;
    int64_t m_navigationRequestStartNs = 0;
    int64_t m_droppedFrames = 0;

    // --- Performance diagnostics -----------------------------------------
    // Summarised once a second at debug level rather than logged per frame.
    int64_t m_presentedFrames = 0;
    int64_t m_decodedFrames = 0;
    int64_t m_perfWindowStartNs = 0;
    /// Frames the decoder should stay ahead by, from time and memory. Replaces
    /// the fixed 24, which was untenable above 1080p.
    int m_lookaheadFrames = 0;
    int64_t m_pendingSeekFrame = -1;
    /// The decoder has no more frames to produce for this run. Distinct from
    /// playback being over -- frames already decoded still have to be shown.
    bool m_decoderAtEnd = false;
};

} // namespace atk::playback

Q_DECLARE_METATYPE(atk::playback::PlayerState)
