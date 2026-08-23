#pragma once

#include <QWidget>

class QSpinBox;

namespace atk::ui {

/// One-based frame entry backed by the application's zero-based playhead.
/// This widget owns no playback state; confirmed edits request a controller seek.
class FrameNumberInput final : public QWidget {
    Q_OBJECT

public:
    explicit FrameNumberInput(QWidget* parent = nullptr);

    void setFrameCount(qint64 count);
    void setCurrentFrame(qint64 zeroBasedFrame);
    void setMediaAvailable(bool available);
    int visibleFrame() const;
    int maximumVisibleFrame() const;

signals:
    void seekFrameRequested(qint64 zeroBasedFrame);

private:
    void updateFieldWidth();

    QSpinBox* m_spinBox = nullptr;
    qint64 m_frameCount = 0;
    bool m_mediaAvailable = false;
    bool m_commitPending = false;
};

} // namespace atk::ui
