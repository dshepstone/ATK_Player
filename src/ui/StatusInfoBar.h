#pragma once

#include <QList>
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

    /// Names the loaded media and says whether its frame count is exact.
    /// An estimated count is marked, because the last frame of an estimate may
    /// not be reachable and the user is entitled to know that.
    void setMediaInfo(const QString& fileName, bool frameCountIsExact);

    /// Returns to the no-media readout.
    void clearMediaInfo();

    /// Shows a status bar message (empty clears it). The strip fills the
    /// status bar, so QStatusBar has no room to draw its own messages; they
    /// are shown here instead.
    void setMessage(const QString& message);

    /// Hides or shows the frame, timecode, FPS and media fields. The message
    /// stays: the Review Library has no frame to report, but saves still say so.
    void setReadoutVisible(bool visible);

private:
    /// Builds a caption + monospaced value pair and appends it to the layout.
    QLabel* addField(const QString& caption, const QString& initialValue, int minimumValueWidth);
    void refresh();

    timeline::TimelineModel* m_model = nullptr;
    QLabel* m_frameValue = nullptr;
    QLabel* m_timecodeValue = nullptr;
    QLabel* m_fpsValue = nullptr;
    /// Shown whenever the numbers to its left describe a placeholder extent
    /// rather than an open file.
    QLabel* m_placeholderTag = nullptr;
    QLabel* m_mediaValue = nullptr;
    QLabel* m_message = nullptr;
    QList<QWidget*> m_readout;
    bool m_frameCountIsExact = true;
    bool m_readoutVisible = true;
};

} // namespace atk::ui
