#pragma once

#include <QString>

namespace atk::media {

/// Version summary of the media libraries this build links, for the startup log.
///
/// Declared in a header that includes no FFmpeg types on purpose. The
/// application layer wants to log which FFmpeg it is running against, but
/// letting it include libav* headers would put an FFmpeg dependency in the
/// executable target and quietly erode the rule that only the media layer
/// talks to FFmpeg. One neutral string crosses the boundary instead.
QString libraryVersionSummary();

} // namespace atk::media
