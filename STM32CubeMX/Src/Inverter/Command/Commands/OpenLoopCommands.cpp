#include "Inverter/Command/CommandInterface.h"
#include "Inverter/Command/CommandContext.h"
#include "Inverter/Control/FocControlManager.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Drivers/GateDriver/gate_driver.h"
#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Telemetry.h"

#include "main.h"

#include <cmath>

using Inverter::dcLinkVoltageSensor;
using Inverter::encoderADC;
using Inverter::FocControlManager;
using Inverter::OpenLoopController;
using Inverter::FaultManager;
using Inverter::focControlManager;
using Inverter::openLoopController;
using Inverter::phaseCurrentADC;

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

class SwFreqCommand : public CommandInterface {
public:
    SwFreqCommand()
      : CommandInterface("swfreq", "Set PWM switching frequency [Hz]",
            ArgSpec{"freq_hz", "Hz", 1000.0f, 16000.0f, 0.0f, false, ArgSpec::FLOAT}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        if (!args[0].present) {
            Telemetry::printf("[SHELL] swfreq = %.0f Hz (update %.0f Hz)",
                              static_cast<double>(PWM_GetFrequency()),
                              static_cast<double>(PWM_GetUpdateFrequency()));
            return;
        }
        if (openLoopController().isRunning() || focControlManager().isRunning()) {
            Telemetry::printf("[SHELL] stop the motor before changing swfreq");
            return;
        }
        PWM_SetFrequency(static_cast<uint32_t>(args[0].f_val + 0.5f));
        Telemetry::printf("[SHELL] swfreq set to %.0f Hz (update %.0f Hz)",
                          static_cast<double>(PWM_GetFrequency()),
                          static_cast<double>(PWM_GetUpdateFrequency()));
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

/**
 * @brief Apply short static voltage-vector pulses at 0/90/180/270 deg and log
 * the sensor response.
 *
 * Useful for verifying current-sensor polarity, encoder offset sign, and phase
 * mapping before running closed-loop FOC.  The command is blocking and parks
 * the outputs at 50 % when finished.
 *
 * Because the motor resistance is very low, long DC vectors would pull huge
 * current.  Each vector is held only for a short pulse so the current stays
 * small but the polarity is still visible.
 *
 * Usage: vectorscan <mod_idx> <pulse_ms> [max_a]
 */
class VectorScanCommand : public CommandInterface {
public:
    VectorScanCommand()
      : CommandInterface("vectorscan", "Apply 0/90/180/270 deg voltage pulses and log currents/encoder",
            {ArgSpec{"mod_idx", "", 0.0f, 1.2f, 0.05f, true, ArgSpec::FLOAT},
             ArgSpec{"pulse_ms", "ms", 1.0f, 5000.0f, 10.0f, true, ArgSpec::FLOAT},
             ArgSpec{"max_a", "A", 0.0f, 2000.0f, 0.0f, false, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        const float mod_idx = args[0].f_val;
        const uint32_t pulse_ms = static_cast<uint32_t>(args[1].f_val);
        const float max_a = args[2].present ? args[2].f_val : 0.0f;

        if (focControlManager().isRunning()) {
            focControlManager().stop();
            Telemetry::printf("[SHELL] stopped FOC first");
        }
        if (openLoopController().isRunning()) {
            openLoopController().stop();
            Telemetry::printf("[SHELL] stopped open-loop first");
        }

        if (FaultManager::instance().isSeverityActive(Inverter::FaultSeverity::Critical) ||
            FaultManager::instance().isSeverityActive(Inverter::FaultSeverity::High)) {
            Telemetry::printf("[SHELL] active Critical/High faults, cannot run vectorscan");
            FaultManager::instance().printSummary();
            return;
        }

        /* Safe start: assert reset, then release and wait for ready. */
        PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);
        GateDriver_DisableOutputs();
        HAL_Delay(10);
        GateDriver_EnableOutputs();
        HAL_Delay(10);

        if (!GateDriver_IsReady() || GateDriver_IsFault()) {
            Telemetry::printf("[SHELL] ERROR: gate driver not ready or fault latched");
            GateDriver_DisableOutputs();
            return;
        }

        PWM_ClearFault();
        PWM_Start();

        Telemetry::printf("[SHELL] vectorscan mod=%.3f pulse=%lu ms max_a=%.1f A",
                          static_cast<double>(mod_idx),
                          static_cast<unsigned long>(pulse_ms),
                          static_cast<double>(max_a));

        static constexpr uint32_t ANGLE_COUNT = 4U;
        const float angles_deg[ANGLE_COUNT] = {0.0f, 90.0f, 180.0f, 270.0f};
        bool aborted = false;

        for (uint32_t i = 0; i < ANGLE_COUNT && !aborted; ++i) {
            const float angle_rad = angles_deg[i] * (3.14159265358979323846f / 180.0f);
            PWM_SetVoltageAngle(angle_rad, mod_idx);
            HAL_Delay(pulse_ms);

            float iu = 0.0f, iv = 0.0f, iw = 0.0f;
            (void)phaseCurrentADC().sample(iu, iv, iw);
            const float enc_deg = encoderADC().lastAngle();
            const float vdc = dcLinkVoltageSensor().voltage();

            Telemetry::printf("[SHELL] vectorscan angle=%.0f deg | iu=%+.2f iv=%+.2f iw=%+.2f | enc=%.1f | vdc=%.1f",
                              static_cast<double>(angles_deg[i]),
                              static_cast<double>(iu),
                              static_cast<double>(iv),
                              static_cast<double>(iw),
                              static_cast<double>(enc_deg),
                              static_cast<double>(vdc));

            if (max_a > 0.0f) {
                float peak = std::fabs(iu);
                if (std::fabs(iv) > peak) peak = std::fabs(iv);
                if (std::fabs(iw) > peak) peak = std::fabs(iw);
                if (peak > max_a) {
                    Telemetry::printf("[SHELL] vectorscan ABORT: current %.1f A exceeded limit %.1f A",
                                      static_cast<double>(peak),
                                      static_cast<double>(max_a));
                    aborted = true;
                }
            }
        }

        PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);
        GateDriver_DisableOutputs();
        Telemetry::printf("[SHELL] vectorscan done%s", aborted ? " (aborted)" : "");
    }
};

static StartCommand    sStartCmd;
static StopCommand     sStopCmd;
static FreqCommand     sFreqCmd;
static ModCommand      sModCmd;
static SwFreqCommand   sSwFreqCmd;
static StatusCommand   sStatusCmd;
static RampCurrentLimitCommand sRampCurrentLimitCmd;
static VectorScanCommand sVectorScanCmd;

#include "Inverter/Command/CommandManager.h"

void registerOpenLoopCommands(CommandManager& mgr) {
    mgr.registerCommand(&sStartCmd);
    mgr.registerCommand(&sStopCmd);
    mgr.registerCommand(&sFreqCmd);
    mgr.registerCommand(&sModCmd);
    mgr.registerCommand(&sSwFreqCmd);
    mgr.registerCommand(&sStatusCmd);
    mgr.registerCommand(&sRampCurrentLimitCmd);
    mgr.registerCommand(&sVectorScanCmd);
}
