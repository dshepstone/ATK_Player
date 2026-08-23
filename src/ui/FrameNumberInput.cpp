#include "ui/FrameNumberInput.h"
#include "ui/Theme.h"

#include <QAbstractSpinBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>

#include <algorithm>
#include <limits>

namespace atk::ui {
namespace {

constexpr int kMinimumFieldWidth = 64;
constexpr int kFieldHorizontalChrome = 40;

class CancelableSpinBox final : public QSpinBox {
public:
    explicit CancelableSpinBox(QWidget* parent = nullptr)
        : QSpinBox(parent)
    {
        lineEdit()->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == lineEdit() && event->type() == QEvent::FocusIn) {
            m_valueBeforeEdit = value();
            m_editing = true;
        } else if (watched == lineEdit() && event->type() == QEvent::FocusOut) {
            m_editing = false;
        } else if (watched == lineEdit() && event->type() == QEvent::KeyPress
                   && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            setValue(m_valueBeforeEdit);
            m_editing = false;
            lineEdit()->clearFocus();
            clearFocus();
            parentWidget()->setFocus();
            event->accept();
            return true;
        } else if (watched == lineEdit() && event->type() == QEvent::KeyPress && !m_editing) {
            m_valueBeforeEdit = value();
            m_editing = true;
        }
        return QSpinBox::eventFilter(watched, event);
    }

private:
    int m_valueBeforeEdit = 1;
    bool m_editing = false;
};

int spinMaximum(qint64 count)
{
    return static_cast<int>(std::clamp<qint64>(count, 1, std::numeric_limits<int>::max()));
}

} // namespace

FrameNumberInput::FrameNumberInput(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("FrameNumberInput"));
    setFocusPolicy(Qt::StrongFocus);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 6, 0);
    layout->setSpacing(5);

    auto* label = new QLabel(tr("FRAME"), this);
    label->setProperty("atkRole", "statusCaption");
    layout->addWidget(label);

    m_spinBox = new CancelableSpinBox(this);
    m_spinBox->setObjectName(QStringLiteral("CurrentFrameNumber"));
    m_spinBox->setRange(1, 1);
    m_spinBox->setAlignment(Qt::AlignCenter);
    m_spinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spinBox->setKeyboardTracking(false);
    m_spinBox->setFixedHeight(26);
    m_spinBox->setStyleSheet(QStringLiteral(R"CSS(
        QSpinBox#CurrentFrameNumber {
            background-color: %1;
            color: %2;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 2px 10px;
            selection-background-color: %3;
        }
        QSpinBox#CurrentFrameNumber:hover {
            background-color: %4;
            border-color: %5;
        }
        QSpinBox#CurrentFrameNumber:focus {
            background-color: %4;
            border-color: %2;
        }
        QSpinBox#CurrentFrameNumber:disabled {
            background-color: %6;
            color: %7;
            border-color: transparent;
        }
    )CSS")
        .arg(theme::controlBackground().name(), theme::accent().name(),
             theme::accentMuted().name(), theme::controlHover().name(),
             theme::panelBorder().name(), theme::panelBackground().name(),
             theme::textDisabled().name()));
    updateFieldWidth();
    m_spinBox->setToolTip(tr("Current visible frame. Type a frame number and press Enter."));
    m_spinBox->setEnabled(false);
    layout->addWidget(m_spinBox);

    auto* editor = m_spinBox->findChild<QLineEdit*>();
    connect(editor, &QLineEdit::returnPressed, this, [this, editor] {
        if (m_commitPending) return;
        if (!m_mediaAvailable || m_frameCount <= 0) return;
        m_commitPending = true;
        m_spinBox->interpretText();
        emit seekFrameRequested(static_cast<qint64>(m_spinBox->value()) - 1);
        QTimer::singleShot(0, m_spinBox, [this, editor] {
            editor->clearFocus();
            m_spinBox->clearFocus();
            setFocus();
            m_commitPending = false;
        });
    });
}

void FrameNumberInput::setFrameCount(qint64 count)
{
    m_frameCount = std::max<qint64>(0, count);
    QSignalBlocker blocker(m_spinBox);
    m_spinBox->setRange(1, spinMaximum(m_frameCount));
    m_spinBox->setValue(std::clamp(m_spinBox->value(), 1, m_spinBox->maximum()));
    updateFieldWidth();
    m_spinBox->setEnabled(m_mediaAvailable && m_frameCount > 0);
}

void FrameNumberInput::updateFieldWidth()
{
    const QString maximumText = QString::number(m_spinBox->maximum());
    const int measuredWidth = QFontMetrics(m_spinBox->font()).horizontalAdvance(maximumText)
        + kFieldHorizontalChrome;
    m_spinBox->setFixedWidth(std::max(kMinimumFieldWidth, measuredWidth));
}

void FrameNumberInput::setCurrentFrame(qint64 zeroBasedFrame)
{
    if (m_frameCount <= 0) return;
    const qint64 clamped = std::clamp<qint64>(zeroBasedFrame, 0, m_frameCount - 1);
    QSignalBlocker blocker(m_spinBox);
    m_spinBox->setValue(static_cast<int>(std::min<qint64>(
        clamped + 1, std::numeric_limits<int>::max())));
}

void FrameNumberInput::setMediaAvailable(bool available)
{
    m_mediaAvailable = available;
    m_spinBox->setEnabled(m_mediaAvailable && m_frameCount > 0);
}

int FrameNumberInput::visibleFrame() const { return m_spinBox->value(); }
int FrameNumberInput::maximumVisibleFrame() const { return m_spinBox->maximum(); }

} // namespace atk::ui
