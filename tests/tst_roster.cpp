#include "review/BatchRename.h"
#include "review/Roster.h"
#include "review/RosterMatcher.h"
#include "review/ZipArchive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

using namespace atk::review;

namespace {

/// Raw DEFLATE data from Qt's zlib: qCompress gives a 4-byte length, a
/// 2-byte zlib header, the raw stream and a 4-byte checksum.
QByteArray rawDeflate(const QByteArray& data, int level = -1)
{
    const QByteArray z = qCompress(data, level);
    return z.mid(6, z.size() - 10);
}

void put16(QByteArray& b, quint16 v) { b.append(char(v & 0xff)).append(char(v >> 8)); }
void put32(QByteArray& b, quint32 v) { put16(b, quint16(v & 0xffff)); put16(b, quint16(v >> 16)); }

/// A minimal zip writer (deflated entries), enough to make an .xlsx.
QByteArray makeZip(const QVector<QPair<QString, QByteArray>>& files)
{
    QByteArray zip, central;
    for (const auto& [name, data] : files) {
        const QByteArray packed = rawDeflate(data, 9);
        const quint32 offset = quint32(zip.size());
        const QByteArray n = name.toUtf8();
        put32(zip, 0x04034b50); put16(zip, 20); put16(zip, 0); put16(zip, 8); put16(zip, 0); put16(zip, 0);
        put32(zip, 0); put32(zip, quint32(packed.size())); put32(zip, quint32(data.size()));
        put16(zip, quint16(n.size())); put16(zip, 0); zip.append(n); zip.append(packed);
        put32(central, 0x02014b50); put16(central, 20); put16(central, 20); put16(central, 0); put16(central, 8);
        put16(central, 0); put16(central, 0); put32(central, 0); put32(central, quint32(packed.size()));
        put32(central, quint32(data.size())); put16(central, quint16(n.size())); put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0); put32(central, 0); put32(central, offset); central.append(n);
    }
    const quint32 centralOffset = quint32(zip.size());
    zip.append(central);
    put32(zip, 0x06054b50); put16(zip, 0); put16(zip, 0); put16(zip, quint16(files.size()));
    put16(zip, quint16(files.size())); put32(zip, quint32(central.size())); put32(zip, centralOffset); put16(zip, 0);
    return zip;
}

QByteArray makeXlsx()
{
    const QByteArray workbook = "<?xml version='1.0'?><workbook xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main' xmlns:r='http://schemas.openxmlformats.org/officeDocument/2006/relationships'><sheets><sheet name='Class' sheetId='1' r:id='rId7'/></sheets></workbook>";
    const QByteArray rels = "<?xml version='1.0'?><Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'><Relationship Id='rId7' Type='worksheet' Target='worksheets/class.xml'/></Relationships>";
    const QByteArray strings = "<?xml version='1.0'?><sst xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main'><si><t>Student ID</t></si><si><t>First Name</t></si><si><t>Last Name</t></si><si><r><t>Jor</t></r><r><t>dan</t></r></si><si><t>Logel</t></si><si><t>José</t></si></sst>";
    const QByteArray sheet = "<?xml version='1.0'?><worksheet xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main'><sheetData><row r='1'><c r='A1' t='s'><v>0</v></c><c r='B1' t='s'><v>1</v></c><c r='C1' t='s'><v>2</v></c></row><row r='2'><c r='A2'><v>1004217</v></c><c r='B2' t='s'><v>3</v></c><c r='C2' t='s'><v>4</v></c></row><row r='4'><c r='A4'><v>1004218</v></c><c r='B4' t='s'><v>5</v></c><c r='C4' t='inlineStr'><is><t>Núñez</t></is></c></row></sheetData></worksheet>";
    return makeZip({{QStringLiteral("xl/workbook.xml"), workbook},
                    {QStringLiteral("xl/_rels/workbook.xml.rels"), rels},
                    {QStringLiteral("xl/sharedStrings.xml"), strings},
                    {QStringLiteral("xl/worksheets/class.xml"), sheet}});
}

void touch(const QString& path, const QByteArray& content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QByteArray contents(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class TestRoster : public QObject {
    Q_OBJECT
private slots:
    void inflateMatchesZlib_data();
    void inflateMatchesZlib();
    void inflateRefusesDamageAndBombs();
    void readsTheFirstSheetOfAnXlsx();
    void readsCsvInExcelsEncodingsAndDelimiters();
    void csvQuotesBlankRowsAndNoHeader();
    void columnsAreGuessedFromHeaders();
    void matchesClipNamesToPeople();
    void templatesBuildSafeNames();
    void planCatchesEveryProblemBeforeTouchingFiles();
    void renameSwapsSafelyAndUndoes();
    void failedRenameRollsBack();
    void interruptedRenameIsRecovered();
};

void TestRoster::inflateMatchesZlib_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<int>("level");
    QByteArray text;
    for (int i = 0; i < 400; ++i) text += "Smith_Jane_walk_cycle_v" + QByteArray::number(i % 7) + ".mov,";
    QByteArray noise(70000, 0);
    QRandomGenerator rng(42);
    for (char& c : noise) c = char(rng.bounded(256));
    QTest::newRow("short text (fixed codes)") << QByteArray("hello hello hello") << -1;
    QTest::newRow("long text (dynamic codes)") << text << 9;
    QTest::newRow("stored blocks") << text << 0;
    QTest::newRow("incompressible noise") << noise << 6;
}

void TestRoster::inflateMatchesZlib()
{
    QFETCH(QByteArray, data);
    QFETCH(int, level);
    const auto out = inflateRaw(rawDeflate(data, level));
    QVERIFY(out.has_value());
    QCOMPARE(*out, data);
}

void TestRoster::inflateRefusesDamageAndBombs()
{
    // The empty stream: one final fixed block holding only end-of-block.
    QCOMPARE(inflateRaw(QByteArray("\x03\x00", 2)).value_or(QByteArray("x")), QByteArray());
    QByteArray text(100000, 'a');
    const QByteArray packed = rawDeflate(text, 9);
    QVERIFY(!inflateRaw(packed.left(packed.size() / 2)).has_value()); // truncated
    QVERIFY(!inflateRaw(packed, 1000).has_value());                    // bigger than allowed
    QVERIFY(!inflateRaw(QByteArray("\xff\xff\xff\xff", 4)).has_value());
}

void TestRoster::readsTheFirstSheetOfAnXlsx()
{
    const RosterResult result = parseXlsx(makeXlsx());
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.roster.headers, (QStringList{QStringLiteral("Student ID"), QStringLiteral("First Name"),
                                                 QStringLiteral("Last Name")}));
    QCOMPARE(result.roster.rows.size(), 2); // the empty row 3 is dropped
    QCOMPARE(result.roster.rows[0], (QStringList{QStringLiteral("1004217"), QStringLiteral("Jordan"), QStringLiteral("Logel")}));
    QCOMPARE(result.roster.rows[1][1], QStringLiteral("José"));
    QCOMPARE(result.roster.rows[1][2], QStringLiteral("Núñez")); // inline string

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("class.xlsx"));
    touch(path, makeXlsx());
    QVERIFY(readRosterFile(path).ok);
    QVERIFY(!parseXlsx("not a zip").ok);
    touch(dir.filePath(QStringLiteral("old.xls")), "x");
    QVERIFY(readRosterFile(dir.filePath(QStringLiteral("old.xls"))).error.contains(QStringLiteral(".xlsx")));
}

void TestRoster::readsCsvInExcelsEncodingsAndDelimiters()
{
    // UTF-8 with BOM, commas.
    RosterResult r = parseCsv("\xEF\xBB\xBFId,First,Last\r\n7,Zo\xC3\xAB,Smith\r\n");
    QVERIFY(r.ok);
    QCOMPARE(r.roster.rows[0][1], QStringLiteral("Zoë"));
    // Windows-1252 (Excel "CSV (Comma delimited)"), semicolons (European Excel).
    r = parseCsv("Id;First;Last\n8;Ren\xE9\x92s;O\x92Neil\n");
    QVERIFY(r.ok);
    QCOMPARE(r.roster.headers.size(), 3);
    QCOMPARE(r.roster.rows[0][2], QStringLiteral("O’Neil"));
    // UTF-16 LE with BOM (Excel "Unicode Text"), tabs.
    QString text = QStringLiteral("Id\tFirst\tLast\n9\tAi\tTanaka\n");
    QByteArray utf16("\xFF\xFE");
    utf16.append(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
    r = parseCsv(utf16);
    QVERIFY(r.ok);
    QCOMPARE(r.roster.rows[0][2], QStringLiteral("Tanaka"));
}

void TestRoster::csvQuotesBlankRowsAndNoHeader()
{
    const RosterResult r = parseCsv("\"Logel, Jordan\",\"says \"\"hi\"\"\",\"two\nlines\"\n\n,,\nSmith,x,y\n", false);
    QVERIFY(r.ok);
    QCOMPARE(r.roster.headers, (QStringList{QStringLiteral("Column A"), QStringLiteral("Column B"), QStringLiteral("Column C")}));
    QCOMPARE(r.roster.rows.size(), 2);
    QCOMPARE(r.roster.rows[0][0], QStringLiteral("Logel, Jordan"));
    QCOMPARE(r.roster.rows[0][1], QStringLiteral("says \"hi\""));
    QCOMPARE(r.roster.rows[0][2], QStringLiteral("two\nlines"));
    QVERIFY(!parseCsv("\n\n").ok);
    QCOMPARE(columnIndex(QStringLiteral("AA")), 26);
    QCOMPARE(columnLetters(27), QStringLiteral("AB"));
}

void TestRoster::columnsAreGuessedFromHeaders()
{
    RosterColumns c = guessColumns({QStringLiteral("Student #"), QStringLiteral("Surname"), QStringLiteral("Given Name"),
                                    QStringLiteral("Email")});
    QCOMPARE(c.last, 1);
    QCOMPARE(c.first, 2);
    QCOMPARE(c.id, 3); // e-mail is the identifier when there is no number column
    c = guessColumns({QStringLiteral("Student ID"), QStringLiteral("Name")});
    QCOMPARE(c.id, 0);
    QCOMPARE(c.full, 1);
    const PersonName n = personName({QStringLiteral("7"), QStringLiteral("Logel, Jordan")}, c);
    QCOMPARE(n.first, QStringLiteral("Jordan"));
    QCOMPARE(n.last, QStringLiteral("Logel"));
    QCOMPARE(personName({QStringLiteral("7"), QStringLiteral("Mary Ann Lee")}, c).last, QStringLiteral("Lee"));
}

void TestRoster::matchesClipNamesToPeople()
{
    Roster roster;
    roster.headers = {QStringLiteral("ID"), QStringLiteral("First"), QStringLiteral("Last")};
    roster.rows = {{QStringLiteral("1004217"), QStringLiteral("Jordan"), QStringLiteral("Logel")},
                   {QStringLiteral("1004218"), QStringLiteral("José"), QStringLiteral("Núñez")},
                   {QStringLiteral("1004219"), QStringLiteral("Ann"), QStringLiteral("Smith")},
                   {QStringLiteral("1004220"), QStringLiteral("Bo"), QStringLiteral("Smith")},
                   {QStringLiteral("1004221"), QStringLiteral("Kai"), QStringLiteral("Hartmann")},
                   {QStringLiteral("1004222"), QStringLiteral("Lee"), QStringLiteral("Okafor")},
                   {QStringLiteral("1004223"), QStringLiteral("No"), QStringLiteral("Submission")}};
    const RosterColumns columns{0, 1, 2, -1};
    const QStringList clips{QStringLiteral("A1_Logel_Jordan_ThreeShotAnimatic_v002.mp4"), // last_first
                            QStringLiteral("jose-nunez walk.mov"),                        // accents dropped
                            QStringLiteral("ASmith_blocking.mp4"),                        // initial + last
                            QStringLiteral("1004220_final.mov"),                          // ID
                            QStringLiteral("hartman_kai.mp4"),                            // a typo: still first+last? no
                            QStringLiteral("okafr_walk.mp4"),                             // last name one letter off
                            QStringLiteral("untitled.mov")};
    const QVector<RosterMatch> m = matchClipsToRoster(clips, roster, columns);
    QCOMPARE(m.size(), clips.size());
    QCOMPARE(m[0].roster, 0);
    QCOMPARE(m[0].confidence, MatchConfidence::High);
    QCOMPARE(m[1].roster, 1);
    QCOMPARE(m[2].roster, 2);
    QCOMPARE(m[3].roster, 3);
    QCOMPARE(m[3].score, 100);
    QCOMPARE(m[4].roster, 4); // "hartman" is one letter off "hartmann"
    QCOMPARE(m[4].confidence, MatchConfidence::Low);
    QCOMPARE(m[5].roster, 5);
    QCOMPARE(m[5].confidence, MatchConfidence::Low);
    QCOMPARE(m[6].roster, -1);
    QCOMPARE(m[6].confidence, MatchConfidence::None);

    // "Smith" alone is ambiguous (two Smiths): no guess.
    const QVector<RosterMatch> ambiguous = matchClipsToRoster({QStringLiteral("smith.mov")}, roster, columns);
    QCOMPARE(ambiguous[0].roster, -1);
    // One person, one clip: the better clip wins.
    const QVector<RosterMatch> twice = matchClipsToRoster(
        {QStringLiteral("logel.mov"), QStringLiteral("jordan_logel.mov")}, roster, columns);
    QCOMPARE(twice[1].roster, 0);
    QCOMPARE(twice[0].roster, -1);
}

void TestRoster::templatesBuildSafeNames()
{
    NameContext c;
    c.order = 3;
    c.id = QStringLiteral("1004217");
    c.first = QStringLiteral("Jordan");
    c.last = QStringLiteral("Logel");
    c.headers = {QStringLiteral("Section")};
    c.row = {QStringLiteral("B2")};
    c.original = QStringLiteral("animatic v2");
    c.review = QStringLiteral("Week 13");
    c.date = QDate(2026, 10, 12);
    QCOMPARE(renderName(QStringLiteral("{order:02}_{last}_{first}"), c), QStringLiteral("03_Logel_Jordan"));
    QCOMPARE(renderName(QStringLiteral("{id}_{full} ({col:Section})"), c), QStringLiteral("1004217_Jordan Logel (B2)"));
    QCOMPARE(renderName(QStringLiteral("{review} - {original} {date}"), c), QStringLiteral("Week 13 - animatic v2 2026-10-12"));
    QCOMPARE(renderName(QStringLiteral("{last}{typo}"), c), QStringLiteral("Logel{typo}"));
    QCOMPARE(renderName(QStringLiteral("a/b:c*d. "), c), QStringLiteral("a_b_c_d"));
    QCOMPARE(safeFileName(QStringLiteral("CON")), QStringLiteral("CON_"));
}

void TestRoster::planCatchesEveryProblemBeforeTouchingFiles()
{
    QTemporaryDir dir;
    const QDir d(dir.path());
    for (const char* name : {"a.mov", "b.mov", "c.mov", "d.mov", "keep.mov", "taken.mov"})
        touch(d.filePath(QString::fromLatin1(name)), name);
    const RenamePlan plan = planRenames({{d.filePath("a.mov"), d.filePath("x.mov")},
                                         {d.filePath("b.mov"), d.filePath("X.mov")},      // same as x on Windows/macOS
                                         {d.filePath("c.mov"), d.filePath("taken.mov")},  // exists, not being renamed
                                         {d.filePath("d.mov"), d.filePath("bad:name.mov")},
                                         {d.filePath("keep.mov"), d.filePath("keep.mov")},
                                         {d.filePath("gone.mov"), d.filePath("y.mov")},
                                         {d.filePath("a.mov"), d.filePath(" .mov")}},
                                        true);
    QCOMPARE(plan.items[0].problem, RenameProblem::DuplicateTarget);
    QCOMPARE(plan.items[1].problem, RenameProblem::DuplicateTarget);
    QCOMPARE(plan.items[2].problem, RenameProblem::TargetExists);
    QCOMPARE(plan.items[3].problem, RenameProblem::IllegalName);
    QCOMPARE(plan.items[4].problem, RenameProblem::Unchanged);
    QCOMPARE(plan.items[5].problem, RenameProblem::SourceMissing);
    QCOMPARE(plan.items[6].problem, RenameProblem::EmptyName);
    QVERIFY(!plan.ok());
    const RenameResult refused = executeRenames(plan);
    QVERIFY(!refused.ok);
    QVERIFY(QFileInfo::exists(d.filePath("a.mov"))); // nothing touched
    QVERIFY(!QFileInfo::exists(d.filePath("x.mov")));
}

void TestRoster::renameSwapsSafelyAndUndoes()
{
    QTemporaryDir dir;
    const QDir d(dir.path());
    touch(d.filePath("one.mov"), "ONE");
    touch(d.filePath("two.mov"), "TWO");
    touch(d.filePath("three.mov"), "THREE");
    // A swap, and a case-only change: both need the two-step rename.
    const RenamePlan plan = planRenames({{d.filePath("one.mov"), d.filePath("two.mov")},
                                         {d.filePath("two.mov"), d.filePath("one.mov")},
                                         {d.filePath("three.mov"), d.filePath("Three.mov")}});
    QVERIFY(plan.ok());
    QCOMPARE(plan.changeCount(), 3);
    const RenameResult done = executeRenames(plan);
    QVERIFY2(done.ok, qPrintable(done.error));
    QCOMPARE(done.renamed, 3);
    QCOMPARE(contents(d.filePath("two.mov")), QByteArray("ONE"));
    QCOMPARE(contents(d.filePath("one.mov")), QByteArray("TWO"));
    QCOMPARE(contents(d.filePath("Three.mov")), QByteArray("THREE"));
    QVERIFY(QFileInfo::exists(done.journalPath));
    QCOMPARE(latestUndoableJournal(d.path()), done.journalPath);
    QVERIFY(d.entryList({QStringLiteral(".atk-renaming-*")}, QDir::Files | QDir::Hidden).isEmpty()); // no temp left

    const RenameResult undone = undoRenames(done.journalPath);
    QVERIFY2(undone.ok, qPrintable(undone.error));
    QCOMPARE(contents(d.filePath("one.mov")), QByteArray("ONE"));
    QCOMPARE(contents(d.filePath("two.mov")), QByteArray("TWO"));
    QCOMPARE(contents(d.filePath("three.mov")), QByteArray("THREE"));
    QVERIFY(latestUndoableJournal(d.path()).isEmpty()); // undone once only
    QVERIFY(!undoRenames(done.journalPath).ok);
}

void TestRoster::failedRenameRollsBack()
{
    QTemporaryDir dir;
    const QDir d(dir.path());
    touch(d.filePath("a.mov"), "A");
    touch(d.filePath("b.mov"), "B");
    // The second target's folder does not exist, so its rename fails part way.
    const RenamePlan plan{{{d.filePath("a.mov"), d.filePath("a2.mov"), RenameProblem::None},
                           {d.filePath("b.mov"), d.filePath("missing-folder/b2.mov"), RenameProblem::None}}};
    const RenameResult result = executeRenames(plan);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(contents(d.filePath("a.mov")), QByteArray("A")); // put back
    QCOMPARE(contents(d.filePath("b.mov")), QByteArray("B"));
    QVERIFY(!QFileInfo::exists(d.filePath("a2.mov")));
    QVERIFY(d.entryList({QStringLiteral(".atk-renaming-*")}, QDir::Files | QDir::Hidden).isEmpty());
    QVERIFY(latestUndoableJournal(d.path()).isEmpty());
}

void TestRoster::interruptedRenameIsRecovered()
{
    QTemporaryDir dir;
    const QDir d(dir.path());
    touch(d.filePath("a.mov"), "A");
    touch(d.filePath("b.mov"), "B");
    touch(d.filePath("c.mov"), "C");
    // Simulate a crash: a journal still "started", a at its new name, b
    // parked at its temporary name, c never moved.
    const QString journal = d.filePath(QStringLiteral(".atk-rename-20261012-101010-000.json"));
    const QByteArray json = QJsonDocument(QJsonObject{
        {QStringLiteral("format"), QStringLiteral("ATKRenameJournal")}, {QStringLiteral("version"), 1},
        {QStringLiteral("state"), QStringLiteral("started")}, {QStringLiteral("created"), 1.0},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{{QStringLiteral("from"), d.filePath("a.mov")}, {QStringLiteral("temp"), d.filePath(".atk-renaming-1-0.mov")}, {QStringLiteral("to"), d.filePath("a2.mov")}},
            QJsonObject{{QStringLiteral("from"), d.filePath("b.mov")}, {QStringLiteral("temp"), d.filePath(".atk-renaming-1-1.mov")}, {QStringLiteral("to"), d.filePath("b2.mov")}},
            QJsonObject{{QStringLiteral("from"), d.filePath("c.mov")}, {QStringLiteral("temp"), d.filePath(".atk-renaming-1-2.mov")}, {QStringLiteral("to"), d.filePath("c2.mov")}}}}}).toJson();
    touch(journal, json);
    QVERIFY(QFile::rename(d.filePath("a.mov"), d.filePath("a2.mov")));
    QVERIFY(QFile::rename(d.filePath("b.mov"), d.filePath(".atk-renaming-1-1.mov")));

    QCOMPARE(interruptedJournals(d.path()), QStringList{journal});
    const RenameResult recovered = recoverInterrupted(journal);
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
    QCOMPARE(contents(d.filePath("a.mov")), QByteArray("A"));
    QCOMPARE(contents(d.filePath("b.mov")), QByteArray("B"));
    QCOMPARE(contents(d.filePath("c.mov")), QByteArray("C"));
    QVERIFY(interruptedJournals(d.path()).isEmpty());
}

QTEST_MAIN(TestRoster)
#include "tst_roster.moc"
