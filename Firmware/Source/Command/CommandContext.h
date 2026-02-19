#ifndef COMMAND_CONTEXT_H
#define COMMAND_CONTEXT_H

#include <cstdint>

// Forward declarations
class CommutationManager;


// ======================
// Command context (core0-safe)
// ======================
struct CommandContext {
    // Read-only shared config (must not be modified after core1 starts)
    CommutationManager* zone_mgr;
};

#endif // COMMAND_CONTEXT_H
