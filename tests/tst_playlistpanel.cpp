#include "annotation/AnnotationTypes.h"
#include "media/MediaSource.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "ui/ArrangePlaylistDialog.h"
#include "ui/MainWindow.h"
#include "ui/PlaylistCardDelegate.h"
#include "ui/ReviewHeaderBar.h"
#include "ui/SourcesPanel.h"
#include "ui/TimelineWidget.h"
#include "ui/commands/CommandRegistry.h"
#include "core/commands/CommandId.h"
#include "timeline/TimelineModel.h"

#include <QApplication>
#include <QComboBox>
#include <QToolButton>
#include <QLabel>
#include <QDir>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
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
    void arrangeSortsMovesAndFilesClips();
    void arrangeMovesClipsToATypedNumber();
    void arrangingKeepsTheLoadedClipAndShowsCategories();
    void inOutButtonsMarkTheTimeline();
    void categoryHeadingsAndFilter();
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

void TestPlaylistPanel::arrangeSortsMovesAndFilesClips()
{
    using Dialog = ui::ArrangePlaylistDialog;
    const auto clip = [](const char* name, int day, qint64 seconds, const char* category) {
        Dialog::Clip c;
        c.id = QUuid::createUuid();
        c.name = QString::fromLatin1(name);
        c.modified = QDateTime(QDate(2026, 10, day), QTime(9, 0));
        c.durationUs = seconds * 1'000'000;
        c.category = QString::fromLatin1(category);
        return c;
    };
    const QVector<Dialog::Clip> clips{clip("shot10.mp4", 3, 8, "B"), clip("Shot2.mp4", 1, 3, ""),
                                      clip("shot1.mp4", 2, 20, "A")};
    const auto names = [](const QVector<Dialog::Clip>& list) {
        QStringList result;
        for (const Dialog::Clip& c : list) result << c.name;
        return result;
    };
    QVector<Dialog::Clip> sorted = clips;
    // Natural, case-blind name order: 1, 2, 10.
    Dialog::sortClips(sorted, Dialog::SortKey::NameAscending, {});
    QCOMPARE(names(sorted), (QStringList{"shot1.mp4", "Shot2.mp4", "shot10.mp4"}));
    Dialog::sortClips(sorted, Dialog::SortKey::DateNewestFirst, {});
    QCOMPARE(names(sorted), (QStringList{"shot10.mp4", "shot1.mp4", "Shot2.mp4"}));
    Dialog::sortClips(sorted, Dialog::SortKey::LengthShortestFirst, {});
    QCOMPARE(names(sorted), (QStringList{"Shot2.mp4", "shot10.mp4", "shot1.mp4"}));
    // Categories in the list's order; none last.
    Dialog::sortClips(sorted, Dialog::SortKey::Category, {QStringLiteral("B"), QStringLiteral("A")});
    QCOMPARE(names(sorted), (QStringList{"shot10.mp4", "shot1.mp4", "Shot2.mp4"}));
    Dialog::sortClips(sorted, Dialog::SortKey::Reverse, {});
    QCOMPARE(names(sorted), (QStringList{"Shot2.mp4", "shot1.mp4", "shot10.mp4"}));

    Dialog dialog(clips, {QStringLiteral("B"), QStringLiteral("A")});
    QCOMPARE(dialog.list()->topLevelItemCount(), 3);
    QCOMPARE(dialog.list()->topLevelItem(2)->text(0), QStringLiteral("3"));
    // Two picked clips move together, keeping their own order.
    dialog.selectRows({1, 2});
    dialog.moveSelected(-1);
    QCOMPARE(names(dialog.clips()), (QStringList{"Shot2.mp4", "shot1.mp4", "shot10.mp4"}));
    dialog.moveSelected(-1); // already at the top: stays
    QCOMPARE(names(dialog.clips()), (QStringList{"Shot2.mp4", "shot1.mp4", "shot10.mp4"}));
    dialog.selectRows({0});
    dialog.moveSelected(1'000'000);
    QCOMPARE(names(dialog.clips()), (QStringList{"shot1.mp4", "shot10.mp4", "Shot2.mp4"}));
    dialog.moveSelected(-1);
    QCOMPARE(names(dialog.clips()), (QStringList{"shot1.mp4", "Shot2.mp4", "shot10.mp4"}));
    // A new category name is added to the list.
    dialog.selectRows({0, 1});
    dialog.setSelectedCategory(QStringLiteral("Late"));
    QCOMPARE(dialog.categories(), (QStringList{"B", "A", "Late"}));
    QCOMPARE(dialog.clips().at(1).category, QStringLiteral("Late"));
    dialog.sortBy(Dialog::SortKey::Category);
    QCOMPARE(names(dialog.clips()), (QStringList{"shot10.mp4", "shot1.mp4", "Shot2.mp4"}));
    QCOMPARE(dialog.list()->topLevelItem(0)->text(0), QStringLiteral("1"));
}

void TestPlaylistPanel::arrangeMovesClipsToATypedNumber()
{
    using Dialog = ui::ArrangePlaylistDialog;
    QVector<Dialog::Clip> clips;
    for (const char* name : {"a", "b", "c", "d", "e"}) {
        Dialog::Clip clip;
        clip.id = QUuid::createUuid();
        clip.name = QString::fromLatin1(name);
        clips.append(clip);
    }
    const auto names = [](const Dialog& dialog) {
        QString result;
        for (const Dialog::Clip& c : dialog.clips()) result += c.name;
        return result;
    };
    Dialog dialog(clips, {});
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    // Move to a number: the first picked clip lands there, the rest follow.
    dialog.selectRows({4});
    dialog.moveSelectedTo(2);
    QCOMPARE(names(dialog), QStringLiteral("aebcd"));
    dialog.selectRows({0, 1});
    dialog.moveSelectedTo(99); // past the end: to the bottom
    QCOMPARE(names(dialog), QStringLiteral("bcdae"));

    // The Move to # box, with Enter moving rather than closing the window.
    auto* number = dialog.findChild<QSpinBox*>(QStringLiteral("ArrangeMoveToNumber"));
    QVERIFY(number);
    dialog.selectRows({2});
    QCOMPARE(number->value(), 3); // starts at where the clip is
    number->setFocus();
    number->setValue(1);
    QTest::keyClick(number, Qt::Key_Return);
    QCOMPARE(names(dialog), QStringLiteral("dbcae"));
    QVERIFY(dialog.isVisible());

    // Typing a number on the list moves the current clip there.
    QTreeWidget* list = dialog.list();
    list->setFocus();
    list->setCurrentItem(list->topLevelItem(4));
    dialog.selectRows({4});
    QTest::keyClick(list, Qt::Key_2);
    QTRY_VERIFY(list->findChild<QSpinBox*>(QStringLiteral("ArrangePositionEditor")));
    QTest::keyClick(list->findChild<QSpinBox*>(QStringLiteral("ArrangePositionEditor")), Qt::Key_Return);
    QTRY_COMPARE(names(dialog), QStringLiteral("debca"));
    QVERIFY(dialog.isVisible());
    QCOMPARE(list->topLevelItem(1)->text(0), QStringLiteral("2"));
}

void TestPlaylistPanel::arrangingKeepsTheLoadedClipAndShowsCategories()
{
    QTemporaryDir directory;
    project::Project project;
    project.addSource(std::make_shared<media::MediaSource>(fixture("atk_fixture_48f.mp4")));
    project.addSource(std::make_shared<media::MediaSource>(fixture("atk_review_10s.mkv")));
    project.setActiveIndex(0);
    const QString path = directory.filePath(QStringLiteral("arrange.atkproj"));
    QVERIFY(project::ProjectSerializer::save(project, path).ok);

    ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openProjectFile(path);
    auto* panel = window.findChild<ui::SourcesPanel*>();
    QVERIFY(panel);
    QVERIFY(panel->findChild<QPushButton*>(QStringLiteral("PlaylistArrangeButton")));
    QTRY_COMPARE_WITH_TIMEOUT(panel->list()->count(), 2, 5000);
    const QUuid first = window.project()->entries().at(0).id;
    const QUuid second = window.project()->entries().at(1).id;

    ui::ArrangePlaylistDialog::Clip a, b;
    a.id = first;
    a.category = QStringLiteral("Needs work");
    b.id = second;
    window.applyPlaylistArrangement({b, a}, {QStringLiteral("Needs work"), QStringLiteral("Done")});
    QCOMPARE(window.project()->entries().at(0).id, second);
    QCOMPARE(window.project()->currentSourceId(), first); // still the loaded clip
    QCOMPARE(window.project()->activeIndex(), 1);
    QCOMPARE(window.project()->categories(), (QStringList{"Needs work", "Done"}));
    QTRY_COMPARE(panel->list()->item(1)->data(role::Category).toString(), QStringLiteral("Needs work"));
    QVERIFY(panel->list()->item(0)->data(role::Category).toString().isEmpty());
    QCOMPARE(panel->list()->item(1)->data(role::Order).toInt(), 2);
    QVERIFY(window.project()->needsSavePrompt());
}

void TestPlaylistPanel::inOutButtonsMarkTheTimeline()
{
    QTemporaryDir directory;
    project::Project project;
    project.addSource(std::make_shared<media::MediaSource>(fixture("atk_fixture_48f.mp4")));
    project.setActiveIndex(0);
    const QString path = directory.filePath(QStringLiteral("range.atkproj"));
    QVERIFY(project::ProjectSerializer::save(project, path).ok);

    ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openProjectFile(path);
    auto* timelineWidget = window.findChild<ui::TimelineWidget*>();
    timeline::TimelineModel* model = timelineWidget->model();
    QTRY_COMPARE_WITH_TIMEOUT(model->frameCount(), qint64(48), 10000);
    QToolButton* setIn = window.setInButton();
    QToolButton* setOut = window.setOutButton();
    QVERIFY(setIn && setIn->isVisible() && setIn->isEnabled());
    QVERIFY(setOut && setOut->isVisible() && setOut->isEnabled());
    QCOMPARE(timelineWidget->inMarkX(), -1);

    // The hotkeys are [ and ].
    auto* commands = window.findChild<ui::CommandRegistry*>();
    QVERIFY(commands);
    QCOMPARE(commands->action(commands::CommandId::SetRangeIn)->shortcut(), QKeySequence(QStringLiteral("[")));
    QCOMPARE(commands->action(commands::CommandId::SetRangeOut)->shortcut(), QKeySequence(QStringLiteral("]")));

    // In on frame 11, out on frame 30: two marks on the timeline, which still
    // shows the whole clip, saved with the clip.
    window.playbackController()->seekFrame(10);
    QTRY_COMPARE_WITH_TIMEOUT(model->currentFrame(), qint64(10), 5000);
    setIn->click();
    window.playbackController()->seekFrame(29);
    QTRY_COMPARE_WITH_TIMEOUT(model->currentFrame(), qint64(29), 5000);
    setOut->click();
    QCOMPARE(model->playbackRange(), (timeline::PlaybackRange{10, 29, true}));
    QCOMPARE(model->viewport().startFrame(), qint64(0));
    QCOMPARE(model->viewport().endFrame(), qint64(47));
    QCOMPARE(model->effectiveStartFrame(), qint64(10));
    QCOMPARE(model->effectiveEndFrame(), qint64(29));
    QVERIFY(timelineWidget->inMarkX() >= 0);
    QVERIFY(timelineWidget->outMarkX() > timelineWidget->inMarkX());
    QVERIFY(setIn->property("markSet").toBool());
    QVERIFY(setIn->toolTip().contains(QStringLiteral("11")));
    QCOMPARE(window.project()->entries().at(0).playbackRange, (timeline::PlaybackRange{10, 29, true}));

    // The playhead can still go past the out point, to set a later one.
    window.playbackController()->seekFrame(40);
    QTRY_COMPARE_WITH_TIMEOUT(model->currentFrame(), qint64(40), 5000);
    setOut->click();
    QCOMPARE(model->playbackRange(), (timeline::PlaybackRange{10, 40, true}));

    // The On/Off toggle: off hides the marks and plays the whole clip, and
    // the clip saves them switched off; on brings them back.
    QToolButton* toggle = window.inOutToggle();
    QVERIFY(toggle && toggle->isVisible() && toggle->isEnabled() && toggle->isChecked());
    toggle->click();
    QVERIFY(!toggle->isChecked());
    QVERIFY(!model->hasPlaybackMarks() && model->hasKeptMarks());
    QCOMPARE(timelineWidget->inMarkX(), -1);
    QCOMPARE(model->effectiveStartFrame(), qint64(0));
    QCOMPARE(model->effectiveEndFrame(), qint64(47));
    QCOMPARE(window.project()->entries().at(0).playbackRange, (timeline::PlaybackRange{10, 40, false}));
    toggle->click();
    QVERIFY(toggle->isChecked());
    QCOMPARE(model->playbackRange(), (timeline::PlaybackRange{10, 40, true}));
    QCOMPARE(commands->action(commands::CommandId::ToggleInOut)->shortcut(), QKeySequence(QStringLiteral("\\")));

    // Right-click on any of the buttons clears both marks.
    QAction* clear = commands->action(commands::CommandId::ClearRange);
    QCOMPARE(setIn->contextMenuPolicy(), Qt::CustomContextMenu);
    QVERIFY(clear->isEnabled());
    clear->trigger();
    QVERIFY(!model->hasPlaybackMarks());
    QCOMPARE(timelineWidget->outMarkX(), -1);
    QVERIFY(!setIn->property("markSet").toBool());
    QVERIFY(!clear->isEnabled());
    QVERIFY(!toggle->isEnabled());
    QCOMPARE(model->effectiveEndFrame(), qint64(47));
}

void TestPlaylistPanel::categoryHeadingsAndFilter()
{
    project::Project project;
    for (const char* name : {"a.mp4", "b.mp4", "c.mp4", "d.mp4", "e.mp4"})
        project.addSource(readySource(fixture(name), 24));
    project.setActiveIndex(0);
    ui::SourcesPanel panel;
    panel.resize(300, 600);
    panel.show();
    panel.setProject(&project);
    QListWidget* list = panel.list();
    QComboBox* filter = panel.categoryFilterBox();
    QVERIFY(filter);
    // No categories in use: no headings and no filter, as before.
    QVERIFY(filter->parentWidget()->isHidden());
    for (int row = 0; row < list->count(); ++row)
        QVERIFY(list->item(row)->data(role::GroupHeading).toString().isEmpty());

    // Layout, Layout, none, Animation, Layout.
    const auto& entries = project.entries();
    project.setClipCategory(entries.at(0).id, QStringLiteral("Layout"));
    project.setClipCategory(entries.at(1).id, QStringLiteral("Layout"));
    project.setClipCategory(entries.at(3).id, QStringLiteral("Animation"));
    project.setClipCategory(entries.at(4).id, QStringLiteral("Layout"));
    QVERIFY(!filter->parentWidget()->isHidden());
    const auto heading = [list](int row) { return list->item(row)->data(role::GroupHeading).toString(); };
    QCOMPARE(heading(0), QStringLiteral("Layout"));
    QCOMPARE(list->item(0)->data(role::GroupCount).toInt(), 2);
    QVERIFY(heading(1).isEmpty());
    QCOMPARE(heading(2), QStringLiteral("No category"));
    QCOMPARE(heading(3), QStringLiteral("Animation"));
    QCOMPARE(heading(4), QStringLiteral("Layout"));
    // A card that starts a group is taller, for its heading.
    QCOMPARE(list->sizeHintForRow(0), ui::PlaylistCardDelegate::kCardHeight + ui::PlaylistCardDelegate::kHeadingHeight);
    QCOMPARE(list->sizeHintForRow(1), ui::PlaylistCardDelegate::kCardHeight);

    // Filter choices: all, the categories in the project's order, and none.
    QStringList choices;
    for (int i = 0; i < filter->count(); ++i) choices << filter->itemText(i);
    QCOMPARE(choices, (QStringList{"All categories", "Layout", "Animation", "No category"}));

    // One category: the others are hidden, and its clips form one group.
    filter->setCurrentIndex(1);
    emit filter->activated(1);
    QCOMPARE(panel.categoryFilter(), QStringLiteral("Layout"));
    QVERIFY(!list->isRowHidden(0) && !list->isRowHidden(1) && !list->isRowHidden(4));
    QVERIFY(list->isRowHidden(2) && list->isRowHidden(3));
    QCOMPARE(heading(0), QStringLiteral("Layout"));
    QCOMPARE(list->item(0)->data(role::GroupCount).toInt(), 3);
    QVERIFY(heading(4).isEmpty());
    QVERIFY(panel.showsCategory(QStringLiteral("Layout")) && !panel.showsCategory(QString()));

    // "No category" shows the clips without one; the filter survives a refresh.
    panel.setCategoryFilter(QStringLiteral(""));
    QVERIFY(!list->isRowHidden(2));
    QVERIFY(list->isRowHidden(0));
    project.setActiveIndex(1);
    QVERIFY(!list->isRowHidden(2) && list->isRowHidden(0));
    QVERIFY(!panel.grab().isNull());

    // Back to all.
    panel.setCategoryFilter(QString());
    for (int row = 0; row < list->count(); ++row) QVERIFY(!list->isRowHidden(row));
    QVERIFY(panel.showsCategory(QStringLiteral("Animation")));
}

QTEST_MAIN(TestPlaylistPanel)
#include "tst_playlistpanel.moc"
