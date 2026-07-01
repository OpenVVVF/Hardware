#include "Inverter/Command/CommandInterface.h"
#include "Inverter/Command/CommandContext.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Drivers/GateDriver/gate_driver.h"
#include "Inverter/Telemetry.h"

using Inverter::OpenLoopController;
using Inverter::FaultManager;
using Inverter::openLoopController;

class StartCommand : public CommandInterface {
public:
    StartCommand()
      : CommandInterface("start", "Start open-loop PWM output",
            {ArgSpec{"freq_hz", "Hz", 0.0f, 1000.0f, 0.0f, true, ArgSpec::FLOAT},
             ArgSpec{"mod_idx", "", 0.0f, 1.2f, 0.0f, true, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        openLoopController().start(args[0].f_val, args[1].f_val);
    }
};

class StopCommand : public CommandInterface {
public:
    StopCommand() : CommandInterface("stop", "Stop open-loop PWM output") {}

    void execute(const ArgValue*, CommandContext&) override {
        openLoopController().stop();
    }
};

class FreqCommand : public CommandInterface {
public:
    FreqCommand()
      : CommandInterface("freq", "Set open-loop frequency",
            ArgSpec{"freq_hz", "Hz", 0.0f, 1000.0f, 0.0f, true, ArgSpec::FLOAT}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        openLoopController().setFrequency(args[0].f_val);
        Telemetry::printf("[SHELL] freq set to %.2f Hz", static_cast<double>(args[0].f_val));
    }
};

class ModCommand : public CommandInterface {
public:
    ModCommand()
      : CommandInterface("mod", "Set open-loop modulation index",
            ArgSpec{"mod_idx", "", 0.0f, 1.2f, 0.0f, true, ArgSpec::FLOAT}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        openLoopController().setModulationIndex(args[0].f_val);
        Telemetry::printf("[SHELL] mod set to %.3f", static_cast<double>(args[0].f_val));
    }
};

class StatusCommand : public CommandInterface {
public:
    StatusCommand() : CommandInterface("status", "Show OL controller and fault status") {}

    void execute(const ArgValue*, CommandContext&) override {
        OpenLoopController& ol = openLoopController();
        Telemetry::printf("[SHELL] run=%s f=%.2f m=%.3f",
                          ol.isRunning() ? "Y" : "N",
                          static_cast<double>(ol.frequencyHz()),
                          static_cast<double>(ol.modulationIndex()));
        Telemetry::printf("[SHELL] ready=%s gd_fault=%s",
                          GateDriver_IsReady() ? "Y" : "N",
                          GateDriver_IsFault() ? "Y" : "N");
        FaultManager::instance().printSummary();
    }
};

class RampCurrentLimitCommand : public CommandInterface {
public:
    RampCurrentLimitCommand()
      : CommandInterface("rclimit", "Set ramp current limit",
            ArgSpec{"amps", "A", 0.0f, 2000.0f, 0.0f, true, ArgSpec::FLOAT}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        float amps = args[0].f_val;
        if (amps < 0.0f) amps = 0.0f;
        openLoopController().setRampCurrentLimit(amps);
        Telemetry::printf("[SHELL] ramp current limit set to %.3f A", static_cast<double>(amps));
    }
};

CommandInterface* makeStartCommand()    { static StartCommand inst; return &inst; }
CommandInterface* makeStopCommand()     { static StopCommand inst; return &inst; }
CommandInterface* makeFreqCommand()     { static FreqCommand inst; return &inst; }
CommandInterface* makeModCommand()      { static ModCommand inst; return &inst; }
CommandInterface* makeStatusCommand()   { static StatusCommand inst; return &inst; }
CommandInterface* makeRampCurrentLimitCommand() { static RampCurrentLimitCommand inst; return &inst; }
