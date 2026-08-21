#include "ui/VideoFullscreenWindow.h"

#include "ui/ViewerWidget.h"

#include <QAction>
#include <QCloseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace atk::ui {

VideoFullscreenWindow::VideoFullscreenWindow(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
    , m_cursorTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("VideoFullscreenWindow"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setStyleSheet(QStringLiteral("background: #000000;"));
    setMouseTracking(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_cursorTimer->setSingleShot(true);
    m_cursorTimer->setInterval(2500);
    connect(m_cursorTimer, &QTimer::timeout, this, [this] { setCursor(Qt::BlankCursor); });
}

void VideoFullscreenWindow::hostViewer(ViewerWidget* viewer)
{
    m_viewer = viewer;
    m_viewer->installEventFilter(this);
    layout()->addWidget(m_viewer);
    showCursorTemporarily();
}

ViewerWidget* VideoFullscreenWindow::releaseViewer()
{
    m_cursorTimer->stop();
    unsetCursor();
    if (m_viewer) {
        m_viewer->removeEventFilter(this);
        m_viewer->unsetCursor();
        layout()->removeWidget(m_viewer);
    }
    ViewerWidget* result = m_viewer;
    m_viewer = nullptr;
    return result;
}

void VideoFullscreenWindow::installCommandActions(const QList<QAction*>& actions)
{
    addActions(actions);
}

bool VideoFullscreenWindow::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

bool VideoFullscreenWindow::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);
    if (event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) {
            event->accept();
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) {
            emit exitRequested();
            return true;
        }
    }
    if (event->type() == QEvent::MouseMove) showCursorTemporarily();
    return false;
}

void VideoFullscreenWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        emit exitRequested();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void VideoFullscreenWindow::closeEvent(QCloseEvent* event)
{
    emit exitRequested();
    event->ignore();
}

void VideoFullscreenWindow::showCursorTemporarily()
{
    unsetCursor();
    m_cursorTimer->start();
}

} // namespace atk::ui
