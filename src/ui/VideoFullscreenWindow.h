#pragma once

#include <QWidget>
#include <QList>

class QAction;
class QCloseEvent;
class QEvent;
class QKeyEvent;
class QTimer;

namespace atk::ui {

class ViewerWidget;

/// Transient, video-only top-level host for the application's one ViewerWidget.
class VideoFullscreenWindow final : public QWidget {
    Q_OBJECT

public:
    explicit VideoFullscreenWindow(QWidget* parent = nullptr);
    void hostViewer(ViewerWidget* viewer);
    ViewerWidget* releaseViewer();
    void installCommandActions(const QList<QAction*>& actions);

signals:
    void exitRequested();

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void showCursorTemporarily();

    ViewerWidget* m_viewer = nullptr;
    QTimer* m_cursorTimer = nullptr;
};

} // namespace atk::ui
