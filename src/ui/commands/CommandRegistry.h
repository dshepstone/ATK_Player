#pragma once

#include "core/commands/CommandDefinitions.h"
#include "core/commands/CommandId.h"

#include <QHash>
#include <QList>
#include <QObject>

class QAction;
class QKeySequence;

namespace atk::ui {

/// Owns one QAction per command and routes every trigger through a single
/// signal.
///
/// Widgets never install their own key handlers. A button, a menu item and a
/// key press all end at the same QAction, so the three can never disagree about
/// what a command does or whether it is currently available.
///
/// Because shortcuts live on the QActions built from one table
/// (core/commands/CommandDefinitions.cpp), making them user-configurable in M2
/// means loading overrides here -- no widget has to change.
class CommandRegistry : public QObject {
    Q_OBJECT

public:
    explicit CommandRegistry(QObject* parent = nullptr);

    /// The action for a command. Never null for a defined command.
    QAction* action(commands::CommandId id) const;

    /// Actions in the given menu category, in table order.
    QList<QAction*> actionsFor(commands::CommandCategory category) const;

    /// All actions, so a window can add them once and get application-wide
    /// shortcuts.
    QList<QAction*> allActions() const;

    /// Replaces a shortcut at runtime. Passing an empty sequence clears it.
    void setShortcut(commands::CommandId id, const QKeySequence& shortcut);

    /// Restores every shortcut to the value in the command table.
    void resetShortcutsToDefaults();

    /// Applies persisted overrides keyed by CommandDefinition::key. Unknown
    /// keys are ignored, so an override file from a newer build is harmless.
    /// TODO(M2): call this from preferences once the shortcut editor exists.
    void applyShortcutOverrides(const QHash<QString, QString>& overrides);

signals:
    /// Emitted for every command. `checked` is meaningful only for checkable
    /// commands such as Loop.
    void commandTriggered(atk::commands::CommandId id, bool checked);

private:
    QHash<int, QAction*> m_actions;
    QList<commands::CommandId> m_order;
};

} // namespace atk::ui
