#pragma once

#include <QVector>

#include <cstdint>

namespace atk::media {

/// One waveform bucket: the extremes of every sample it covers.
///
/// min/max rather than a single RMS value because a review tool needs to see
/// transients. An impact or a plosive is a brief excursion that RMS averages
/// away, and those are exactly the events an animator lines action up against.
///
/// Stored as normalised floats in [-1, 1]. Eight bytes per bucket, which is what
/// keeps a two-hour file's waveform in a few megabytes.
struct WaveformPeak {
    float minimum = 0.0f;
    float maximum = 0.0f;

    /// Half the peak-to-peak excursion -- the drawn height of this bucket.
    float amplitude() const { return (maximum - minimum) * 0.5f; }

    bool isSilent() const { return maximum <= 0.0001f && minimum >= -0.0001f; }
};

/// Amplitude peaks for a file's audio, at several resolutions.
///
/// WHY A PYRAMID
/// -------------
/// The timeline draws one bucket per pixel, give or take. A 90-minute file at
/// the finest resolution is far more buckets than a 1200-pixel widget can show,
/// so drawing it directly would mean iterating tens of thousands of values to
/// produce each column -- on every repaint, including every playhead move.
///
/// Storing progressively halved levels means the renderer can pick the level
/// whose bucket duration is closest to one pixel and read it more or less
/// straight out. Levels cost 50% more memory in total than level 0 alone
/// (1 + 1/2 + 1/4 + ... converges to 2x, and the tail is truncated), which is a
/// good trade for making repaint proportional to pixels rather than duration.
///
/// It is also the groundwork for timeline zoom in a later milestone: zooming in
/// selects a finer level rather than needing a different data structure.
///
/// TIME MAPPING
/// ------------
/// Bucket k of level L covers media time
///     [k * bucketDurationUs(L), (k+1) * bucketDurationUs(L))
/// measured from the start of the *media*, not from the first audio sample. The
/// worker subtracts the audio stream's start timestamp before bucketing, so a
/// container whose audio and video begin at different timestamps still lines the
/// waveform up with the picture.
class WaveformData {
public:
    /// Duration covered by one level-0 bucket.
    ///
    /// 10 ms is a little finer than one frame at 24 fps, so a single frame is
    /// always more than one bucket and frame-level detail survives. It is also
    /// roughly the shortest interval in which a distinct speech sound occurs, so
    /// syllable structure stays visible.
    static constexpr int64_t kBaseBucketUs = 10'000;

    /// Number of levels kept. Level n covers 2^n base buckets, so level 7
    /// covers 1.28 s -- coarse enough for a multi-hour file in a small widget.
    static constexpr int kLevelCount = 8;

    bool isEmpty() const { return m_levels.isEmpty() || m_levels.at(0).isEmpty(); }

    /// Which source this waveform belongs to. Peaks from a file that is no
    /// longer open must never be drawn under the current one.
    uint64_t sourceGeneration() const { return m_sourceGeneration; }
    void setSourceGeneration(uint64_t generation);

    /// Media duration covered so far. Grows while analysis runs.
    int64_t coveredUs() const;

    /// True once the whole stream has been analysed.
    bool isComplete() const { return m_complete; }
    void setComplete(bool complete) { m_complete = complete; }

    /// Appends level-0 buckets and folds them up through the pyramid.
    void appendBaseBuckets(const QVector<WaveformPeak>& buckets);

    /// Duration one bucket covers at `level`.
    static int64_t bucketDurationUs(int level);

    /// The level whose buckets are closest to `targetUs` without being finer,
    /// so a repaint reads roughly one bucket per pixel.
    int levelForBucketDuration(int64_t targetUs) const;

    const QVector<WaveformPeak>& level(int index) const;
    int levelCount() const { return m_levels.size(); }

    /// Peak covering `mediaUs` at `level`. Silent when out of range, so callers
    /// never need to bounds-check while drawing.
    WaveformPeak peakAt(int level, int64_t mediaUs) const;

    /// Combined peak across [startUs, endUs) at `level`. This is what the
    /// renderer calls per pixel column.
    WaveformPeak peakOverRange(int level, int64_t startUs, int64_t endUs) const;

    /// Largest excursion anywhere in the analysed audio, in [0, 1].
    ///
    /// The renderer scales by this. Without it, material mastered well below
    /// full scale -- which most dialogue is -- draws as a thin flat band, and a
    /// single loud effect elsewhere in the file flattens every line of speech
    /// next to it. Normalising by the file's own peak makes quiet dialogue
    /// legible without changing where anything sits in time.
    float peakAmplitude() const { return m_peakAmplitude; }

    /// Gain the renderer should apply, derived from peakAmplitude().
    ///
    /// Clamped so near-silent audio is not amplified into visual noise: a file
    /// that really is quiet should look quiet.
    float displayGain() const;

    /// Approximate memory held, for diagnostics.
    int64_t memoryBytes() const;

    void clear();

private:
    /// m_levels[0] is the finest. Each subsequent level halves the count by
    /// combining pairs, so a bucket's extremes always survive upward.
    QVector<QVector<WaveformPeak>> m_levels;
    uint64_t m_sourceGeneration = 0;
    bool m_complete = false;
    float m_peakAmplitude = 0.0f;
};

} // namespace atk::media
