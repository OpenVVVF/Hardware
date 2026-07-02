#include "Inverter/Command/CommandInterface.h"
#include "Inverter/Command/CommandContext.h"
#include "Inverter/Command/CommandManager.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Calibration/PoleCalibrator.h"
#include "Inverter/Calibration/EncoderOffsetCalibrator.h"
#include "Inverter/Calibration/ResistanceCalibrator.h"
#include "Inverter/Calibration/EncoderCycleCalibrator.h"
#include "Inverter/Calibration/AutoCalibrationCoordinator.h"
#include "Inverter/Drivers/Sensors/PoleEstimator.h"
#include "Inverter/Telemetry.h"

#include <cstring>
#include <cctype>

using Inverter::OpenLoopController;
using Inverter::PoleEstimator;
using Inverter::PoleCalibrator;
using Inverter::EncoderOffsetCalibrator;
using Inverter::ResistanceCalibrator;
using Inverter::EncoderCycleCalibrator;
using Inverter::AutoCalibrationCoordinator;
using Inverter::openLoopController;
using Inverter::poleCalibrator;
using Inverter::encoderOffsetCalibrator;
using Inverter::resistanceCalibrator;
using Inverter::autoCalibrationCoordinator;

static bool stringsEqual(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return false;
    while (*a && *b) {
        if (std::tolower(static_cast<unsigned char>(*a)) !=
            std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool parsePair(const char* token, ResistanceCalibrator::Pair& out) {
    if (stringsEqual(token, "uv")) {
        out = ResistanceCalibrator::Pair::UV;
        return true;
    }
    if (stringsEqual(token, "uw")) {
        out = ResistanceCalibrator::Pair::UW;
        return true;
    }
    if (stringsEqual(token, "vw")) {
        out = ResistanceCalibrator::Pair::VW;
        return true;
    }
    return false;
}

class PolesCommand : public CommandInterface {
public:
    PolesCommand() : CommandInterface("poles", "Print estimated pole count and cycle counts") {}

    void execute(const ArgValue*, CommandContext&) override {
        PoleEstimator& poles = PoleEstimator::instance();
        Telemetry::printf("[CAL] poles=%.3f mech=%.2f elec=%.2f",
                          static_cast<double>(poles.estimate()),
                          static_cast<double>(poles.mechanicalCycles()),
                          static_cast<double>(poles.electricalCycles()));
    }
};

class EncoderCalCommand : public CommandInterface {
public:
    EncoderCalCommand()
      : CommandInterface("encodercal", "Manual one-revolution encoder-cycle calibration",
            ArgSpec{"subcmd", "", 0.0f, 0.0f, 0.0f, true, ArgSpec::STRING}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        EncoderCycleCalibrator& cal = EncoderCycleCalibrator::instance();
        const char* sub = args[0].s_val;

        if (stringsEqual(sub, "start")) {
            cal.start();
            Telemetry::printf("[CAL] encodercal started; rotate shaft exactly 1 rev, then 'encodercal stop'");
        } else if (stringsEqual(sub, "stop")) {
            cal.stop();
            const float cycles = cal.cycles();
            Telemetry::printf("[CAL] encoder cycles in 1 rev = %.2f; true poles = poles_estimate * %.2f",
                              static_cast<double>(cycles), static_cast<double>(cycles));
        } else if (stringsEqual(sub, "status")) {
            Telemetry::printf("[CAL] encodercal: active=%s cycles=%.2f",
                              cal.isActive() ? "Y" : "N",
                              static_cast<double>(cal.cycles()));
        } else {
            Telemetry::printf("[CAL] encodercal: unknown subcommand '%s' (start/stop/status)", sub);
        }
    }
};

class CalPolesCommand : public CommandInterface {
public:
    CalPolesCommand() : CommandInterface("calpoles", "Start automatic pole calibration") {}

    void execute(const ArgValue*, CommandContext&) override {
        PoleCalibrator& cal = poleCalibrator();
        if (openLoopController().isRunning()) {
            Telemetry::printf("[CAL] stop the motor before starting calpoles");
        } else if (cal.isActive()) {
            Telemetry::printf("[CAL] calibration already running");
        } else {
            cal.start();
        }
    }
};

class EncOffsetCommand : public CommandInterface {
public:
    EncOffsetCommand()
      : CommandInterface("encoffset", "Encoder-offset calibration",
            {ArgSpec{"subcmd", "", 0.0f, 0.0f, 0.0f, true, ArgSpec::STRING},
             ArgSpec{"poles", "", 2.0f, 100.0f, 10.0f, false, ArgSpec::FLOAT},
             ArgSpec{"enc_cycles", "", 0.1f, 100.0f, 1.0f, false, ArgSpec::FLOAT},
             ArgSpec{"breakaway_mod", "", 0.0f, 1.0f, 0.0f, false, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        EncoderOffsetCalibrator& cal = encoderOffsetCalibrator();
        const char* sub = args[0].s_val;

        if (stringsEqual(sub, "start")) {
            if (openLoopController().isRunning()) {
                Telemetry::printf("[CAL] stop the motor before starting encoffset");
                return;
            }
            if (cal.isActive()) {
                Telemetry::printf("[CAL] calibration already running");
                return;
            }
            if (!args[1].present || !args[2].present) {
                Telemetry::printf("[CAL] encoffset start <poles> <enc_cycles> [breakaway_mod]");
                return;
            }
            cal.start(args[1].f_val, args[2].f_val, args[3].f_val);
        } else if (stringsEqual(sub, "status")) {
            Telemetry::printf("[CAL] encoffset: samples=%d avg=%.3f deg",
                              cal.sampleCount(),
                              static_cast<double>(cal.averageOffset()));
        } else {
            Telemetry::printf("[CAL] encoffset: unknown subcommand '%s' (start/status)", sub);
        }
    }
};

class ResCalCommand : public CommandInterface {
public:
    ResCalCommand()
      : CommandInterface("rescal", "Resistance calibration",
            {ArgSpec{"subcmd", "", 0.0f, 0.0f, 0.0f, true, ArgSpec::STRING},
             ArgSpec{"pair", "", 0.0f, 0.0f, 0.0f, false, ArgSpec::STRING},
             ArgSpec{"value", "", 0.0f, 1000.0f, 0.0f, false, ArgSpec::FLOAT},
             ArgSpec{"limit", "", 0.0f, 1000.0f, 0.0f, false, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        ResistanceCalibrator& rc = resistanceCalibrator();
        const char* sub = args[0].s_val;

        if (stringsEqual(sub, "stop")) {
            rc.stop();
            return;
        }

        if (stringsEqual(sub, "status")) {
            Telemetry::printf("[CAL] rescal: R_uv=%.3f R_uw=%.3f R_vw=%.3f avg=%.3f ohm",
                              static_cast<double>(rc.lastResult(ResistanceCalibrator::Pair::UV)),
                              static_cast<double>(rc.lastResult(ResistanceCalibrator::Pair::UW)),
                              static_cast<double>(rc.lastResult(ResistanceCalibrator::Pair::VW)),
                              static_cast<double>(rc.lastAverage()));
            return;
        }

        const char* pairStr = args[1].present ? args[1].s_val : "all";
        const bool runAll = stringsEqual(pairStr, "all");
        ResistanceCalibrator::Pair pair = ResistanceCalibrator::Pair::UV;
        if (!runAll && !parsePair(pairStr, pair)) {
            Telemetry::printf("[CAL] rescal: pair must be uv, uw, vw, or all");
            return;
        }

        if (stringsEqual(sub, "start")) {
            if (!args[2].present) {
                Telemetry::printf("[CAL] rescal start <pair|all> <bus_pct> [max_a]");
                return;
            }
            const float maxA = args[3].present ? args[3].f_val : 50.0f;
            if (!rc.start(args[2].f_val, pair, runAll, 15000U, maxA)) {
                Telemetry::printf("[CAL] rescal start failed");
            }
        } else if (stringsEqual(sub, "ictrl")) {
            if (!args[2].present) {
                Telemetry::printf("[CAL] rescal ictrl <pair|all> <current_a> [oc_limit_a]");
                return;
            }
            const float ocLimit = args[3].present ? args[3].f_val : 0.0f;
            if (!rc.startCurrentCtrl(args[2].f_val, pair, runAll, 15000U, ocLimit)) {
                Telemetry::printf("[CAL] rescal ictrl failed");
            }
        } else {
            Telemetry::printf("[CAL] rescal: unknown subcommand '%s' (start/ictrl/stop/status)", sub);
        }
    }
};

class MotorCalCommand : public CommandInterface {
public:
    MotorCalCommand()
      : CommandInterface("motorcal", "Automatic motor profiling (poles, encoder cycles, offset, resistance)",
            ArgSpec{"subcmd", "", 0.0f, 0.0f, 0.0f, true, ArgSpec::STRING}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        AutoCalibrationCoordinator& coord = autoCalibrationCoordinator();
        const char* sub = args[0].s_val;

        if (stringsEqual(sub, "start")) {
            if (openLoopController().isRunning()) {
                Telemetry::printf("[CAL] stop the motor before starting motorcal");
                return;
            }
            if (!coord.start()) {
                Telemetry::printf("[CAL] motorcal start failed");
            }
        } else if (stringsEqual(sub, "stop")) {
            coord.stop();
        } else if (stringsEqual(sub, "status")) {
            Telemetry::printf("[CAL] motorcal: state=%s poles=%.2f enc_cycles=%.2f offset=%.3f R_avg=%.4f",
                              coord.stateName(),
                              static_cast<double>(coord.lastPoles()),
                              static_cast<double>(coord.lastEncoderCyclesPerRev()),
                              static_cast<double>(coord.lastEncoderOffset()),
                              static_cast<double>(coord.lastResistanceAverage()));
        } else {
            Telemetry::printf("[CAL] motorcal: unknown subcommand '%s' (start/status)", sub);
        }
    }
};

static PolesCommand      sPolesCmd;
static EncoderCalCommand sEncoderCalCmd;
static CalPolesCommand   sCalPolesCmd;
static EncOffsetCommand  sEncOffsetCmd;
static ResCalCommand     sResCalCmd;
static MotorCalCommand   sMotorCalCmd;

void registerCalibrationCommands(CommandManager& mgr) {
    mgr.registerCommand(&sPolesCmd);
    mgr.registerCommand(&sEncoderCalCmd);
    mgr.registerCommand(&sCalPolesCmd);
    mgr.registerCommand(&sEncOffsetCmd);
    mgr.registerCommand(&sResCalCmd);
    mgr.registerCommand(&sMotorCalCmd);
}
