#pragma once

#include "annotation/AnnotationTypes.h"
#include "export/ExportSpec.h"

#include <QImage>
#include <QObject>
#include <QPageSize>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

class QThread;

namespace atk::exporter {

/// One frame of a Review Notes PDF: the picture with its drawings, and its notes.
struct ReviewNotesRow {
    qint64 frame = 0; // zero-based source frame
    QString timecode;
    QImage image;     // already annotated; any size, drawn aspect-correct
    QVector<annotation::FrameNote> notes;
};

/// Everything the PDF shows, decided up front (immutable during writing).
struct ReviewNotesDocument {
    QString projectName;
    QString clipName;
    QString reviewers;      // shown in the title bar, e.g. "David, Selena"
    QString generatedText;  // e.g. "from Oct 10th, 2026, 09:12"
    QString footerLeft;     // e.g. "ATK Player 0.3.0"
    QPageSize pageSize = QPageSize(QPageSize::A4);
    QVector<ReviewNotesRow> rows;
};

/// Lays out and writes a SyncSketch-style review notes PDF: a header block,
/// then one row per frame (picture on the left; frame, timecode, status and
/// notes on the right), never splitting a row across pages, with page numbers
/// in the footer. Text and images are vector/embedded, so the PDF is
/// searchable and prints sharply.
class ReviewNotesPdfWriter {
public:
    /// Builds the PDF in memory, checks it (verifyBytes), and saves it in one
    /// atomic write. If the normal, searchable output fails the check, the
    /// pages are written as pictures instead (no fonts involved);
    /// `usedFallback` reports that. `pages` receives the page count.
    enum class Mode { Auto, PagePictures };
    static bool write(const ReviewNotesDocument& document, const QString& path, QString* error,
                      int* pages = nullptr, bool* usedFallback = nullptr, Mode mode = Mode::Auto);

    /// Structural check of a PDF: header, end marker, and every object where
    /// the cross-reference table says it is. Catches truncated or scrambled
    /// output before it replaces a good file.
    static bool verifyBytes(const QByteArray& data, QString* error);
    static bool verify(const QString& path, QString* error);

    /// Which page each row lands on, given row heights and the space available
    /// on the first page (after the header) and on following pages. A row taller
    /// than a page gets a page of its own. Pure, for tests.
    static QVector<int> paginate(const QVector<qreal>& rowHeights, qreal firstPageSpace, qreal pageSpace);
};

/// Options chosen in the export dialog.
struct ReviewNotesOptions {
    QString outputPath;
    QPageSize pageSize = QPageSize(QPageSize::A4);
    bool includeResolved = true;
    int imageWidth = 1280; // pixels per frame picture in the PDF
    int tracingPaperPercent = 0; // white wash under the drawings, as in the viewer
};

/// Builds rows from a source's annotations and writes the PDF: decoding each
/// annotated frame (with its own decoder) and burning in the drawings happens
/// on a worker thread; the finished rows come back to the owner's thread,
/// which writes the PDF (fonts are only dependable there), verifies it, and
/// replaces the destination atomically. Shutdown order: cancel(), then wait()
/// or destroy; a cancel before the rows arrive writes nothing.
class ReviewNotesJob final : public QObject {
    Q_OBJECT

public:
    /// `spec` provides the source and its annotations snapshot (as for video
    /// export); `document` provides the header text.
    ReviewNotesJob(ExportSpec spec, ReviewNotesDocument header, ReviewNotesOptions options,
                   QObject* parent = nullptr);
    ~ReviewNotesJob() override;

    /// Frames that will become rows, in order (frames with drawings or with
    /// notes that pass the resolved filter).
    static QVector<qint64> framesToInclude(const annotation::SourceAnnotations& annotations, bool includeResolved);

    void start();
    void cancel();
    void wait();
    bool isRunning() const;
    const ReviewNotesOptions& options() const { return m_options; }

signals:
    void progress(int percent);
    void completed(const QString& outputPath, int pages);
    void cancelled();
    void failed(const QString& message);

private:
    void run();
    void writeDocument(const ReviewNotesDocument& doc);

    ExportSpec m_spec;
    ReviewNotesDocument m_header;
    ReviewNotesOptions m_options;
    std::atomic<bool> m_cancel{false};
    QThread* m_thread = nullptr;
};

} // namespace atk::exporter
