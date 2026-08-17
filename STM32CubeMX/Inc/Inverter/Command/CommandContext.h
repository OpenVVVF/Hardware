/*
 * Command execution context.
 * Ported from PicoFirmware/Source/Command/CommandContext.h
 */
#ifndef COMMAND_CONTEXT_H
#define COMMAND_CONTEXT_H

struct CommandContext {
    /* Currently no shared state is required; commands access the global
     * inverter objects directly. */
};

#endif // COMMAND_CONTEXT_H
