// CommandManager.cpp
#include "Inverter/Command/CommandManager.h"

#include "Inverter/Telemetry.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <strings.h>
#include <cstdlib>

CommandManager& CommandManager::instance() {
    static CommandManager inst;
    return inst;
}

void CommandManager::registerCommand(CommandInterface* cmd) {
    if (count_ < MAX_CMDS && cmd != nullptr) {
        commands_[count_++] = cmd;
    }
}

void CommandManager::setContext(CommandContext& ctx) {
    context_ = &ctx;
}

CommandInterface* CommandManager::findCommand(const char* name) {
    for (size_t i = 0; i < count_; i++) {
        if (strcasecmp(name, commands_[i]->getCommandName()) == 0) {
            return commands_[i];
        }
    }
    return nullptr;
}

static const char* nextToken(const char* str, char* token, size_t tokenSize) {
    while (*str && isspace((unsigned char)*str)) str++;

    if (*str == '\0') {
        token[0] = '\0';
        return str;
    }

    const char* end = str;
    while (*end && !isspace((unsigned char)*end)) end++;

    size_t len = static_cast<size_t>(end - str);
    if (len >= tokenSize) len = tokenSize - 1;
    memcpy(token, str, len);
    token[len] = '\0';

    return end;
}

void CommandManager::processLine(const char* line) {
    if (!line || !context_) return;

    while (*line && isspace((unsigned char)*line)) line++;
    if (*line == '\0') return;

    char cmdName[64];
    const char* rest = nextToken(line, cmdName, sizeof(cmdName));

    CommandInterface* cmd = findCommand(cmdName);
    if (!cmd) {
        Telemetry::printf("[SHELL] Unknown command '%s'. Type HELP for list.", cmdName);
        return;
    }

    int argc = cmd->getArgCount();
    ArgValue values[8] = {};

    for (int i = 0; i < argc; i++) {
        ArgSpec spec = cmd->getArgSpec(i);
        char token[32];
        rest = nextToken(rest, token, sizeof(token));

        if (token[0] == '\0') {
            if (spec.required) {
                Telemetry::printf("[SHELL] Error: Missing required argument <%s>", spec.name);
                return;
            } else {
                values[i] = {spec.default_val, (int32_t)spec.default_val, false};
                continue;
            }
        }

        float val;
        if (spec.type == ArgSpec::FLOAT) {
            val = static_cast<float>(atof(token));
        } else {
            val = static_cast<float>(atoi(token));
        }

        if (val < spec.min || val > spec.max) {
            char rangeStr[32];
            spec.printRange(rangeStr, sizeof(rangeStr));
            Telemetry::printf("[SHELL] Error: %s out of range (%s)", spec.name, rangeStr);
            return;
        }

        values[i] = {val, (int32_t)val, true};
    }

    char extra[32];
    rest = nextToken(rest, extra, sizeof(extra));
    if (extra[0] != '\0') {
        Telemetry::printf("[SHELL] Error: Too many arguments. Expected %d, got more.", argc);
        return;
    }

    cmd->execute(values, *context_);
}

void CommandManager::printHelp() const {
    Telemetry::printf("[SHELL] === Command Reference ===");

    for (size_t i = 0; i < count_; i++) {
        CommandInterface* cmd = commands_[i];

        int argc = cmd->getArgCount();
        char sigBuffer[64] = "";
        char* p = sigBuffer;
        size_t remaining = sizeof(sigBuffer);

        for (int j = 0; j < argc; j++) {
            ArgSpec spec = cmd->getArgSpec(j);
            int n;
            if (spec.required) {
                n = snprintf(p, remaining, " <%s>", spec.name);
            } else {
                n = snprintf(p, remaining, " [%s]", spec.name);
            }
            if (n < 0) break;
            if ((size_t)n >= remaining) { remaining = 0; break; }
            p += n;
            remaining -= (size_t)n;
        }

        Telemetry::printf("[SHELL] %-8s%-20s - %s",
                          cmd->getCommandName(), sigBuffer, cmd->getShortDescription());

        if (argc > 0) {
            for (int j = 0; j < argc; j++) {
                ArgSpec spec = cmd->getArgSpec(j);
                char range[32];
                spec.printRange(range, sizeof(range));
                Telemetry::printf("[SHELL]         %s:%s", spec.name, range);
            }
        }
    }
    Telemetry::printf("[SHELL] =========================");
}
