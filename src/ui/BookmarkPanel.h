#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace atk::timeline { class TimelineModel; }

namespace atk::ui {

class BookmarkPanel : public QWidget {
    Q_OBJECT
public:
    explicit BookmarkPanel(QWidget* parent = nullptr);
    void setModel(timeline::TimelineModel* model);
    quint64 selectedBookmarkId() const { return m_selectedId; }
    void selectBookmark(quint64 id);
    void useCurrentReviewRange();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void bookmarkActivated(quint64 id);
    void bookmarkSelected(quint64 id);
    void addPointRequested();
    void addRangeRequested(qint64 startFrame, qint64 endFrame);

private:
    void rebuildList();
    void loadSelection();
    void commitMetadata();
    void commitFrames();
    void deleteSelection();
    void addDraftRange();
    void updateCreationBounds();

    timeline::TimelineModel* m_model = nullptr;
    quint64 m_selectedId = 0;
    bool m_refreshing = false;
    QListWidget* m_list = nullptr;
    QLabel* m_empty = nullptr;
    QSpinBox* m_createStart = nullptr;
    QSpinBox* m_createEnd = nullptr;
    QLabel* m_createError = nullptr;
    QPushButton* m_addPoint = nullptr;
    QPushButton* m_useReviewRange = nullptr;
    QPushButton* m_addRange = nullptr;
    QLineEdit* m_name = nullptr;
    QPlainTextEdit* m_note = nullptr;
    QComboBox* m_color = nullptr;
    QLabel* m_startLabel = nullptr;
    QSpinBox* m_start = nullptr;
    QLabel* m_endLabel = nullptr;
    QSpinBox* m_end = nullptr;
    QPushButton* m_delete = nullptr;
};

} // namespace atk::ui
