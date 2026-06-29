#include "../CommandInterface.h"
#include "../CommandManager.h"
#include <cstdio>

class HelpCommand : public CommandInterface {
public:
    HelpCommand() : CommandInterface("HELP", "List commands") {}
    void execute(const ArgValue* args, CommandContext& ctx) override {
        CommandManager::instance().printHelp();
    }
};

CommandInterface* makeHelpCommand() {
   static HelpCommand inst;
   return &inst;
}