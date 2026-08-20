#pragma once

#include "media/MediaMetadata.h"
#include "media/VideoFrame.h"

#include <QString>

#include <cstdint>
#include <memory>

namespace atk::media {

/// Backend-neutral decoding interface.
///
/// PlaybackController and MediaSource depend only on this interface, never on
/// FFmpeg types, so an alternative backend (platform hardware decoders, an
/// image-sequence reader) can be added without touching playback logic.
///
/// Implementations are not required to be thread-safe. A decoder instance is
/// owned by exactly one MediaSource and, from M1, driven from a dedicated
/// decode thread.
class IDecoder {
public:
    virtual ~IDecoder() = default;

    IDecoder(const IDecoder&) = delete;
    IDecoder& operator=(const IDecoder&) = delete;

    /// Opens the file and probes it. Returns false and leaves the decoder
    /// closed on failure; call lastError() for a user-facing message.
    virtual bool open(const QString& filePath) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    /// Valid only while isOpen() is true.
    virtual const MediaMetadata& metadata() const = 0;

    /// Decodes the given zero-based frame, seeking if it is not the next one in
    /// sequence. Returns false if the frame does not exist or decoding failed.
    virtual bool decodeFrame(int64_t frameNumber, VideoFrame& out) = 0;

    /// Human-readable description of the most recent failure.
    virtual QString lastError() const = 0;

protected:
    IDecoder() = default;
};

/// Creates the decoder appropriate for a file.
///
/// Phase 0 always returns an FFmpegDecoder. Once image sequences and other
/// backends exist this dispatches on extension and probe results.
std::unique_ptr<IDecoder> createDecoderForFile(const QString& filePath);

} // namespace atk::media
