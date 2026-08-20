#pragma once

#include <QWidget>

class QLabel;

namespace atk::timeline { class TimelineModel; }

namespace atk::ui {

/// The readout strip: current frame, total frames, timecode and frame rate.
///
/// It observes a TimelineModel and shows a defined empty state when nothing is
/// loaded, so the fields are never blank or misleading:
///
///     Frame: 0 / 0      Timecode: --:--:--:--      FPS: --
class StatusInfoBar : public QWidget {
    Q_OBJECT

public:
    explicit StatusInfoBar(QWidget* parent = nullptr);
    ~StatusInfoBar() override;

    /// Observes a model; not owned. Passing nullptr detaches and shows the
    /// empty state.
    void setModel(timeline::TimelineModel* model);

private:
    /// Builds a caption + monospaced value pair and appends it to the layout.
    QLabel* addField(const QString& caption, const QString& initialValue, int minimumValueWidth);
    void refresh();

    timeline::TimelineModel* m_model = nullptr;
    QLabel* m_frameValue = nullptr;
    QLabel* m_timecodeValue = nullptr;
    QLabel* m_fpsValue = nullptr;
};

} // namespace atk::ui
