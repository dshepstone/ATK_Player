#pragma once

#include <QWidget>
#include <QList>

class QAction;
class QCloseEvent;
class QEvent;
class QKeyEvent;
class QTimer;

namespace atk::ui {

/// Transient, video-only top-level host for a viewer or comparison surface.
class VideoFullscreenWindow final : public QWidget {
    Q_OBJECT

public:
    explicit VideoFullscreenWindow(QWidget* parent = nullptr);
    void hostPresentation(QWidget* presentation);
    QWidget* releasePresentation();
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

    QWidget* m_presentation = nullptr;
    QTimer* m_cursorTimer = nullptr;
};

} // namespace atk::ui
