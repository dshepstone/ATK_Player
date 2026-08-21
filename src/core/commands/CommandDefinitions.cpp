#include "core/commands/CommandDefinitions.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>

namespace atk::commands {
namespace {

using C = CommandCategory;

// ---------------------------------------------------------------------------
// The command table.
//
// Default shortcuts required by the Phase 0 specification:
//   Space  Play/Pause      Home  First Frame     I  Set Range In
//   Left   Previous Frame  End   Last Frame      O  Set Range Out
//   Right  Next Frame      L     Toggle Loop     B  Add Bookmark
//   Esc    Stop / cancel the current temporary action
//
// Adding a command means adding one row here; menus, shortcuts and the API
// command surface all derive from this array.
// ---------------------------------------------------------------------------
constexpr std::array kCommands{
    // File
    CommandDefinition{ CommandId::OpenMedia,          "file.openMedia",        QT_TRANSLATE_NOOP("Command", "Open Media..."),        C::File,     "Ctrl+O",       false },
    CommandDefinition{ CommandId::OpenProject,        "file.openProject",      QT_TRANSLATE_NOOP("Command", "Open Project..."),      C::File,     "Ctrl+Shift+O", false },
    CommandDefinition{ CommandId::SaveProject,        "file.saveProject",      QT_TRANSLATE_NOOP("Command", "Save Project"),         C::File,     "Ctrl+S",       false },
    CommandDefinition{ CommandId::SaveProjectAs,      "file.saveProjectAs",    QT_TRANSLATE_NOOP("Command", "Save Project As..."),   C::File,     "Ctrl+Shift+S", false },
    CommandDefinition{ CommandId::CloseSource,        "file.closeSource",      QT_TRANSLATE_NOOP("Command", "Close Source"),         C::File,     "Ctrl+W",       false },
    CommandDefinition{ CommandId::Quit,               "file.quit",             QT_TRANSLATE_NOOP("Command", "Quit"),                 C::File,     "Ctrl+Q",       false },

    // Playback
    CommandDefinition{ CommandId::PlayPause,          "playback.playPause",    QT_TRANSLATE_NOOP("Command", "Play / Pause"),         C::Playback, "Space",        false },
    CommandDefinition{ CommandId::Stop,               "playback.stop",         QT_TRANSLATE_NOOP("Command", "Stop"),                 C::Playback, "Esc",          false },
    CommandDefinition{ CommandId::PreviousFrame,      "playback.previousFrame",QT_TRANSLATE_NOOP("Command", "Previous Frame"),       C::Playback, "Left",         false },
    CommandDefinition{ CommandId::NextFrame,          "playback.nextFrame",    QT_TRANSLATE_NOOP("Command", "Next Frame"),           C::Playback, "Right",        false },
    CommandDefinition{ CommandId::FirstFrame,         "playback.firstFrame",   QT_TRANSLATE_NOOP("Command", "First Frame"),          C::Playback, "Home",         false },
    CommandDefinition{ CommandId::LastFrame,          "playback.lastFrame",    QT_TRANSLATE_NOOP("Command", "Last Frame"),           C::Playback, "End",          false },
    CommandDefinition{ CommandId::ToggleLoop,         "playback.toggleLoop",   QT_TRANSLATE_NOOP("Command", "Loop"),                 C::Playback, "L",            true  },
    CommandDefinition{ CommandId::SetRangeIn,         "playback.setRangeIn",   QT_TRANSLATE_NOOP("Command", "Set Range In"),         C::Playback, "I",            false },
    CommandDefinition{ CommandId::SetRangeOut,        "playback.setRangeOut",  QT_TRANSLATE_NOOP("Command", "Set Range Out"),        C::Playback, "O",            false },
    CommandDefinition{ CommandId::ClearRange,         "playback.clearRange",   QT_TRANSLATE_NOOP("Command", "Clear Range"),          C::Playback, nullptr,        false },
    CommandDefinition{ CommandId::AddBookmark,        "playback.addBookmark",  QT_TRANSLATE_NOOP("Command", "Add Bookmark"),         C::Playback, "B",            false },
    CommandDefinition{ CommandId::AddRangeBookmark,   "playback.addRangeBookmark",QT_TRANSLATE_NOOP("Command", "Add Range Bookmark"),C::Playback, "Shift+B",      false },
    CommandDefinition{ CommandId::NextBookmark,       "playback.nextBookmark", QT_TRANSLATE_NOOP("Command", "Next Bookmark"),        C::Playback, "Alt+Right",    false },
    CommandDefinition{ CommandId::PreviousBookmark,   "playback.prevBookmark", QT_TRANSLATE_NOOP("Command", "Previous Bookmark"),    C::Playback, "Alt+Left",     false },
    CommandDefinition{ CommandId::DeleteBookmark,     "playback.deleteBookmark",QT_TRANSLATE_NOOP("Command", "Delete Bookmark"),     C::Playback, "Ctrl+Shift+B", false },
    CommandDefinition{ CommandId::ToggleBookmarkSnap, "playback.snapBookmarks", QT_TRANSLATE_NOOP("Command", "Snap to Bookmarks"),   C::Playback, nullptr,        true  },

    // Audio
    CommandDefinition{ CommandId::ToggleMute,         "audio.toggleMute",      QT_TRANSLATE_NOOP("Command", "Mute"),                 C::Audio,    "M",            true  },
    CommandDefinition{ CommandId::ToggleAudioScrub,   "audio.toggleScrub",     QT_TRANSLATE_NOOP("Command", "Audio Scrub"),          C::Audio,    nullptr,        true  },
    CommandDefinition{ CommandId::ToggleFrameStepAudio,"audio.toggleFrameStep",QT_TRANSLATE_NOOP("Command", "Frame-step Audio"),     C::Audio,    nullptr,        true  },
    CommandDefinition{ CommandId::VolumeUp,           "audio.volumeUp",        QT_TRANSLATE_NOOP("Command", "Volume Up"),            C::Audio,    "Ctrl+Up",      false },
    CommandDefinition{ CommandId::VolumeDown,         "audio.volumeDown",      QT_TRANSLATE_NOOP("Command", "Volume Down"),          C::Audio,    "Ctrl+Down",    false },

    // View
    CommandDefinition{ CommandId::ZoomIn,             "view.zoomIn",           QT_TRANSLATE_NOOP("Command", "Zoom In"),              C::View,     "Ctrl+=",       false },
    CommandDefinition{ CommandId::ZoomOut,            "view.zoomOut",          QT_TRANSLATE_NOOP("Command", "Zoom Out"),             C::View,     "Ctrl+-",       false },
    CommandDefinition{ CommandId::ZoomFit,            "view.zoomFit",          QT_TRANSLATE_NOOP("Command", "Viewer Fit"),           C::View,     "Ctrl+0",       false },
    CommandDefinition{ CommandId::ZoomActualSize,     "view.zoomActualSize",   QT_TRANSLATE_NOOP("Command", "Viewer 100%"),          C::View,     "Ctrl+1",       false },
    CommandDefinition{ CommandId::ToggleFullScreen,   "view.toggleFullScreen", QT_TRANSLATE_NOOP("Command", "Full Screen"),          C::View,     "F11",          true  },
    CommandDefinition{ CommandId::ToggleSourcesPanel, "view.toggleSources",    QT_TRANSLATE_NOOP("Command", "Sources Panel"),        C::View,     "F4",           true  },
    CommandDefinition{ CommandId::ToggleBookmarksPanel,"view.toggleBookmarks", QT_TRANSLATE_NOOP("Command", "Bookmarks Panel"),      C::View,     "F5",           true  },
    CommandDefinition{ CommandId::TimelineZoomIn,     "view.timelineZoomIn",   QT_TRANSLATE_NOOP("Command", "Timeline Zoom In"),     C::View,     "=",            false },
    CommandDefinition{ CommandId::TimelineZoomOut,    "view.timelineZoomOut",  QT_TRANSLATE_NOOP("Command", "Timeline Zoom Out"),    C::View,     "-",            false },
    CommandDefinition{ CommandId::TimelineZoomFit,    "view.timelineZoomFit",  QT_TRANSLATE_NOOP("Command", "Fit Entire Timeline"), C::View,     "F",            false },

    // Help
    CommandDefinition{ CommandId::About,              "help.about",            QT_TRANSLATE_NOOP("Command", "About ATK Player"),     C::Help,     nullptr,        false },
};

} // namespace

std::span<const CommandDefinition> allCommands()
{
    return { kCommands.data(), kCommands.size() };
}

const CommandDefinition* find(CommandId id)
{
    const auto it = std::find_if(kCommands.begin(), kCommands.end(),
                                 [id](const CommandDefinition& d) { return d.id == id; });
    return it == kCommands.end() ? nullptr : &(*it);
}

const CommandDefinition* find(QStringView key)
{
    const auto it = std::find_if(kCommands.begin(), kCommands.end(),
                                 [key](const CommandDefinition& d) {
                                     return QLatin1StringView(d.key) == key;
                                 });
    return it == kCommands.end() ? nullptr : &(*it);
}

QString categoryTitle(CommandCategory category)
{
    switch (category) {
    case CommandCategory::File:     return QCoreApplication::translate("Command", "&File");
    case CommandCategory::Playback: return QCoreApplication::translate("Command", "&Playback");
    case CommandCategory::Audio:    return QCoreApplication::translate("Command", "&Audio");
    case CommandCategory::View:     return QCoreApplication::translate("Command", "&View");
    case CommandCategory::Help:     return QCoreApplication::translate("Command", "&Help");
    }
    return {};
}

} // namespace atk::commands
