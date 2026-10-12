#include "media/ThumbnailWorker.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using atk::media::ThumbnailWorker;

namespace {
QString fixture(const char* name)
{
    return QStringLiteral(ATK_TEST_MEDIA_DIR "/") + QString::fromLatin1(name);
}
} // namespace

class TestThumbnailWorker : public QObject {
    Q_OBJECT
private slots:
    void picksAFrameALittleWayIn();
    void rendersScaledAndCachesOnDisk();
    void changedFileGetsAFreshThumbnail();
    void missingFileReportsAnError();
    void runsOnItsOwnThread();
};

void TestThumbnailWorker::picksAFrameALittleWayIn()
{
    QCOMPARE(ThumbnailWorker::representativeFrame(-1), qint64(0)); // unknown length
    QCOMPARE(ThumbnailWorker::representativeFrame(1), qint64(0));  // a still
    QCOMPARE(ThumbnailWorker::representativeFrame(48), qint64(4));
    QCOMPARE(ThumbnailWorker::representativeFrame(240), qint64(24));
    QCOMPARE(ThumbnailWorker::representativeFrame(100000), qint64(240)); // capped for long clips
}

void TestThumbnailWorker::rendersScaledAndCachesOnDisk()
{
    QTemporaryDir cache;
    ThumbnailWorker worker;
    QSignalSpy ready(&worker, &ThumbnailWorker::thumbnailReady);
    const QUuid id = QUuid::createUuid();
    const QString path = fixture("atk_fixture_48f.mp4"); // 640x360
    worker.render(id, path, 7, {}, QSize(320, 180), cache.path());
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.at(0).at(0).toUuid(), id);
    QCOMPARE(ready.at(0).at(1).toULongLong(), quint64(7));
    const QImage image = ready.at(0).at(2).value<QImage>();
    QVERIFY2(!image.isNull(), qPrintable(ready.at(0).at(3).toString()));
    QCOMPARE(image.size(), QSize(320, 180)); // the source's 16:9 kept
    QVERIFY(!ready.at(0).at(4).toBool());     // decoded, not cached

    const QString cached = ThumbnailWorker::cacheFilePath(cache.path(), path, QSize(320, 180));
    QVERIFY(QFileInfo::exists(cached));
    worker.render(id, path, 8, {}, QSize(320, 180), cache.path());
    QCOMPARE(ready.count(), 2);
    QVERIFY(ready.at(1).at(4).toBool()); // served from the disk cache
    QCOMPARE(ready.at(1).at(2).value<QImage>().size(), QSize(320, 180));

    // A different size is a different picture.
    QVERIFY(ThumbnailWorker::cacheFilePath(cache.path(), path, QSize(160, 90)) != cached);
}

void TestThumbnailWorker::changedFileGetsAFreshThumbnail()
{
    QTemporaryDir directory;
    const QString copy = directory.filePath(QStringLiteral("clip.mp4"));
    QVERIFY(QFile::copy(fixture("atk_fixture_48f.mp4"), copy));
    const QString before = ThumbnailWorker::cacheFilePath(directory.path(), copy, QSize(320, 180));
    QVERIFY(!before.isEmpty());
    // Re-rendered over the same name: new contents, new modification time.
    QVERIFY(QFile::remove(copy));
    QVERIFY(QFile::copy(fixture("atk_longgop_96f.mp4"), copy));
    QFile file(copy);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(60), QFileDevice::FileModificationTime));
    file.close();
    QVERIFY(ThumbnailWorker::cacheFilePath(directory.path(), copy, QSize(320, 180)) != before);
}

void TestThumbnailWorker::missingFileReportsAnError()
{
    QTemporaryDir cache;
    ThumbnailWorker worker;
    QSignalSpy ready(&worker, &ThumbnailWorker::thumbnailReady);
    const QString missing = cache.filePath(QStringLiteral("gone.mov"));
    QVERIFY(ThumbnailWorker::cacheFilePath(cache.path(), missing, QSize(320, 180)).isEmpty());
    worker.render(QUuid::createUuid(), missing, 1, {}, QSize(320, 180), cache.path());
    QCOMPARE(ready.count(), 1);
    QVERIFY(ready.at(0).at(2).value<QImage>().isNull());
    QVERIFY(!ready.at(0).at(3).toString().isEmpty());
}

void TestThumbnailWorker::runsOnItsOwnThread()
{
    QTemporaryDir cache;
    QThread workerThread;
    auto* worker = new ThumbnailWorker;
    worker->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, worker, &QObject::deleteLater);
    QThread* renderedOn = nullptr;
    QImage delivered;
    connect(worker, &ThumbnailWorker::thumbnailReady, this,
            [&](QUuid, quint64, const QImage& image, const QString&, bool) {
                QCOMPARE(QThread::currentThread(), this->thread()); // delivered on this (GUI) thread
                delivered = image;
            });
    workerThread.start();
    const QString path = fixture("atk_review_10s.mkv");
    QMetaObject::invokeMethod(worker, [worker, path, &renderedOn, dir = cache.path()] {
        renderedOn = QThread::currentThread();
        worker->render(QUuid::createUuid(), path, 1, {}, QSize(160, 90), dir);
    }, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(!delivered.isNull(), 10000);
    QCOMPARE(renderedOn, &workerThread);
    QVERIFY(delivered.width() <= 160 && delivered.height() <= 90);
    // Shutdown order: stop the thread, wait for it; the worker deletes itself.
    workerThread.quit();
    QVERIFY(workerThread.wait(5000));
}

QTEST_MAIN(TestThumbnailWorker)
#include "tst_thumbnailworker.moc"
