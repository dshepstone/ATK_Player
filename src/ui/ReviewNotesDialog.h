#pragma once

#include "export/ReviewNotesPdf.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace atk::ui {

/// Options for "Export Review Notes (PDF)": where to save, paper size, and
/// whether resolved notes are included. Shows how many frames will be listed.
class ReviewNotesDialog final : public QDialog {
    Q_OBJECT

public:
    ReviewNotesDialog(const annotation::SourceAnnotations& annotations, const QString& suggestedPath,
                      QWidget* parent = nullptr);

    exporter::ReviewNotesOptions options() const;
    QString destination() const;

private:
    void browse();
    void refreshSummary();

    annotation::SourceAnnotations m_annotations;
    QLineEdit* m_destination = nullptr;
    QComboBox* m_paper = nullptr;
    QCheckBox* m_includeResolved = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_export = nullptr;
};

} // namespace atk::ui
