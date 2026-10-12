#pragma once

#include "export/ReviewNotesPdf.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace atk::ui {

/// Options for "Export Notes PDF": where to save, paper size, whether the note
/// text is included at all (off gives an images-only PDF) and, if so, whether
/// resolved notes are. Shows how many frames will be listed.
class ReviewNotesDialog final : public QDialog {
    Q_OBJECT

public:
    ReviewNotesDialog(const annotation::SourceAnnotations& annotations, const QString& suggestedPath,
                      int tracingPaperPercent = 0, QWidget* parent = nullptr);

    exporter::ReviewNotesOptions options() const;
    QString destination() const;
    void setOpenWhenDone(bool open);
    bool openWhenDone() const;
    /// Off: an images-only PDF, no note text.
    void setIncludeNoteText(bool include);
    bool includeNoteText() const;

private:
    void browse();
    void refreshSummary();

    annotation::SourceAnnotations m_annotations;
    QLineEdit* m_destination = nullptr;
    QComboBox* m_paper = nullptr;
    QCheckBox* m_includeNoteText = nullptr;
    QCheckBox* m_includeResolved = nullptr;
    QComboBox* m_tracingPaper = nullptr;
    QCheckBox* m_openWhenDone = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_export = nullptr;
};

} // namespace atk::ui
