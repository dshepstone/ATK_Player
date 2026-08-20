#pragma once

#include "media/MediaSource.h"

#include <QObject>

#include <cstdint>
#include <memory>

namespace atk::playback { class PlaybackController; }

namespace atk::compare {

/// How the two viewers are arranged.
enum class CompareLayout {
    Horizontal, ///< A left, B right
    Vertical,   ///< A top, B bottom
    /// Both sources drawn in the same rectangle, split by a draggable divider.
    /// A later addition; listed here so the enum does not need renumbering.
    Wipe,
};

/// A/B comparison of two media sources.
///
/// PHASE 0 STATUS: architecture placeholder. The class holds the intended
/// state and its accessors; no second decode path and no second viewer exist
/// yet. Implemented in milestone M4.
///
/// THE SYNCHRONISATION RULE
/// -----------------------
/// There is exactly ONE PlaybackClock, owned by the single PlaybackController.
/// Neither viewer runs a clock of its own.
///
/// On every tick the controller computes the master frame N. Viewer A requests
/// frame `N + offsetA` from source A, viewer B requests `N + offsetB` from
/// source B. Both requests resolve against their own decoder and frame cache,
/// which may take different amounts of time -- but because both are derived
/// from the same N, they cannot accumulate drift. A slow decode shows a stale
/// frame for one tick; it never shifts the sync point.
///
/// The alternative -- giving each viewer its own clock and periodically
/// resyncing them -- was rejected: it makes drift the normal state and
/// correctness a matter of how often you correct it.
///
/// The B offset exists because two takes of the same shot rarely start on the
/// same frame: different handles, a re-time, or a retake that begins mid-action.
/// Offsetting B lets the reviewer align the moment that matters and then scrub
/// both takes together.
///
/// Sources with different frame rates are resolved by converting through the
/// master clock's time base, not by assuming a shared frame numbering.
class CompareSession : public QObject {
    Q_OBJECT

public:
    explicit CompareSession(QObject* parent = nullptr);
    ~CompareSession() override;

    void setSourceA(std::shared_ptr<media::MediaSource> source);
    void setSourceB(std::shared_ptr<media::MediaSource> source);
    const std::shared_ptr<media::MediaSource>& sourceA() const { return m_sourceA; }
    const std::shared_ptr<media::MediaSource>& sourceB() const { return m_sourceB; }

    CompareLayout layout() const { return m_layout; }
    void setLayout(CompareLayout layout);

    /// Frames added to the master frame number when reading from A / B.
    int64_t offsetA() const { return m_offsetA; }
    int64_t offsetB() const { return m_offsetB; }
    void setOffsetA(int64_t frames);
    void setOffsetB(int64_t frames);

    /// True once both slots hold a source.
    bool isReady() const { return m_sourceA != nullptr && m_sourceB != nullptr; }

    /// Whether comparison mode is currently displayed.
    bool isActive() const { return m_active; }
    void setActive(bool active);

signals:
    void sourcesChanged();
    void layoutChanged(atk::compare::CompareLayout layout);
    void offsetsChanged();
    void activeChanged(bool active);

private:
    std::shared_ptr<media::MediaSource> m_sourceA;
    std::shared_ptr<media::MediaSource> m_sourceB;
    CompareLayout m_layout = CompareLayout::Horizontal;
    int64_t m_offsetA = 0;
    int64_t m_offsetB = 0;
    bool m_active = false;
};

} // namespace atk::compare
