#include "Inverter/Command/CommandInitializer.h"
#include "Inverter/Command/CommandManager.h"

void registerOpenLoopCommands(CommandManager& mgr);
void registerSystemCommands(CommandManager& mgr);
void registerCalibrationCommands(CommandManager& mgr);
void registerFaultCommands(CommandManager& mgr);
void registerSensorCommands(CommandManager& mgr);
void registerHelpCommand(CommandManager& mgr);
void registerFocCommands(CommandManager& mgr);

void initializeCommands() {
    CommandManager& mgr = CommandManager::instance();

    registerOpenLoopCommands(mgr);
    registerSystemCommands(mgr);
    registerCalibrationCommands(mgr);
    registerFaultCommands(mgr);
    registerSensorCommands(mgr);
    registerFocCommands(mgr);
    registerHelpCommand(mgr);
}
