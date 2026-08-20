#pragma once

#include "core/commands/CommandId.h"

#include <QString>

#include <span>

namespace atk::commands {

/// Where a command appears in the menu bar. Used to build menus from the same
/// table that supplies shortcuts, so the two can never drift apart.
enum class CommandCategory {
    File,
    Playback,
    Audio,
    View,
    Help,
};

/// Static description of one command.
///
/// `key` is the stable identifier used by persisted shortcut overrides and by
/// the external API (docs/API.md). It must not change once released, even if
/// the display name does.
struct CommandDefinition {
    CommandId id;
    const char* key;
    const char* displayName;
    CommandCategory category;
    /// Portable QKeySequence string ("Space", "Ctrl+O"), or nullptr for none.
    const char* defaultShortcut;
    /// True for on/off commands such as Loop, which map to checkable QActions.
    bool checkable;
};

/// The single source of truth for all commands, in menu order.
std::span<const CommandDefinition> allCommands();

/// Returns nullptr if the command is not defined.
const CommandDefinition* find(CommandId id);

/// Lookup by stable string key, for settings and the external API.
const CommandDefinition* find(QStringView key);

/// Human-readable category name for menu titles.
QString categoryTitle(CommandCategory category);

} // namespace atk::commands
