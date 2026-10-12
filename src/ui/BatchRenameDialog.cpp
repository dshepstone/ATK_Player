#include "ui/BatchRenameDialog.h"

#include "ui/IconProvider.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>

namespace atk::ui {
namespace {

enum Column { OrderColumn, ClipColumn, PersonColumn, MatchColumn, NewNameColumn, ColumnCount };

QString confidenceText(review::MatchConfidence confidence, int score)
{
    switch (confidence) {
    case review::MatchConfidence::High: return score >= 100 ? BatchRenameDialog::tr("Sure (ID)") : BatchRenameDialog::tr("Sure");
    case review::MatchConfidence::Medium: return BatchRenameDialog::tr("Likely");
    case review::MatchConfidence::Low: return BatchRenameDialog::tr("Check");
    case review::MatchConfidence::None: return BatchRenameDialog::tr("No match");
    }
    return {};
}

/// The clip's name without its file extension.
QString baseName(const BatchRenameDialog::Clip& clip)
{
    const QString suffix = QFileInfo(clip.path).suffix();
    QString name = clip.displayName;
    if (!clip.sequence && !suffix.isEmpty() && name.endsWith(QLatin1Char('.') + suffix, Qt::CaseInsensitive))
        name.chop(suffix.size() + 1);
    return name;
}

void fillColumnCombo(QComboBox* combo, const QStringList& headers, int selected)
{
    const QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItem(BatchRenameDialog::tr("(none)"), -1);
    for (int i = 0; i < headers.size(); ++i) combo->addItem(headers[i], i);
    combo->setCurrentIndex(std::max(0, combo->findData(selected)));
}

} // namespace

BatchRenameDialog::BatchRenameDialog(QVector<Clip> clips, QString reviewName, QString nameTemplate, QWidget* parent)
    : QDialog(parent), m_clips(std::move(clips)), m_reviewName(std::move(reviewName))
{
    setObjectName(QStringLiteral("BatchRenameDialog"));
    setWindowTitle(tr("Rename Clips from Class List"));
    resize(920, 680);
    m_matches.resize(m_clips.size());
    auto* root = new QVBoxLayout(this);

    // --- The class list ----------------------------------------------------
    auto* listBox = new QGroupBox(tr("Class or staff list (CSV or Excel)"), this);
    auto* listLayout = new QVBoxLayout(listBox);
    auto* fileRow = new QHBoxLayout;
    m_rosterLabel = new QLabel(tr("No list chosen."), listBox);
    m_rosterLabel->setObjectName(QStringLiteral("BatchRenameRosterLabel"));
    auto* choose = new QPushButton(icons::icon(QStringLiteral("file-spreadsheet")), tr("Choose List…"), listBox);
    choose->setObjectName(QStringLiteral("BatchRenameChooseRoster"));
    m_headerRow = new QCheckBox(tr("First row has the column names"), listBox);
    m_headerRow->setChecked(true);
    fileRow->addWidget(m_rosterLabel, 1);
    fileRow->addWidget(m_headerRow);
    fileRow->addWidget(choose);
    listLayout->addLayout(fileRow);
    auto* columns = new QHBoxLayout;
    auto addColumnPicker = [&](const QString& label, QComboBox*& combo, const QString& name) {
        columns->addWidget(new QLabel(label, listBox));
        combo = new QComboBox(listBox);
        combo->setObjectName(name);
        combo->setMinimumContentsLength(10);
        columns->addWidget(combo, 1);
        connect(combo, &QComboBox::currentIndexChanged, this, &BatchRenameDialog::applyColumns);
    };
    addColumnPicker(tr("ID"), m_idColumn, QStringLiteral("BatchRenameIdColumn"));
    addColumnPicker(tr("First name"), m_firstColumn, QStringLiteral("BatchRenameFirstColumn"));
    addColumnPicker(tr("Last name"), m_lastColumn, QStringLiteral("BatchRenameLastColumn"));
    addColumnPicker(tr("Full name"), m_fullColumn, QStringLiteral("BatchRenameFullColumn"));
    listLayout->addLayout(columns);
    root->addWidget(listBox);
    connect(choose, &QPushButton::clicked, this, &BatchRenameDialog::chooseRoster);
    connect(m_headerRow, &QCheckBox::toggled, this, [this] {
        if (!m_rosterPath.isEmpty()) loadRoster(m_rosterPath);
    });

    // --- Matches -------------------------------------------------------------
    m_table = new QTableWidget(int(m_clips.size()), ColumnCount, this);
    m_table->setObjectName(QStringLiteral("BatchRenameTable"));
    m_table->setHorizontalHeaderLabels({tr("#"), tr("Clip"), tr("Person"), tr("Match"), tr("New name")});
    m_table->verticalHeader()->hide();
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(ClipColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(NewNameColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(PersonColumn, QHeaderView::ResizeToContents);
    root->addWidget(m_table, 1);
    m_missing = new QLabel(this);
    m_missing->setObjectName(QStringLiteral("BatchRenameMissing"));
    m_missing->setWordWrap(true);
    m_missing->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_missing);

    // --- New names and how to apply them -------------------------------------
    auto* form = new QFormLayout;
    m_template = new QLineEdit(nameTemplate.isEmpty() ? QStringLiteral("{last}_{first}") : nameTemplate, this);
    m_template->setObjectName(QStringLiteral("BatchRenameTemplate"));
    form->addRow(tr("New name"), m_template);
    auto* help = new QLabel(tr("Fill-ins: {last} {first} {full} {id} {order:02} {original} {review} {date} "
                               "{col:Column name}. The file extension is kept."),
                            this);
    help->setProperty("atkRole", "placeholder");
    help->setWordWrap(true);
    form->addRow(QString(), help);
    root->addLayout(form);
    connect(m_template, &QLineEdit::textChanged, this, &BatchRenameDialog::refreshNames);

    m_displayOnly = new QRadioButton(tr("Show the new names in ATK Player only. The files are not changed."), this);
    m_displayOnly->setObjectName(QStringLiteral("BatchRenameDisplayOnly"));
    m_displayOnly->setChecked(true);
    m_onDisk = new QRadioButton(tr("Rename the files on disk too (checked first, and can be undone)."), this);
    m_onDisk->setObjectName(QStringLiteral("BatchRenameOnDisk"));
    m_order = new QCheckBox(tr("Put the review in class-list order (clips with no match go last)"), this);
    m_order->setObjectName(QStringLiteral("BatchRenameOrder"));
    root->addWidget(m_displayOnly);
    root->addWidget(m_onDisk);
    root->addWidget(m_order);
    connect(m_onDisk, &QRadioButton::toggled, this, &BatchRenameDialog::refreshNames);
    connect(m_order, &QCheckBox::toggled, this, &BatchRenameDialog::refreshNames);

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("BatchRenameSummary"));
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_undo = buttons->addButton(tr("Undo Last Rename"), QDialogButtonBox::ResetRole);
    m_undo->setObjectName(QStringLiteral("BatchRenameUndo"));
    m_apply = buttons->addButton(tr("Rename"), QDialogButtonBox::AcceptRole);
    m_apply->setObjectName(QStringLiteral("BatchRenameApply"));
    m_apply->setProperty("atkRole", "primary");
    m_apply->style()->unpolish(m_apply);
    m_apply->style()->polish(m_apply);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (renameOnDisk()) {
            const int count = diskPlan().changeCount();
            if (QMessageBox::question(this, tr("Rename Files"),
                                      tr("Rename %n file(s) on disk?\n\nA record is kept in the folder so you can "
                                         "undo this later from this window.", nullptr, count),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    const QString folder = m_clips.isEmpty() ? QString() : QFileInfo(m_clips.first().path).absolutePath();
    const QString undoable = review::latestUndoableJournal(folder);
    m_undo->setEnabled(!undoable.isEmpty());
    m_undo->setToolTip(undoable.isEmpty() ? tr("No earlier rename in this folder to undo")
                                          : tr("Put back the file names from the last rename in this folder"));
    connect(m_undo, &QPushButton::clicked, this, [this, undoable] {
        m_undoJournal = undoable;
        accept();
    });

    applyColumns();
    rebuildTable();
}

bool BatchRenameDialog::loadRoster(const QString& path)
{
    const review::RosterResult result = review::readRosterFile(path, m_headerRow->isChecked());
    if (!result.ok) {
        QMessageBox::warning(this, tr("Class List"), tr("The list could not be read:\n%1").arg(result.error));
        return false;
    }
    m_rosterPath = path;
    m_rosterLabel->setText(tr("%1 — %n person(s)", nullptr, int(result.roster.rows.size()))
                               .arg(QDir::toNativeSeparators(QFileInfo(path).fileName())));
    m_rosterLabel->setToolTip(QDir::toNativeSeparators(path));
    setRoster(result.roster);
    return true;
}

void BatchRenameDialog::setRoster(const review::Roster& roster)
{
    m_roster = roster;
    const review::RosterColumns guess = review::guessColumns(roster.headers);
    m_updating = true;
    fillColumnCombo(m_idColumn, roster.headers, guess.id);
    fillColumnCombo(m_firstColumn, roster.headers, guess.first);
    fillColumnCombo(m_lastColumn, roster.headers, guess.last);
    fillColumnCombo(m_fullColumn, roster.headers, guess.full);
    m_updating = false;
    if (m_rosterPath.isEmpty()) m_rosterLabel->setText(tr("%n person(s)", nullptr, int(roster.rows.size())));
    applyColumns();
}

void BatchRenameDialog::chooseRoster()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose the Class List"), m_rosterPath.isEmpty() ? QString() : QFileInfo(m_rosterPath).absolutePath(),
        tr("Class lists (*.csv *.xlsx *.txt *.tsv);;All Files (*)"));
    if (!path.isEmpty()) loadRoster(path);
}

void BatchRenameDialog::applyColumns()
{
    if (m_updating) return;
    m_columns.id = m_idColumn->currentData().toInt();
    m_columns.first = m_firstColumn->currentData().toInt();
    m_columns.last = m_lastColumn->currentData().toInt();
    m_columns.full = m_fullColumn->currentData().toInt();
    rematch();
}

void BatchRenameDialog::rematch()
{
    QStringList names;
    for (const Clip& clip : m_clips) names << QFileInfo(clip.path).fileName() + QLatin1Char(' ') + clip.displayName;
    m_matches = m_roster.rows.isEmpty() ? QVector<review::RosterMatch>(m_clips.size())
                                        : review::matchClipsToRoster(names, m_roster, m_columns);
    rebuildTable();
}

void BatchRenameDialog::rebuildTable()
{
    m_updating = true;
    for (int i = 0; i < m_clips.size(); ++i) {
        auto* clip = new QTableWidgetItem(m_clips[i].displayName);
        clip->setToolTip(QDir::toNativeSeparators(m_clips[i].path));
        m_table->setItem(i, ClipColumn, clip);
        auto* person = new QComboBox(m_table);
        person->setObjectName(QStringLiteral("BatchRenamePerson%1").arg(i));
        person->addItem(tr("(no match: keep as is)"), -1);
        for (int r = 0; r < m_roster.rows.size(); ++r) {
            const review::PersonName name = review::personName(m_roster.rows[r], m_columns);
            QString label = name.last.isEmpty() ? name.full() : name.last + QStringLiteral(", ") + name.first;
            if (m_columns.id >= 0 && m_columns.id < m_roster.rows[r].size() && !m_roster.rows[r][m_columns.id].isEmpty())
                label += QStringLiteral("  ·  ") + m_roster.rows[r][m_columns.id];
            person->addItem(label, r);
        }
        person->setCurrentIndex(std::max(0, person->findData(m_matches[i].roster)));
        connect(person, &QComboBox::currentIndexChanged, this, [this, i, person] {
            if (m_updating) return;
            setMatch(i, person->currentData().toInt());
        });
        m_table->setCellWidget(i, PersonColumn, person);
    }
    m_updating = false;
    refreshNames();
}

int BatchRenameDialog::matchFor(int clipIndex) const
{
    return clipIndex >= 0 && clipIndex < m_matches.size() ? m_matches[clipIndex].roster : -1;
}

void BatchRenameDialog::setMatch(int clipIndex, int rosterRow)
{
    if (clipIndex < 0 || clipIndex >= m_matches.size()) return;
    // One clip per person: whoever had this person loses them.
    if (rosterRow >= 0)
        for (int i = 0; i < m_matches.size(); ++i)
            if (i != clipIndex && m_matches[i].roster == rosterRow) m_matches[i] = {};
    m_matches[clipIndex] = rosterRow < 0 ? review::RosterMatch{}
                                         : review::RosterMatch{rosterRow, 100, review::MatchConfidence::High};
    m_matches[clipIndex].score = rosterRow < 0 ? 0 : -1; // -1: chosen by hand
    m_updating = true;
    for (int i = 0; i < m_clips.size(); ++i)
        if (auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(i, PersonColumn)))
            combo->setCurrentIndex(std::max(0, combo->findData(m_matches[i].roster)));
    m_updating = false;
    refreshNames();
}

QVector<int> BatchRenameDialog::order() const
{
    QVector<int> indices(m_clips.size());
    std::iota(indices.begin(), indices.end(), 0);
    if (orderByRoster())
        std::stable_sort(indices.begin(), indices.end(), [this](int a, int b) {
            const int ra = m_matches[a].roster, rb = m_matches[b].roster;
            if ((ra < 0) != (rb < 0)) return rb < 0; // matched first
            return ra >= 0 && ra < rb;
        });
    return indices;
}

review::NameContext BatchRenameDialog::contextFor(int clipIndex, int position) const
{
    review::NameContext c;
    c.order = position + 1;
    c.original = baseName(m_clips[clipIndex]);
    c.review = m_reviewName;
    c.date = QDate::currentDate();
    c.headers = m_roster.headers;
    const int row = m_matches[clipIndex].roster;
    if (row >= 0 && row < m_roster.rows.size()) {
        c.row = m_roster.rows[row];
        const review::PersonName name = review::personName(c.row, m_columns);
        c.first = name.first;
        c.last = name.last;
        if (m_columns.id >= 0 && m_columns.id < c.row.size()) c.id = c.row[m_columns.id];
    }
    return c;
}

QString BatchRenameDialog::nameTemplate() const
{
    return m_template->text().trimmed();
}

bool BatchRenameDialog::renameOnDisk() const
{
    return m_onDisk->isChecked();
}

bool BatchRenameDialog::orderByRoster() const
{
    return m_order->isChecked();
}

QVector<BatchRenameDialog::Decision> BatchRenameDialog::decisions() const
{
    QVector<Decision> out;
    const QVector<int> sequence = order();
    for (int position = 0; position < sequence.size(); ++position) {
        const int i = sequence[position];
        Decision d;
        d.id = m_clips[i].id;
        d.rosterRow = m_matches[i].roster;
        if (d.rosterRow >= 0) {
            d.newName = review::renderName(nameTemplate(), contextFor(i, position));
            if (renameOnDisk() && !m_clips[i].sequence) {
                const QFileInfo info(m_clips[i].path);
                d.newPath = info.dir().filePath(d.newName + (info.suffix().isEmpty() ? QString()
                                                                                      : QLatin1Char('.') + info.suffix()));
            }
        }
        out.append(d);
    }
    return out;
}

review::RenamePlan BatchRenameDialog::diskPlan() const
{
    if (!renameOnDisk()) return {};
    QVector<review::RenameItem> items;
    const QVector<Decision> all = decisions();
    for (const Decision& d : all) {
        if (d.newPath.isEmpty()) continue;
        const auto it = std::find_if(m_clips.cbegin(), m_clips.cend(), [&](const Clip& c) { return c.id == d.id; });
        items.append({it->path, d.newPath});
    }
    return review::planRenames(items);
}

void BatchRenameDialog::refreshNames()
{
    const QVector<int> sequence = order();
    const QVector<Decision> all = decisions();
    const review::RenamePlan plan = diskPlan();
    QHash<QString, review::RenameProblem> problems;
    for (const review::PlannedRename& r : plan.items) problems.insert(QDir::cleanPath(r.from), r.problem);

    int named = 0, blocked = 0, sequencesSkipped = 0;
    for (int position = 0; position < sequence.size(); ++position) {
        const int i = sequence[position];
        const Decision& d = all[position];
        // The table stays in the clips' own order; "#" shows the new position.
        auto* orderItem = new QTableWidgetItem(QString::number(position + 1));
        orderItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, OrderColumn, orderItem);
        const int score = m_matches[i].score;
        auto* match = new QTableWidgetItem(score < 0 ? tr("Chosen by you")
                                                     : confidenceText(m_matches[i].confidence, m_matches[i].score));
        if (m_matches[i].confidence == review::MatchConfidence::Low && score >= 0)
            match->setForeground(theme::statusNeedsChanges());
        m_table->setItem(i, MatchColumn, match);

        QString shown = d.newName.isEmpty() ? tr("(unchanged)") : d.newName;
        QString problem;
        if (!d.newName.isEmpty()) ++named;
        if (renameOnDisk() && m_clips[i].sequence && !d.newName.isEmpty()) {
            problem = tr("Image sequences keep their file names; only the shown name changes.");
            ++sequencesSkipped;
        }
        const auto p = problems.constFind(QDir::cleanPath(QFileInfo(m_clips[i].path).absoluteFilePath()));
        if (p != problems.cend() && *p != review::RenameProblem::None && *p != review::RenameProblem::Unchanged) {
            problem = review::renameProblemText(*p);
            ++blocked;
        }
        auto* name = new QTableWidgetItem(problem.isEmpty() ? shown : shown + QStringLiteral("  —  ") + problem);
        if (d.newName.isEmpty()) name->setForeground(theme::textDisabled());
        if (!problem.isEmpty() && p != problems.cend()) name->setForeground(theme::statusNeedsChanges());
        name->setToolTip(problem);
        m_table->setItem(i, NewNameColumn, name);
    }

    // People on the list with no clip: likely missing work.
    QStringList missing;
    QVector<bool> used(m_roster.rows.size(), false);
    for (const review::RosterMatch& m : m_matches)
        if (m.roster >= 0 && m.roster < used.size()) used[m.roster] = true;
    for (int r = 0; r < m_roster.rows.size(); ++r)
        if (!used[r]) missing << review::personName(m_roster.rows[r], m_columns).full();
    m_missing->setText(m_roster.rows.isEmpty() ? QString()
                       : missing.isEmpty()     ? tr("Everyone on the list has a clip.")
                                               : tr("No clip found for: %1").arg(missing.join(QStringLiteral(", "))));

    QString summary;
    if (m_roster.rows.isEmpty()) summary = tr("Choose a class list to match the clips to.");
    else if (named == 0) summary = tr("No clip is matched to anyone yet. Pick people in the Person column.");
    else if (nameTemplate().isEmpty()) summary = tr("Enter a pattern for the new names.");
    else if (blocked > 0) summary = tr("%n file(s) cannot be renamed as shown. Change the pattern or the matches.", nullptr, blocked);
    else if (renameOnDisk()) summary = tr("%n file(s) will be renamed on disk.", nullptr, plan.changeCount());
    else summary = tr("%n clip(s) will be shown under new names. The files stay as they are.", nullptr, named);
    if (sequencesSkipped > 0)
        summary += QLatin1Char(' ') + tr("%n image sequence(s) keep their file names.", nullptr, sequencesSkipped);
    m_summary->setText(summary);
    m_apply->setEnabled(!m_roster.rows.isEmpty() && named > 0 && !nameTemplate().isEmpty() && blocked == 0);
    m_apply->setText(renameOnDisk() ? tr("Rename Files") : tr("Apply Names"));
}

} // namespace atk::ui
