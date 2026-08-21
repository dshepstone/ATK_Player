#include "media/PlaylistProbeWorker.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using atk::media::MediaMetadata;
using atk::media::PlaylistProbeWorker;

class TestPlaylistProbe : public QObject {
    Q_OBJECT
private slots:
    void validFixtureReturnsMetadata();
    void missingAndCorruptAreDistinguished();
};

void TestPlaylistProbe::validFixtureReturnsMetadata()
{
    qRegisterMetaType<MediaMetadata>();
    PlaylistProbeWorker worker;
    QSignalSpy finished(&worker, &PlaylistProbeWorker::probeFinished);
    const QUuid id = QUuid::createUuid();
    worker.probe(id, QStringLiteral(ATK_TEST_MEDIA_DIR "/atk_fixture_48f.mkv"), 9);
    QCOMPARE(finished.count(), 1);
    const QList<QVariant> result = finished.takeFirst();
    QCOMPARE(result[0].toUuid(), id);
    QCOMPARE(result[2].toULongLong(), quint64(9));
    const MediaMetadata metadata = qvariant_cast<MediaMetadata>(result[3]);
    QVERIFY(metadata.isValid());
    QCOMPARE(metadata.resolution, QSize(640, 360));
    QCOMPARE(metadata.frameRate, atk::media::FrameRate::fromInteger(24));
    QVERIFY(metadata.durationUs > 0);
    QVERIFY(result[4].toString().isEmpty());
    QVERIFY(!result[5].toBool());
}

void TestPlaylistProbe::missingAndCorruptAreDistinguished()
{
    PlaylistProbeWorker worker;
    QSignalSpy finished(&worker, &PlaylistProbeWorker::probeFinished);
    const QUuid id = QUuid::createUuid();
    worker.probe(id, QStringLiteral(ATK_TEST_MEDIA_DIR "/does-not-exist.mkv"), 1);
    QVERIFY(finished.takeFirst()[5].toBool());

    QTemporaryDir directory;
    QFile corrupt(directory.filePath(QStringLiteral("corrupt.mkv")));
    QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("not media"); corrupt.close();
    worker.probe(id, corrupt.fileName(), 2);
    const auto result = finished.takeFirst();
    QVERIFY(!result[5].toBool());
    QVERIFY(!result[4].toString().isEmpty());
}

QTEST_MAIN(TestPlaylistProbe)
#include "tst_playlistprobe.moc"
