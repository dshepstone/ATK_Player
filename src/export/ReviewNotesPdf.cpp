#include "export/ReviewNotesPdf.h"

#include "export/ExportRenderer.h"
#include "timeline/Timecode.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QPainter>
#include <QPdfWriter>
#include <QPointer>
#include <QSaveFile>
#include <QTextLayout>
#include <QThread>

#include <algorithm>
#include <cmath>

namespace atk::exporter {
namespace {

// Layout in PDF points (1/72 inch); the writer maps them to its resolution.
constexpr qreal kMargin = 34.0;
constexpr qreal kRowGap = 12.0;
constexpr qreal kHeaderHeight = 92.0;
constexpr qreal kFooterHeight = 26.0;
constexpr int kResolution = 144; // PDF device resolution (dpi)
const QColor kInk(40, 44, 52);
const QColor kMuted(130, 140, 150);
const QColor kAccent(32, 160, 140); // like SyncSketch's teal labels
const QColor kBar(56, 60, 68);

/// A font whose size is `points` in the page's point coordinates. Fonts are
/// resolved against the PDF device (kResolution dpi) and the painter is scaled
/// from points to device pixels, so the nominal size compensates for both.
QFont fontPt(qreal points, bool bold = false, int weight = -1)
{
    // Common outline fonts first, so text embeds as a plain TrueType subset.
    QFont font;
    font.setFamilies({QStringLiteral("Arial"), QStringLiteral("Helvetica"), QStringLiteral("Liberation Sans"),
                      QStringLiteral("DejaVu Sans")});
    font.setStyleHint(QFont::SansSerif, QFont::PreferOutline);
    font.setPointSizeF(points * 72.0 / kResolution);
    font.setBold(bold);
    if (weight > 0) font.setWeight(QFont::Weight(weight));
    return font;
}

/// Height of `text` wrapped to `width` in `font` (points).
qreal wrappedHeight(const QString& text, const QFont& font, qreal width, QPaintDevice* device)
{
    QTextLayout layout(text, font, device);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal height = 0;
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(width);
        height += line.height();
    }
    layout.endLayout();
    return height;
}

QString noteText(const annotation::FrameNote& note)
{
    const QString author = note.author.trimmed();
    return author.isEmpty() ? note.text.trimmed() : author + QStringLiteral(": ") + note.text.trimmed();
}

struct RowGeometry {
    qreal imageWidth = 0;
    qreal imageHeight = 0;
    qreal textWidth = 0;
    qreal height = 0;
};

RowGeometry measureRow(const ReviewNotesRow& row, qreal contentWidth, QPaintDevice* device)
{
    RowGeometry g;
    g.imageWidth = contentWidth * 0.45;
    const qreal aspect = row.image.isNull() ? 16.0 / 9.0
                                            : qreal(row.image.width()) / std::max(1, row.image.height());
    g.imageHeight = g.imageWidth / std::max(0.1, aspect);
    g.textWidth = contentWidth - g.imageWidth - 14.0;
    qreal text = 22.0; // the FRAME / TIMECODE line
    const QFont body = fontPt(9.5);
    if (row.notes.isEmpty()) text += 14.0;
    for (const annotation::FrameNote& note : row.notes) text += wrappedHeight(noteText(note), body, g.textWidth, device) + 7.0;
    g.height = std::max(g.imageHeight, text);
    return g;
}

void drawWrapped(QPainter& p, const QString& text, const QFont& font, const QRectF& box)
{
    p.setFont(font);
    QTextLayout layout(text, font, p.device());
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal y = 0;
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(box.width());
        line.setPosition(QPointF(0, y));
        y += line.height();
    }
    layout.endLayout();
    layout.draw(&p, box.topLeft());
}

} // namespace

QVector<int> ReviewNotesPdfWriter::paginate(const QVector<qreal>& rowHeights, qreal firstPageSpace, qreal pageSpace)
{
    QVector<int> pages;
    pages.reserve(rowHeights.size());
    int page = 0;
    qreal remaining = firstPageSpace;
    bool pageEmpty = true;
    for (qreal height : rowHeights) {
        // Rows never split: move to a new page unless this one is still empty.
        if (height > remaining && !pageEmpty) {
            ++page;
            remaining = pageSpace;
            pageEmpty = true;
        }
        pages.append(page);
        remaining -= height + kRowGap;
        pageEmpty = false;
    }
    return pages;
}

bool ReviewNotesPdfWriter::write(const ReviewNotesDocument& doc, const QString& path, QString* error, int* pages)
{
    QPdfWriter writer(path);
    writer.setPageSize(doc.pageSize);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setResolution(kResolution);
    writer.setTitle(QStringLiteral("Review Notes - %1").arg(doc.clipName));
    writer.setCreator(doc.footerLeft);
    QPainter p;
    if (!p.begin(&writer)) {
        if (error) *error = QStringLiteral("Could not write %1.").arg(QDir::toNativeSeparators(path));
        return false;
    }
    // Work in points regardless of the writer's resolution.
    const qreal scale = writer.resolution() / 72.0;
    p.scale(scale, scale);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QSizeF page = doc.pageSize.size(QPageSize::Point);
    const qreal contentWidth = page.width() - 2 * kMargin;
    const qreal bodyTop = kMargin;
    const qreal bodyBottom = page.height() - kMargin - kFooterHeight;

    QVector<RowGeometry> geometry;
    QVector<qreal> heights;
    for (const ReviewNotesRow& row : doc.rows) {
        geometry.append(measureRow(row, contentWidth, &writer));
        heights.append(geometry.last().height);
    }
    const qreal firstSpace = bodyBottom - bodyTop - kHeaderHeight;
    const qreal pageSpace = bodyBottom - bodyTop;
    const QVector<int> rowPages = paginate(heights, firstSpace, pageSpace);
    const int pageCount = rowPages.isEmpty() ? 1 : rowPages.constLast() + 1;
    auto metrics = [&](const QFont& font) { return QFontMetricsF(font, &writer); };

    auto drawFooter = [&](int index) {
        const qreal y = page.height() - kMargin - kFooterHeight + 8;
        p.setPen(QPen(QColor(200, 204, 210), 0.6));
        p.drawLine(QPointF(kMargin, y), QPointF(page.width() - kMargin, y));
        p.setFont(fontPt(8));
        p.setPen(kMuted);
        const QRectF band(kMargin, y + 3, contentWidth, 14);
        p.drawText(band, Qt::AlignLeft | Qt::AlignVCenter, doc.footerLeft);
        p.drawText(band, Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("Page %1 of %2").arg(index + 1).arg(pageCount));
        p.drawText(band, Qt::AlignRight | Qt::AlignVCenter, doc.projectName);
    };

    // Header (first page).
    qreal y = bodyTop;
    p.setPen(QColor(120, 128, 140));
    p.setFont(fontPt(26, false, 300));
    const QString title = QStringLiteral("REVIEW NOTES");
    p.drawText(QPointF(kMargin, y + 26), title);
    const qreal titleWidth = metrics(fontPt(26, false, 300)).horizontalAdvance(title);
    p.setFont(fontPt(10));
    p.setPen(kMuted);
    p.drawText(QPointF(kMargin + titleWidth + 10, y + 26), doc.generatedText);
    p.setFont(fontPt(14));
    p.setPen(QColor(120, 128, 140));
    p.drawText(QPointF(kMargin, y + 46), doc.projectName);
    const QRectF bar(kMargin, y + 56, contentWidth, 20);
    p.fillRect(bar, kBar);
    p.setPen(Qt::white);
    p.setFont(fontPt(9));
    p.drawText(bar.adjusted(6, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter,
               metrics(fontPt(9)).elidedText(doc.clipName, Qt::ElideMiddle, contentWidth * 0.6));
    if (!doc.reviewers.isEmpty()) {
        p.setPen(QColor(200, 205, 212));
        p.drawText(bar.adjusted(6, 0, -6, 0), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("Notes by %1").arg(doc.reviewers));
    }
    y = bodyTop + kHeaderHeight;
    if (doc.rows.isEmpty()) {
        p.setFont(fontPt(11));
        p.setPen(kMuted);
        p.drawText(QPointF(kMargin, y + 20), QStringLiteral("There are no notes or drawings on this clip."));
    }

    int currentPage = 0;
    for (int i = 0; i < doc.rows.size(); ++i) {
        if (rowPages[i] != currentPage) {
            drawFooter(currentPage);
            writer.newPage();
            currentPage = rowPages[i];
            y = bodyTop;
        }
        const ReviewNotesRow& row = doc.rows[i];
        const RowGeometry& g = geometry[i];
        // Picture (fit inside its slot, keeping aspect).
        const QRectF slot(kMargin, y, g.imageWidth, g.imageHeight);
        if (!row.image.isNull()) p.drawImage(slot, row.image);
        else p.fillRect(slot, QColor(230, 232, 235));

        // FRAME n   TIMECODE hh:mm:ss:ff                OPEN / RESOLVED
        const qreal x = kMargin + g.imageWidth + 14.0;
        qreal ty = y + 2;
        const QFont label = fontPt(6.5);
        const QFont value = fontPt(12);
        qreal cx = x;
        auto pair = [&](const QString& l, const QString& v) {
            p.setFont(label);
            p.setPen(kAccent);
            p.drawText(QPointF(cx, ty + 12), l);
            cx += metrics(label).horizontalAdvance(l) + 4;
            p.setFont(value);
            p.setPen(kInk);
            p.drawText(QPointF(cx, ty + 12), v);
            cx += metrics(value).horizontalAdvance(v) + 16;
        };
        pair(QStringLiteral("FRAME"), QString::number(row.frame + 1));
        pair(QStringLiteral("TIMECODE"), row.timecode);
        const bool allResolved = !row.notes.isEmpty()
            && std::all_of(row.notes.cbegin(), row.notes.cend(), [](const auto& n) { return n.resolved; });
        p.setFont(label);
        p.setPen(allResolved ? kMuted : kAccent);
        p.drawText(QRectF(x, ty + 3, g.textWidth, 12), Qt::AlignRight | Qt::AlignVCenter,
                   allResolved ? QStringLiteral("RESOLVED") : QStringLiteral("OPEN"));
        ty += 22;
        const QFont body = fontPt(9.5);
        if (row.notes.isEmpty()) {
            p.setFont(body);
            p.setPen(kMuted);
            p.drawText(QPointF(x, ty + 10), QStringLiteral("(drawing only)"));
        }
        for (const annotation::FrameNote& note : row.notes) {
            // Resolved notes are greyed (and the row says RESOLVED); no symbol
            // glyphs that would pull in a fallback font.
            const QString text = noteText(note);
            const qreal h = wrappedHeight(text, body, g.textWidth, &writer);
            p.setPen(note.resolved ? kMuted : kInk);
            drawWrapped(p, text, body, QRectF(x, ty, g.textWidth, h));
            ty += h + 7.0;
        }
        y += g.height + kRowGap;
    }
    drawFooter(currentPage);
    if (!p.end()) {
        if (error) *error = QStringLiteral("Could not finish writing %1.").arg(QDir::toNativeSeparators(path));
        return false;
    }
    if (pages) *pages = pageCount;
    return true;
}

bool ReviewNotesPdfWriter::verify(const QString& path, QString* error)
{
    auto fail = [&](const QString& why) {
        if (error) *error = QStringLiteral("The PDF did not come out right (%1). Nothing was saved; please try again.").arg(why);
        return false;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("it could not be read back"));
    const QByteArray data = file.readAll();
    if (!data.startsWith("%PDF-")) return fail(QStringLiteral("no PDF header"));
    if (!data.right(64).contains("%%EOF")) return fail(QStringLiteral("it is incomplete"));
    // The cross-reference table must be where the trailer says, and every
    // object it lists must start at its recorded offset.
    const qsizetype start = data.lastIndexOf("startxref");
    if (start < 0) return fail(QStringLiteral("no cross-reference"));
    bool ok = false;
    const qsizetype xref = data.mid(start + 9, 24).trimmed().split('\n').value(0).trimmed().toLongLong(&ok);
    if (!ok || xref < 0 || xref >= data.size() || !data.mid(xref, 4).startsWith("xref"))
        return fail(QStringLiteral("the cross-reference is misplaced"));
    qsizetype at = data.indexOf('\n', xref) + 1;
    int checked = 0;
    while (at > 0 && at < data.size() && !data.mid(at, 7).startsWith("trailer")) {
        const qsizetype lineEnd = data.indexOf('\n', at);
        if (lineEnd < 0) return fail(QStringLiteral("the cross-reference is cut short"));
        const QList<QByteArray> head = data.mid(at, lineEnd - at).trimmed().split(' ');
        if (head.size() != 2) return fail(QStringLiteral("the cross-reference is malformed"));
        const qint64 first = head[0].toLongLong(&ok);
        if (!ok) return fail(QStringLiteral("the cross-reference is malformed"));
        const qint64 count = head[1].toLongLong(&ok);
        if (!ok || count < 0 || count > 10000000) return fail(QStringLiteral("the cross-reference is malformed"));
        at = lineEnd + 1;
        for (qint64 i = 0; i < count; ++i, at += 20) {
            const QByteArray entry = data.mid(at, 20);
            if (entry.size() < 18) return fail(QStringLiteral("the cross-reference is cut short"));
            if (entry.at(17) != 'n') continue; // free entry
            const qint64 offset = entry.left(10).toLongLong(&ok);
            if (!ok || offset <= 0 || offset >= data.size()) return fail(QStringLiteral("an object is missing"));
            const QByteArray expected = QByteArray::number(first + i) + ' ';
            if (!data.mid(offset, expected.size()) .startsWith(expected) || !data.mid(offset, 32).contains(" obj"))
                return fail(QStringLiteral("an object is out of place"));
            ++checked;
        }
    }
    if (checked == 0) return fail(QStringLiteral("it has no content"));
    return true;
}

// --- Job ---------------------------------------------------------------------

ReviewNotesJob::ReviewNotesJob(ExportSpec spec, ReviewNotesDocument header, ReviewNotesOptions options,
                               QObject* parent)
    : QObject(parent), m_spec(std::move(spec)), m_header(std::move(header)), m_options(std::move(options))
{
}

ReviewNotesJob::~ReviewNotesJob()
{
    cancel();
    wait();
}

QVector<qint64> ReviewNotesJob::framesToInclude(const annotation::SourceAnnotations& annotations,
                                                bool includeResolved)
{
    QVector<qint64> frames;
    for (const annotation::FrameAnnotation& frame : annotations.frames()) {
        const bool openNote = std::any_of(frame.notes.cbegin(), frame.notes.cend(),
                                          [&](const annotation::FrameNote& n) { return includeResolved || !n.resolved; });
        if (frame.hasDrawings() || openNote) frames.append(frame.frame);
    }
    return frames;
}

void ReviewNotesJob::start()
{
    if (m_thread) return;
    m_cancel = false;
    m_thread = QThread::create([this] { run(); });
    m_thread->start();
}

void ReviewNotesJob::cancel() { m_cancel = true; }

void ReviewNotesJob::wait()
{
    if (!m_thread) return;
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
}

bool ReviewNotesJob::isRunning() const { return m_thread && m_thread->isRunning(); }

void ReviewNotesJob::run()
{
    // Results go back to the owner's thread through queued signals.
    auto report = [this](auto&& fn) { QMetaObject::invokeMethod(this, std::forward<decltype(fn)>(fn), Qt::QueuedConnection); };
    const QVector<qint64> frames = framesToInclude(m_spec.annotations, m_options.includeResolved);

    // Render each frame with its drawings burned in, using a decoder owned by
    // this thread.
    ExportSpec renderSpec = m_spec;
    renderSpec.kind = ExportKind::CurrentFrame;
    renderSpec.comparison = false;
    renderSpec.burnIns = {};
    renderSpec.burnIns.annotations = true;
    renderSpec.firstFrame = -1;
    renderSpec.lastFrame = -1;
    ExportRenderer renderer;
    QString error;
    const bool needDecoder = !frames.isEmpty();
    if (needDecoder && !renderer.open(renderSpec, &error)) {
        report([this, error] { emit failed(error); });
        return;
    }
    ReviewNotesDocument doc = m_header;
    doc.pageSize = m_options.pageSize;
    for (int i = 0; i < frames.size(); ++i) {
        if (m_cancel) {
            report([this] { emit cancelled(); });
            return;
        }
        RenderedExportFrame out;
        if (!renderer.render(frames[i], out, &error, [this] { return m_cancel.load(); })) {
            if (m_cancel) {
                report([this] { emit cancelled(); });
                return;
            }
            report([this, error] { emit failed(error); });
            return;
        }
        ReviewNotesRow row;
        row.frame = frames[i];
        row.timecode = timeline::timecode::fromFrame(frames[i], m_spec.sourceA.metadata.frameRate);
        const int width = std::clamp(m_options.imageWidth, 160, 3840);
        row.image = out.image.width() > width
            ? out.image.scaledToWidth(width, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32)
            : out.image.convertToFormat(QImage::Format_RGB32);
        for (const annotation::FrameNote& note : m_spec.annotations.frame(frames[i]).notes)
            if (m_options.includeResolved || !note.resolved) row.notes.append(note);
        doc.rows.append(std::move(row));
        const int percent = int(90.0 * (i + 1) / std::max<qsizetype>(1, frames.size()));
        report([this, percent] { emit progress(percent); });
    }
    if (m_cancel) {
        report([this] { emit cancelled(); });
        return;
    }

    // Hand the finished rows to the owner's thread to write: QPdfWriter lays
    // out and embeds fonts, which is only dependable on the GUI thread on
    // every platform. Writing takes milliseconds; the slow part (decoding and
    // rendering) stayed here.
    report([this, doc = std::move(doc)] { writeDocument(doc); });
}

void ReviewNotesJob::writeDocument(const ReviewNotesDocument& doc)
{
    if (m_cancel) {
        emit cancelled();
        return;
    }
    // Write beside the destination, check it, then replace it, so a failure
    // never leaves a broken PDF where the old one was.
    const QString temporary = m_options.outputPath + QStringLiteral(".partial");
    QFile::remove(temporary);
    int pageCount = 0;
    QString error;
    if (!ReviewNotesPdfWriter::write(doc, temporary, &error, &pageCount)
        || !ReviewNotesPdfWriter::verify(temporary, &error)) {
        QFile::remove(temporary);
        emit failed(error);
        return;
    }
    if (QFile::exists(m_options.outputPath) && !QFile::remove(m_options.outputPath)) {
        QFile::remove(temporary);
        emit failed(QStringLiteral("The existing PDF could not be replaced (is it open in another app?)."));
        return;
    }
    if (!QFile::rename(temporary, m_options.outputPath)) {
        QFile::remove(temporary);
        emit failed(QStringLiteral("The PDF could not be saved."));
        return;
    }
    emit progress(100);
    emit completed(m_options.outputPath, pageCount);
}

} // namespace atk::exporter
