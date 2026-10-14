// Start the standalone app at login (RS-03 section 1). Per-user, no admin rights needed.
#pragma once
#include <juce_core/juce_core.h>

namespace pacemaker {
bool setAutoStart(bool enable);
bool isAutoStartEnabled();
}
