#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QString>

class QFrame;
class QHBoxLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

namespace atk::ui {

/// The shared look of the export windows (video, notes PDF, current frame,
/// image sequence): a header with a format badge, title and summary, the
/// settings in rounded cards, and a footer with the primary action. Modelled
/// on SyncSketch's export windows.
namespace exportui {

/// Style sheet for an export window; set on the dialog itself.
QString styleSheet();

/// Header: format badge ("MP4", "PDF"...), title and a one-line summary.
/// Returns the summary label so callers can update it.
QLabel* addHeader(QVBoxLayout* root, QWidget* parent, const QString& badge, const QString& title,
                  const QString& summary);

/// A titled card; add rows to the returned layout.
QVBoxLayout* addCard(QVBoxLayout* body, QWidget* parent, const QString& title);

/// A "Label   [field]" row inside a card.
QHBoxLayout* addRow(QVBoxLayout* card, QWidget* parent, const QString& label, QWidget* field, int stretch = 1);

/// Footer with an optional left-hand widget, Cancel and the primary button.
/// Returns the primary button; Cancel rejects the dialog, primary accepts.
QPushButton* addFooter(QVBoxLayout* root, QDialog* dialog, const QString& primaryText, QWidget* left = nullptr);

/// Body layout with the standard padding, for the cards.
QVBoxLayout* addBody(QVBoxLayout* root);

} // namespace exportui

/// Progress window for a running export: a large bar, what is happening,
/// percent, time left, and Cancel. Non-modal, so the app stays usable.
class ExportProgressDialog final : public QDialog {
    Q_OBJECT
public:
    ExportProgressDialog(const QString& badge, const QString& title, const QString& fileName,
                         QWidget* parent = nullptr);
    void setValue(int percent);
    void setLabelText(const QString& text);
    int value() const;
    /// Closes the window once the export has ended (done, failed or cancelled).
    void finish();

signals:
    void canceled();

protected:
    void reject() override;

private:
    QProgressBar* m_bar = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_percent = nullptr;
    QLabel* m_remaining = nullptr;
    QPushButton* m_cancel = nullptr;
    QElapsedTimer m_clock;
    bool m_cancelled = false;
    bool m_finishing = false;
};

} // namespace atk::ui
