#pragma once
#include "Command/CommandContext.h"
#include "Switching/CommutationManager.h"
#include "Switching/PWMDriver.h" // for FocMeasurement

namespace RtBridge {
    // Launches core1 RT loop and returns a core0-safe context.
    // Assumption: zone_mgr is configured before calling and then treated as read-only.
    CommandContext initAndGetContext(CommutationManager* zone_mgr);

    // Core0 -> Core1: publish latest fast measurements used by FOC.
    // This is lock-free (seqlock) and safe to call as often as you like.
    void publishFocMeasurement(const FocMeasurement& m);
}
