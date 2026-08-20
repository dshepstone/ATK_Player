#pragma once

#include "media/MediaMetadata.h"

#include <QString>

#include <cstdint>

namespace atk::media {

/// A media file referenced by a project, together with what is known about it.
///
/// This is a *descriptor*, not a decoder. Decoding is owned by MediaDecoder on
/// the decode thread (see MediaDecoder.h); an object that a project holds in a
/// list must not also own an FFmpeg context, or a playlist would mean many open
/// demuxers and many decode threads.
///
/// The per-source frame offset lives here because it belongs to the source
/// rather than to playback: A/B comparison shifts one take relative to the
/// master clock so two versions of a shot line up (milestone M4).
class MediaSource {
public:
    MediaSource() = default;
    explicit MediaSource(QString filePath);

    const QString& filePath() const { return m_filePath; }

    /// File name without directory, for the sources list.
    QString displayName() const;

    /// What probing found. Empty until the decoder fills it in.
    const MediaMetadata& metadata() const { return m_metadata; }
    void setMetadata(const MediaMetadata& metadata) { m_metadata = metadata; }

    /// True once metadata has been populated by a successful open.
    bool isProbed() const { return m_metadata.isValid(); }

    /// Frames added to the master frame number before reading from this source.
    /// May be negative.
    int64_t frameOffset() const { return m_frameOffset; }
    void setFrameOffset(int64_t offset) { m_frameOffset = offset; }

private:
    QString m_filePath;
    MediaMetadata m_metadata;
    int64_t m_frameOffset = 0;
};

} // namespace atk::media
