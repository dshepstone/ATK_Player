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

    /// While drawing, the brush-size keys ([ and ]) are the brush size; at
    /// other times they belong to whatever else shares them (in and out).
    void setDrawing(bool drawing);
    bool isDrawing() const { return m_drawing; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    /// Emitted for every command. `checked` is meaningful only for checkable
    /// commands such as Loop.
    void commandTriggered(atk::commands::CommandId id, bool checked);

private:
    /// Turns off, by shortcut context, whichever side of a shared key is not
    /// in use: the brush size when not drawing, the other command when drawing.
    void applyShortcutContexts();

    QHash<int, QAction*> m_actions;
    bool m_drawing = false;
    QList<commands::CommandId> m_order;
};

} // namespace atk::ui
