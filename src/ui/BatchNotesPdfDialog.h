#pragma once

#include "export/ReviewNotesPdf.h"

#include <QDialog>
#include <QUuid>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace atk::ui {

/// One playlist clip offered for a batch notes export.
struct BatchNotesClip {
    QUuid id;
    QString name;
    annotation::SourceAnnotations annotations;
    /// Empty when the clip can be exported; otherwise why not ("missing").
    QString unavailableReason;
};

/// Options for "Export Notes PDFs for Playlist": which clips, the folder the
/// PDFs go to (one per clip), paper size and the same content choices as
/// Export Notes PDF. Clips with nothing to list cannot be chosen.
class BatchNotesPdfDialog final : public QDialog {
    Q_OBJECT

public:
    BatchNotesPdfDialog(QVector<BatchNotesClip> clips, const QString& folder, int tracingPaperPercent = 0,
                        QWidget* parent = nullptr);

    /// The chosen clips, in playlist order.
    QVector<QUuid> chosenClips() const;
    /// Absolute folder, or empty.
    QString folder() const;
    /// The content and paper choices; `outputPath` is left empty (one per clip).
    exporter::ReviewNotesOptions options() const;
    void setOpenFolderWhenDone(bool open);
    bool openFolderWhenDone() const;
    void setIncludeNoteText(bool include);
    bool includeNoteText() const;

private:
    void browse();
    void refresh();
    int framesFor(const BatchNotesClip& clip) const;

    QVector<BatchNotesClip> m_clips;
    QListWidget* m_list = nullptr;
    QLineEdit* m_folder = nullptr;
    QComboBox* m_paper = nullptr;
    QCheckBox* m_includeNoteText = nullptr;
    QCheckBox* m_includeResolved = nullptr;
    QComboBox* m_tracingPaper = nullptr;
    QCheckBox* m_openWhenDone = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_export = nullptr;
    bool m_refreshing = false;
};

} // namespace atk::ui
