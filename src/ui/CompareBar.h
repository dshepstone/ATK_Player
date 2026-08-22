#pragma once
#include <QWidget>
#include <QUuid>
class QComboBox;
class QLabel;
namespace atk::project { class Project; }
namespace atk::playback { enum class CompareAudioMode; }
namespace atk::ui { class CommandRegistry; }
namespace atk::ui {
class CompareBar final : public QWidget {
    Q_OBJECT
public:
    CompareBar(CommandRegistry* commands, QWidget* parent = nullptr);
    void setProject(project::Project* project);
    void refresh(const QUuid& sourceAId, const QUuid& sourceBId);
    void setAudioState(playback::CompareAudioMode mode, const QString& path,
                       bool aHasAudio, bool bHasAudio);
signals:
    void sourceASelected(const QUuid& id);
    void sourceBSelected(const QUuid& id);
    void audioModeSelected(atk::playback::CompareAudioMode mode);
private:
    project::Project* m_project = nullptr;
    QComboBox* m_sourceA = nullptr;
    QComboBox* m_sourceB = nullptr;
    QComboBox* m_audioMode = nullptr;
    QLabel* m_externalName = nullptr;
    QWidget* m_externalControls = nullptr;
};
} // namespace atk::ui
