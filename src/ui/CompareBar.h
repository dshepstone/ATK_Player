#pragma once
#include <QWidget>
#include <QUuid>
class QComboBox;
namespace atk::project { class Project; }
namespace atk::ui { class CommandRegistry; }
namespace atk::ui {
class CompareBar final : public QWidget {
    Q_OBJECT
public:
    CompareBar(CommandRegistry* commands, QWidget* parent = nullptr);
    void setProject(project::Project* project);
    void refresh(const QUuid& sourceAId, const QUuid& sourceBId);
signals:
    void sourceASelected(const QUuid& id);
    void sourceBSelected(const QUuid& id);
private:
    project::Project* m_project = nullptr;
    QComboBox* m_sourceA = nullptr;
    QComboBox* m_sourceB = nullptr;
};
} // namespace atk::ui
