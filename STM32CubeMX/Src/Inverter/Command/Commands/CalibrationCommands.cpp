#include "Inverter/Command/CommandInterface.h"
#include "Inverter/Command/CommandContext.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Calibration/PoleCalibrator.h"
#include "Inverter/Calibration/EncoderOffsetCalibrator.h"
#include "Inverter/Calibration/ResistanceCalibrator.h"
#include "Inverter/Drivers/Sensors/PoleEstimator.h"
#include "Inverter/Telemetry.h"

using Inverter::OpenLoopController;
using Inverter::PoleEstimator;
using Inverter::PoleCalibrator;
using Inverter::EncoderOffsetCalibrator;
using Inverter::ResistanceCalibrator;
using Inverter::openLoopController;
using Inverter::poleCalibrator;
using Inverter::encoderOffsetCalibrator;
using Inverter::resistanceCalibrator;

class PolesCommand : public CommandInterface {
public:
    PolesCommand() : CommandInterface("poles", "Print estimated pole count and cycle counts") {}

    void execute(const ArgValue*, CommandContext&) override {
        PoleEstimator& poles = PoleEstimator::instance();
        Telemetry::printf("[SHELL] poles=%.3f mech=%.2f elec=%.2f",
                          static_cast<double>(poles.estimate()),
                          static_cast<double>(poles.mechanicalCycles()),
                          static_cast<double>(poles.electricalCycles()));
    }
};

class EncoderCalStartCommand : public CommandInterface {
public:
    EncoderCalStartCommand() : CommandInterface("encodercal_start", "Start manual one-revolution encoder calibration") {}

    void execute(const ArgValue*, CommandContext&) override {
        PoleEstimator::instance().startManualEncoderCal();
        Telemetry::printf("[SHELL] encoder cal started; rotate shaft exactly 1 rev, then 'encodercal_stop'");
    }
};

class EncoderCalStopCommand : public CommandInterface {
public:
    EncoderCalStopCommand() : CommandInterface("encodercal_stop", "Finish manual encoder calibration") {}

    void execute(const ArgValue*, CommandContext&) override {
        PoleEstimator& poles = PoleEstimator::instance();
        poles.stopManualEncoderCal();
        const float cycles = poles.manualEncoderCycles();
        Telemetry::printf("[SHELL] encoder cycles in 1 rev = %.2f; true poles = poles_estimate * %.2f",
                          static_cast<double>(cycles), static_cast<double>(cycles));
    }
};

class CalPolesCommand : public CommandInterface {
public:
    CalPolesCommand() : CommandInterface("calpoles", "Start automatic pole calibration") {}

    void execute(const ArgValue*, CommandContext&) override {
        PoleCalibrator& cal = poleCalibrator();
        if (openLoopController().isRunning()) {
            Telemetry::printf("[SHELL] stop the motor before starting calpoles");
        } else if (cal.isActive()) {
            Telemetry::printf("[SHELL] calibration already running");
        } else {
            cal.start();
        }
    }
};

class EncOffsetStartCommand : public CommandInterface {
public:
    EncOffsetStartCommand()
      : CommandInterface("encoffset_start", "Start encoder-offset calibration",
            {ArgSpec{"poles", "", 2.0f, 100.0f, 10.0f, true, ArgSpec::FLOAT},
             ArgSpec{"enc_cycles", "", 0.1f, 100.0f, 1.0f, true, ArgSpec::FLOAT},
             ArgSpec{"breakaway_mod", "", 0.0f, 1.0f, 0.0f, false, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        EncoderOffsetCalibrator& cal = encoderOffsetCalibrator();
        if (openLoopController().isRunning()) {
            Telemetry::printf("[SHELL] stop the motor before starting encoffset");
        } else if (cal.isActive()) {
            Telemetry::printf("[SHELL] calibration already running");
        } else {
            cal.start(args[0].f_val, args[1].f_val, args[2].f_val);
        }
    }
};

class EncOffsetStatusCommand : public CommandInterface {
public:
    EncOffsetStatusCommand() : CommandInterface("encoffset_status", "Report encoder-offset calibration status") {}

    void execute(const ArgValue*, CommandContext&) override {
        EncoderOffsetCalibrator& cal = encoderOffsetCalibrator();
        Telemetry::printf("[SHELL] encoffset: samples=%d avg=%.3f deg",
                          cal.sampleCount(),
                          static_cast<double>(cal.averageOffset()));
    }
};

class ResCalStartAllCommand : public CommandInterface {
public:
    ResCalStartAllCommand()
      : CommandInterface("rescal_start_all", "Run resistance calibration on all pairs",
            {ArgSpec{"bus_pct", "%", 0.0f, 100.0f, 0.0f, true, ArgSpec::FLOAT},
             ArgSpec{"max_a", "A", 0.0f, 500.0f, 50.0f, false, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        if (!resistanceCalibrator().start(args[0].f_val, ResistanceCalibrator::Pair::UV,
                                          true, 15000U, args[1].f_val)) {
            Telemetry::printf("[SHELL] rescal start failed");
        }
    }
};

class ResCalStartUvCommand : public CommandInterface {
public:
    ResCalStartUvCommand()
      : CommandInterface("rescal_start_uv", "Run resistance calibration on UV pair",
            {ArgSpec{"bus_pct", "%", 0.0f, 100.0f, 0.0f, true, ArgSpec::FLOAT},
             ArgSpec{"max_a", "A", 0.0f, 500.0f, 50.0f, false, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        if (!resistanceCalibrator().start(args[0].f_val, ResistanceCalibrator::Pair::UV,
                                          false, 15000U, args[1].f_val)) {
            Telemetry::printf("[SHELL] rescal start failed");
        }
    }
};

class ResCalStartUwCommand : public CommandInterface {
public:
    ResCalStartUwCommand()
      : CommandInterface("rescal_start_uw", "Run resistance calibration on UW pair",
            {ArgSpec{"bus_pct", "%", 0.0f, 100.0f, 0.0f, true, ArgSpec::FLOAT},
             ArgSpec{"max_a", "A", 0.0f, 500.0f, 50.0f, false, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        if (!resistanceCalibrator().start(args[0].f_val, ResistanceCalibrator::Pair::UW,
                                          false, 15000U, args[1].f_val)) {
            Telemetry::printf("[SHELL] rescal start failed");
        }
    }
};

class ResCalStartVwCommand : public CommandInterface {
public:
    ResCalStartVwCommand()
      : CommandInterface("rescal_start_vw", "Run resistance calibration on VW pair",
            {ArgSpec{"bus_pct", "%", 0.0f, 100.0f, 0.0f, true, ArgSpec::FLOAT},
             ArgSpec{"max_a", "A", 0.0f, 500.0f, 50.0f, false, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        if (!resistanceCalibrator().start(args[0].f_val, ResistanceCalibrator::Pair::VW,
                                          false, 15000U, args[1].f_val)) {
            Telemetry::printf("[SHELL] rescal start failed");
        }
    }
};

class ResCalIctrlAllCommand : public CommandInterface {
public:
    ResCalIctrlAllCommand()
      : CommandInterface("rescal_ictrl_all", "Current-controlled resistance calibration on all pairs",
            {ArgSpec{"current_a", "A", 0.0f, 500.0f, 0.0f, true, ArgSpec::FLOAT},
             ArgSpec{"oc_limit_a", "A", 0.0f, 500.0f, 0.0f, false, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        if (!resistanceCalibrator().startCurrentCtrl(args[0].f_val, ResistanceCalibrator::Pair::UV,
                                                     true, 15000U, args[1].f_val)) {
            Telemetry::printf("[SHELL] rescal ictrl failed");
        }
    }
};

class ResCalIctrlUvCommand : public CommandInterface {
public:
    ResCalIctrlUvCommand()
      : CommandInterface("rescal_ictrl_uv", "Current-controlled resistance calibration on UV pair",
            {ArgSpec{"current_a", "A", 0.0f, 500.0f, 0.0f, true, ArgSpec::FLOAT},
             ArgSpec{"oc_limit_a", "A", 0.0f, 500.0f, 0.0f, false, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        if (!resistanceCalibrator().startCurrentCtrl(args[0].f_val, ResistanceCalibrator::Pair::UV,
                                                     false, 15000U, args[1].f_val)) {
            Telemetry::printf("[SHELL] rescal ictrl failed");
        }
    }
};

class ResCalIctrlUwCommand : public CommandInterface {
public:
    ResCalIctrlUwCommand()
      : CommandInterface("rescal_ictrl_uw", "Current-controlled resistance calibration on UW pair",
            {ArgSpec{"current_a", "A", 0.0f, 500.0f, 0.0f, true, ArgSpec::FLOAT},
             ArgSpec{"oc_limit_a", "A", 0.0f, 500.0f, 0.0f, false, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        if (!resistanceCalibrator().startCurrentCtrl(args[0].f_val, ResistanceCalibrator::Pair::UW,
                                                     false, 15000U, args[1].f_val)) {
            Telemetry::printf("[SHELL] rescal ictrl failed");
        }
    }
};

class ResCalIctrlVwCommand : public CommandInterface {
public:
    ResCalIctrlVwCommand()
      : CommandInterface("rescal_ictrl_vw", "Current-controlled resistance calibration on VW pair",
            {ArgSpec{"current_a", "A", 0.0f, 500.0f, 0.0f, true, ArgSpec::FLOAT},
             ArgSpec{"oc_limit_a", "A", 0.0f, 500.0f, 0.0f, false, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        if (!resistanceCalibrator().startCurrentCtrl(args[0].f_val, ResistanceCalibrator::Pair::VW,
                                                     false, 15000U, args[1].f_val)) {
            Telemetry::printf("[SHELL] rescal ictrl failed");
        }
    }
};

class ResCalStopCommand : public CommandInterface {
public:
    ResCalStopCommand() : CommandInterface("rescal_stop", "Stop resistance calibration") {}

    void execute(const ArgValue*, CommandContext&) override {
        resistanceCalibrator().stop();
    }
};

class ResCalStatusCommand : public CommandInterface {
public:
    ResCalStatusCommand() : CommandInterface("rescal_status", "Print resistance calibration results") {}

    void execute(const ArgValue*, CommandContext&) override {
        ResistanceCalibrator& rc = resistanceCalibrator();
        Telemetry::printf("[SHELL] rescal: R_uv=%.3f R_uw=%.3f R_vw=%.3f avg=%.3f ohm",
                          static_cast<double>(rc.lastResult(ResistanceCalibrator::Pair::UV)),
                          static_cast<double>(rc.lastResult(ResistanceCalibrator::Pair::UW)),
                          static_cast<double>(rc.lastResult(ResistanceCalibrator::Pair::VW)),
                          static_cast<double>(rc.lastAverage()));
    }
};

static PolesCommand             sPolesCmd;
static EncoderCalStartCommand   sEncoderCalStartCmd;
static EncoderCalStopCommand    sEncoderCalStopCmd;
static CalPolesCommand          sCalPolesCmd;
static EncOffsetStartCommand    sEncOffsetStartCmd;
static EncOffsetStatusCommand   sEncOffsetStatusCmd;
static ResCalStartAllCommand    sResCalStartAllCmd;
static ResCalStartUvCommand     sResCalStartUvCmd;
static ResCalStartUwCommand     sResCalStartUwCmd;
static ResCalStartVwCommand     sResCalStartVwCmd;
static ResCalIctrlAllCommand    sResCalIctrlAllCmd;
static ResCalIctrlUvCommand     sResCalIctrlUvCmd;
static ResCalIctrlUwCommand     sResCalIctrlUwCmd;
static ResCalIctrlVwCommand     sResCalIctrlVwCmd;
static ResCalStopCommand        sResCalStopCmd;
static ResCalStatusCommand      sResCalStatusCmd;

#include "Inverter/Command/CommandManager.h"

void registerCalibrationCommands(CommandManager& mgr) {
    mgr.registerCommand(&sPolesCmd);
    mgr.registerCommand(&sEncoderCalStartCmd);
    mgr.registerCommand(&sEncoderCalStopCmd);
    mgr.registerCommand(&sCalPolesCmd);
    mgr.registerCommand(&sEncOffsetStartCmd);
    mgr.registerCommand(&sEncOffsetStatusCmd);
    mgr.registerCommand(&sResCalStartAllCmd);
    mgr.registerCommand(&sResCalStartUvCmd);
    mgr.registerCommand(&sResCalStartUwCmd);
    mgr.registerCommand(&sResCalStartVwCmd);
    mgr.registerCommand(&sResCalIctrlAllCmd);
    mgr.registerCommand(&sResCalIctrlUvCmd);
    mgr.registerCommand(&sResCalIctrlUwCmd);
    mgr.registerCommand(&sResCalIctrlVwCmd);
    mgr.registerCommand(&sResCalStopCmd);
    mgr.registerCommand(&sResCalStatusCmd);
}
