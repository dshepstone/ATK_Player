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

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void bookmarkActivated(quint64 id);

private:
    void rebuildList();
    void loadSelection();
    void commitMetadata();
    void commitFrames();
    void deleteSelection();

    timeline::TimelineModel* m_model = nullptr;
    quint64 m_selectedId = 0;
    bool m_refreshing = false;
    QListWidget* m_list = nullptr;
    QLabel* m_empty = nullptr;
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
