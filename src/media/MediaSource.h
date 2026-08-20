#pragma once

#include "media/MediaMetadata.h"
#include "media/VideoFrame.h"
#include "media/decoders/IDecoder.h"

#include <QString>

#include <cstdint>
#include <memory>

namespace atk::media {

/// One piece of media loaded into ATK Player: a file, its probed metadata, its
/// decoder and its per-source frame offset.
///
/// The offset exists for A/B comparison: viewer B may be shifted relative to
/// the master clock so two takes with different handles line up. It applies to
/// this source only and never moves the shared clock.
///
/// This class has no UI dependency and no Qt object identity; it is owned by
/// the Project and referenced by PlaybackController.
class MediaSource {
public:
    explicit MediaSource(QString filePath);
    ~MediaSource();

    MediaSource(const MediaSource&) = delete;
    MediaSource& operator=(const MediaSource&) = delete;

    const QString& filePath() const { return m_filePath; }
    /// File name without directory, for the sources list.
    QString displayName() const;

    /// Opens the underlying decoder and probes metadata.
    ///
    /// PHASE 0: always fails, because FFmpegDecoder is a stub. The failure is
    /// reported through lastError() and logged; callers must handle it.
    bool open();
    void close();
    bool isOpen() const;

    const MediaMetadata& metadata() const;
    QString lastError() const;

    /// Frames added to the master frame number before decoding from this
    /// source. May be negative.
    int64_t frameOffset() const { return m_frameOffset; }
    void setFrameOffset(int64_t offset) { m_frameOffset = offset; }

    /// Decodes the frame corresponding to a master timeline frame, applying
    /// frameOffset(). Returns false when out of range or not yet implemented.
    bool frameAt(int64_t masterFrame, VideoFrame& out);

private:
    QString m_filePath;
    std::unique_ptr<IDecoder> m_decoder;
    MediaMetadata m_emptyMetadata;
    int64_t m_frameOffset = 0;
};

} // namespace atk::media
