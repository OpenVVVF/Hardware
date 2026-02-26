#ifndef COMMAND_CONTEXT_H
#define COMMAND_CONTEXT_H

#include <cstdint>



// ======================
// Command context (core0-safe)
// ======================
struct CommandContext {
    float encoderOffset = 0.341701f;//2.383f;//2.04f;//2.3830f;
    // Read-only shared config (must not be modified after core1 starts)
};

#endif // COMMAND_CONTEXT_H
