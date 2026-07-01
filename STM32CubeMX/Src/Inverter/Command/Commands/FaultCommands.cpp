#include "Inverter/Command/CommandInterface.h"
#include "Inverter/Command/CommandContext.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Telemetry.h"

using Inverter::FaultManager;

class FaultListCommand : public CommandInterface {
public:
    FaultListCommand() : CommandInterface("fault_list", "Print fault summary") {}

    void execute(const ArgValue*, CommandContext&) override {
        FaultManager::instance().printSummary();
    }
};

class FaultClearCommand : public CommandInterface {
public:
    FaultClearCommand() : CommandInterface("fault_clear", "Clear all latched faults") {}

    void execute(const ArgValue*, CommandContext&) override {
        FaultManager::instance().clearAll();
        Telemetry::printf("[SHELL] latched faults cleared");
    }
};

class FaultTestCommand : public CommandInterface {
public:
    FaultTestCommand()
      : CommandInterface("fault_test", "Inject a test fault by index",
            ArgSpec{"index", "", 0.0f, 32.0f, 0.0f, true, ArgSpec::INT}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        const int idx = args[0].i_val;
        if (idx < 0 || static_cast<size_t>(idx) >= FaultManager::metaCount()) {
            Telemetry::printf("[SHELL] fault index out of range (0-%d)",
                              static_cast<int>(FaultManager::metaCount()) - 1);
            return;
        }
        const Inverter::FaultSource src = FaultManager::metaTable()[idx].source;
        FaultManager::instance().testFault(src);
        Telemetry::printf("[SHELL] injected test fault %d", idx);
    }
};

static FaultListCommand  sFaultListCmd;
static FaultClearCommand sFaultClearCmd;
static FaultTestCommand  sFaultTestCmd;

#include "Inverter/Command/CommandManager.h"

void registerFaultCommands(CommandManager& mgr) {
    mgr.registerCommand(&sFaultListCmd);
    mgr.registerCommand(&sFaultClearCmd);
    mgr.registerCommand(&sFaultTestCmd);
}
