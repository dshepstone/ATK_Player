#pragma once

#include <QDialog>
#include <QHash>

class QCheckBox;
class QKeySequence;
class QKeySequenceEdit;
class QPushButton;
class QTableWidget;
class QSpinBox;
class QLabel;

namespace atk::ui {

class ApplicationSettings;
class CommandRegistry;

/// Edits an isolated draft. Nothing reaches QSettings or live QActions until
/// MainWindow accepts the dialog.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    PreferencesDialog(const ApplicationSettings& settings,
                      const CommandRegistry& commands,
                      QWidget* parent = nullptr);

    bool restoreWindowLayout() const;
    bool audioScrubEnabled() const;
    bool frameStepAudioEnabled() const;
    bool bookmarkSnapEnabled() const;
    bool reopenLastProject() const;
    bool apiEnabled() const;
    int apiPort() const;
    void setApiRuntimeStatus(const QString& status);
    bool resetAllRequested() const { return m_resetAllRequested; }
    const QHash<QString, QString>& shortcuts() const { return m_shortcuts; }

    /// Returns the stable key of another command using `sequence`, or empty.
    static QString conflictingCommand(const QHash<QString, QString>& shortcuts,
                                      const QString& commandKey,
                                      const QKeySequence& sequence);

private:
    void populateShortcutTable();
    void loadSelectedShortcut();
    void assignSelectedShortcut(const QKeySequence& sequence);
    void resetSelectedShortcut();
    void resetAllShortcuts();
    void resetPreferencesDraft();
    void updateShortcutRow(const QString& commandKey);

    QCheckBox* m_restoreLayout = nullptr;
    QCheckBox* m_reopenLast = nullptr;
    QCheckBox* m_audioScrub = nullptr;
    QCheckBox* m_frameStepAudio = nullptr;
    QCheckBox* m_bookmarkSnap = nullptr;
    QCheckBox* m_apiEnabled = nullptr;
    QSpinBox* m_apiPort = nullptr;
    QLabel* m_apiStatus = nullptr;
    QTableWidget* m_shortcutTable = nullptr;
    QKeySequenceEdit* m_shortcutEdit = nullptr;
    QPushButton* m_clearShortcut = nullptr;
    QPushButton* m_resetSelected = nullptr;
    QHash<QString, QString> m_shortcuts;
    bool m_loadingShortcut = false;
    bool m_resetAllRequested = false;
};

} // namespace atk::ui
