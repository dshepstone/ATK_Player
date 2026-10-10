#include "export/ReviewNotesPdf.h"

#include "export/ExportRenderer.h"
#include "platform/PlatformInfo.h"
#include "timeline/Timecode.h"

#include <QBuffer>
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

namespace {

/// The document laid out once, then painted page by page onto any device
/// whose resolution is kResolution (the PDF itself, or a page image).
class PageLayout {
public:
    PageLayout(const ReviewNotesDocument& doc, QPaintDevice* metricsDevice)
        : m_doc(doc), m_device(metricsDevice)
    {
        m_page = doc.pageSize.size(QPageSize::Point);
        m_contentWidth = m_page.width() - 2 * kMargin;
        for (const ReviewNotesRow& row : doc.rows) m_geometry.append(measureRow(row, m_contentWidth, m_device));
        QVector<qreal> heights;
        for (const RowGeometry& g : m_geometry) heights.append(g.height);
        const qreal bodyHeight = m_page.height() - 2 * kMargin - kFooterHeight;
        m_rowPages = ReviewNotesPdfWriter::paginate(heights, bodyHeight - kHeaderHeight, bodyHeight);
        m_pageCount = m_rowPages.isEmpty() ? 1 : m_rowPages.constLast() + 1;
    }

    int pageCount() const { return m_pageCount; }
    QSizeF pagePoints() const { return m_page; }

    /// Paints page `index` in points (the painter must already map points to
    /// the device).
    void paint(QPainter& p, int index) const
    {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        auto metrics = [&](const QFont& font) { return QFontMetricsF(font, m_device); };
        qreal y = kMargin;
        if (index == 0) {
            p.setPen(QColor(120, 128, 140));
            p.setFont(fontPt(26, false, 300));
            const QString title = QStringLiteral("REVIEW NOTES");
            p.drawText(QPointF(kMargin, y + 26), title);
            const qreal titleWidth = metrics(fontPt(26, false, 300)).horizontalAdvance(title);
            p.setFont(fontPt(10));
            p.setPen(kMuted);
            p.drawText(QPointF(kMargin + titleWidth + 10, y + 26), m_doc.generatedText);
            p.setFont(fontPt(14));
            p.setPen(QColor(120, 128, 140));
            p.drawText(QPointF(kMargin, y + 46), m_doc.projectName);
            const QRectF bar(kMargin, y + 56, m_contentWidth, 20);
            p.fillRect(bar, kBar);
            p.setPen(Qt::white);
            p.setFont(fontPt(9));
            p.drawText(bar.adjusted(6, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter,
                       metrics(fontPt(9)).elidedText(m_doc.clipName, Qt::ElideMiddle, m_contentWidth * 0.6));
            if (!m_doc.reviewers.isEmpty()) {
                p.setPen(QColor(200, 205, 212));
                p.drawText(bar.adjusted(6, 0, -6, 0), Qt::AlignRight | Qt::AlignVCenter,
                           QStringLiteral("Notes by %1").arg(m_doc.reviewers));
            }
            y += kHeaderHeight;
            if (m_doc.rows.isEmpty()) {
                p.setFont(fontPt(11));
                p.setPen(kMuted);
                p.drawText(QPointF(kMargin, y + 20), QStringLiteral("There are no notes or drawings on this clip."));
            }
        }
        for (int i = 0; i < m_doc.rows.size(); ++i) {
            if (m_rowPages[i] != index) continue;
            const ReviewNotesRow& row = m_doc.rows[i];
            const RowGeometry& g = m_geometry[i];
            const QRectF slot(kMargin, y, g.imageWidth, g.imageHeight);
            if (!row.image.isNull()) p.drawImage(slot, row.image);
            else p.fillRect(slot, QColor(230, 232, 235));

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
                const qreal h = wrappedHeight(text, body, g.textWidth, m_device);
                p.setPen(note.resolved ? kMuted : kInk);
                drawWrapped(p, text, body, QRectF(x, ty, g.textWidth, h));
                ty += h + 7.0;
            }
            y += g.height + kRowGap;
        }
        // Footer.
        const qreal fy = m_page.height() - kMargin - kFooterHeight + 8;
        p.setPen(QPen(QColor(200, 204, 210), 0.6));
        p.drawLine(QPointF(kMargin, fy), QPointF(m_page.width() - kMargin, fy));
        p.setFont(fontPt(8));
        p.setPen(kMuted);
        const QRectF band(kMargin, fy + 3, m_contentWidth, 14);
        p.drawText(band, Qt::AlignLeft | Qt::AlignVCenter, m_doc.footerLeft);
        p.drawText(band, Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("Page %1 of %2").arg(index + 1).arg(m_pageCount));
        p.drawText(band, Qt::AlignRight | Qt::AlignVCenter, m_doc.projectName);
    }

private:
    const ReviewNotesDocument& m_doc;
    QPaintDevice* m_device;
    QSizeF m_page;
    qreal m_contentWidth = 0;
    QVector<RowGeometry> m_geometry;
    QVector<int> m_rowPages;
    int m_pageCount = 1;
};

/// A blank page image at the PDF's resolution, for the raster fallback.
QImage pageImage(const QSizeF& points)
{
    QImage image((points * (kResolution / 72.0)).toSize(), QImage::Format_RGB32);
    const int dotsPerMeter = qRound(kResolution / 0.0254);
    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);
    image.fill(Qt::white);
    return image;
}

/// Renders the document into PDF bytes. `rasterPages` draws each page as one
/// picture (no fonts in the file at all), the fallback if the normal output
/// does not verify.
bool renderPdf(const ReviewNotesDocument& doc, bool rasterPages, QByteArray* out, int* pages, QString* error)
{
    QBuffer buffer(out);
    buffer.open(QIODevice::WriteOnly);
    bool ok = false;
    int pageCount = 0;
    {
        QPdfWriter writer(&buffer);
        writer.setPageSize(doc.pageSize);
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        writer.setResolution(kResolution);
        writer.setTitle(QStringLiteral("Review Notes - %1").arg(doc.clipName));
        writer.setCreator(doc.footerLeft);
        QPainter p;
        if (!p.begin(&writer)) {
            if (error) *error = QStringLiteral("The PDF could not be started.");
            return false;
        }
        const qreal scale = writer.resolution() / 72.0;
        if (!rasterPages) {
            const PageLayout layout(doc, &writer);
            pageCount = layout.pageCount();
            for (int page = 0; page < pageCount; ++page) {
                if (page > 0) writer.newPage();
                p.save();
                p.scale(scale, scale);
                layout.paint(p, page);
                p.restore();
            }
        } else {
            QImage probe = pageImage(doc.pageSize.size(QPageSize::Point));
            const PageLayout layout(doc, &probe);
            pageCount = layout.pageCount();
            for (int page = 0; page < pageCount; ++page) {
                if (page > 0) writer.newPage();
                QImage image = pageImage(layout.pagePoints());
                {
                    QPainter ip(&image);
                    ip.scale(scale, scale);
                    layout.paint(ip, page);
                }
                p.drawImage(QRectF(QPointF(0, 0), QSizeF(image.size())), image);
            }
        }
        ok = p.end();
    } // the writer finishes the file when it goes away
    buffer.close();
    if (!ok) {
        if (error) *error = QStringLiteral("The PDF could not be finished.");
        return false;
    }
    if (pages) *pages = pageCount;
    return true;
}

} // namespace

bool ReviewNotesPdfWriter::write(const ReviewNotesDocument& doc, const QString& path, QString* error, int* pages,
                                 bool* usedFallback, Mode mode)
{
    // Build the whole file in memory and check it before anything touches the
    // disk; if the normal (searchable) output does not check out, fall back to
    // page pictures, which need no fonts.
    QByteArray bytes;
    QString why;
    bool fallback = false;
    if (mode == Mode::PagePictures || !renderPdf(doc, false, &bytes, pages, &why) || !verifyBytes(bytes, &why)) {
        if (mode == Mode::Auto)
            qWarning("Review notes PDF: normal output failed (%s, %lld bytes); writing page pictures instead",
                     qPrintable(why), static_cast<long long>(bytes.size()));
        bytes.clear();
        fallback = true;
        if (!renderPdf(doc, true, &bytes, pages, &why) || !verifyBytes(bytes, &why)) {
            if (error) *error = why;
            return false;
        }
    }
    if (usedFallback) *usedFallback = fallback;
    // One atomic write: the destination is either the old file or the new one.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("Could not save %1 (%2).").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

bool ReviewNotesPdfWriter::verify(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("The PDF did not come out right (it could not be read back).");
        return false;
    }
    return verifyBytes(file.readAll(), error);
}

bool ReviewNotesPdfWriter::verifyBytes(const QByteArray& data, QString* error)
{
    auto fail = [&](const QString& why) {
        if (error) *error = QStringLiteral("The PDF did not come out right (%1).").arg(why);
        return false;
    };
    if (!data.startsWith("%PDF-")) return fail(QStringLiteral("no PDF header"));
    if (!data.right(1024).contains("%%EOF")) return fail(QStringLiteral("it is incomplete"));
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
            if (!data.mid(offset, expected.size()).startsWith(expected) || !data.mid(offset, 32).contains(" obj"))
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
    renderSpec.burnIns.tracingPaperPercent = m_options.tracingPaperPercent;
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
    int pageCount = 0;
    QString error;
    QString saved = m_options.outputPath;
    // An earlier PDF still open in a viewer must not be replaced under it
    // (the viewer would be left on a deleted file): save beside it.
    if (platform::PlatformInfo::isFileOpenElsewhere(saved)) {
        const QString sibling = numberedSibling(saved);
        if (!sibling.isEmpty()) saved = sibling;
    }
    if (!ReviewNotesPdfWriter::write(doc, saved, &error, &pageCount)) {
        // The usual cause: the old PDF is open in a viewer, which on Windows
        // stops it being replaced. Save beside it instead of failing.
        const QString sibling = QFileInfo::exists(saved) ? numberedSibling(saved) : QString();
        QString siblingError;
        if (sibling.isEmpty() || !ReviewNotesPdfWriter::write(doc, sibling, &siblingError, &pageCount)) {
            emit failed(error + QStringLiteral(" Nothing was saved."));
            return;
        }
        saved = sibling;
    }
    emit progress(100);
    emit completed(saved, pageCount);
}

} // namespace atk::exporter
