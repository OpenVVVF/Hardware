#include "Inverter/Command/CommandInitializer.h"
#include "Inverter/Command/CommandManager.h"
#include "Inverter/Command/CommandInterface.h"

// Open-loop commands
CommandInterface* makeStartCommand();
CommandInterface* makeStopCommand();
CommandInterface* makeFreqCommand();
CommandInterface* makeModCommand();
CommandInterface* makeStatusCommand();
CommandInterface* makeRampCurrentLimitCommand();

// System commands
CommandInterface* makeClearFaultCommand();
CommandInterface* makeCalCommand();
CommandInterface* makeRawCommand();
CommandInterface* makeVZeroCommand();
CommandInterface* makeSupplyStatusCommand();
CommandInterface* makeRebootCommand();

// Calibration commands
CommandInterface* makePolesCommand();
CommandInterface* makeEncoderCalStartCommand();
CommandInterface* makeEncoderCalStopCommand();
CommandInterface* makeCalPolesCommand();
CommandInterface* makeEncOffsetStartCommand();
CommandInterface* makeEncOffsetStatusCommand();
CommandInterface* makeResCalStartAllCommand();
CommandInterface* makeResCalStartUvCommand();
CommandInterface* makeResCalStartUwCommand();
CommandInterface* makeResCalStartVwCommand();
CommandInterface* makeResCalIctrlAllCommand();
CommandInterface* makeResCalIctrlUvCommand();
CommandInterface* makeResCalIctrlUwCommand();
CommandInterface* makeResCalIctrlVwCommand();
CommandInterface* makeResCalStopCommand();
CommandInterface* makeResCalStatusCommand();

// Fault commands
CommandInterface* makeFaultListCommand();
CommandInterface* makeFaultClearCommand();
CommandInterface* makeFaultTestCommand();

// Sensor commands
CommandInterface* makeOcSetCommand();
CommandInterface* makeHwOcSetCommand();
CommandInterface* makeMaxCfgOvCommand();
CommandInterface* makeMaxCfgUvCommand();
CommandInterface* makeMaxCfgStatusCommand();
CommandInterface* makeMaxCfgThresholdsCommand();
CommandInterface* makeMaxCfgFilterClearCommand();
CommandInterface* makeMaxCfgRawCommand();
CommandInterface* makeMaxCfgFilteredCommand();

// Help
CommandInterface* makeHelpCommand();

void initializeCommands() {
    auto& mgr = CommandManager::instance();

    CommandInterface* cmds[] = {
        makeStartCommand(),
        makeStopCommand(),
        makeFreqCommand(),
        makeModCommand(),
        makeStatusCommand(),
        makeRampCurrentLimitCommand(),

        makeClearFaultCommand(),
        makeCalCommand(),
        makeRawCommand(),
        makeVZeroCommand(),
        makeSupplyStatusCommand(),
        makeRebootCommand(),

        makePolesCommand(),
        makeEncoderCalStartCommand(),
        makeEncoderCalStopCommand(),
        makeCalPolesCommand(),
        makeEncOffsetStartCommand(),
        makeEncOffsetStatusCommand(),
        makeResCalStartAllCommand(),
        makeResCalStartUvCommand(),
        makeResCalStartUwCommand(),
        makeResCalStartVwCommand(),
        makeResCalIctrlAllCommand(),
        makeResCalIctrlUvCommand(),
        makeResCalIctrlUwCommand(),
        makeResCalIctrlVwCommand(),
        makeResCalStopCommand(),
        makeResCalStatusCommand(),

        makeFaultListCommand(),
        makeFaultClearCommand(),
        makeFaultTestCommand(),

        makeOcSetCommand(),
        makeHwOcSetCommand(),
        makeMaxCfgOvCommand(),
        makeMaxCfgUvCommand(),
        makeMaxCfgStatusCommand(),
        makeMaxCfgThresholdsCommand(),
        makeMaxCfgFilterClearCommand(),
        makeMaxCfgRawCommand(),
        makeMaxCfgFilteredCommand(),

        makeHelpCommand(),
    };

    for (auto* c : cmds) {
        mgr.registerCommand(c);
    }
}
