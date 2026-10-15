#include "ui/ApplicationSettings.h"

#include "core/Version.h"
#include "core/commands/CommandDefinitions.h"

#include <QKeySequence>
#include <QSettings>
#include <QFileInfo>
#include <QDir>

#include <algorithm>

namespace atk::ui {
namespace {
constexpr auto kRestoreLayout = "ui/restoreWindowLayout";
constexpr auto kGeometry = "ui/mainWindowGeometry";
constexpr auto kWindowState = "ui/mainWindowState";
constexpr auto kAudioScrub = "review/audioScrub";
constexpr auto kFrameStepAudio = "review/frameStepAudio";
constexpr auto kBookmarkSnap = "review/bookmarkSnap";
constexpr auto kVolume = "review/volume";
constexpr auto kMuted = "review/muted";
constexpr auto kReopenLast = "projects/reopenLast";
constexpr auto kShowLibraryAtStartup = "startup/showReviewLibrary";
constexpr auto kRecentProjects = "projects/recent";
constexpr auto kLastProject = "projects/lastPath";
constexpr auto kLastRoster = "review/lastRosterPath";
constexpr auto kRenameTemplate = "review/renameTemplate";
constexpr auto kStopAfterEachClip = "playback/stopAfterEachClip";
constexpr auto kLoopWholePlaylist = "playback/loopWholePlaylist";
constexpr auto kApiEnabled = "api/enabled";
constexpr auto kApiPort = "api/port";
constexpr auto kStillHoldFrames = "media/stillImageHoldFrames";
constexpr auto kSequenceFrameRate = "media/imageSequenceFrameRate";
constexpr auto kShortcutGroup = "shortcuts";
constexpr auto kWelcomeShownVersion = "ui/welcomeShownVersion";
constexpr auto kDrawTool = "draw/tool";
constexpr auto kDrawColor = "draw/color";
constexpr auto kDrawSize = "draw/sizePixels";
constexpr auto kDrawOpacity = "draw/opacity";
constexpr auto kDrawPressure = "draw/usePressure";
constexpr auto kDrawCurve = "draw/pressureCurve";
constexpr auto kDrawStabilizer = "draw/stabilizer";
constexpr auto kDrawEraserSize = "draw/eraserSizePixels";
constexpr auto kDrawEraserMode = "draw/eraserMode";
constexpr auto kDrawBrushTip = "draw/brushTip";
constexpr auto kReviewerName = "review/reviewerName";
constexpr auto kTracingPaper = "draw/tracingPaper";
constexpr auto kTracingPaperLevel = "draw/tracingPaperLevel";
constexpr auto kOpenExportsWhenDone = "export/openWhenDone";
constexpr auto kReviewNotesIncludeText = "export/notesPdfIncludeText";
constexpr auto kGhostEnabled = "draw/ghostEnabled";
constexpr auto kGhostBefore = "draw/ghostBefore";
constexpr auto kGhostAfter = "draw/ghostAfter";
constexpr auto kGhostMode = "draw/ghostMode";
constexpr auto kGhostTint = "draw/ghostTint";
constexpr auto kHoldDrawings = "draw/holdDrawings";
constexpr auto kHoldDrawingsOnMode = "draw/holdDrawingsOnMode";
constexpr auto kDragMarkers = "timeline/dragMarkers";

QString curveName(annotation::PressureCurve curve)
{
    switch (curve) {
    case annotation::PressureCurve::Soft: return QStringLiteral("soft");
    case annotation::PressureCurve::Firm: return QStringLiteral("firm");
    case annotation::PressureCurve::Linear: break;
    }
    return QStringLiteral("linear");
}
}

ApplicationSettings::ApplicationSettings()
    : m_settings(std::make_unique<QSettings>(
          QString::fromLatin1(version::kOrganizationName),
          QString::fromLatin1(version::kApplicationName)))
{
}

ApplicationSettings::ApplicationSettings(const QString& iniFilePath)
    : m_settings(std::make_unique<QSettings>(iniFilePath, QSettings::IniFormat))
{
}

ApplicationSettings::~ApplicationSettings() = default;

bool ApplicationSettings::readBool(const char* key, bool fallback) const
{
    const QVariant raw = m_settings->value(QString::fromLatin1(key));
    if (!raw.isValid()) return fallback;
    const QString text = raw.toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1")) return true;
    if (text == QStringLiteral("false") || text == QStringLiteral("0")) return false;
    return fallback;
}

bool ApplicationSettings::restoreWindowLayout() const { return readBool(kRestoreLayout, defaultRestoreWindowLayout()); }
bool ApplicationSettings::audioScrubEnabled() const { return readBool(kAudioScrub, defaultAudioScrubEnabled()); }
bool ApplicationSettings::frameStepAudioEnabled() const { return readBool(kFrameStepAudio, defaultFrameStepAudioEnabled()); }
bool ApplicationSettings::bookmarkSnapEnabled() const { return readBool(kBookmarkSnap, defaultBookmarkSnapEnabled()); }
bool ApplicationSettings::muted() const { return readBool(kMuted, defaultMuted()); }
bool ApplicationSettings::reopenLastProject() const { return readBool(kReopenLast, defaultReopenLastProject()); }
bool ApplicationSettings::showLibraryAtStartup() const
{
    return readBool(kShowLibraryAtStartup, defaultShowLibraryAtStartup());
}
QStringList ApplicationSettings::recentProjects() const { return m_settings->value(QString::fromLatin1(kRecentProjects)).toStringList(); }
QString ApplicationSettings::lastProjectPath() const { return m_settings->value(QString::fromLatin1(kLastProject)).toString(); }
QString ApplicationSettings::lastRosterPath() const { return m_settings->value(QString::fromLatin1(kLastRoster)).toString(); }
QString ApplicationSettings::renameTemplate() const
{
    return m_settings->value(QString::fromLatin1(kRenameTemplate), QStringLiteral("{last}_{first}")).toString();
}
bool ApplicationSettings::apiEnabled() const { return readBool(kApiEnabled, defaultApiEnabled()); }
int ApplicationSettings::apiPort() const
{
    bool ok = false;
    const int value = m_settings->value(QString::fromLatin1(kApiPort), defaultApiPort()).toInt(&ok);
    return ok && value >= 1024 && value <= 65535 ? value : defaultApiPort();
}

int ApplicationSettings::stillImageHoldFrames() const
{
    bool ok = false;
    const int value = m_settings->value(QString::fromLatin1(kStillHoldFrames),
                                        defaultStillImageHoldFrames()).toInt(&ok);
    return ok && value >= minimumStillImageHoldFrames() && value <= maximumStillImageHoldFrames()
        ? value : defaultStillImageHoldFrames();
}

media::FrameRate ApplicationSettings::imageSequenceFrameRate() const
{
    // Stored as an exact rational ("24000/1001"), never a rounded decimal.
    const QStringList parts = m_settings->value(QString::fromLatin1(kSequenceFrameRate))
                                  .toString().split(QLatin1Char('/'));
    if (parts.size() == 2) {
        bool numeratorOk = false, denominatorOk = false;
        const media::FrameRate rate{ parts[0].trimmed().toInt(&numeratorOk),
                                     parts[1].trimmed().toInt(&denominatorOk) };
        if (numeratorOk && denominatorOk && rate.isValid() && rate.toDouble() <= 1000.0) return rate;
    }
    return media::ImageSourceOptions::kDefaultFrameRate;
}

QString ApplicationSettings::welcomeShownVersion() const { return m_settings->value(QString::fromLatin1(kWelcomeShownVersion)).toString(); }
void ApplicationSettings::setWelcomeShownVersion(const QString& version) { m_settings->setValue(QString::fromLatin1(kWelcomeShownVersion), version); }

double ApplicationSettings::volume() const
{
    bool ok = false;
    const double value = m_settings->value(QString::fromLatin1(kVolume), defaultVolume()).toDouble(&ok);
    return ok && value >= 0.0 && value <= 1.0 ? value : defaultVolume();
}

void ApplicationSettings::setRestoreWindowLayout(bool value) { m_settings->setValue(QString::fromLatin1(kRestoreLayout), value); }
void ApplicationSettings::setAudioScrubEnabled(bool value) { m_settings->setValue(QString::fromLatin1(kAudioScrub), value); }
void ApplicationSettings::setFrameStepAudioEnabled(bool value) { m_settings->setValue(QString::fromLatin1(kFrameStepAudio), value); }
void ApplicationSettings::setBookmarkSnapEnabled(bool value) { m_settings->setValue(QString::fromLatin1(kBookmarkSnap), value); }
void ApplicationSettings::setVolume(double value) { m_settings->setValue(QString::fromLatin1(kVolume), std::clamp(value, 0.0, 1.0)); }
void ApplicationSettings::setMuted(bool value) { m_settings->setValue(QString::fromLatin1(kMuted), value); }
void ApplicationSettings::setReopenLastProject(bool value) { m_settings->setValue(QString::fromLatin1(kReopenLast), value); }
void ApplicationSettings::setShowLibraryAtStartup(bool value)
{
    m_settings->setValue(QString::fromLatin1(kShowLibraryAtStartup), value);
}
void ApplicationSettings::addRecentProject(const QString& path)
{
    const QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QStringList recent = recentProjects();
    for (auto it = recent.begin(); it != recent.end();) {
        if (QString::compare(*it, normalized, Qt::CaseInsensitive) == 0) it = recent.erase(it);
        else ++it;
    }
    recent.prepend(normalized);
    while (recent.size() > maximumRecentProjects()) recent.removeLast();
    m_settings->setValue(QString::fromLatin1(kRecentProjects), recent);
    setLastProjectPath(normalized);
}
void ApplicationSettings::clearRecentProjects() { m_settings->remove(QString::fromLatin1(kRecentProjects)); }
void ApplicationSettings::setLastProjectPath(const QString& path) { m_settings->setValue(QString::fromLatin1(kLastProject), path); }
bool ApplicationSettings::stopAfterEachClip() const { return readBool(kStopAfterEachClip, false); }
bool ApplicationSettings::loopWholePlaylist() const { return readBool(kLoopWholePlaylist, false); }
void ApplicationSettings::setStopAfterEachClip(bool value) { m_settings->setValue(QString::fromLatin1(kStopAfterEachClip), value); }
void ApplicationSettings::setLoopWholePlaylist(bool value) { m_settings->setValue(QString::fromLatin1(kLoopWholePlaylist), value); }

void ApplicationSettings::setLastRosterPath(const QString& path) { m_settings->setValue(QString::fromLatin1(kLastRoster), path); }
void ApplicationSettings::setRenameTemplate(const QString& nameTemplate)
{
    m_settings->setValue(QString::fromLatin1(kRenameTemplate), nameTemplate);
}
void ApplicationSettings::setApiEnabled(bool value) { m_settings->setValue(QString::fromLatin1(kApiEnabled), value); }
void ApplicationSettings::setApiPort(int value)
{
    m_settings->setValue(QString::fromLatin1(kApiPort), std::clamp(value, 1024, 65535));
}
void ApplicationSettings::setImageSequenceFrameRate(const media::FrameRate& rate)
{
    if (!rate.isValid()) return;
    m_settings->setValue(QString::fromLatin1(kSequenceFrameRate),
                         QStringLiteral("%1/%2").arg(rate.numerator).arg(rate.denominator));
}
void ApplicationSettings::setStillImageHoldFrames(int value)
{
    m_settings->setValue(QString::fromLatin1(kStillHoldFrames),
                         std::clamp(value, minimumStillImageHoldFrames(), maximumStillImageHoldFrames()));
}

QByteArray ApplicationSettings::windowGeometry() const { return m_settings->value(QString::fromLatin1(kGeometry)).toByteArray(); }
QByteArray ApplicationSettings::windowState() const { return m_settings->value(QString::fromLatin1(kWindowState)).toByteArray(); }
void ApplicationSettings::setWindowGeometry(const QByteArray& value) { m_settings->setValue(QString::fromLatin1(kGeometry), value); }
void ApplicationSettings::setWindowState(const QByteArray& value) { m_settings->setValue(QString::fromLatin1(kWindowState), value); }

QHash<QString, QString> ApplicationSettings::shortcutOverrides() const
{
    QHash<QString, QString> result;
    m_settings->beginGroup(QString::fromLatin1(kShortcutGroup));
    for (const QString& key : m_settings->childKeys()) {
        if (commands::find(key) == nullptr) continue;
        const QString stored = m_settings->value(key).toString();
        if (!stored.isEmpty() && QKeySequence::fromString(stored, QKeySequence::PortableText).isEmpty()) continue;
        result.insert(key, stored);
    }
    m_settings->endGroup();
    return result;
}

bool ApplicationSettings::setShortcutOverride(const QString& commandKey, const QString& portableShortcut)
{
    if (commands::find(commandKey) == nullptr) return false;
    if (!portableShortcut.isEmpty()
        && QKeySequence::fromString(portableShortcut, QKeySequence::PortableText).isEmpty()) return false;
    m_settings->setValue(QStringLiteral("%1/%2").arg(QString::fromLatin1(kShortcutGroup), commandKey), portableShortcut);
    return true;
}

void ApplicationSettings::resetShortcutOverride(const QString& commandKey)
{
    m_settings->remove(QStringLiteral("%1/%2").arg(QString::fromLatin1(kShortcutGroup), commandKey));
}

void ApplicationSettings::resetAllShortcuts() { m_settings->remove(QString::fromLatin1(kShortcutGroup)); }
void ApplicationSettings::resetAll()
{
    // Resetting preferences is not a reinstall; keep the welcome from reappearing.
    const QVariant welcome = m_settings->value(QString::fromLatin1(kWelcomeShownVersion));
    m_settings->clear();
    if (welcome.isValid()) m_settings->setValue(QString::fromLatin1(kWelcomeShownVersion), welcome);
}
void ApplicationSettings::sync() { m_settings->sync(); }

annotation::AnnotationToolSettings ApplicationSettings::annotationTool() const
{
    annotation::AnnotationToolSettings result;
    annotation::AnnotationTool tool;
    if (annotation::toolFromName(m_settings->value(QString::fromLatin1(kDrawTool)).toString(), &tool))
        result.tool = tool;
    const QColor color(m_settings->value(QString::fromLatin1(kDrawColor)).toString());
    if (color.isValid()) result.color = color;
    bool ok = false;
    const double size = m_settings->value(QString::fromLatin1(kDrawSize)).toDouble(&ok);
    if (ok && size >= annotation::AnnotationToolSettings::kMinSizePixels
        && size <= annotation::AnnotationToolSettings::kMaxSizePixels)
        result.sizePixels = size;
    const double opacity = m_settings->value(QString::fromLatin1(kDrawOpacity)).toDouble(&ok);
    if (ok && opacity >= 0.05 && opacity <= 1.0) result.opacity = opacity;
    result.usePressure = readBool(kDrawPressure, result.usePressure);
    const QString curve = m_settings->value(QString::fromLatin1(kDrawCurve)).toString();
    if (curve == QStringLiteral("soft")) result.pressureCurve = annotation::PressureCurve::Soft;
    else if (curve == QStringLiteral("firm")) result.pressureCurve = annotation::PressureCurve::Firm;
    const double stabilizer = m_settings->value(QString::fromLatin1(kDrawStabilizer)).toDouble(&ok);
    if (ok && stabilizer >= 0.0 && stabilizer <= 0.95) result.stabilizer = stabilizer;
    const double eraserSize = m_settings->value(QString::fromLatin1(kDrawEraserSize)).toDouble(&ok);
    if (ok && eraserSize >= annotation::AnnotationToolSettings::kMinSizePixels
        && eraserSize <= annotation::AnnotationToolSettings::kMaxSizePixels)
        result.eraserSizePixels = eraserSize;
    if (m_settings->value(QString::fromLatin1(kDrawEraserMode)).toString() == QStringLiteral("stroke"))
        result.eraserMode = annotation::EraserMode::WholeStroke;
    result.brushTipId = m_settings->value(QString::fromLatin1(kDrawBrushTip)).toString().left(200);
    return result;
}

QString ApplicationSettings::reviewerName() const
{
    const QVariant stored = m_settings->value(QString::fromLatin1(kReviewerName));
    if (stored.isValid()) return stored.toString().left(200);
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty()) user = qEnvironmentVariable("USER");
    return user.left(200);
}

int ApplicationSettings::tracingPaper() const
{
    bool ok = false;
    const int value = m_settings->value(QString::fromLatin1(kTracingPaper)).toInt(&ok);
    return ok && value >= 0 && value <= 100 ? value : 0;
}

int ApplicationSettings::tracingPaperLevel() const
{
    bool ok = false;
    const int value = m_settings->value(QString::fromLatin1(kTracingPaperLevel)).toInt(&ok);
    return ok && value > 0 && value <= 100 ? value : 50;
}

annotation::GhostSettings ApplicationSettings::ghostSettings() const
{
    annotation::GhostSettings result;
    result.enabled = readBool(kGhostEnabled, result.enabled);
    bool ok = false;
    const int before = m_settings->value(QString::fromLatin1(kGhostBefore)).toInt(&ok);
    if (ok && before >= 0 && before <= annotation::GhostSettings::kMaxRange) result.before = before;
    const int after = m_settings->value(QString::fromLatin1(kGhostAfter)).toInt(&ok);
    if (ok && after >= 0 && after <= annotation::GhostSettings::kMaxRange) result.after = after;
    if (m_settings->value(QString::fromLatin1(kGhostMode)).toString() == QStringLiteral("drawings"))
        result.mode = annotation::GhostSettings::Mode::Drawings;
    result.tint = readBool(kGhostTint, result.tint);
    return result;
}

annotation::HoldMode ApplicationSettings::holdDrawings() const
{
    return annotation::holdModeFromKey(m_settings->value(QString::fromLatin1(kHoldDrawings)).toString());
}

annotation::HoldMode ApplicationSettings::holdDrawingsOnMode() const
{
    const annotation::HoldMode mode = annotation::holdModeFromKey(
        m_settings->value(QString::fromLatin1(kHoldDrawingsOnMode)).toString(), annotation::HoldMode::AddToCurrent);
    return mode == annotation::HoldMode::Off ? annotation::HoldMode::AddToCurrent : mode;
}

void ApplicationSettings::setHoldDrawings(annotation::HoldMode mode)
{
    m_settings->setValue(QString::fromLatin1(kHoldDrawings), annotation::holdModeKey(mode));
    if (mode != annotation::HoldMode::Off)
        m_settings->setValue(QString::fromLatin1(kHoldDrawingsOnMode), annotation::holdModeKey(mode));
}

bool ApplicationSettings::dragMarkersEnabled() const { return readBool(kDragMarkers, false); }

void ApplicationSettings::setDragMarkersEnabled(bool enabled)
{
    m_settings->setValue(QString::fromLatin1(kDragMarkers), enabled);
}

bool ApplicationSettings::openExportsWhenDone() const { return readBool(kOpenExportsWhenDone, true); }

void ApplicationSettings::setOpenExportsWhenDone(bool open)
{
    m_settings->setValue(QString::fromLatin1(kOpenExportsWhenDone), open);
}

bool ApplicationSettings::reviewNotesIncludeText() const { return readBool(kReviewNotesIncludeText, true); }

void ApplicationSettings::setReviewNotesIncludeText(bool include)
{
    m_settings->setValue(QString::fromLatin1(kReviewNotesIncludeText), include);
}

void ApplicationSettings::setTracingPaper(int percent)
{
    m_settings->setValue(QString::fromLatin1(kTracingPaper), std::clamp(percent, 0, 100));
}

void ApplicationSettings::setTracingPaperLevel(int percent)
{
    if (percent > 0) m_settings->setValue(QString::fromLatin1(kTracingPaperLevel), std::clamp(percent, 1, 100));
}

void ApplicationSettings::setGhostSettings(const annotation::GhostSettings& settings)
{
    m_settings->setValue(QString::fromLatin1(kGhostEnabled), settings.enabled);
    m_settings->setValue(QString::fromLatin1(kGhostBefore), settings.before);
    m_settings->setValue(QString::fromLatin1(kGhostAfter), settings.after);
    m_settings->setValue(QString::fromLatin1(kGhostMode),
                         settings.mode == annotation::GhostSettings::Mode::Drawings ? QStringLiteral("drawings")
                                                                                    : QStringLiteral("frames"));
    m_settings->setValue(QString::fromLatin1(kGhostTint), settings.tint);
}

void ApplicationSettings::setReviewerName(const QString& name)
{
    m_settings->setValue(QString::fromLatin1(kReviewerName), name.left(200));
}

void ApplicationSettings::setAnnotationTool(const annotation::AnnotationToolSettings& settings)
{
    m_settings->setValue(QString::fromLatin1(kDrawTool), annotation::toolName(settings.tool));
    m_settings->setValue(QString::fromLatin1(kDrawColor), settings.color.name(QColor::HexArgb));
    m_settings->setValue(QString::fromLatin1(kDrawSize), settings.sizePixels);
    m_settings->setValue(QString::fromLatin1(kDrawOpacity), settings.opacity);
    m_settings->setValue(QString::fromLatin1(kDrawPressure), settings.usePressure);
    m_settings->setValue(QString::fromLatin1(kDrawCurve), curveName(settings.pressureCurve));
    m_settings->setValue(QString::fromLatin1(kDrawStabilizer), settings.stabilizer);
    m_settings->setValue(QString::fromLatin1(kDrawEraserSize), settings.eraserSizePixels);
    m_settings->setValue(QString::fromLatin1(kDrawEraserMode),
                         settings.eraserMode == annotation::EraserMode::WholeStroke ? QStringLiteral("stroke")
                                                                                    : QStringLiteral("area"));
    m_settings->setValue(QString::fromLatin1(kDrawBrushTip), settings.brushTipId);
}

} // namespace atk::ui
