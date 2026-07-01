/*
 * Singleton command registry and dispatcher.
 * Ported from PicoFirmware/Source/Command/CommandManager.h
 */
#ifndef COMMAND_MANAGER_H
#define COMMAND_MANAGER_H

#include "CommandInterface.h"
#include "CommandContext.h"
#include <array>
#include <cstddef>

class CommandManager {
public:
    static CommandManager& instance();

    void registerCommand(CommandInterface* cmd);
    void setContext(CommandContext& ctx);
    void processLine(const char* line);

    void printHelp() const;

private:
    CommandManager() = default;

    CommandInterface* findCommand(const char* name);

    bool nameEquals(const char* a, const char* b);

    static constexpr size_t MAX_CMDS = 64;
    std::array<CommandInterface*, MAX_CMDS> commands_;
    size_t count_ = 0;
    CommandContext* context_ = nullptr;
};

#endif
