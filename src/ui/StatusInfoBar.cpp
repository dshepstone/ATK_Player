#include "ui/StatusInfoBar.h"

#include "timeline/Timecode.h"
#include "timeline/TimelineModel.h"

#include <QHBoxLayout>
#include <QLabel>

namespace atk::ui {

StatusInfoBar::StatusInfoBar(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(8);

    m_frameValue    = addField(tr("FRAME"),    QStringLiteral("0 / 0"),  70);
    layout->addSpacing(16);
    m_timecodeValue = addField(tr("TIMECODE"), timeline::timecode::placeholder(), 90);
    layout->addSpacing(16);
    m_fpsValue      = addField(tr("FPS"),      QStringLiteral("--"),     42);

    layout->addStretch(1);

    // The transport is usable in Phase 0 against a placeholder extent. Saying
    // so here is what keeps a moving frame counter from reading as an open file.
    m_placeholderTag = new QLabel(tr("NO MEDIA — placeholder values"), this);
    m_placeholderTag->setProperty("atkRole", "statusCaption");
    layout->addWidget(m_placeholderTag);

    refresh();
}

StatusInfoBar::~StatusInfoBar() = default;

QLabel* StatusInfoBar::addField(const QString& caption,
                                const QString& initialValue,
                                int minimumValueWidth)
{
    auto* boxLayout = qobject_cast<QHBoxLayout*>(layout());
    Q_ASSERT(boxLayout != nullptr);

    auto* captionLabel = new QLabel(caption, this);
    captionLabel->setProperty("atkRole", "statusCaption");
    boxLayout->addWidget(captionLabel);

    auto* valueLabel = new QLabel(initialValue, this);
    valueLabel->setProperty("atkRole", "statusValue");
    // A fixed minimum keeps the row from reflowing every time the frame number
    // gains a digit, which is distracting during playback.
    valueLabel->setMinimumWidth(minimumValueWidth);
    boxLayout->addWidget(valueLabel);

    return valueLabel;
}

void StatusInfoBar::setModel(timeline::TimelineModel* model)
{
    if (m_model == model) {
        return;
    }

    if (m_model != nullptr) {
        m_model->disconnect(this);
    }

    m_model = model;

    if (m_model != nullptr) {
        connect(m_model, &timeline::TimelineModel::currentFrameChanged,
                this, [this] { refresh(); });
        connect(m_model, &timeline::TimelineModel::frameCountChanged,
                this, [this] { refresh(); });
        connect(m_model, &timeline::TimelineModel::frameRateChanged,
                this, [this] { refresh(); });
        connect(m_model, &timeline::TimelineModel::placeholderChanged,
                this, [this] { refresh(); });
    }

    refresh();
}

void StatusInfoBar::refresh()
{
    if (m_model == nullptr) {
        m_frameValue->setText(QStringLiteral("0 / 0"));
        m_timecodeValue->setText(timeline::timecode::placeholder());
        m_fpsValue->setText(QStringLiteral("--"));
        m_placeholderTag->setVisible(false);
        return;
    }

    m_placeholderTag->setVisible(m_model->isPlaceholder());

    m_frameValue->setText(QStringLiteral("%1 / %2")
                              .arg(m_model->currentFrame())
                              .arg(m_model->frameCount()));

    m_timecodeValue->setText(
        timeline::timecode::fromFrame(m_model->currentFrame(), m_model->frameRate()));

    const media::FrameRate rate = m_model->frameRate();
    m_fpsValue->setText(rate.isValid()
                            ? QString::number(rate.toDouble(), 'g', 5)
                            : QStringLiteral("--"));
}

} // namespace atk::ui
