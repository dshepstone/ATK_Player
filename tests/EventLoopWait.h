#pragma once

#include <QDeadlineTimer>
#include <QEventLoop>
#include <QObject>
#include <QTimer>

#include <functional>

// ---------------------------------------------------------------------------
// Waiting for real-time playback in tests.
//
// QTest::qWait and the QTRY_* macros process pending events, then sleep the
// thread for up to 10 ms, and repeat. Timers due during that sleep fire late.
// On macOS, and markedly on virtual machines such as CI runners, those sleeps
// overrun, so playback ticks there arrive tens of milliseconds late and
// frames are skipped that the application, sitting in a real event loop,
// presents on time. Tests that measure playback timing wait with these
// instead.
// ---------------------------------------------------------------------------

namespace atk::test {

/// Runs an event loop, as the application does, for `ms` milliseconds.
inline void runEventLoopFor(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, Qt::PreciseTimer, &loop, &QEventLoop::quit);
    loop.exec();
}

/// Runs an event loop until `done` returns true or `timeoutMs` passes.
/// Returns the final value of `done`.
inline bool runEventLoopUntil(const std::function<bool()>& done, int timeoutMs)
{
    if (done()) return true;
    const QDeadlineTimer deadline(timeoutMs);
    QEventLoop loop;
    QTimer poll;
    poll.setTimerType(Qt::PreciseTimer);
    poll.setInterval(5);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (done() || deadline.hasExpired()) loop.quit();
    });
    poll.start();
    loop.exec();
    return done();
}

} // namespace atk::test
