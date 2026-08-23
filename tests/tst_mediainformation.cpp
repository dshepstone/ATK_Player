#include "media/MediaMetadata.h"
#include "playback/PlaybackController.h"
#include "ui/MainWindow.h"
#include "ui/MediaInformationDialog.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QPushButton>
#include <QTest>

namespace {

QString fixture(const char* name)
{
    return QStringLiteral(ATK_TEST_MEDIA_DIR "/") + QString::fromLatin1(name);
}

QLabel* value(atk::ui::MediaInformationDialog* dialog, const char* key)
{
    return dialog->findChild<QLabel*>(QStringLiteral("MediaInfo%1").arg(QString::fromLatin1(key)));
}

} // namespace

class TestMediaInformation : public QObject {
    Q_OBJECT

private slots:
    void formatsMetadataAndCopiesSafely();
    void commandOwnsOneRefreshingDialog();
};

void TestMediaInformation::formatsMetadataAndCopiesSafely()
{
    atk::ui::MediaInformationDialog dialog;
    QCOMPARE(value(&dialog, "Name")->text(), QStringLiteral("No Media"));
    QCOMPARE(value(&dialog, "Source")->text(), QStringLiteral("—"));

    atk::media::MediaMetadata metadata;
    metadata.filePath = QStringLiteral("C:/private/production/shot010/review.mov");
    metadata.fileName = QStringLiteral("C:/private/production/shot010/review.mov");
    metadata.hasVideo = true;
    metadata.videoCodecName = QStringLiteral("h264");
    metadata.videoCodecLongName = QStringLiteral("H.264 / AVC");
    metadata.pixelFormatName = QStringLiteral("yuv420p");
    metadata.resolution = QSize(1920, 1080);
    metadata.frameRate = { 24000, 1001 };
    metadata.durationUs = 6'667'000;
    metadata.hasAudio = true;
    metadata.audioCodecName = QStringLiteral("aac");
    metadata.audioCodecLongName = QStringLiteral("AAC");
    metadata.audioChannelCount = 2;
    metadata.audioChannelLayout = QStringLiteral("stereo");
    metadata.audioSampleRate = 48000;

    dialog.setMediaInformation(metadata, 160);
    QCOMPARE(value(&dialog, "Name")->text(), QStringLiteral("review.mov"));
    QCOMPARE(value(&dialog, "Source")->text(), QStringLiteral("review.mov"));
    QCOMPARE(value(&dialog, "Duration")->text(), QStringLiteral("00:00:06.667"));
    QCOMPARE(value(&dialog, "NormalSize")->text(), QStringLiteral("1920 × 1080"));
    QCOMPARE(value(&dialog, "CurrentSize")->text(), QStringLiteral("—"));
    QCOMPARE(value(&dialog, "Frames")->text(), QStringLiteral("160"));
    QCOMPARE(value(&dialog, "FrameRate")->text(), QStringLiteral("23.976 fps"));
    QCOMPARE(value(&dialog, "VideoCodec")->text(), QStringLiteral("H.264 (AVC)"));
    QCOMPARE(value(&dialog, "PixelFormat")->text(), QStringLiteral("yuv420p"));
    QCOMPARE(value(&dialog, "AudioCodec")->text(), QStringLiteral("AAC"));
    QCOMPARE(value(&dialog, "Channels")->text(), QStringLiteral("2 (Stereo)"));
    QCOMPARE(value(&dialog, "SampleRate")->text(), QStringLiteral("48000 Hz"));

    const QList<QPair<QString, QString>> friendlyVideoCodecs{
        { QStringLiteral("h264"), QStringLiteral("H.264 (AVC)") },
        { QStringLiteral("hevc"), QStringLiteral("HEVC (H.265)") },
        { QStringLiteral("prores"), QStringLiteral("ProRes") },
        { QStringLiteral("unknown_codec"), QStringLiteral("UNKNOWN_CODEC") },
    };
    for (const auto& [shortName, expected] : friendlyVideoCodecs) {
        metadata.videoCodecName = shortName;
        metadata.videoCodecLongName = QStringLiteral("Verbose FFmpeg description");
        dialog.setMediaInformation(metadata, 160);
        QCOMPARE(value(&dialog, "VideoCodec")->text(), expected);
    }

    metadata.videoCodecName.clear();
    metadata.videoCodecLongName = QStringLiteral("Available Long Name");
    dialog.setMediaInformation(metadata, 160);
    QCOMPARE(value(&dialog, "VideoCodec")->text(), QStringLiteral("Available Long Name"));

    metadata.videoCodecName = QStringLiteral("h264");
    metadata.videoCodecLongName = QStringLiteral("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10");
    metadata.audioCodecName = QStringLiteral("opus");
    metadata.audioCodecLongName = QStringLiteral("Opus (Opus Interactive Audio Codec)");
    dialog.setMediaInformation(metadata, 160);
    QCOMPARE(value(&dialog, "AudioCodec")->text(), QStringLiteral("Opus"));

    const QString copied = dialog.copyText();
    QVERIFY(copied.contains(QStringLiteral("Source: review.mov")));
    QVERIFY(copied.contains(QStringLiteral("Frames: 160")));
    QVERIFY(copied.contains(QStringLiteral("Codec: H.264 (AVC)")));
    QVERIFY(copied.contains(QStringLiteral("Codec: Opus")));
    QVERIFY(!copied.contains(QStringLiteral("MPEG-4 part 10")));
    QVERIFY(!copied.contains(QStringLiteral("C:/private")));
    dialog.findChild<QPushButton*>(QStringLiteral("MediaInfoCopy"))->click();
    QCOMPARE(QApplication::clipboard()->text(), copied);

    metadata.audioCodecName = QStringLiteral("aac");
    metadata.audioCodecLongName = QStringLiteral("AAC (Advanced Audio Coding)");
    dialog.setMediaInformation(metadata, 160);
    QCOMPARE(value(&dialog, "AudioCodec")->text(), QStringLiteral("AAC"));

    metadata.hasAudio = false;
    dialog.setMediaInformation(metadata, 160);
    QCOMPARE(value(&dialog, "AudioCodec")->text(), QStringLiteral("None"));
    QCOMPARE(value(&dialog, "Channels")->text(), QStringLiteral("—"));
    QCOMPARE(value(&dialog, "SampleRate")->text(), QStringLiteral("—"));
}

void TestMediaInformation::commandOwnsOneRefreshingDialog()
{
    atk::ui::MainWindow window;
    auto* action = window.findChild<QAction*>(QStringLiteral("view.mediaInformation"));
    QVERIFY(action);
    action->trigger();
    auto dialogs = window.findChildren<atk::ui::MediaInformationDialog*>();
    QCOMPARE(dialogs.size(), 1);
    auto* dialog = dialogs.first();
    QCOMPARE(value(dialog, "Name")->text(), QStringLiteral("No Media"));

    action->trigger();
    QCOMPARE(window.findChildren<atk::ui::MediaInformationDialog*>().size(), 1);

    dialog->close();
    QTRY_VERIFY(window.findChildren<atk::ui::MediaInformationDialog*>().isEmpty());
    action->trigger();
    dialog = window.findChild<atk::ui::MediaInformationDialog*>();
    QVERIFY(dialog);

    window.playbackController()->openMedia(fixture("atk_fixture_48f.mkv"));
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->hasMedia(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(value(dialog, "Source")->text(),
                              QStringLiteral("atk_fixture_48f.mkv"), 10000);
    QCOMPARE(value(dialog, "Frames")->text(), QStringLiteral("48"));
    QCOMPARE(value(dialog, "NormalSize")->text(), QStringLiteral("640 × 360"));
    QCOMPARE(value(dialog, "FrameRate")->text(), QStringLiteral("24 fps"));
    QVERIFY(value(dialog, "VideoCodec")->text() != QStringLiteral("—"));
    QVERIFY(value(dialog, "AudioCodec")->text() != QStringLiteral("None"));
    QCOMPARE(value(dialog, "SampleRate")->text(), QStringLiteral("48000 Hz"));

    window.playbackController()->openMedia(fixture("atk_compare_5994fps.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(value(dialog, "Source")->text(),
                              QStringLiteral("atk_compare_5994fps.mkv"), 10000);
    QCOMPARE(value(dialog, "NormalSize")->text(), QStringLiteral("320 × 180"));
    QCOMPARE(value(dialog, "FrameRate")->text(), QStringLiteral("59.940 fps"));
    QCOMPARE(value(dialog, "AudioCodec")->text(), QStringLiteral("None"));

    window.playbackController()->closeMedia();
    QTRY_COMPARE(value(dialog, "Name")->text(), QStringLiteral("No Media"));
    QCOMPARE(value(dialog, "Source")->text(), QStringLiteral("—"));
}

QTEST_MAIN(TestMediaInformation)
#include "tst_mediainformation.moc"
