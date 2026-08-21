#include "ui/Resources.h"

#include <QResource>

void initializeAtkResources()
{
    Q_INIT_RESOURCE(atk_resources);
}

namespace atk::ui {

void ensureResourcesInitialized()
{
    static const bool initialized = [] { initializeAtkResources(); return true; }();
    Q_UNUSED(initialized);
}

} // namespace atk::ui
