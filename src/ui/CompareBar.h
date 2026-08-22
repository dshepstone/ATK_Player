#pragma once
#include "playback/CompareSession.h"
#include <QWidget>
#include <QUuid>
class QComboBox;
class QLabel;
class QSpinBox;
class QSlider;
namespace atk::project { class Project; }
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
    void setComparisonState(playback::CompareLayout layout, int bOffsetFrames,
                            int audioOffsetFrames, int wipePosition, int blendAmount,
                            bool externalLoaded);
signals:
    void sourceASelected(const QUuid& id);
    void sourceBSelected(const QUuid& id);
    void audioModeSelected(atk::playback::CompareAudioMode mode);
    void viewModeSelected(atk::playback::CompareLayout mode);
    void sourceBOffsetFramesChanged(int frames);
    void externalAudioOffsetFramesChanged(int frames);
    void wipePositionChanged(int percent);
    void blendAmountChanged(int percent);
private:
    project::Project* m_project = nullptr;
    QComboBox* m_sourceA = nullptr;
    QComboBox* m_sourceB = nullptr;
    QComboBox* m_audioMode = nullptr;
    QLabel* m_externalName = nullptr;
    QWidget* m_externalControls = nullptr;
    QComboBox* m_viewMode = nullptr;
    QSpinBox* m_bOffset = nullptr;
    QSpinBox* m_audioOffset = nullptr;
    QSlider* m_wipe = nullptr;
    QSlider* m_blend = nullptr;
    QWidget* m_wipeControls = nullptr;
    QWidget* m_blendControls = nullptr;
};
} // namespace atk::ui
