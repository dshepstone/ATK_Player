#include "ui/commands/CommandRegistry.h"

#include "core/Logging.h"

#include <QAction>
#include <QCoreApplication>
#include <QKeySequence>

namespace atk::ui {

CommandRegistry::CommandRegistry(QObject* parent)
    : QObject(parent)
{
    for (const commands::CommandDefinition& definition : commands::allCommands()) {
        auto* action = new QAction(
            QCoreApplication::translate("Command", definition.displayName), this);

        action->setObjectName(QString::fromLatin1(definition.key));
        action->setCheckable(definition.checkable);

        if (definition.defaultShortcut != nullptr) {
            action->setShortcut(QKeySequence(QString::fromLatin1(definition.defaultShortcut)));
            // The shortcut must work wherever focus sits inside the window --
            // the viewer, the timeline or the sources list.
            action->setShortcutContext(Qt::WindowShortcut);
        }

        const commands::CommandId id = definition.id;
        connect(action, &QAction::triggered, this, [this, id](bool checked) {
            emit commandTriggered(id, checked);
        });

        m_actions.insert(static_cast<int>(id), action);
        m_order.push_back(id);
    }

    qCDebug(log::ui) << "Command registry built with" << m_actions.size() << "commands";
}

QAction* CommandRegistry::action(commands::CommandId id) const
{
    return m_actions.value(static_cast<int>(id), nullptr);
}

QList<QAction*> CommandRegistry::actionsFor(commands::CommandCategory category) const
{
    QList<QAction*> result;
    for (const commands::CommandDefinition& definition : commands::allCommands()) {
        if (definition.category == category) {
            if (QAction* a = action(definition.id)) {
                result.push_back(a);
            }
        }
    }
    return result;
}

QList<QAction*> CommandRegistry::allActions() const
{
    QList<QAction*> result;
    result.reserve(m_order.size());
    for (const commands::CommandId id : m_order) {
        if (QAction* a = action(id)) {
            result.push_back(a);
        }
    }
    return result;
}

void CommandRegistry::setShortcut(commands::CommandId id, const QKeySequence& shortcut)
{
    if (QAction* a = action(id)) {
        a->setShortcut(shortcut);
        a->setShortcutContext(Qt::WindowShortcut);
    }
}

void CommandRegistry::resetShortcutsToDefaults()
{
    for (const commands::CommandDefinition& definition : commands::allCommands()) {
        QAction* a = action(definition.id);
        if (a == nullptr) {
            continue;
        }
        a->setShortcut(definition.defaultShortcut != nullptr
                           ? QKeySequence(QString::fromLatin1(definition.defaultShortcut))
                           : QKeySequence());
    }
}

void CommandRegistry::applyShortcutOverrides(const QHash<QString, QString>& overrides)
{
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
        const commands::CommandDefinition* definition = commands::find(it.key());
        if (definition == nullptr) {
            qCWarning(log::ui) << "Ignoring shortcut override for unknown command" << it.key();
            continue;
        }
        setShortcut(definition->id, QKeySequence(it.value()));
    }
}

} // namespace atk::ui
