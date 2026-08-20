#pragma once

#include <QLoggingCategory>

// ---------------------------------------------------------------------------
// Central logging categories.
//
// All ATK code logs through these categories rather than raw std::cout or a
// bare qDebug(). That keeps output filterable at runtime via QT_LOGGING_RULES
// and gives us a single place to add file/rotating sinks later.
//
//     qCInfo(atk::log::playback) << "seek to frame" << frame;
//
// Categories are named "atk.<subsystem>" so a rule such as
//     QT_LOGGING_RULES="atk.*.debug=true"
// enables debug output across the whole application.
// ---------------------------------------------------------------------------

namespace atk::log {

Q_DECLARE_LOGGING_CATEGORY(app)
Q_DECLARE_LOGGING_CATEGORY(ui)
Q_DECLARE_LOGGING_CATEGORY(media)
Q_DECLARE_LOGGING_CATEGORY(playback)
Q_DECLARE_LOGGING_CATEGORY(timeline)
Q_DECLARE_LOGGING_CATEGORY(project)
Q_DECLARE_LOGGING_CATEGORY(api)
Q_DECLARE_LOGGING_CATEGORY(platform)

/// Installs the ATK message pattern and writes the startup banner: application
/// name, version, Qt version, operating system and build configuration.
///
/// Call once, immediately after constructing QApplication.
void initialize();

} // namespace atk::log
