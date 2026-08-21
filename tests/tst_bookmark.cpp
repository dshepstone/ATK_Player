#include "timeline/Bookmark.h"

#include <QTest>

using atk::timeline::Bookmark;
using atk::timeline::bookmarkColor;
using atk::timeline::bookmarkColorCount;

class TestBookmark : public QObject {
    Q_OBJECT
private slots:
    void defaultsAreEmpty();
    void optionalFieldsAndLabel();
    void paletteBoundsAreSafe();
    void equalityIncludesReviewData();
};

void TestBookmark::defaultsAreEmpty()
{
    const Bookmark bookmark;
    QCOMPARE(bookmark.frame, qint64(0));
    QVERIFY(!bookmark.hasName());
    QVERIFY(!bookmark.hasNote());
    QVERIFY(!bookmark.hasColor());
    QCOMPARE(bookmark.displayLabel(), QStringLiteral("1"));
    QCOMPARE(bookmark.frameLabel(), QStringLiteral("1"));
}

void TestBookmark::optionalFieldsAndLabel()
{
    Bookmark bookmark;
    bookmark.name = QStringLiteral("contact");
    bookmark.note = QStringLiteral("foot slides");
    bookmark.colorIndex = 2;
    QVERIFY(bookmark.hasName());
    QVERIFY(bookmark.hasNote());
    QVERIFY(bookmark.hasColor());
    QCOMPARE(bookmark.displayLabel(), QStringLiteral("contact"));
}

void TestBookmark::paletteBoundsAreSafe()
{
    QVERIFY(bookmarkColorCount() > 0);
    QVERIFY(!bookmarkColor(Bookmark::kNoColor).isValid());
    QVERIFY(!bookmarkColor(bookmarkColorCount()).isValid());
    QVERIFY(bookmarkColor(0).isValid());
}

void TestBookmark::equalityIncludesReviewData()
{
    Bookmark first;
    first.id = 7;
    first.frame = 10;
    first.mediaTimeUs = 416'667;
    first.name = QStringLiteral("pose");
    first.note = QStringLiteral("note");
    first.colorIndex = 3;
    Bookmark second = first;
    QCOMPARE(first, second);
    second.note = QStringLiteral("changed");
    QVERIFY(!(first == second));
}

QTEST_GUILESS_MAIN(TestBookmark)
#include "tst_bookmark.moc"
