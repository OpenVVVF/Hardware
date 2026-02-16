#pragma once
#include "Command/CommandContext.h"
#include "Switching/CommutationManager.h"

namespace RtBridge {
    // Launches core1 RT loop and returns a core0-safe context.
    // Assumption: zone_mgr is configured before calling and then treated as read-only.
    CommandContext initAndGetContext(CommutationManager* zone_mgr);
}
