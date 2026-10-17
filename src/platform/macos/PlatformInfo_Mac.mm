#include "platform/PlatformInfo.h"

#include "core/Logging.h"

#include <QDir>
#include <QStandardPaths>
#include <QSysInfo>

#import <Foundation/Foundation.h>

// macOS-only implementation, Objective-C++ so it can reach Foundation. Built
// without ARC: retain and release are explicit.

namespace {

/// The activity held while playing; nil otherwise.
id<NSObject> g_playbackActivity = nil;

} // namespace

namespace atk::platform {

QString PlatformInfo::operatingSystemDescription()
{
    return QStringLiteral("%1 (%2)")
        .arg(QSysInfo::prettyProductName(), QSysInfo::productVersion());
}

QString PlatformInfo::applicationDataDirectory()
{
    // ~/Library/Application Support/ATK/ATK Player
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(path);
    return path;
}

bool PlatformInfo::setDisplaySleepInhibited(bool inhibited)
{
    // One activity covers both halves of "playing": the display stays awake,
    // and App Nap stays away. Without the latter macOS coalesces the
    // display timer of an app whose window is hidden or covered, and playback
    // drops to a fraction of its frame rate.
    if (inhibited == (g_playbackActivity != nil)) return true;
    NSProcessInfo* process = [NSProcessInfo processInfo];
    if (inhibited) {
        const NSActivityOptions options = NSActivityUserInitiated | NSActivityIdleDisplaySleepDisabled
            | NSActivityLatencyCritical;
        g_playbackActivity = [[process beginActivityWithOptions:options
                                                         reason:@"ATK Player playback"] retain];
        qCDebug(log::platform) << "Playback activity started (no display sleep, no App Nap)";
    } else {
        [process endActivity:g_playbackActivity];
        [g_playbackActivity release];
        g_playbackActivity = nil;
        qCDebug(log::platform) << "Playback activity ended";
    }
    return true;
}

bool PlatformInfo::isFileOpenElsewhere(const QString& path)
{
    // Open files do not stop a file being replaced here, and a viewer that
    // has it open keeps reading its own copy safely.
    Q_UNUSED(path);
    return false;
}

} // namespace atk::platform
