#include "media/MediaSource.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "review/FolderScanner.h"
#include "review/ReviewLibrary.h"
#include "ui/MainWindow.h"
#include "review/BatchRename.h"
#include "review/Roster.h"
#include "ui/BatchRenameDialog.h"
#include "ui/NewReviewDialog.h"
#include "ui/ReviewLibraryPage.h"
#include "ui/SourcesPanel.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

using namespace atk;

namespace {
QString fixture(const char* name)
{
    return QStringLiteral(ATK_TEST_MEDIA_DIR "/") + QString::fromLatin1(name);
}

/// A class folder: three student clips, named so name order differs from
/// plain string order.
QString makeClassFolder(QTemporaryDir& directory)
{
    const QString folder = directory.filePath(QStringLiteral("Week 13 Capstone"));
    QDir().mkpath(folder);
    QFile::copy(fixture("atk_fixture_48f.mp4"), folder + QStringLiteral("/student10.mp4"));
    QFile::copy(fixture("atk_fixture_48f.mp4"), folder + QStringLiteral("/student2.mp4"));
    QFile::copy(fixture("atk_review_10s.mkv"), folder + QStringLiteral("/student1.mkv"));
    return folder;
}

/// A review of three student clips and the matching class list.
struct ClassReview {
    QString folder;
    QString projectPath;
    QString rosterPath;
};

ClassReview makeClassReview(QTemporaryDir& directory, ui::MainWindow& window)
{
    ClassReview r;
    r.folder = directory.filePath(QStringLiteral("Animatics"));
    QDir().mkpath(r.folder);
    QFile::copy(fixture("atk_fixture_48f.mp4"), r.folder + QStringLiteral("/A1_Logel_Jordan_Animatic_v002.mp4"));
    QFile::copy(fixture("atk_fixture_48f.mp4"), r.folder + QStringLiteral("/jsmith_walk.mp4"));
    QFile::copy(fixture("atk_review_10s.mkv"), r.folder + QStringLiteral("/1004219_final.mkv"));
    r.rosterPath = directory.filePath(QStringLiteral("class.csv"));
    QFile roster(r.rosterPath);
    roster.open(QIODevice::WriteOnly);
    roster.write("Student ID,Last Name,First Name\r\n1004219,Adams,Kim\r\n1004218,Logel,Jordan\r\n"
                 "1004217,Smith,Jane\r\n1004220,Zhou,Wei\r\n");
    roster.close();
    r.projectPath = QDir(r.folder).filePath(QStringLiteral("Animatics.atkproj"));
    window.createReview(QStringLiteral("Animatics"), r.folder, false, review::scanFolder(r.folder, false),
                        r.projectPath, false);
    return r;
}

QVector<ui::BatchRenameDialog::Clip> clipsOf(const project::Project* project)
{
    QVector<ui::BatchRenameDialog::Clip> clips;
    for (const project::SourceEntry& e : project->entries())
        clips.append({e.id, e.source->filePath(), e.displayName, e.source->isImageSequence()});
    return clips;
}

QAction* command(ui::MainWindow& window, const char* key)
{
    return window.findChild<QAction*>(QString::fromLatin1(key));
}
} // namespace

class TestReviewWorkflow : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("ATK_NO_OPEN_EXPORTS", "1"); }
    void startScreenIsTheLibraryUnlessTurnedOff();
    void anyFileOpenedGoesStraightToThePlayer();
    void newReviewDialogScansChoosesAndOrders();
    void createdReviewIsSavedListedAndReopened();
    void libraryStarsArchivesAndSearches();
    void newClipsInTheFolderAreOffered();
    void classListRenamesShownNamesByDefault();
    void classListRenamesFilesOnDiskAndUndoes();
};

void TestReviewWorkflow::startScreenIsTheLibraryUnlessTurnedOff()
{
    QTemporaryDir directory;
    const QString settings = directory.filePath(QStringLiteral("settings.ini"));
    {
        ui::MainWindow window(settings);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(!window.isShowingReviewLibrary());
        QVERIFY(!window.reviewLibraryPage()); // nothing built until needed
        auto* playlist = window.findChild<QDockWidget*>(QStringLiteral("SourcesDock"));
        QVERIFY(playlist && playlist->isVisible());

        window.showStartScreen(); // what main() does for a launch with no file
        QVERIFY(window.isShowingReviewLibrary());
        QVERIFY(window.reviewLibraryPage()->isVisible());
        QVERIFY(!playlist->isVisible()); // the player's panels step aside
        QVERIFY(!window.statusBar()->isVisible());

        // Back to the player with the house button's command, and again.
        window.showPlayer();
        QVERIFY(!window.isShowingReviewLibrary());
        QVERIFY(playlist->isVisible());
        command(window, "file.reviewLibrary")->trigger();
        QVERIFY(window.isShowingReviewLibrary());
        QCOMPARE(command(window, "file.reviewLibrary")->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+H")));
        window.showPlayer();
        auto* home = window.findChild<QToolButton*>(QStringLiteral("ReviewHeaderHome"));
        QVERIFY(home && home->isVisible());
        home->click();
        QVERIFY(window.isShowingReviewLibrary());
    }
    // Turned off in Preferences: a plain launch shows the empty player.
    QSettings(settings, QSettings::IniFormat).setValue(QStringLiteral("startup/showReviewLibrary"), false);
    ui::MainWindow window(settings);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showStartScreen();
    QVERIFY(!window.isShowingReviewLibrary());
}

void TestReviewWorkflow::anyFileOpenedGoesStraightToThePlayer()
{
    QTemporaryDir directory;
    ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // A clip sent by double-click, Maya or Harmony (--open, a bare path, the API).
    window.showReviewLibrary();
    window.openMediaFile(fixture("atk_fixture_48f.mp4"));
    QVERIFY(!window.isShowingReviewLibrary());
    QTRY_COMPARE(window.project()->entries().size(), 1);

    // A project file, likewise.
    project::Project project;
    project.addSource(std::make_shared<media::MediaSource>(fixture("atk_review_10s.mkv")));
    const QString path = directory.filePath(QStringLiteral("p.atkproj"));
    QVERIFY(project::ProjectSerializer::save(project, path).ok);
    window.showReviewLibrary();
    QVERIFY(window.openProjectFile(path));
    QVERIFY(!window.isShowingReviewLibrary());
}

void TestReviewWorkflow::newReviewDialogScansChoosesAndOrders()
{
    QTemporaryDir directory;
    const QString folder = makeClassFolder(directory);
    ui::NewReviewDialog dialog;
    auto* create = dialog.findChild<QPushButton*>(QStringLiteral("NewReviewCreate"));
    QVERIFY(create && !create->isEnabled()); // no folder yet
    dialog.setFolder(folder);
    QTRY_VERIFY(!dialog.isScanning());
    QCOMPARE(dialog.reviewName(), QStringLiteral("Week 13 Capstone")); // the folder's name
    QCOMPARE(dialog.projectPath(), QDir(folder).filePath(QStringLiteral("Week 13 Capstone.atkproj")));
    QVector<review::FoundClip> chosen = dialog.chosenClips();
    QCOMPARE(chosen.size(), 3);
    QCOMPARE(chosen[0].displayName, QStringLiteral("student1.mkv")); // natural order
    QCOMPARE(chosen[1].displayName, QStringLiteral("student2.mp4"));
    QCOMPARE(chosen[2].displayName, QStringLiteral("student10.mp4"));
    QVERIFY(create->isEnabled());

    // Untick one; the rest keep their order.
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("NewReviewClips"));
    list->item(1)->setCheckState(Qt::Unchecked);
    chosen = dialog.chosenClips();
    QCOMPARE(chosen.size(), 2);
    QCOMPARE(chosen[1].displayName, QStringLiteral("student10.mp4"));

    // A name that cannot be a file name, or that is already taken, is refused.
    auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("NewReviewName"));
    name->setText(QStringLiteral("week:13"));
    emit name->textEdited(name->text());
    QVERIFY(!create->isEnabled());
    QFile(QDir(folder).filePath(QStringLiteral("Taken.atkproj"))).open(QIODevice::WriteOnly);
    name->setText(QStringLiteral("Taken"));
    emit name->textEdited(name->text());
    QVERIFY(!create->isEnabled());
    name->setText(QStringLiteral("Final"));
    emit name->textEdited(name->text());
    QVERIFY(create->isEnabled());

    // Leaving the folder field (as closing or clicking Create does) must not
    // rescan and lose the ticks: the choice is what Create reads.
    auto* folderField = dialog.findChild<QLineEdit*>(QStringLiteral("NewReviewFolder"));
    emit folderField->editingFinished();
    QVERIFY(!dialog.isScanning());
    QCOMPARE(dialog.chosenClips().size(), 2);
    dialog.accept();
    QCOMPARE(dialog.chosenClips().size(), 2);
}

void TestReviewWorkflow::createdReviewIsSavedListedAndReopened()
{
    QTemporaryDir directory;
    const QString folder = makeClassFolder(directory);
    const QString projectPath = QDir(folder).filePath(QStringLiteral("Capstone.atkproj"));
    QUuid id;
    {
        ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.showReviewLibrary();
        QVector<review::FoundClip> clips = review::scanFolder(folder, false);
        std::reverse(clips.begin(), clips.end()); // the order chosen in the dialog is kept
        QVERIFY(window.createReview(QStringLiteral("Capstone"), folder, false, clips, projectPath, false));
        QVERIFY(!window.isShowingReviewLibrary()); // straight into reviewing
        QVERIFY(QFileInfo::exists(projectPath));
        QCOMPARE(window.project()->entries().size(), 3);
        QCOMPARE(window.project()->entries()[0].source->filePath(), QDir(folder).filePath(QStringLiteral("student10.mp4")));
        QCOMPARE(window.project()->activeIndex(), 0);
        QCOMPARE(window.project()->name(), QStringLiteral("Capstone"));

        const review::ReviewInfo* info = window.reviewLibrary()->findByProject(projectPath);
        QVERIFY(info);
        id = info->id;
        QCOMPARE(info->name, QStringLiteral("Capstone"));
        QCOMPARE(info->clipCount, 3);
        QCOMPARE(info->previewClips.size(), 3);
        QVERIFY(info->createdMsUtc > 0);

        window.showReviewLibrary();
        QCOMPARE(window.reviewLibraryPage()->listedReviews(), QVector<QUuid>{id});
    }
    // A new session finds it in the library and opens it.
    ui::MainWindow again(directory.filePath(QStringLiteral("settings.ini")));
    again.show();
    QVERIFY(QTest::qWaitForWindowExposed(&again));
    QVERIFY(again.reviewLibrary()->find(id));
    again.showStartScreen();
    QVERIFY(again.isShowingReviewLibrary());
    QVERIFY(again.openReview(id, false));
    QVERIFY(!again.isShowingReviewLibrary());
    QCOMPARE(again.project()->filePath(), QFileInfo(projectPath).absoluteFilePath());
    QCOMPARE(again.project()->entries().size(), 3);
    QVERIFY(again.reviewLibrary()->find(id)->openedMsUtc >= again.reviewLibrary()->find(id)->createdMsUtc);
}

void TestReviewWorkflow::libraryStarsArchivesAndSearches()
{
    QTemporaryDir directory;
    const QString settings = directory.filePath(QStringLiteral("settings.ini"));
    ui::MainWindow window(settings);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    for (const char* name : {"Week 12", "Week 13", "Blocking A2"}) {
        review::ReviewInfo info;
        info.name = QString::fromLatin1(name);
        info.folder = directory.filePath(info.name);
        info.projectPath = directory.filePath(info.name + QStringLiteral("/r.atkproj"));
        window.reviewLibrary()->add(info);
    }
    window.showReviewLibrary();
    ui::ReviewLibraryPage* page = window.reviewLibraryPage();
    QCOMPARE(page->listedReviews().size(), 3);

    // Star the first listed review from its row.
    const QUuid first = page->listedReviews().first();
    auto* star = page->findChild<QToolButton*>(QStringLiteral("ReviewRowStar"));
    QVERIFY(star);
    star->click();
    QVERIFY(window.reviewLibrary()->find(first)->starred);
    page->setSection(ui::ReviewLibraryPage::Section::Starred);
    QCOMPARE(page->listedReviews(), QVector<QUuid>{first});

    // Saved: a new reader sees the star.
    review::ReviewLibrary reread(window.reviewLibrary()->filePath());
    QVERIFY(reread.load());
    QVERIFY(reread.find(first)->starred);

    // Archived reviews leave All and appear under Archived.
    review::ReviewInfo archived = *window.reviewLibrary()->find(first);
    archived.archived = true;
    window.reviewLibrary()->update(archived);
    page->setSection(ui::ReviewLibraryPage::Section::All);
    QCOMPARE(page->listedReviews().size(), 2);
    page->setSection(ui::ReviewLibraryPage::Section::Archived);
    QCOMPARE(page->listedReviews(), QVector<QUuid>{first});

    // Search by name, and sort by name.
    page->setSection(ui::ReviewLibraryPage::Section::All);
    page->setSearchText(QStringLiteral("week"));
    QCOMPARE(page->listedReviews().size(), 1);
    page->setSearchText(QString());
    page->setOrder(ui::ReviewLibraryPage::Order::Name);
    QCOMPARE(window.reviewLibrary()->find(page->listedReviews().first())->name, QStringLiteral("Blocking A2"));
}

void TestReviewWorkflow::newClipsInTheFolderAreOffered()
{
    QTemporaryDir directory;
    const QString folder = makeClassFolder(directory);
    const QString projectPath = QDir(folder).filePath(QStringLiteral("Week.atkproj"));
    ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.createReview(QStringLiteral("Week"), folder, false, review::scanFolder(folder, false), projectPath,
                                false));
    const QUuid id = window.reviewLibrary()->findByProject(projectPath)->id;
    // A late submission lands in the folder.
    QVERIFY(QFile::copy(fixture("atk_fixture_48f.mp4"), folder + QStringLiteral("/student3.mp4")));

    bool asked = false;
    QTimer driver;
    driver.setInterval(20);
    connect(&driver, &QTimer::timeout, this, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            asked = box->text().contains(QStringLiteral("student3.mp4"));
            box->button(QMessageBox::Yes)->click();
        }
    });
    driver.start();
    QVERIFY(window.openReview(id, true));
    driver.stop();
    QVERIFY(asked);
    QCOMPARE(window.project()->entries().size(), 4);
    QCOMPARE(window.project()->entries().last().source->filePath(), QDir(folder).filePath(QStringLiteral("student3.mp4")));
    QVERIFY(!window.project()->isModified()); // saved with the new clip
    QCOMPARE(window.reviewLibrary()->find(id)->clipCount, 4);
}

void TestReviewWorkflow::classListRenamesShownNamesByDefault()
{
    QTemporaryDir directory;
    ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const ClassReview r = makeClassReview(directory, window);
    QCOMPARE(window.project()->entries().size(), 3);

    ui::BatchRenameDialog dialog(clipsOf(window.project()), QStringLiteral("Animatics"), QStringLiteral("{last}_{first}"));
    QVERIFY(dialog.loadRoster(r.rosterPath));
    // Natural order: 1004219_final, A1_Logel..., jsmith_walk.
    QCOMPARE(dialog.matchFor(0), 0); // by ID -> Adams
    QCOMPARE(dialog.matchFor(1), 1); // last_first -> Logel
    QCOMPARE(dialog.matchFor(2), 2); // initial+last -> Smith
    QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("BatchRenameMissing"))->text().contains(QStringLiteral("Wei Zhou")));
    QVERIFY(!dialog.renameOnDisk()); // the safe choice is the default
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("BatchRenameApply"))->isEnabled());

    // Correct a match by hand: give Jane's clip to nobody.
    dialog.setMatch(2, -1);
    QCOMPARE(dialog.matchFor(2), -1);
    dialog.setMatch(2, 2);
    dialog.findChild<QCheckBox*>(QStringLiteral("BatchRenameOrder"))->setChecked(true);

    const QVector<review::RenameDecision> decisions = dialog.decisions();
    QCOMPARE(decisions.size(), 3);
    QCOMPARE(decisions[0].newName, QStringLiteral("Adams_Kim"));
    QVERIFY(decisions[0].newPath.isEmpty()); // nothing on disk
    QVERIFY(window.applyRosterRename(decisions, false, true));

    const auto& entries = window.project()->entries();
    QCOMPARE(entries[0].displayName, QStringLiteral("Adams_Kim.mkv"));
    QCOMPARE(entries[1].displayName, QStringLiteral("Logel_Jordan.mp4"));
    QCOMPARE(entries[2].displayName, QStringLiteral("Smith_Jane.mp4"));
    // The files are untouched.
    QVERIFY(QFileInfo::exists(r.folder + QStringLiteral("/jsmith_walk.mp4")));
    QVERIFY(!QFileInfo::exists(r.folder + QStringLiteral("/Smith_Jane.mp4")));
    QVERIFY(window.project()->isModified());
}

void TestReviewWorkflow::classListRenamesFilesOnDiskAndUndoes()
{
    QTemporaryDir directory;
    ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const ClassReview r = makeClassReview(directory, window);
    // Review state on a clip must survive its file being renamed.
    const QUuid smith = window.project()->entries()[2].id;
    QVERIFY(window.project()->setClipStatus(smith, project::ClipStatus::Approved));

    ui::BatchRenameDialog dialog(clipsOf(window.project()), QStringLiteral("Animatics"),
                                 QStringLiteral("{order:02}_{last}_{first}"));
    QVERIFY(dialog.loadRoster(r.rosterPath));
    dialog.findChild<QRadioButton*>(QStringLiteral("BatchRenameOnDisk"))->setChecked(true);
    QVERIFY(dialog.diskPlan().ok());
    QCOMPARE(dialog.diskPlan().changeCount(), 3);
    QVERIFY(window.applyRosterRename(dialog.decisions(), true, false));

    QVERIFY(QFileInfo::exists(r.folder + QStringLiteral("/03_Smith_Jane.mp4")));
    QVERIFY(!QFileInfo::exists(r.folder + QStringLiteral("/jsmith_walk.mp4")));
    const int index = window.project()->indexForId(smith);
    QCOMPARE(window.project()->entries()[index].source->filePath(), QDir(r.folder).filePath(QStringLiteral("03_Smith_Jane.mp4")));
    QCOMPARE(window.project()->entries()[index].status, project::ClipStatus::Approved);
    QCOMPARE(window.project()->entries()[index].displayName, QStringLiteral("03_Smith_Jane.mp4"));
    // Saved, so the project on disk points at the new names.
    QVERIFY(!window.project()->isModified());
    project::Project saved;
    QVERIFY(project::ProjectSerializer::load(saved, r.projectPath).ok);
    QCOMPARE(saved.entries()[index].source->filePath(), QDir(r.folder).filePath(QStringLiteral("03_Smith_Jane.mp4")));
    QTRY_VERIFY(window.project()->activeIndex() >= 0); // still playing a clip

    // Undo puts the files and the project back.
    const QString journal = review::latestUndoableJournal(r.folder);
    QVERIFY(!journal.isEmpty());
    QVERIFY(window.undoRosterRename(journal));
    QVERIFY(QFileInfo::exists(r.folder + QStringLiteral("/jsmith_walk.mp4")));
    QVERIFY(!QFileInfo::exists(r.folder + QStringLiteral("/03_Smith_Jane.mp4")));
    QCOMPARE(window.project()->entries()[index].source->filePath(), QDir(r.folder).filePath(QStringLiteral("jsmith_walk.mp4")));
    QCOMPARE(window.project()->entries()[index].status, project::ClipStatus::Approved);
}

QTEST_MAIN(TestReviewWorkflow)
#include "tst_reviewworkflow.moc"
