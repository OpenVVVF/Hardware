#include <vector>

#include "Command/CommandContext.h"
#include "Command/CommandInitializer.h"
#include "Command/CommandManager.h"
#include "Command/SerialProcessor.h"
#include "Hardware.h"
#include "Sensors/MAX2253x.h"
#include "Sensors/MeasurementSystem.h"
#include "Switching/CalibrationManager.h"
#include "Switching/Control/Schemas/FOC.h"
#include "Switching/Control/Schemas/VHz.h"
#include "Switching/DriveManager.h"
#include "Switching/HWInterface/PWMDriver.h"
#include "Switching/Modulation/ModulationSelector.h"
#include "Switching/Modulation/Schemas/NPulse.h"
#include "Switching/Modulation/Schemas/RCFSPWM.h"
#include "Switching/Modulation/Schemas/SPWM.h"
#include "Switching/Modulation/Schemas/SVPWM.h"
#include "Switching/Motion/CurrentController.h"
#include "Telemetry.h"
#include "Utils/Fault/FaultManager.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"  // __dmb(), __sev(), __wfe()
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

// ----------------------------------------------------------------------
// Inter-Core Communication Structures
// ----------------------------------------------------------------------

// Core 0 -> Core 1: Just send raw torque targets!
// ThreadSafeQueue<CurrentCommand> tx_queue;
SerialProcessor serial_proc;

// ----------------------------------------------------------------------
// Global State (Replaces RtBridge Instance State)
// ----------------------------------------------------------------------
namespace {
// We instantiate the specific controllers here so they persist in memory
FocController g_Foc;
// VHzController g_Vhz;

FaultManager g_FaultManager;

// The shared motor config that the ControlSelector will distribute
MotorConfig g_MotorConfig;

PWMDriver* g_Driver = nullptr;
float g_RampRate = 5.0f;
float g_LastCarrierHz = 0.0f;
bool g_LastSyncMode = false;
uint16_t g_LastPulses = 0;

// Measurement system instances
MAX2253x_MultiADC* adc_system = nullptr;
MeasurementSystem* measurements = nullptr;
}  // namespace

/**
 * @file    core1_main.cpp
 * @brief   System entry for high-speed motor control execution.
 */
void core1_entry() {
    // ===========================================================================
    // 1. SYSTEM SETUP
    // ===========================================================================
    static DriveManager g_DriveManager;
    static CalibrationManager g_CalibrationManager;

    static CurrentController g_CurrentController;

    g_CurrentController.SetMotorConfig(g_MotorConfig);

    // A. Configure Modulation (Transitions from SPWM to NPulse at 4Hz)
    // static SVPWMModulationScheme g_Svm;
    // SVPWMConfig svmCfg;
    // svmCfg.InfluenceStart_Hz_ = 0.0f;
    // svmCfg.InfluenceEnd_Hz_   = 5.0f;
    // svmCfg.CarrierStart_Hz_   = 1000.0f;
    // svmCfg.CarrierEnd_Hz_     = 1000.0f;
    // svmCfg.MaxModulationIndex_ = 0.95f;
    // g_Svm.ApplyConfig(svmCfg);
    // g_DriveManager.RegisterModulationScheme(&g_Svm);

    // static SVPWMModulationScheme g_Svm2;
    // SVPWMConfig svmCfg2;
    // svmCfg2.InfluenceStart_Hz_ = 4.0f;
    // svmCfg2.InfluenceEnd_Hz_   = 25.0f;
    // svmCfg2.CarrierStart_Hz_   = 3000.0f;
    // svmCfg2.CarrierEnd_Hz_     = 5000.0f;
    // svmCfg2.MaxModulationIndex_ = 0.95f;
    // g_Svm2.ApplyConfig(svmCfg2);
    // g_DriveManager.RegisterModulationScheme(&g_Svm2);

    static SVPWMModulationScheme g_Svm3;
    SVPWMConfig svmCfg3;
    svmCfg3.InfluenceStart_Hz_ = 0.0f;
    svmCfg3.InfluenceEnd_Hz_ = 150.0f;
    svmCfg3.CarrierStart_Hz_ = 5000.0f;
    svmCfg3.CarrierEnd_Hz_ = 5000.0f;
    svmCfg3.MaxModulationIndex_ = 0.95f;
    g_Svm3.ApplyConfig(svmCfg3);
    g_DriveManager.RegisterModulationScheme(&g_Svm3);

    // B. Configure Control (FOC tuning)
    FocConfig focCfg;
    focCfg.InfluenceStart_RadPerSec_ = 0.0f;
    focCfg.InfluenceEnd_RadPerSec_ = 10000.0f;
    focCfg._Kp_Q_axis = 0.03f;
    focCfg._Ki_Q_axis = 10.0f;
    focCfg._Kp_D_axis = 0.03f;
    focCfg._Ki_D_axis = 10.0f;
    focCfg._SoftVoltageLimit_V = 8.0f;
    g_Foc.ApplyConfig(focCfg);

    // C. Register Components with Manager
    g_DriveManager.SetMotionController(&g_CurrentController);
    g_DriveManager.SetControlScheme(&g_Foc);

    // D. Set Decimation (Outer loop runs 10x slower)
    g_DriveManager.SetMotionUpdateRatio(10);

    // ===========================================================================
    // 2. RUNTIME EXECUTION
    // ===========================================================================
    absolute_time_t next_foc_tick = get_absolute_time();
    absolute_time_t old_t = get_absolute_time();
    absolute_time_t prev_tel_time = get_absolute_time();

    float theta_est = 0.0f;
    float omega_est = 0.0f;
    const float Kp_pll = 200.0f;
    const float Ki_pll = 2000.0f;

    CurrentSetpoint target;
    g_CalibrationManager.SetMode(CalibrationManager::CalibrationMode::ENCODER_OFFSET);
    while (true) {
        if (measurements->update_from_dma()) {
            absolute_time_t currentTime = get_absolute_time();
            float dt_S = (float)absolute_time_diff_us(old_t, currentTime) / 1000000.0f;
            old_t = currentTime;

            SensorData SenseData;
            SenseData._Idc_A = measurements->read("I_DC_MAIN");
            SenseData._DcBusVoltage_V = measurements->read("V_DC_BUS");
            SenseData._Iu_A = measurements->read("I_PH_U");
            SenseData._Iw_A = measurements->read("I_PH_W");
            SenseData._Iv_A = -(SenseData._Iu_A + SenseData._Iw_A);

              // 1. Keep the PLL running exactly as it is so 'omega_est' stays smooth
            float raw_adc_rad = measurements->getRotorPositionDegrees() * 0.01745329251f;
            float error = raw_adc_rad - theta_est;
            while (error > 3.14159265f) error -= 6.283185307f;
            while (error < -3.14159265f) error += 6.283185307f;

            omega_est += Ki_pll * error * dt_S;
            theta_est += (omega_est + Kp_pll * error) * dt_S;
            while (theta_est >= 6.283185307f) theta_est -= 6.283185307f;
            while (theta_est < 0.0f) theta_est += 6.283185307f;

            // 2. THE FIX: Feed the raw, zero-latency angle to the FOC loop
            // Advance by ~500us to account for standard ADC -> Compute -> PWM transport delay
            float phase_advance_rad = omega_est * 0.0005f; 
            SenseData._EncoderPosition_Rad = raw_adc_rad + phase_advance_rad;
            SenseData._EncoderVelocity_RadPerSec = omega_est;

            if (g_Driver && !g_Driver->isEmergencyStopped() && g_Driver->isEnabled()) {
                // 1. Define High-Level Setpoint
                // If sine is >= 0, set to 60. If sine is < 0, set to -60.
                target._TargetIq_A = -10.0f;//* sin(get_absolute_time() / 4'00'000.0f);// ? 60.0f : -60.0f;
                target._TargetId_A = 0.0f;
            //    bool Result = g_DriveManager.Update(&g_FaultManager, &g_MotorConfig, g_Driver, SenseData, target, dt_S);
                g_CalibrationManager.Update(&g_FaultManager, &g_MotorConfig, g_Driver, SenseData, dt_S);
            }
            if (absolute_time_diff_us(prev_tel_time, get_absolute_time()) >= 1000) {
                prev_tel_time = get_absolute_time();
                if(g_CalibrationManager.GetMode() == CalibrationManager::CalibrationMode::IDLE) {
                    Telemetry::log("DEBUG_SEX", g_CalibrationManager.GetEncoderOffset_Rad());
                }
                Telemetry::log("CORE1_LOOP_HZ", 1.0f / dt_S);
                Telemetry::log("ELEC_ANGLE", g_Foc._ElectricalAngle_Rad);
                Telemetry::log("ENC_OFFSET", g_Foc._EncoderOffset_Rad);
                Telemetry::log("DEBUG_VQ", g_Foc._Vq_V);
                Telemetry::log("DEBUG_VD", g_Foc._Vd_V);
                Telemetry::log("IQ_SET_PT", target._TargetIq_A);
                Telemetry::log("DEBUG_I_ALPHA", g_Foc.i_alpha);
                Telemetry::log("DC_BUS_POWER", measurements->read("V_DC_BUS") * measurements->read("I_DC_MAIN"));
                Telemetry::log("DEBUG_I_BETA", g_Foc.i_beta);
                Telemetry::log("DEBUG_I_D", g_Foc.i_d);
                Telemetry::log("DEBUG_I_Q", g_Foc.i_q);
                Telemetry::log("ROTOR_VELOCITY", omega_est * 9.55);
                Telemetry::log("ROTOR_DEG", SenseData._EncoderPosition_Rad);
            }
        }
    }
}
void updateTel() {
    serial_proc.poll();
    Telemetry::updateSensors();
}
// ----------------------------------------------------------------------
// Main Application
// ----------------------------------------------------------------------
int main() {
    g_FaultManager.Init();

    stdio_init_all();
    sleep_ms(500);

    // 1. Initialize Switching & FOC
    PWMDriver::Config driverCfg;
    driverCfg.min_duty_percent = 1.0f;
    driverCfg.max_duty_percent = 99.0f;

    static PWMDriver driver(driverCfg);
    g_Driver = &driver;
    g_Driver->init(2000.0f);
    g_Driver->enable();


    CommandContext ctx{};

    CommandManager::instance().setContext(ctx);
    initializeCommands();

    // 3. Initialize Sensors
    static MAX2253x_MultiADC adc_instance({13, 14, 15, 22});
    adc_system = &adc_instance;

    if (!adc_system->init()) {
        Telemetry::printf("FATAL: ADC initialization failed!\n");
        return -1;
    }

    static MeasurementSystem ms_instance(*adc_system);
    measurements = &ms_instance;
    const std::vector<ChannelConfig> channel_map = {
        {0, 0, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_PH_W", 0.0f},
        {0, 1, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_PH_V", 0.0f},
        {0, 2, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_PH_U", 0.0f},
        {0, 3, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_DC_BUS", 0.0f},
        {2, 2, SensorType::DIRECT, 1.0f, 0.0f, 1.0f, "ENCODER_SIN", 0.0f},
        {2, 1, SensorType::DIRECT, 1.0f, 0.0f, 1.0f, "ENCODER_COS", 0.0f},
        {1, 3, SensorType::BIPOLAR_CURRENT, -1204.8193f, 0.0f, 1.0f, "I_DC_MAIN", 0.410f},
        {1, 1, SensorType::BIPOLAR_CURRENT, -1204.8193f, 0.0f, 1.0f, "I_PH_U", 0.410f},
        {1, 0, SensorType::BIPOLAR_CURRENT, 1204.8193f, 0.0f, 1.0f, "I_PH_W", 0.410f}};

    measurements->addChannels(channel_map);
    measurements->update();
    // measurements->printChannels();
    measurements->calibrateCurrentSensors();

    g_FaultManager.Update();

    // 4. Configure Global Motor Limits
    g_MotorConfig._PolePairs_unitless = 5;
    g_MotorConfig._Ld_Henry = 0.000040f;
    g_MotorConfig._Lq_Henry = 0.000040f;
    g_MotorConfig._FluxLinkage_Wb = 0.0;  // seems abt right from ~523.6 rads/sec @ 12v

    // Motor config limits
    g_MotorConfig._SoftMaxPhaseCurrent_A = 40.0f;
    g_MotorConfig._HardMaxPhaseCurrent_A = 350.0f;

    g_MotorConfig._HardMaxDcBusCurrent_A = 100.0f;
    g_MotorConfig._SoftMaxDcBusCurrent_A = 10.0f;

    g_MotorConfig._HardMaxRegenCurrent_A = 10.0f;
    g_MotorConfig._SoftMaxRegenCurrent_A = 0.0f;

    g_MotorConfig._SoftMaxVelocity_RPM = 1020.0f;
    g_MotorConfig._HardMaxVelocity_RPM = 5000.0f;
    g_MotorConfig._SoftMinVelocity_RPM = -1200.0f;
    g_MotorConfig._HardMinVelocity_RPM = -5000.0f;

    g_MotorConfig._MaxModulation_unitless = 0.9f;

    // Distribute motor config to all controllers
    g_Foc.SetMotorConfig(g_MotorConfig);
    // g_Vhz.SetMotorConfig(g_MotorConfig);

    // 5. Initialize Telemetry
    Telemetry::set_period_us(10000);  // 100 Hz (it's not lmao...)
    Telemetry::init();
    Telemetry::bindMeasurementSystem(*measurements);
    updateTel();

    // --- NEW: Initialize the DMA ---
    adc_system->init_dma();

    // adc_system->set_filtered_read(false);

    // --- NEW: Attach the DMA trigger to the PWM Driver ---
    g_Driver->setPwmWrapCallback([]() {
        auto* adc = MAX2253x_MultiADC::instance;
        if (!adc) return;
        // Only start a new read if the previous one has been processed/cleared.
        if (!adc->is_async_ready()) {
            adc->start_async_read();
        }
    });

    // g_Foc._EncoderOffset_Rad = 3.8f; // WE KNOW THIS IS BEST FOR NOW USE CAL DURING MOTOR DETECTION, THEN STORE THOSE VALUES AND LOAD THEM WHEN NEEDED!!!!
    // g_Foc._EncoderOffset_Rad = 6.94159f;

    // -> LAUNCH CORE 1 <-
    multicore_launch_core1(core1_entry);

    absolute_time_t last_print = get_absolute_time();
    Telemetry::printf("Finished Booting");

    // Initialize our non-blocking timer tracker
    uint64_t lastFaultPrintTime_uS = time_us_64();
    int delay = 0;
    while (true) {
        // printf("fdsafdaf\n");
        // (Optional: You can eventually put simple serial string parsing here
        // to push new CurrentCommands to tx_queue. Example:)
        // if (serial_read == "set iq 5.0") { tx_queue.push({0.0f, 5.0f}); }

        // 1. Drain the telemetry queue from Core 1
        updateTel();
        // TODO: hardcode encoder offset here
        g_Foc._EncoderOffset_Rad = ctx.encoderOffset;

        // 2. Print active faults once per second (1,000,000 microseconds)
        uint64_t currentTime_uS = time_us_64();
        if ((currentTime_uS - lastFaultPrintTime_uS) >= 1000000) {
            lastFaultPrintTime_uS = currentTime_uS;  // Reset the timer

            // Fetch the list of active faults
            FaultRecord currentFaults[MAX_ACTIVE_FAULTS];
            uint8_t faultCount = g_FaultManager.GetActiveFaults(currentFaults, MAX_ACTIVE_FAULTS);

            if (faultCount > 0) {
                Telemetry::printf("--- Active Faults (%d) ---", faultCount);
                for (uint8_t i = 0; i < faultCount; i++) {
                    Telemetry::printf("  [%d] %s", i + 1, currentFaults[i].Description);
                }
                Telemetry::printf("--------------------------");
            } else {
                // Optional: A simple heartbeat so you know the loop is alive
                // Telemetry::printf("System Healthy - No Active Faults");
            }
        }
    }
}
