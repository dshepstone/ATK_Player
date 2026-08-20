#pragma once

#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

// ---------------------------------------------------------------------------
// RAII ownership for FFmpeg objects.
//
// Every FFmpeg resource in ATK Player is held by one of the types below. There
// are no bare avformat_close_input / avcodec_free_context / av_frame_free calls
// anywhere else in the codebase.
//
// The reason is not tidiness. Decoding has many early-return error paths, and
// each one is a place a hand-written free can be forgotten; the resulting leak
// only shows up after repeatedly opening files, which is exactly what a review
// player does all day. Making the destructor responsible removes the entire
// class of bug.
//
// FFmpeg's free functions take a pointer-to-pointer and null it, so each
// deleter takes the address of its local copy.
// ---------------------------------------------------------------------------

namespace atk::media::ffmpeg {

// --- AVFormatContext (demuxer) ---------------------------------------------

struct FormatContextDeleter {
    void operator()(AVFormatContext* context) const noexcept
    {
        if (context != nullptr) {
            avformat_close_input(&context);
        }
    }
};
using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;

// --- AVCodecContext (decoder) ----------------------------------------------

struct CodecContextDeleter {
    void operator()(AVCodecContext* context) const noexcept
    {
        if (context != nullptr) {
            avcodec_free_context(&context);
        }
    }
};
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;

// --- AVPacket ---------------------------------------------------------------

struct PacketDeleter {
    void operator()(AVPacket* packet) const noexcept
    {
        if (packet != nullptr) {
            av_packet_free(&packet);
        }
    }
};
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

/// Allocates a packet. Returns null only on allocation failure.
inline PacketPtr makePacket()
{
    return PacketPtr{ av_packet_alloc() };
}

/// Unrefs a packet's payload on scope exit while keeping the packet itself.
///
/// av_read_frame hands back a reference that must be released before the packet
/// is reused, and the release has to happen on every path out of the decode
/// loop -- including the error ones.
class PacketUnrefGuard {
public:
    explicit PacketUnrefGuard(AVPacket* packet) noexcept
        : m_packet(packet)
    {
    }

    ~PacketUnrefGuard()
    {
        if (m_packet != nullptr) {
            av_packet_unref(m_packet);
        }
    }

    PacketUnrefGuard(const PacketUnrefGuard&) = delete;
    PacketUnrefGuard& operator=(const PacketUnrefGuard&) = delete;

private:
    AVPacket* m_packet = nullptr;
};

// --- AVFrame ----------------------------------------------------------------

struct FrameDeleter {
    void operator()(AVFrame* frame) const noexcept
    {
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
    }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

/// Allocates a frame. Returns null only on allocation failure.
inline FramePtr makeFrame()
{
    return FramePtr{ av_frame_alloc() };
}

// --- SwsContext (pixel format conversion) -----------------------------------

struct SwsContextDeleter {
    void operator()(SwsContext* context) const noexcept
    {
        if (context != nullptr) {
            sws_freeContext(context);
        }
    }
};
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

// --- SwrContext (audio resampling) ------------------------------------------

struct SwrContextDeleter {
    void operator()(SwrContext* context) const noexcept
    {
        if (context != nullptr) {
            swr_free(&context);
        }
    }
};
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;

} // namespace atk::media::ffmpeg
