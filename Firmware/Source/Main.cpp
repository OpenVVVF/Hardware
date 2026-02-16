#include <vector>
#include "pico/stdlib.h"
#include "Switching/CommutationManager.h"
#include "Command/CommandContext.h"
#include "Command/CommandManager.h"
#include "Command/SerialProcessor.h"
#include "Command/CommandInitializer.h"
#include "Sensors/MAX2253x.h"
#include "Sensors/MeasurementSystem.h"
#include "Telemetry.h"
#include "Hardware.h"

// Bring in FOC and Switching headers that used to be hidden in RtBridge
#include "Switching/FOC.h"
#include "Switching/PWMDriver.h"
#include "Switching/Modulation.h"

// ----------------------------------------------------------------------
// Global State (Replaces RtBridge Instance State)
// ----------------------------------------------------------------------
namespace {
    CommutationManager zone_mgr;
    FocController g_Foc;
    PWMDriver* g_Driver = nullptr;
    SPWMStrategy g_SpwmStrategy;

    float g_RampRate = 5.0f;
    float g_ManualCarrierHz = 2000.0f;
    bool  g_ManualCarrierMode = false;

    float    g_LastCarrierHz = 0.0f;
    bool     g_LastSyncMode  = false;
    uint16_t g_LastPulses    = 0;

    // Measurement system instances
    MAX2253x_MultiADC* adc_system = nullptr;
    MeasurementSystem* measurements = nullptr;

    void updateCarrierFromZones() {
        if (!g_Driver || !g_Driver->isEnabled()) return;

        if (g_ManualCarrierMode) {
            if (g_LastCarrierHz != g_ManualCarrierHz || g_LastSyncMode != false) {
                g_Driver->setCarrierFrequency(g_ManualCarrierHz);
                g_Driver->setSynchronousMode(false, 0);
                g_LastCarrierHz = g_ManualCarrierHz;
                g_LastSyncMode = false;
                g_LastPulses = 0;
            }
            return;
        }

        float CurrentFreq = g_Driver->getCurrentFrequency();
        ZoneConfig Zone {};
        float SyncPulsesF = 0.0f;

        if (zone_mgr.getZone(CurrentFreq, &Zone)) {
            float Carrier = zone_mgr.calculateCarrier(CurrentFreq, &Zone, &SyncPulsesF);
            bool SyncMode = (Zone.type == ZoneType::SYNC);
            uint16_t Pulses = SyncMode ? (uint16_t)SyncPulsesF : 0;

            if (g_LastCarrierHz != Carrier || g_LastSyncMode != SyncMode || g_LastPulses != Pulses) {
                g_Driver->setCarrierFrequency(Carrier);
                g_Driver->setSynchronousMode(SyncMode, Pulses);
                g_LastCarrierHz = Carrier;
                g_LastSyncMode = SyncMode;
                g_LastPulses = Pulses;
            }
        }
    }

    void configureZones() {
        zone_mgr.clearZones();
        zone_mgr.addAsyncFixed(0.0f, 2000.0f, 4000.0f);
        // zone_mgr.addRCFM(0, 2000, 1200, 200);
    }
}

// ----------------------------------------------------------------------
// Main Application
// ----------------------------------------------------------------------
int main() {
    stdio_init_all();
    sleep_ms(500);


    // 1. Initialize Switching & FOC
    PWMDriver::Config driverCfg;
    driverCfg.min_duty_percent = 1.0f;
    driverCfg.max_duty_percent = 99.0f;

    static PWMDriver driver(driverCfg);
    g_Driver = &driver;
    g_Driver->setStrategy(&g_SpwmStrategy);
    g_Driver->setAutoModulation(true);
    g_Driver->init(2000.0f);
    g_Driver->enable();
    // Don't auto-enable here, let the command context handle it when ready

    configureZones();

    // 2. Setup Command Context (Using captureless lambdas to cast to C function pointers)
    CommandContext ctx{};
    ctx.zone_mgr = &zone_mgr;
    ctx.set_ramp_rate = [](float val) { g_RampRate = val; };
    ctx.set_manual_carrier_hz = [](float val) { g_ManualCarrierHz = val; };
    ctx.set_manual_carrier_mode = [](bool val) { g_ManualCarrierMode = val; };
    ctx.enable = []() { if (g_Driver) g_Driver->enable(); };
    ctx.disable = []() { if (g_Driver) g_Driver->disable(); };
    ctx.emergency_stop = []() { if (g_Driver) g_Driver->emergencyStop(); };
    ctx.clear_emergency_stop = []() { if (g_Driver) g_Driver->clearEmergency(); };
    ctx.set_target_frequency = [](float val) { if (g_Driver) g_Driver->setTargetFrequency(val, g_RampRate); };
    ctx.set_frequency_immediate = [](float val) { if (g_Driver) g_Driver->setFrequencyImmediate(val); };
    
    // Status polling callback bypasses the seqlock now
    ctx.try_get_status = [](RtStatus* st) -> bool {
        if (!g_Driver) return false;
        st->enabled = g_Driver->isEnabled();
        st->estop = g_Driver->isEmergencyStopped();
        st->current_freq = g_Driver->getCurrentFrequency();
        st->modulation_index = g_Driver->getModulationIndex();
        st->carrier_hz = g_Driver->getCarrierFrequency();
        st->sync_mode = g_Driver->isSynchronousMode();
        st->pulses = g_Driver->getPulsesPerCycle();
        st->manual_carrier_mode = g_ManualCarrierMode;
        st->manual_carrier_hz = g_ManualCarrierHz;
        st->ramp_rate = g_RampRate;
        st->debug_Vd = g_Foc._Vd_V;
        st->debug_Vq = g_Foc._Vq_V;
        st->debug_Iq_measured = g_Foc._Iq_A;
        st->debug_angle_elec = g_Foc._ElectricalAngle_Rad;
        return true;
    };

    CommandManager::instance().setContext(ctx);
    initializeCommands();
    SerialProcessor serial_proc;

    // 3. Initialize Sensors
    static MAX2253x_MultiADC adc_instance({13, 14, 15, 22});
    adc_system = &adc_instance;

    if (!adc_system->init()) {
        printf("FATAL: ADC initialization failed!\n");
        return -1;
    }

    static MeasurementSystem ms_instance(*adc_system);
    measurements = &ms_instance;
    const std::vector<ChannelConfig> channel_map = {
        {0, 0, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 0.1f, "V_PH_W", 0.0f},
        {0, 1, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 0.1f, "V_PH_V", 0.0f},
        {0, 2, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 0.1f, "V_PH_U", 0.0f},
        {0, 3, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_DC_BUS", 0.0f},
        {2, 2, SensorType::DIRECT, 1.0f, 0.0f, 1.0f, "ENCODER_SIN", 0.0f},
        {2, 1, SensorType::DIRECT, 1.0f, 0.0f, 1.0f, "ENCODER_COS", 0.0f},
        {1, 3, SensorType::BIPOLAR_CURRENT, -1204.8193f, 0.0f, 1.0f, "I_DC_MAIN", 0.410f},
        {1, 1, SensorType::BIPOLAR_CURRENT, 1204.8193f, 0.0f, 1.0f, "I_PH_U", 0.410f},
        {1, 0, SensorType::BIPOLAR_CURRENT, 1204.8193f, 0.0f, 1.0f, "I_PH_W", 0.410f}
    };

    measurements->addChannels(channel_map);
    measurements->update();
    measurements->printChannels();

    printf("\nCalibrating current sensors...\n");
    measurements->calibrateCurrentSensors();
    printf("Current sensor calibration complete.\n\n");

    // 4. Configure FOC
    MotorConfig C; 
    C._PolePairs_unitless = 6;
    C._Ld_Henry = 0.000040f;
    C._Lq_Henry = 0.000040f;
    C._DcBusVoltage_V = 60.0f;
    C._MaxDcBusCurrent_A = 10.0f;
    C._MaxPhaseCurrent_A = 50.0f;
    C._MaxModulation_unitless = 0.9f;
    C._FluxLinkage_Wb = 0.0f;
    g_Foc.SetMotorConfig(C);

    CurrentCommand CurrentCmd;
    CurrentCmd._IdCmd_A = 0.0f;
    CurrentCmd._IqCmd_A = 1.0f;
    g_Foc.ApplyCurrentLimits(CurrentCmd);

    // 5. Initialize Telemetry
    Telemetry::set_period_us(10000); // 100 Hz
    Telemetry::init(); 
    Telemetry::bindMeasurementSystem(*measurements);

    // 6. Timing Variables for the Main Loop
    absolute_time_t last_print = get_absolute_time();
    absolute_time_t next_foc_tick = get_absolute_time(); // Track 1kHz control loop

    // ----------------------------------------------------------------------
    // Unified Run Loop
    // ----------------------------------------------------------------------
    while (true) {
        // Fast asynchronous updates
        measurements->update();
        Telemetry::updateSensors();
        Telemetry::log("ROTOR_DEG", measurements->getRotorPositionDegrees());
        serial_proc.poll();

        // ------------------------------------------------------------------
        // ~1kHz FOC Control Tick (Replaces Core 1 Loop)
        // ------------------------------------------------------------------
        if (absolute_time_diff_us(get_absolute_time(), next_foc_tick) <= 0) {
            next_foc_tick = delayed_by_us(next_foc_tick, 1000); // Reset timeout for exactly 1ms from last

            // 1. Gather Sensor Data
            SensorData SenseData;
            SenseData._Idc_A = measurements->read("I_DC_MAIN");
            SenseData._Iu_A  = measurements->read("I_PH_U");
            SenseData._Iw_A  = measurements->read("I_PH_W");
            SenseData._Iv_A  = -(SenseData._Iu_A + SenseData._Iw_A);
            SenseData._DcBusVoltage_V = measurements->read("V_DC_BUS");
            SenseData._EncoderPosition_Rad = measurements->getRotorPositionDegrees() * 0.01745329251f;
            SenseData._EncoderVelocity_RadPerSec = measurements->getRotorOmegaMechanicalRadPerSec(0.001f);
            


            // Keep state between 1kHz ticks
            static float smoothed_angle_rad = 0.0f;
            static float filtered_velocity = 0.0f;

            float raw_adc_rad = measurements->getRotorPositionDegrees() * 0.01745329251f;
            float raw_velocity = SenseData._EncoderVelocity_RadPerSec;

            // 1. Heavily filter the spiky velocity 
            // (Mechanical inertia means actual speed doesn't change instantly)
            filtered_velocity = (0.02f * raw_velocity) + (0.98f * filtered_velocity); 

            // 2. Predict the new angle using the SMOOTH velocity
            smoothed_angle_rad += filtered_velocity * 0.001f;

            // 3. Find the error between our prediction and the raw ADC reading
            float angle_err = raw_adc_rad - smoothed_angle_rad;

            // Wrap error to -PI to PI so it always takes the shortest path
            while (angle_err > 3.14159265f) angle_err -= 6.283185307f;
            while (angle_err < -3.14159265f) angle_err += 6.283185307f;

            // 4. Gently pull the prediction toward the true sensor reading (PLL behavior)
            // A gain of 0.05 acts like a spring pulling it to reality without tracking the noise
            smoothed_angle_rad += angle_err * 0.05f; 

            // 5. Wrap the final output to 0 to 2PI
            while (smoothed_angle_rad >= 6.283185307f) smoothed_angle_rad -= 6.283185307f;
            while (smoothed_angle_rad < 0.0f) smoothed_angle_rad += 6.283185307f;

            // Feed the clean data to FOC
            SenseData._EncoderPosition_Rad = smoothed_angle_rad;
            SenseData._EncoderVelocity_RadPerSec = filtered_velocity; // Feeds decouple terms too!


            // Pass the smoothly extrapolated angle to the FOC controller
            // SenseData._EncoderPosition_Rad = smoothed_angle_rad;
            
            Telemetry::log("SMOOTH_DEG", smoothed_angle_rad * 57.29577f);


            g_Foc.UpdateSensors(SenseData);

            // 2. Execute Modulation if Active
            if (g_Driver && !g_Driver->isEmergencyStopped() && g_Driver->isEnabled()) {
                FocOutput FOC_Out = g_Foc.UpdateVoltages();
                
                PhaseVoltages TargetDutyCycles;
                GenerateSpwm(FOC_Out, 0.95f, TargetDutyCycles);
                 
                g_Driver->setDutyCycles(TargetDutyCycles._Du_unitless, TargetDutyCycles._Dv_unitless, TargetDutyCycles._Dw_unitless);
                updateCarrierFromZones();
            }
        }


        RtStatus st{};
        const bool have = (ctx.try_get_status && ctx.try_get_status(&st));

        Telemetry::log("DEBUG_VQ", st.debug_Vq);
        Telemetry::log("DEBUG_VD", st.debug_Vd);
        Telemetry::log("DEBUG_AngleElec", st.debug_angle_elec);
        Telemetry::log("DEBUG_IQ_MEASURED", st.debug_Iq_measured);

        // ------------------------------------------------------------------
        // 1Hz Console Status Prints
        // ------------------------------------------------------------------
        if (absolute_time_diff_us(last_print, get_absolute_time()) > 1000000) {



            if (!have) {
                printf("RT STATUS: unavailable\r\n");
            } else if (st.estop) {
                printf("EMERGENCY STOP ACTIVE\r\n");
            } else if (!st.enabled) {
                printf("IDLE: Gates LOW, freq=0\r\n");
            } else {
                if (st.manual_carrier_mode) {
                    printf("MANUAL CARRIER F:%6.2fHz Mod:%3.0f%% Car:%4.0fHz [AUTO OFF]\r\n",
                           st.current_freq, st.modulation_index * 100.0f, st.carrier_hz);
                } else {
                    ZoneConfig zone{};
                    const char* zone_str = "DEF";
                    if (zone_mgr.getZone(st.current_freq, &zone)) {
                        zone_str = zoneTypeToStr(zone.type);
                    }

                    printf("%s-%s F:%6.2fHz Mod:%3.0f%% n:%2u Car:%4.0fHz\r\n",
                           zone_str, st.sync_mode ? "SYNC" : "ASYNC",
                           st.current_freq, st.modulation_index * 100.0f,
                           (unsigned)st.pulses, st.carrier_hz);
                }
            }
            last_print = get_absolute_time();
        }
    }
}