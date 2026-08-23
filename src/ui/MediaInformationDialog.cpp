#include "ui/MediaInformationDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace atk::ui {
namespace {

const QString kUnavailable = QStringLiteral("—");

QString durationText(qint64 microseconds)
{
    if (microseconds < 0) return kUnavailable;
    const qint64 totalMilliseconds = microseconds / 1000;
    const qint64 hours = totalMilliseconds / 3'600'000;
    const qint64 minutes = (totalMilliseconds / 60'000) % 60;
    const qint64 seconds = (totalMilliseconds / 1000) % 60;
    const qint64 milliseconds = totalMilliseconds % 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(milliseconds, 3, 10, QLatin1Char('0'));
}

QString resolutionText(const QSize& size)
{
    return size.isValid()
        ? QStringLiteral("%1 × %2").arg(size.width()).arg(size.height())
        : kUnavailable;
}

QString frameRateText(const media::FrameRate& rate)
{
    if (!rate.isValid()) return kUnavailable;
    const int precision = rate.denominator == 1 ? 0 : 3;
    return QStringLiteral("%1 fps").arg(QString::number(rate.toDouble(), 'f', precision));
}

QString codecText(const QString& shortName, const QString& longName)
{
    const QString codec = shortName.trimmed().toLower();
    if (codec == QStringLiteral("h264")) return QStringLiteral("H.264 (AVC)");
    if (codec == QStringLiteral("hevc")) return QStringLiteral("HEVC (H.265)");
    if (codec == QStringLiteral("av1")) return QStringLiteral("AV1");
    if (codec == QStringLiteral("vp9")) return QStringLiteral("VP9");
    if (codec == QStringLiteral("vp8")) return QStringLiteral("VP8");
    if (codec == QStringLiteral("prores")) return QStringLiteral("ProRes");
    if (codec == QStringLiteral("dnxhd")) return QStringLiteral("DNxHD / DNxHR");
    if (codec == QStringLiteral("mpeg4")) return QStringLiteral("MPEG-4");
    if (codec == QStringLiteral("aac")) return QStringLiteral("AAC");
    if (codec.startsWith(QStringLiteral("pcm_"))) return QStringLiteral("PCM");
    if (codec == QStringLiteral("opus")) return QStringLiteral("Opus");
    if (codec == QStringLiteral("vorbis")) return QStringLiteral("Vorbis");
    if (codec == QStringLiteral("mp3")) return QStringLiteral("MP3");
    if (codec == QStringLiteral("flac")) return QStringLiteral("FLAC");
    if (codec == QStringLiteral("ac3")) return QStringLiteral("AC-3");
    if (codec == QStringLiteral("eac3")) return QStringLiteral("E-AC-3");
    if (!codec.isEmpty()) return codec.toUpper();
    if (!longName.trimmed().isEmpty()) return longName.trimmed();
    return kUnavailable;
}

QString channelsText(const media::MediaMetadata& metadata)
{
    if (metadata.audioChannelCount <= 0) return kUnavailable;
    QString layout = metadata.audioChannelLayout.trimmed();
    if (!layout.isEmpty()) layout[0] = layout.at(0).toUpper();
    return layout.isEmpty()
        ? QString::number(metadata.audioChannelCount)
        : QStringLiteral("%1 (%2)").arg(metadata.audioChannelCount).arg(layout);
}

} // namespace

MediaInformationDialog::MediaInformationDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("MediaInformationDialog"));
    setWindowTitle(tr("Media Information"));
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(360);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(16, 14, 16, 14);
    m_layout->setSpacing(8);

    m_mediaName = new QLabel(this);
    m_mediaName->setObjectName(QStringLiteral("MediaInfoName"));
    m_mediaName->setProperty("atkRole", "statusValue");
    QFont titleFont = m_mediaName->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    m_mediaName->setFont(titleFont);
    m_mediaName->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_layout->addWidget(m_mediaName);

    addSection(tr("GENERAL"));
    addField(QStringLiteral("Source"), tr("Source:"));
    addField(QStringLiteral("Duration"), tr("Duration:"));
    addField(QStringLiteral("NormalSize"), tr("Normal Size:"));
    addField(QStringLiteral("CurrentSize"), tr("Current Size:"));

    addSection(tr("VIDEO"));
    addField(QStringLiteral("VideoCodec"), tr("Codec:"));
    addField(QStringLiteral("Frames"), tr("Frames:"));
    addField(QStringLiteral("FrameRate"), tr("Frame Rate:"));
    addField(QStringLiteral("PixelFormat"), tr("Pixel Format:"));

    addSection(tr("AUDIO"));
    addField(QStringLiteral("AudioCodec"), tr("Codec:"));
    addField(QStringLiteral("Channels"), tr("Channels:"));
    addField(QStringLiteral("SampleRate"), tr("Sample Rate:"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* copy = buttons->addButton(tr("Copy Info"), QDialogButtonBox::ActionRole);
    copy->setObjectName(QStringLiteral("MediaInfoCopy"));
    connect(copy, &QPushButton::clicked, this, &MediaInformationDialog::copyInformation);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    m_layout->addWidget(buttons);

    clearMediaInformation();
    adjustSize();
}

QLabel* MediaInformationDialog::addSection(const QString& title)
{
    auto* heading = new QLabel(title, this);
    heading->setProperty("atkRole", "statusCaption");
    QFont font = heading->font();
    font.setBold(true);
    heading->setFont(font);
    m_layout->addWidget(heading);

    auto* grid = new QGridLayout;
    grid->setContentsMargins(4, 0, 4, 2);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(1, 1);
    m_layout->addLayout(grid);
    m_currentGrid = grid;
    return heading;
}

QLabel* MediaInformationDialog::addField(const QString& key, const QString& label)
{
    const int row = m_currentGrid->rowCount();
    auto* caption = new QLabel(label, this);
    caption->setProperty("atkRole", "statusCaption");
    auto* value = new QLabel(kUnavailable, this);
    value->setObjectName(QStringLiteral("MediaInfo%1").arg(key));
    value->setProperty("atkRole", "statusValue");
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    value->setWordWrap(true);
    m_currentGrid->addWidget(caption, row, 0, Qt::AlignTop);
    m_currentGrid->addWidget(value, row, 1, Qt::AlignTop);
    m_values.insert(key, value);
    return value;
}

void MediaInformationDialog::setValue(const QString& key, const QString& value)
{
    if (QLabel* label = m_values.value(key)) label->setText(value.isEmpty() ? kUnavailable : value);
}

void MediaInformationDialog::setMediaInformation(const media::MediaMetadata& metadata,
                                                  qint64 authoritativeFrameCount)
{
    const QString sourceCandidate = !metadata.fileName.trimmed().isEmpty()
        ? metadata.fileName.trimmed() : metadata.filePath;
    const QString source = QFileInfo(sourceCandidate).fileName();
    m_mediaName->setText(source.isEmpty() ? tr("No Media") : source);
    setValue(QStringLiteral("Source"), source);
    setValue(QStringLiteral("Duration"), durationText(metadata.durationUs));
    setValue(QStringLiteral("NormalSize"), resolutionText(metadata.resolution));
    setValue(QStringLiteral("CurrentSize"), kUnavailable);
    setValue(QStringLiteral("VideoCodec"),
             metadata.hasVideo ? codecText(metadata.videoCodecName, metadata.videoCodecLongName) : kUnavailable);
    setValue(QStringLiteral("Frames"), authoritativeFrameCount > 0
        ? QString::number(authoritativeFrameCount) : kUnavailable);
    setValue(QStringLiteral("FrameRate"), frameRateText(metadata.frameRate));
    setValue(QStringLiteral("PixelFormat"), metadata.pixelFormatName);
    setValue(QStringLiteral("AudioCodec"), metadata.hasAudio
        ? codecText(metadata.audioCodecName, metadata.audioCodecLongName) : tr("None"));
    setValue(QStringLiteral("Channels"), metadata.hasAudio ? channelsText(metadata) : kUnavailable);
    setValue(QStringLiteral("SampleRate"), metadata.hasAudio && metadata.audioSampleRate > 0
        ? tr("%1 Hz").arg(metadata.audioSampleRate) : kUnavailable);
    adjustSize();
}

void MediaInformationDialog::clearMediaInformation()
{
    m_mediaName->setText(tr("No Media"));
    for (QLabel* value : std::as_const(m_values)) value->setText(kUnavailable);
    adjustSize();
}

QString MediaInformationDialog::copyText() const
{
    const auto value = [this](const char* key) { return m_values.value(QString::fromLatin1(key))->text(); };
    return tr("ATK Player Media Information\n\n"
              "Source: %1\nDuration: %2\nNormal Size: %3\nCurrent Size: %4\n\n"
              "Video\nCodec: %5\nFrames: %6\nFrame Rate: %7\nPixel Format: %8\n\n"
              "Audio\nCodec: %9\nChannels: %10\nSample Rate: %11")
        .arg(value("Source"), value("Duration"), value("NormalSize"), value("CurrentSize"),
             value("VideoCodec"), value("Frames"), value("FrameRate"), value("PixelFormat"),
             value("AudioCodec"), value("Channels"), value("SampleRate"));
}

void MediaInformationDialog::copyInformation() const
{
    QApplication::clipboard()->setText(copyText());
}

} // namespace atk::ui
