#include "review/FolderScanner.h"
#include "review/ReviewLibrary.h"
#include "ui/BatchRenameDialog.h"
#include "ui/IconProvider.h"
#include "ui/MainWindow.h"
#include "ui/NewReviewDialog.h"
#include "ui/ReviewLibraryPage.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QComboBox>
#include <QHash>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>

using namespace atk;

namespace {

/// What a screen reader announces for a control: its accessible name, else
/// its text. Icon-only buttons with neither are announced as just "button".
QStringList unnamedControls(const QWidget* root)
{
    QStringList unnamed;
    const auto describe = [](const QWidget* w) {
        QString path = w->objectName().isEmpty() ? QString::fromLatin1(w->metaObject()->className()) : w->objectName();
        for (const QWidget* p = w->parentWidget(); p; p = p->parentWidget())
            if (!p->objectName().isEmpty()) {
                path = p->objectName() + QLatin1Char('/') + path;
                break;
            }
        return path;
    };
    for (const QAbstractButton* button : root->findChildren<QAbstractButton*>()) {
        // Qt's own: toolbar and menu-bar overflow arrows, a line edit's clear button.
        if (button->objectName().startsWith(QLatin1String("qt_")) || button->inherits("QLineEditIconButton")
            || button->inherits("QTableCornerButton"))
            continue;
        QString name = button->accessibleName();
        if (name.isEmpty()) name = button->text();
        name.remove(QLatin1Char('&'));
        if (name.trimmed().isEmpty()) unnamed << describe(button);
    }
    // Fields and lists without a visible label next to them need a name too.
    for (const QWidget* field : root->findChildren<QWidget*>()) {
        const bool needsName = qobject_cast<const QLineEdit*>(field) || qobject_cast<const QAbstractSlider*>(field)
                               || qobject_cast<const QAbstractItemView*>(field);
        if (!needsName || field->inherits("QComboBoxListView") || field->parentWidget()->inherits("QComboBox")
            || field->parentWidget()->inherits("QAbstractSpinBox") || field->inherits("QHeaderView")
            || field->inherits("QScrollBar")) // scroll bars are announced by the view they belong to
            continue;
        if (field->accessibleName().trimmed().isEmpty()) unnamed << describe(field);
    }
    unnamed.removeDuplicates();
    return unnamed;
}

/// The review header has the top row to itself, with the drawing tools on the
/// row below it.
bool headerAboveDrawingTools(const ui::MainWindow& window)
{
    auto* header = window.findChild<QToolBar*>(QStringLiteral("ReviewHeaderToolbar"));
    auto* drawing = window.findChild<QToolBar*>(QStringLiteral("annotationToolbar"));
    return header && drawing && header->isVisible() && window.toolBarArea(header) == Qt::TopToolBarArea
        && window.toolBarArea(drawing) == Qt::TopToolBarArea && !window.toolBarBreak(header)
        && window.toolBarBreak(drawing);
}

} // namespace

class TestUiPolish : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("ATK_NO_OPEN_EXPORTS", "1"); }
    void everyControlHasAName();
    void dialogsHaveNamedControls();
    void reviewLibraryWorksFromTheKeyboard();
    void iconsAreSharpOnHighDpiScreens();
    void aMaximizedLayoutReopens();
    void aLayoutWithTheHeaderElsewhereIsReset();
};

void TestUiPolish::everyControlHasAName()
{
    QTemporaryDir directory;
    ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const QStringList unnamed = unnamedControls(&window);
    QVERIFY2(unnamed.isEmpty(), qPrintable(unnamed.join(QStringLiteral(", "))));
}

void TestUiPolish::dialogsHaveNamedControls()
{
    QTemporaryDir directory;
    ui::NewReviewDialog review(directory.path());
    QStringList unnamed = unnamedControls(&review);
    QVERIFY2(unnamed.isEmpty(), qPrintable(QStringLiteral("New Review: ") + unnamed.join(QStringLiteral(", "))));

    ui::BatchRenameDialog rename({{QUuid::createUuid(), directory.filePath(QStringLiteral("a.mp4")), QStringLiteral("a.mp4"), false}},
                                 QStringLiteral("Review"), QStringLiteral("{last}_{first}"));
    unnamed = unnamedControls(&rename);
    QVERIFY2(unnamed.isEmpty(), qPrintable(QStringLiteral("Rename: ") + unnamed.join(QStringLiteral(", "))));
}

void TestUiPolish::reviewLibraryWorksFromTheKeyboard()
{
    QTemporaryDir directory;
    ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    review::ReviewInfo info;
    info.name = QStringLiteral("Week 13 Capstone");
    info.folder = directory.path();
    info.projectPath = directory.filePath(QStringLiteral("Week 13 Capstone.atkproj"));
    info.clipCount = 3;
    const QUuid id = window.reviewLibrary()->add(info);
    window.showReviewLibrary();
    ui::ReviewLibraryPage* page = window.reviewLibraryPage();
    page->refresh();

    // Every control on a review's row has a name.
    const QStringList unnamed = unnamedControls(page);
    QVERIFY2(unnamed.isEmpty(), qPrintable(unnamed.join(QStringLiteral(", "))));

    // Sections are reachable with Tab and switch with the arrow keys.
    auto* sections = page->findChild<QListWidget*>(QStringLiteral("ReviewSections"));
    QVERIFY(sections->focusPolicy() & Qt::TabFocus);
    sections->setFocus();
    QTest::keyClick(sections, Qt::Key_Down);
    QCOMPARE(sections->currentRow(), 1); // Starred
    QTest::keyClick(sections, Qt::Key_Up);
    QCOMPARE(sections->currentRow(), 0);

    // A review opens from its Open button with Space, like any button.
    disconnect(page, &ui::ReviewLibraryPage::openReviewRequested, nullptr, nullptr); // not a real project
    QSignalSpy open(page, &ui::ReviewLibraryPage::openReviewRequested);
    // Rows are rebuilt when the section changes; take the one on screen.
    QPushButton* openButton = nullptr;
    const auto shownOpenButton = [&] {
        for (QPushButton* b : page->findChildren<QPushButton*>(QStringLiteral("ReviewRowOpen")))
            if (b->isVisible()) openButton = b;
        return openButton != nullptr;
    };
    QTRY_VERIFY(shownOpenButton());
    QVERIFY(openButton->focusPolicy() & Qt::TabFocus);
    openButton->setFocus();
    QTest::keyClick(openButton, Qt::Key_Space); // not the window's Play/Pause shortcut
    QCOMPARE(open.count(), 1);
    QCOMPARE(open.first().first().toUuid(), id);
    QTest::keyClick(openButton, Qt::Key_Return);
    QTRY_COMPARE(open.count(), 2);
}

void TestUiPolish::iconsAreSharpOnHighDpiScreens()
{
    // Icons are drawn from vector sources at the screen's scale, not
    // stretched from a 1x bitmap.
    const QIcon icon = ui::icons::icon(QStringLiteral("repeat"));
    for (const qreal dpr : {1.0, 1.5, 2.0, 3.0}) {
        const QPixmap pixmap = icon.pixmap(QSize(20, 20), dpr);
        QCOMPARE(pixmap.devicePixelRatio(), dpr);
        QCOMPARE(pixmap.size(), (QSizeF(20, 20) * dpr).toSize());
        // Something is drawn at full resolution: thin strokes keep some
        // pixels fully opaque rather than smeared.
        const QImage image = pixmap.toImage();
        int opaque = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) opaque += qAlpha(image.pixel(x, y)) > 200;
        QVERIFY2(opaque > 0, qPrintable(QStringLiteral("no crisp pixels at %1x").arg(dpr)));
    }
}

void TestUiPolish::aMaximizedLayoutReopens()
{
    // Closing the window maximized saves a maximized layout. The next start
    // restores it while the window is still hidden, and Qt finishes applying
    // it when the window is shown at its maximized size. 0.5.3 crashed there
    // on every start after the first.
    QTemporaryDir directory;
    const QString settings = directory.filePath(QStringLiteral("settings.ini"));
    {
        ui::MainWindow window(settings);
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(window.isMaximized());
        QVERIFY(window.close());
    }
    QVERIFY(!QSettings(settings, QSettings::IniFormat).value(QStringLiteral("ui/mainWindowState")).toByteArray().isEmpty());
    for (int start = 0; start < 3; ++start) {
        ui::MainWindow window(settings);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.isMaximized());
        QVERIFY(headerAboveDrawingTools(window));
        QVERIFY(window.close());
    }
}

void TestUiPolish::aLayoutWithTheHeaderElsewhereIsReset()
{
    // A layout saved with the review header somewhere else -- as layouts from
    // before the header did -- opens with the header on its own top row.
    // Saved maximized, so it is restored while the window is still hidden.
    QTemporaryDir directory;
    const QString settings = directory.filePath(QStringLiteral("settings.ini"));
    {
        ui::MainWindow window(settings);
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(window.isMaximized());
        auto* header = window.findChild<QToolBar*>(QStringLiteral("ReviewHeaderToolbar"));
        QVERIFY(header);
        window.removeToolBar(header);
        window.addToolBar(Qt::TopToolBarArea, header); // after the drawing tools, on their row
        header->show();
        QVERIFY(!headerAboveDrawingTools(window));
        QVERIFY(window.close());
    }
    const QStringList layoutKeys{QStringLiteral("ui/mainWindowGeometry"), QStringLiteral("ui/mainWindowState")};
    QHash<QString, QByteArray> saved;
    {
        const QSettings ini(settings, QSettings::IniFormat);
        for (const QString& key : layoutKeys) saved.insert(key, ini.value(key).toByteArray());
    }
    QVERIFY(!saved.value(layoutKeys.last()).isEmpty());
    for (int start = 0; start < 2; ++start) {
        {
            QSettings ini(settings, QSettings::IniFormat);
            for (const QString& key : layoutKeys) ini.setValue(key, saved.value(key));
        }
        ui::MainWindow window(settings);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.isMaximized());
        QVERIFY(headerAboveDrawingTools(window));
        QVERIFY(window.close());
    }
}

QTEST_MAIN(TestUiPolish)
#include "tst_uipolish.moc"
