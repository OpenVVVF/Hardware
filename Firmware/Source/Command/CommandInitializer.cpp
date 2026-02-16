#include "CommandInitializer.h"
#include "CommandManager.h"
#include "CommandInterface.h"

CommandInterface* makeFreqCommand();
CommandInterface* makeRampCommand();
CommandInterface* makeCarrierCommand();
CommandInterface* makeAutoCommand();
CommandInterface* makeAsyncCommand();
CommandInterface* makeFlashCommand();
CommandInterface* makeSoftStopCommand();
CommandInterface* makeEmergencyStopCommand();
CommandInterface* makeEnableCommand();
CommandInterface* makeImmediateCommand();
CommandInterface* makeHelpCommand();
CommandInterface* makeEncoderOffsetCommand();

void initializeCommands() {
    auto& mgr = CommandManager::instance();

    CommandInterface* cmds[] = {
        makeFreqCommand(),
        makeRampCommand(),
        makeCarrierCommand(),
        makeAutoCommand(),
        makeAsyncCommand(),
        makeFlashCommand(),
        makeSoftStopCommand(),
        makeEmergencyStopCommand(),
        makeEnableCommand(),
        makeImmediateCommand(),
        makeHelpCommand(),
        makeEncoderOffsetCommand()
    };

    for (auto* c : cmds) mgr.registerCommand(c);
}
