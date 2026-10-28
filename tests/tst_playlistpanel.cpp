#include "annotation/AnnotationTypes.h"
#include "media/MediaSource.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "ui/MainWindow.h"
#include "ui/PlaylistCardDelegate.h"
#include "ui/ReviewHeaderBar.h"
#include "ui/SourcesPanel.h"

#include <QApplication>
#include <QComboBox>
#include <QToolButton>
#include <QLabel>
#include <QDir>
#include <QListWidget>
#include <QMenu>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace atk;
namespace role = ui::playlistrole;

namespace {
QString fixture(const char* name)
{
    return QStringLiteral(ATK_TEST_MEDIA_DIR "/") + QString::fromLatin1(name);
}

std::shared_ptr<media::MediaSource> readySource(const QString& path, int64_t frames)
{
    auto source = std::make_shared<media::MediaSource>(path);
    media::MediaMetadata metadata;
    metadata.hasVideo = true;
    metadata.resolution = QSize(640, 360);
    metadata.frameRate = media::FrameRate{24000, 1001};
    metadata.frameCount = frames;
    metadata.frameCountSource = media::FrameCountSource::StreamMetadata;
    metadata.durationUs = frames * 1001000 / 24;
    source->setMetadata(metadata);
    return source;
}
} // namespace

class TestPlaylistPanel : public QObject {
    Q_OBJECT
private slots:
    void cardsCarryTheClipDetails();
    void thumbnailsAreAskedForOnceAndShown();
    void drawingAndNotesUpdateTheCardInPlace();
    void mainWindowFillsInThumbnails();
    void headerShowsTheClipAndSetsItsStatus();
};

void TestPlaylistPanel::cardsCarryTheClipDetails()
{
    project::Project project;
    project.addSource(readySource(fixture("atk_fixture_48f.mp4"), 48));
    project.addSource(readySource(fixture("missing.mov"), 0));
    project.mutableEntries()[0].availability = project::SourceAvailability::Ready;
    project.mutableEntries()[1].availability = project::SourceAvailability::Missing;
    project.setActiveIndex(0);

    ui::SourcesPanel panel;
    panel.resize(300, 400);
    panel.setProject(&project);
    QListWidget* list = panel.list();
    QCOMPARE(list->count(), 2);
    QVERIFY(qobject_cast<ui::PlaylistCardDelegate*>(list->itemDelegate()));

    QListWidgetItem* first = list->item(0);
    QCOMPARE(first->text(), QStringLiteral("atk_fixture_48f.mp4"));
    QCOMPARE(first->data(role::Order).toInt(), 1);
    QVERIFY(first->data(role::Active).toBool());
    QVERIFY(!first->data(role::Unavailable).toBool());
    QCOMPARE(first->data(role::FrameCount).toLongLong(), qint64(48));
    // The details line keeps the rational rate, never a rounded 24.
    QVERIFY2(first->data(role::Subtitle).toString().contains(QStringLiteral("23.976 fps")),
             qPrintable(first->data(role::Subtitle).toString()));

    QListWidgetItem* second = list->item(1);
    QVERIFY(second->data(role::Unavailable).toBool());
    QVERIFY(!second->data(role::Active).toBool());
    QCOMPARE(second->data(role::Subtitle).toString(), QStringLiteral("Missing"));

    // Cards are a fixed height and the list never scrolls sideways.
    QCOMPARE(list->sizeHintForRow(0), ui::PlaylistCardDelegate::kCardHeight);
    QCOMPARE(list->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    QVERIFY(!panel.grab().isNull()); // paints, including the missing card
    QVERIFY(panel.findChild<QLabel*>(QStringLiteral("PlaylistDropHint")));
}

void TestPlaylistPanel::thumbnailsAreAskedForOnceAndShown()
{
    project::Project project;
    project.addSource(readySource(fixture("atk_fixture_48f.mp4"), 48));
    project.addSource(readySource(fixture("atk_review_10s.mkv"), 240));
    project.mutableEntries()[0].availability = project::SourceAvailability::Ready;
    project.mutableEntries()[1].availability = project::SourceAvailability::Probing;

    ui::SourcesPanel panel;
    QSignalSpy wanted(&panel, &ui::SourcesPanel::thumbnailWanted);
    panel.setProject(&project);
    // Only readable clips are asked for; one still being probed waits.
    QCOMPARE(wanted.count(), 1);
    const QUuid firstId = project.entries()[0].id;
    QCOMPARE(wanted.at(0).at(0).toUuid(), firstId);

    // Probed: now it is asked for, and the first is not asked for again.
    project.mutableEntries()[1].availability = project::SourceAvailability::Ready;
    emit project.entriesChanged();
    QCOMPARE(wanted.count(), 2);
    QCOMPARE(wanted.at(1).at(0).toUuid(), project.entries()[1].id);
    emit project.entriesChanged();
    QCOMPARE(wanted.count(), 2);

    QImage picture(160, 90, QImage::Format_RGB32);
    picture.fill(Qt::red);
    panel.setThumbnail(firstId, picture);
    QCOMPARE(panel.list()->item(0)->data(role::Thumbnail).value<QImage>(), picture);
    // It survives the list being rebuilt (reorder, probe results...).
    project.moveSource(0, 1);
    QCOMPARE(panel.list()->item(1)->data(Qt::UserRole).toUuid(), firstId);
    QCOMPARE(panel.list()->item(1)->data(role::Thumbnail).value<QImage>(), picture);
}

void TestPlaylistPanel::drawingAndNotesUpdateTheCardInPlace()
{
    project::Project project;
    for (int i = 0; i < 30; ++i) project.addSource(readySource(fixture("atk_fixture_48f.mp4"), 48));
    ui::SourcesPanel panel;
    panel.resize(300, 300);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    panel.setProject(&project);
    QListWidget* list = panel.list();
    list->scrollToBottom();
    const int scrolled = list->verticalScrollBar()->value();
    QVERIFY(scrolled > 0);
    QListWidgetItem* last = list->item(29);

    annotation::SourceAnnotations annotations;
    annotation::FrameAnnotation frame;
    frame.frame = 3;
    annotation::AnnotationItem stroke;
    stroke.id = 1;
    stroke.points = {{0.1f, 0.1f, 1, 0, 0}, {0.2f, 0.2f, 1, 0, 0}};
    frame.items = {stroke};
    annotation::FrameNote note;
    note.id = 2;
    note.text = QStringLiteral("hold longer");
    frame.notes = {note, note};
    annotations.setFrame(frame);
    QVERIFY(project.setAnnotations(project.entries()[29].id, annotations));

    QCOMPARE(list->item(29), last); // updated in place, not rebuilt
    QCOMPARE(last->data(role::NoteCount).toInt(), 2);
    QCOMPARE(last->data(role::DrawnFrames).toInt(), 1);
    QCOMPARE(list->verticalScrollBar()->value(), scrolled);
}

void TestPlaylistPanel::mainWindowFillsInThumbnails()
{
    QTemporaryDir directory;
    project::Project project;
    project.addSource(std::make_shared<media::MediaSource>(fixture("atk_fixture_48f.mp4")));
    project.addSource(std::make_shared<media::MediaSource>(fixture("atk_review_10s.mkv")));
    project.setActiveIndex(0);
    const QString path = directory.filePath(QStringLiteral("thumbs.atkproj"));
    QVERIFY(project::ProjectSerializer::save(project, path).ok);

    ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openProjectFile(path);
    auto* panel = window.findChild<ui::SourcesPanel*>();
    QVERIFY(panel);
    QTRY_COMPARE_WITH_TIMEOUT(panel->list()->count(), 2, 5000);
    for (int row = 0; row < 2; ++row) {
        QTRY_VERIFY_WITH_TIMEOUT(!panel->list()->item(row)->data(role::Thumbnail).value<QImage>().isNull(), 15000);
        const QImage thumb = panel->list()->item(row)->data(role::Thumbnail).value<QImage>();
        QVERIFY(thumb.width() <= 320 && thumb.height() <= 180);
    }
    // Kept on disk beside the settings, so the next session shows them at once.
    QVERIFY(!QDir(directory.filePath(QStringLiteral("thumbnails"))).entryList({QStringLiteral("*.png")}).isEmpty());
}

void TestPlaylistPanel::headerShowsTheClipAndSetsItsStatus()
{
    QTemporaryDir directory;
    project::Project project;
    project.setName(QStringLiteral("Week 13 Capstone"));
    project.addSource(std::make_shared<media::MediaSource>(fixture("atk_fixture_48f.mp4")));
    project.addSource(std::make_shared<media::MediaSource>(fixture("atk_review_10s.mkv")));
    project.mutableEntries()[1].displayName = QStringLiteral("Smith_Jane");
    project.setActiveIndex(1);
    const QString path = directory.filePath(QStringLiteral("status.atkproj"));
    QVERIFY(project::ProjectSerializer::save(project, path).ok);

    ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* header = window.findChild<ui::ReviewHeaderBar*>();
    auto* status = window.findChild<QComboBox*>(QStringLiteral("ClipStatusCombo"));
    auto* clip = window.findChild<QLabel*>(QStringLiteral("ReviewHeaderClip"));
    QVERIFY(header && status && clip);
    QVERIFY(header->isVisible());
    QVERIFY(!status->isEnabled()); // nothing loaded yet

    window.openProjectFile(path);
    QTRY_COMPARE_WITH_TIMEOUT(window.project()->activeIndex(), 1, 5000);
    QTRY_COMPARE(clip->text(), QStringLiteral("Smith_Jane"));
    QCOMPARE(window.findChild<QLabel*>(QStringLiteral("ReviewHeaderProject"))->text(),
             QStringLiteral("Week 13 Capstone"));
    QVERIFY(status->isEnabled());
    QCOMPARE(header->status(), project::ClipStatus::NotReviewed);

    // Choosing a status sets it on the loaded clip and shows on its card.
    status->setCurrentIndex(status->findData(int(project::ClipStatus::Approved)));
    QCOMPARE(window.project()->entries()[1].status, project::ClipStatus::Approved);
    QCOMPARE(window.project()->entries()[0].status, project::ClipStatus::NotReviewed);
    auto* panel = window.findChild<ui::SourcesPanel*>();
    QCOMPARE(panel->list()->item(1)->data(role::Status).toInt(), int(project::ClipStatus::Approved));
    QVERIFY(window.project()->isModified());
    QCOMPARE(ui::clipStatusLabel(project::ClipStatus::Approved), QStringLiteral("Approved"));

    // The export button offers the window's own export commands.
    auto* exportButton = window.findChild<QToolButton*>(QStringLiteral("ReviewHeaderExport"));
    QVERIFY(exportButton && exportButton->menu());
    QVERIFY(exportButton->menu()->actions().contains(window.findChild<QAction*>(QStringLiteral("file.exportReviewNotes"))));

    // Save and Save As sit beside it, backed by the File menu's own commands.
    auto* save = window.findChild<QToolButton*>(QStringLiteral("ReviewHeaderSave"));
    auto* saveAs = window.findChild<QToolButton*>(QStringLiteral("ReviewHeaderSaveAs"));
    QVERIFY(save && saveAs);
    QVERIFY(save->isVisible() && saveAs->isVisible());
    QCOMPARE(save->defaultAction(), window.findChild<QAction*>(QStringLiteral("file.saveProject")));
    QCOMPARE(saveAs->defaultAction(), window.findChild<QAction*>(QStringLiteral("file.saveProjectAs")));
    QVERIFY(save->toolTip().contains(QStringLiteral("Ctrl+S")));
    save->click();
    QTRY_VERIFY(!window.project()->isModified()); // saved to the file it came from
}

QTEST_MAIN(TestPlaylistPanel)
#include "tst_playlistpanel.moc"
