#include "Inverter/Command/CommandInterface.h"
#include "Inverter/Command/CommandContext.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
#include "Inverter/Drivers/Logging/SupplyMonitor.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include "pwm.h"
#include "gate_driver.h"

using Inverter::OpenLoopController;
using Inverter::FaultManager;
using Inverter::PhaseCurrentADC;
using Inverter::DcLinkVoltageSensor;
using Inverter::MAX22530;
using Inverter::phaseCurrentADC;
using Inverter::dcLinkVoltageSensor;
using Inverter::openLoopController;
using Inverter::supplyMonitorPrintStatus;

class ClearFaultCommand : public CommandInterface {
public:
    ClearFaultCommand() : CommandInterface("clearfault", "Clear latched faults without starting switching") {}

    void execute(const ArgValue*, CommandContext&) override {
        /* Re-enable gate-driver power so the board can be started again. */
        GateDriver_EnablePower(true);
        HAL_Delay(50);

        /* Remember whether the gate-driver outputs were enabled before the clear
         * so we can restore that state afterwards. */
        const bool outputs_were_enabled =
            (HAL_GPIO_ReadPin(GATE_DRIVER_RESET_GPIO_Port, GATE_DRIVER_RESET_Pin) == GPIO_PIN_SET);

        /* Assert reset to clear the NCD57100 DESAT fault latch, then release it
         * so /RDY and /FLT can be read. */
        GateDriver_DisableOutputs();
        HAL_Delay(10);
        GateDriver_EnableOutputs();
        HAL_Delay(10);

        /* Clear the timer break flag, but do NOT re-enable MOE here. */
        PWM_ClearBreakFlag();
        FaultManager::instance().clearAll();

        /* Clear any latched MAX22530 interrupt/filter and pending EXTI1. */
        MAX22530& adc = dcLinkVoltageSensor().adc();
        (void)adc.clearInterruptStatus();
        (void)adc.clearFilter(0);
        __HAL_GPIO_EXTI_CLEAR_IT(VSENSE_ISO_ADC_INTERRUPT_Pin);
        HAL_NVIC_ClearPendingIRQ(EXTI1_IRQn);

        /* Restore the previous gate-driver output state. */
        if (!outputs_were_enabled) {
            GateDriver_DisableOutputs();
        }

        bool ready = GateDriver_IsReady();
        bool fault = GateDriver_IsFault();
        uint32_t bdtr = TIM1->BDTR;
        Telemetry::printf("[SHELL] clearfault done | ready=%s fault=%s MOE=%lu outputs=%s",
                          ready ? "Y" : "N",
                          fault ? "Y" : "N",
                          (bdtr >> 15) & 1UL,
                          outputs_were_enabled ? "Y" : "N");
    }
};

class CalCommand : public CommandInterface {
public:
    CalCommand() : CommandInterface("cal", "Recalibrate phase-current ADC offsets") {}

    void execute(const ArgValue*, CommandContext&) override {
        if (openLoopController().isRunning()) {
            Telemetry::printf("[SHELL] stop motor before calibrating");
            return;
        }

        /* Use the same safe offset path as auto-cal: gate driver in reset,
         * PWM parked at 50 %, then capture. */
        if (!openLoopController().isInitialized()) {
            Telemetry::printf("[SHELL] running init first");
            if (!openLoopController().init()) {
                Telemetry::printf("[SHELL] init failed");
                return;
            }
        }

        if (openLoopController().recalibrateOffsets()) {
            PhaseCurrentADC& adc = phaseCurrentADC();
            Telemetry::printf("[SHELL] calibrated offsets U=%.3f V=%.3f",
                              static_cast<double>(adc.lastOffsetU()),
                              static_cast<double>(adc.lastOffsetV()));
        } else {
            Telemetry::printf("[SHELL] calibration failed");
        }
    }
};

class RawCommand : public CommandInterface {
public:
    RawCommand() : CommandInterface("raw", "Print raw phase-current ADC values") {}

    void execute(const ArgValue*, CommandContext&) override {
        PhaseCurrentADC& adc = phaseCurrentADC();
        const int32_t u_diff = static_cast<int32_t>(adc.lastRawUSig()) -
                               static_cast<int32_t>(adc.lastRawURef());
        const int32_t v_diff = static_cast<int32_t>(adc.lastRawVSig()) -
                               static_cast<int32_t>(adc.lastRawVRef());
        Telemetry::printf("[SHELL] raw U sig=%lu ref=%lu diff=%ld",
                          adc.lastRawUSig(), adc.lastRawURef(), u_diff);
        Telemetry::printf("[SHELL] raw V sig=%lu ref=%lu diff=%ld",
                          adc.lastRawVSig(), adc.lastRawVRef(), v_diff);
        Telemetry::printf("[SHELL] offsets U=%.3f V=%.3f",
                          static_cast<double>(adc.lastOffsetU()),
                          static_cast<double>(adc.lastOffsetV()));
    }
};

class VZeroCommand : public CommandInterface {
public:
    VZeroCommand() : CommandInterface("vzero", "Zero-calibrate DC-link voltage sensor") {}

    void execute(const ArgValue*, CommandContext&) override {
        DcLinkVoltageSensor& vdc = dcLinkVoltageSensor();
        if (vdc.zeroCalibrate()) {
            Telemetry::printf("[SHELL] Vdc zero calibrated: %.3f V", static_cast<double>(vdc.voltage()));
        } else {
            Telemetry::printf("[SHELL] Vdc zero cal failed (no sample)");
        }
    }
};

class SupplyStatusCommand : public CommandInterface {
public:
    SupplyStatusCommand() : CommandInterface("supply_status", "Print supply monitor status") {}

    void execute(const ArgValue*, CommandContext&) override {
        supplyMonitorPrintStatus();
    }
};

class RebootCommand : public CommandInterface {
public:
    RebootCommand() : CommandInterface("reboot", "Reboot the system") {}

    void execute(const ArgValue*, CommandContext&) override {
        Telemetry::printf("[SHELL] Rebooting...");
        HAL_NVIC_SystemReset();
    }
};

static ClearFaultCommand   sClearFaultCmd;
static CalCommand          sCalCmd;
static RawCommand          sRawCmd;
static VZeroCommand        sVZeroCmd;
static SupplyStatusCommand sSupplyStatusCmd;
static RebootCommand       sRebootCmd;

#include "Inverter/Command/CommandManager.h"

void registerSystemCommands(CommandManager& mgr) {
    mgr.registerCommand(&sClearFaultCmd);
    mgr.registerCommand(&sCalCmd);
    mgr.registerCommand(&sRawCmd);
    mgr.registerCommand(&sVZeroCmd);
    mgr.registerCommand(&sSupplyStatusCmd);
    mgr.registerCommand(&sRebootCmd);
}
