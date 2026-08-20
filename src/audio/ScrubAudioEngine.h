#pragma once

#include "media/AudioBuffer.h"

#include <QByteArray>
#include <QMutex>
#include <QObject>

#include <memory>

class QAudioSink;
class QIODevice;

namespace atk::audio {

/// Plays short audio grains while the user drags the timeline.
///
/// WHY THIS IS NOT THE PLAYBACK AUDIO PATH
/// ---------------------------------------
/// Normal playback and scrubbing want opposite things. Playback wants a
/// continuous stream whose consumption *is* the master clock. Scrubbing wants
/// short bursts at positions the mouse chooses, where the clock is the pointer
/// and the audio follows. Driving both through one ring buffer would mean the
/// scrub audio's consumption moved the playhead, which is precisely backwards,
/// and would leave scrub PCM in the buffer for normal playback to pick up
/// afterwards.
///
/// So scrubbing gets its own QAudioSink and its own buffer, and the playback
/// path is left untouched. Scrub audio never becomes a clock.
///
/// WHY GRAINS, AND WHY THE SINK IS PERSISTENT
/// ------------------------------------------
/// Each scrub position produces a short snippet -- long enough to be a
/// recognisable speech sound, short enough to track the pointer. The sink is
/// started once and kept running: stopping and restarting a QAudioSink per
/// mouse move costs milliseconds of device latency each time, which is the
/// difference between hearing a syllable and hearing a stutter.
///
/// Latest-position-wins: a newly submitted grain replaces whatever has not been
/// played yet, so dragging quickly does not build a backlog of stale audio
/// trailing the cursor.
class ScrubAudioEngine : public QObject {
    Q_OBJECT

public:
    explicit ScrubAudioEngine(QObject* parent = nullptr);
    ~ScrubAudioEngine() override;

    /// Grain length requested from the decoder.
    ///
    /// 80 ms is long enough to carry a syllable or a consonant burst -- below
    /// about 40 ms speech stops being identifiable and every grain sounds like
    /// a click -- and short enough that a slow drag produces a recognisable
    /// progression rather than overlapping words.
    static constexpr int64_t kGrainDurationUs = 80'000;

    /// Fade applied to each end of a grain.
    ///
    /// PCM cut at an arbitrary sample almost never starts or ends at zero, and
    /// that discontinuity is an audible click on every grain. 3 ms is long
    /// enough to remove it and short enough not to blunt a consonant's attack,
    /// which is the transient a lip-sync review is listening for.
    static constexpr int64_t kFadeUs = 3'000;

    /// Opens the output device. Returns false when none is available, which is
    /// normal in CI and must leave visual scrubbing working.
    bool open(const media::AudioFormat& format);
    bool isOpen() const { return m_sink != nullptr; }
    void close();

    media::AudioFormat format() const { return m_format; }

    /// Queues a grain, replacing any not yet played. `pcm` must be in the
    /// format passed to open(). Fades are applied here, not by the caller.
    ///
    /// `reversed` plays the grain backwards, for a backward drag. Reversal
    /// happens here rather than in the worker so the grain cache stays
    /// direction-neutral -- the same decoded PCM serves a drag in either
    /// direction, which matters because review scrubbing changes direction
    /// constantly.
    ///
    /// Only the sample frames are reversed, never the bytes within a sample and
    /// never the channel order: reversing at byte level would produce noise,
    /// and reversing channels would swap left and right.
    void submitGrain(const QByteArray& pcm, bool reversed = false);

    /// Reverses interleaved PCM by sample frame. Exposed for testing.
    static QByteArray reverseFrames(const QByteArray& pcm, int channelCount);

    /// Drops queued audio and silences output, without closing the device.
    /// Called when a drag ends so no grain outlives the gesture.
    void flush();

    void setVolume(qreal volume);
    void setMuted(bool muted);
    bool isMuted() const { return m_muted; }

    /// Grains submitted, and grains dropped because a newer one replaced them.
    /// Diagnostics for whether coalescing is doing its job.
    int64_t submittedGrains() const { return m_submittedGrains; }
    int64_t replacedGrains() const { return m_replacedGrains; }

private:
    void applyVolume();

    /// Shapes the grain's ends so it does not click.
    QByteArray withFades(const QByteArray& pcm) const;

    std::unique_ptr<QAudioSink> m_sink;
    /// Owned by this object; handed to the sink for the device's lifetime.
    QIODevice* m_device = nullptr;

    media::AudioFormat m_format;
    bool m_muted = false;
    qreal m_volume = 1.0;

    int64_t m_submittedGrains = 0;
    int64_t m_replacedGrains = 0;
};

} // namespace atk::audio
