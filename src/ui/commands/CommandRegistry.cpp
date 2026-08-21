#include "ui/commands/CommandRegistry.h"

#include "core/Logging.h"

#include <QAction>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>

namespace atk::ui {

CommandRegistry::CommandRegistry(QObject* parent)
    : QObject(parent)
{
    if (qApp) qApp->installEventFilter(this);
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

bool CommandRegistry::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);
    if (event->type() != QEvent::ShortcutOverride) return false;
    QWidget* focus = QApplication::focusWidget();
    const bool editing = qobject_cast<QLineEdit*>(focus)
        || qobject_cast<QTextEdit*>(focus)
        || qobject_cast<QPlainTextEdit*>(focus)
        || qobject_cast<QAbstractSpinBox*>(focus)
        || qobject_cast<QKeySequenceEdit*>(focus);
    if (!editing) return false;
    auto* key = static_cast<QKeyEvent*>(event);
    const Qt::KeyboardModifiers modifiers = key->modifiers()
        & ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
    const bool typing = modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier;
    const QKeySequence sequence(key->keyCombination());
    const bool standardEdit = sequence.matches(QKeySequence::Copy) == QKeySequence::ExactMatch
        || sequence.matches(QKeySequence::Paste) == QKeySequence::ExactMatch
        || sequence.matches(QKeySequence::Cut) == QKeySequence::ExactMatch
        || sequence.matches(QKeySequence::SelectAll) == QKeySequence::ExactMatch
        || sequence.matches(QKeySequence::Undo) == QKeySequence::ExactMatch
        || sequence.matches(QKeySequence::Redo) == QKeySequence::ExactMatch;
    if (typing || standardEdit) event->accept();
    return false;
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
