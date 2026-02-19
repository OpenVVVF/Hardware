#ifndef COMMAND_CONTEXT_H
#define COMMAND_CONTEXT_H

#include <cstdint>



// ======================
// Command context (core0-safe)
// ======================
struct CommandContext {
    // Read-only shared config (must not be modified after core1 starts)
};

#endif // COMMAND_CONTEXT_H
