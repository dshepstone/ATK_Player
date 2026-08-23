#pragma once

#include "media/MediaMetadata.h"

#include <QDialog>
#include <QHash>

class QLabel;
class QGridLayout;
class QVBoxLayout;

namespace atk::ui {

class MediaInformationDialog final : public QDialog {
    Q_OBJECT

public:
    explicit MediaInformationDialog(QWidget* parent = nullptr);

    void setMediaInformation(const media::MediaMetadata& metadata,
                             qint64 authoritativeFrameCount);
    void clearMediaInformation();
    QString copyText() const;

private:
    QLabel* addSection(const QString& title);
    QLabel* addField(const QString& key, const QString& label);
    void setValue(const QString& key, const QString& value);
    void copyInformation() const;

    QVBoxLayout* m_layout = nullptr;
    QGridLayout* m_currentGrid = nullptr;
    QLabel* m_mediaName = nullptr;
    QHash<QString, QLabel*> m_values;
};

} // namespace atk::ui
