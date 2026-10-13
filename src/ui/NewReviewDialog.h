#pragma once

#include "review/FolderScanner.h"

#include <QDialog>
#include <QPointer>
#include <QThread>

#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace atk::ui {

/// "New Review": choose a folder (Windows, macOS or Linux alike), pick which
/// of its clips to include and in what order, and name the review.
///
/// The folder is scanned on a worker thread so a large or network folder
/// never freezes the window; a newer scan supersedes an older one, and
/// closing the dialog cancels and joins the scan before anything is freed.
class NewReviewDialog final : public QDialog {
    Q_OBJECT

public:
    explicit NewReviewDialog(const QString& startFolder = {}, QWidget* parent = nullptr);
    ~NewReviewDialog() override;

    /// Scans `folder` (also what Browse does). For tests and callers that
    /// already know the folder.
    void setFolder(const QString& folder);

    QString folder() const;
    bool includeSubfolders() const;
    QString reviewName() const;
    /// <folder>/<name>.atkproj
    QString projectPath() const;
    /// The ticked clips, in the order shown.
    QVector<review::FoundClip> chosenClips() const;
    /// True while a scan is running (tests wait on it).
    bool isScanning() const { return m_scanThread != nullptr; }

private:
    enum class Order { Name, Modified };
    void browse();
    void startScan();
    void scanFinished(quint64 token, const QVector<review::FoundClip>& clips);
    void cancelScan();
    void applyOrder();
    void refreshState();

    QLineEdit* m_folder = nullptr;
    QCheckBox* m_subfolders = nullptr;
    QListWidget* m_clips = nullptr;
    QComboBox* m_order = nullptr;
    QLineEdit* m_name = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_create = nullptr;
    QVector<review::FoundClip> m_found;
    QPointer<QThread> m_scanThread;
    std::shared_ptr<std::atomic_bool> m_scanCancel;
    quint64 m_scanToken = 0;
    bool m_nameEdited = false;
    /// What the current list was scanned from, so leaving the folder field
    /// (or closing the dialog) does not rescan and drop the user's choices.
    QString m_scannedFolder;
    bool m_scannedSubfolders = false;
};

} // namespace atk::ui
