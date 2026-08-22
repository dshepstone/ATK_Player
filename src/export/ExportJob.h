#pragma once

#include "export/ExportSpec.h"
#include <QObject>
#include <atomic>
#include <memory>

class QThread;

namespace atk::exporter {
class ExportWorker;

class ExportJob final : public QObject {
    Q_OBJECT
public:
    explicit ExportJob(ExportSpec spec, QObject* parent = nullptr);
    ~ExportJob() override;
    const ExportSpec& spec() const { return m_spec; }
    bool isRunning() const { return m_running; }
    void start();
    void cancel();
    void wait();
signals:
    void started();
    void progress(int percent, qint64 frame, qint64 total);
    void statusChanged(const QString& status);
    void completed(const QString& outputPath);
    void cancelled();
    void failed(const QString& message);
private:
    ExportSpec m_spec;
    QThread* m_thread = nullptr;
    ExportWorker* m_worker = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    bool m_running = false;
};

} // namespace atk::exporter
