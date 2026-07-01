#include "Inverter/Command/CommandInterface.h"
#include "Inverter/Command/CommandManager.h"
#include "Inverter/Command/CommandContext.h"

class HelpCommand : public CommandInterface {
public:
    HelpCommand() : CommandInterface("help", "List commands") {}

    void execute(const ArgValue*, CommandContext&) override {
        CommandManager::instance().printHelp();
    }
};

static HelpCommand sHelpCmd;

void registerHelpCommand(CommandManager& mgr) {
    mgr.registerCommand(&sHelpCmd);
}
