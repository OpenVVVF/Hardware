#include <vector>

#include "Command/CommandContext.h"
#include "Command/CommandInitializer.h"
#include "Command/CommandManager.h"
#include "Command/SerialProcessor.h"
#include "Hardware.h"
#include "Sensors/MAX2253x.h"
#include "Sensors/MeasurementSystem.h"
#include "Telemetry.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "Switching/Control/Schemas/FOC.h"
#include "Switching/Control/Schemas/VHz.h"

#include "Switching/HWInterface/PWMDriver.h"
#include "hardware/sync.h"  // __dmb(), __sev(), __wfe()
#include "pico/multicore.h"

#include "Switching/Modulation/ModulationSelector.h"
#include "Switching/Modulation/Schemas/SPWM.h"
#include "Switching/Modulation/Schemas/SVPWM.h"
#include "Switching/Modulation/Schemas/RCFSPWM.h"
#include "Switching/Modulation/Schemas/NPulse.h"

#include "Switching/Motion/CurrentController.h"

#include "Switching/DriveManager.h"

#include "Utils/Fault/FaultManager.h"


// ----------------------------------------------------------------------
// Inter-Core Communication Structures
// ----------------------------------------------------------------------

// Core 0 -> Core 1: Just send raw torque targets!
// ThreadSafeQueue<CurrentCommand> tx_queue;
SerialProcessor serial_proc;
#include "hardware/sync.h"
#include "pico/stdlib.h"

typedef struct TelemetryPacket {
    float raw_adc_rad, theta_est, vq_v, vd_v, iq_meas, elec_angle, enc_offset, foc_update_hz;
    float rotor_velocity;
    float i_u, i_v, i_w;
    float i_alpha, i_beta;
    float i_d, i_q;
    // float v_alpha, v_beta;
    float v_u, v_v, v_w;

    float iq_set;
} TelemetryPacket;

#define TELEMETRY_RB_SIZE 128u
_Static_assert((TELEMETRY_RB_SIZE & (TELEMETRY_RB_SIZE - 1u)) == 0, "power of 2");

typedef struct {
    TelemetryPacket buf[TELEMETRY_RB_SIZE];

    volatile uint32_t head;  // producer writes
    volatile uint32_t tail;  // consumer writes
    volatile uint32_t dropped;
} telemetry_rb_t;

static telemetry_rb_t g_tel_rb;

static inline void copy_packet(TelemetryPacket* dst, const TelemetryPacket* src) {
    const uint32_t* s = (const uint32_t*)src;
    uint32_t* d = (uint32_t*)dst;
    for (uint32_t i = 0; i < (sizeof(TelemetryPacket) / 4u); i++) d[i] = s[i];
}

// Core 1
static inline bool telemetry_try_push(const TelemetryPacket* p) {
    telemetry_rb_t* r = &g_tel_rb;

    uint32_t h = r->head;
    uint32_t t = r->tail;
    uint32_t next = (h + 1u) & (TELEMETRY_RB_SIZE - 1u);

    if (next == t) {
        r->dropped++;
        return false;
    }  // full

    bool was_empty = (h == t);

    copy_packet(&r->buf[h], p);
    __dmb();
    r->head = next;

    // Only wake consumer if we transitioned empty -> non-empty
    if (was_empty) __sev();

    return true;
}

// Core 0
static inline bool telemetry_try_pop(TelemetryPacket* out) {
    telemetry_rb_t* r = &g_tel_rb;

    uint32_t t = r->tail;
    uint32_t h = r->head;
    if (t == h) return false;  // empty

    __dmb();
    copy_packet(out, &r->buf[t]);
    r->tail = (t + 1u) & (TELEMETRY_RB_SIZE - 1u);
    return true;
}
void updateTel() {
        serial_proc.poll();
        TelemetryPacket tp;
        while (telemetry_try_pop(&tp)) {
            // Push to Telemetry
            Telemetry::log("CORE1_LOOP_HZ", tp.foc_update_hz);
            // Telemetry::log("RAW_ADC_RAD", tp.raw_adc_rad);
            Telemetry::log("THETA_EST_RAD", tp.theta_est);
            Telemetry::log("ELEC_ANGLE", tp.elec_angle);
            Telemetry::log("ENC_OFFSET", tp.enc_offset);
            Telemetry::log("DEBUG_VQ", tp.vq_v);
            Telemetry::log("DEBUG_VD", tp.vd_v);

            Telemetry::log("IQ_SET_PT", tp.iq_set);

            Telemetry::log("DEBUG_I_ALPHA", tp.i_alpha);
            Telemetry::log("SYS_CLOCK", clock_get_hz(clk_sys));
            Telemetry::log("DEBUG_I_BETA", tp.i_beta);

            Telemetry::log("DEBUG_I_D", tp.i_d);
            Telemetry::log("DEBUG_I_Q", tp.i_q);

            // Telemetry::log("FAKE_I_U", tp.i_u);
            // Telemetry::log("FAKE_I_V", tp.i_v);
            // Telemetry::log("FAKE_I_W", tp.i_w);

            // Telemetry::log("V_Alpha", tp.v_alpha);
            // Telemetry::log("V_Beta", tp.v_beta);

            Telemetry::log("ROTOR_VELOCITY", tp.rotor_velocity);

            // Telemetry::log("V_U", tp.v_u);
            // Telemetry::log("V_V", tp.v_v);
            // Telemetry::log("V_W", tp.v_w);
            Telemetry::updateSensors();
        }
}
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
}
/**
 * @file    core1_main.cpp
 * @brief   System entry for high-speed motor control execution.
 */
 void core1_entry() {
    // ===========================================================================
    // 1. SYSTEM SETUP
    // ===========================================================================
    static DriveManager g_DriveManager;
    
    static CurrentController g_CurrentController;



    g_CurrentController.SetMotorConfig(g_MotorConfig);


    // A. Configure Modulation (Transitions from SPWM to NPulse at 4Hz)
    static SVPWMModulationScheme g_Svm;
    SVPWMConfig svmCfg;
    svmCfg.InfluenceStart_Hz_ = 0.0f;
    svmCfg.InfluenceEnd_Hz_   = 150.0f; 
    svmCfg.CarrierStart_Hz_   = 3000.0f;
    svmCfg.CarrierEnd_Hz_     = 3000.0f;
    svmCfg.MaxModulationIndex_ = 0.95f;
    g_Svm.ApplyConfig(svmCfg);
    g_DriveManager.RegisterModulationScheme(&g_Svm);

    // static SVPWMModulationScheme g_Svm3;
    // SVPWMConfig svmCfg3;
    // svmCfg3.InfluenceStart_Hz_ = 30.0f;
    // svmCfg3.InfluenceEnd_Hz_   = 45.0f; 
    // svmCfg3.CarrierStart_Hz_   = 3000.0f;
    // svmCfg3.CarrierEnd_Hz_     = 4000.0f;
    // svmCfg3.MaxModulationIndex_ = 0.95f;
    // g_Svm3.ApplyConfig(svmCfg3);
    // g_DriveManager.RegisterModulationScheme(&g_Svm3);


    // static NPulseModulationScheme g_NPulse;
    // NPulseConfig nPulseCfg;
    // nPulseCfg.InfluenceStart_Hz_ = 8.0f; 
    // nPulseCfg.InfluenceEnd_Hz_   = 19.0f; 
    // nPulseCfg.PulseRatio_        = 250;    
    // nPulseCfg.MinCarrier_Hz_     = 200.0f; 
    // nPulseCfg.MaxModulationIndex_ = 0.95f;
    // g_NPulse.ApplyConfig(nPulseCfg);
    // g_DriveManager.RegisterModulationScheme(&g_NPulse);

    // static NPulseModulationScheme g_NPulse2;
    // NPulseConfig nPulseCfg2;
    // nPulseCfg2.InfluenceStart_Hz_ = 19.0f; 
    // nPulseCfg2.InfluenceEnd_Hz_   = 30.0f; 
    // nPulseCfg2.PulseRatio_        = 105;    
    // nPulseCfg2.MinCarrier_Hz_     = 200.0f; 
    // nPulseCfg2.MaxModulationIndex_ = 0.95f;
    // g_NPulse2.ApplyConfig(nPulseCfg2);
    // g_DriveManager.RegisterModulationScheme(&g_NPulse2);


    // static SVPWMModulationScheme g_Svm2;
    // SVPWMConfig svmCfg2;
    // svmCfg2.InfluenceStart_Hz_ = 45.0f;
    // svmCfg2.InfluenceEnd_Hz_   = 2000.0f; 
    // svmCfg2.CarrierStart_Hz_   = 4000.0f;
    // svmCfg2.CarrierEnd_Hz_     = 4000.0f;
    // svmCfg2.MaxModulationIndex_ = 0.95f;
    // g_Svm2.ApplyConfig(svmCfg2);
    // g_DriveManager.RegisterModulationScheme(&g_Svm2);

    // B. Configure Control (FOC tuning)
    FocConfig focCfg;
    focCfg.InfluenceStart_RadPerSec_ = 0.0f;
    focCfg.InfluenceEnd_RadPerSec_   = 10000.0f;
    focCfg._Kp_Q_axis = 0.03f;
    focCfg._Ki_Q_axis = 10.0f;
    focCfg._Kp_D_axis = 0.03f;
    focCfg._Ki_D_axis = 10.0f;
    focCfg._SoftVoltageLimit_V = 0.0f;
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


    while (true) {
        // Strict 400us Tick (2.5kHz)
        // absolute_time_t currentTime = get_absolute_time();
        // if (absolute_time_diff_us(old_t, currentTime) >= 50) {
        //     float dt_S = (float)absolute_time_diff_us(old_t, currentTime) / 1000000.0f;
        //     old_t = get_absolute_time();



        if (measurements->update_from_dma()) {
                    
                    absolute_time_t currentTime = get_absolute_time();
                    float dt_S = (float)absolute_time_diff_us(old_t, currentTime) / 1000000.0f;
                    old_t = currentTime;

                    // // 1. Gather Telemetry Data (No need to call update() anymore)
                    // SensorData SenseData;
                    // SenseData._Idc_A = measurements->read("I_DC_MAIN");
                    // SenseData._DcBusVoltage_V = measurements->read("V_DC_BUS");
                    // SenseData._Iu_A = measurements->read("I_PH_U");
                    // SenseData._Iw_A = measurements->read("I_PH_W");
                    // SenseData._Iv_A = -(SenseData._Iu_A + SenseData._Iw_A);

            // // Update Physical Sensors
            // measurements->update();

            SensorData SenseData;
            SenseData._Idc_A = measurements->read("I_DC_MAIN");
            SenseData._DcBusVoltage_V = measurements->read("V_DC_BUS");
            SenseData._Iu_A = measurements->read("I_PH_U");
            SenseData._Iw_A = measurements->read("I_PH_W");
            SenseData._Iv_A = -(SenseData._Iu_A + SenseData._Iw_A);
            
            // --- PLL Tracking Observer ---






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

            // Use 'raw_adc_rad' here instead of 'theta_est'
            SenseData._EncoderPosition_Rad = raw_adc_rad + phase_advance_rad;






            if (g_Driver && !g_Driver->isEmergencyStopped() && g_Driver->isEnabled()) {
                
                // 1. Define High-Level Setpoint
                // If sine is >= 0, set to 60. If sine is < 0, set to -60.
                target._TargetIq_A = 50.0f;// * (sin(get_absolute_time() / 4'00'000.0f) >= 0.0f);// ? 60.0f : -60.0f;
                target._TargetId_A = 0.0f;  
                // target._VqFeedforward_V = 0.0f;
                // target._VdFeedforward_V = 0.0f;

                // 2. Process complete cascade via Manager
                bool Result = g_DriveManager.Update(&g_FaultManager, &g_MotorConfig, g_Driver, SenseData, target, dt_S);

            }

            // --- TELEMETRY ---
            if (absolute_time_diff_us(prev_tel_time, get_absolute_time()) >= 10000) {
                prev_tel_time = get_absolute_time();
                TelemetryPacket t_pack;
                // t_pack.raw_adc_rad = raw_adc_rad;
                // t_pack.theta_est = theta_est;
                
                // Pulling debug data from g_Foc. If V/Hz is active, these will be stale, 
                // but safe to read. You could eventually move these into ModulationInput.
                t_pack.vq_v = g_Foc._Vq_V; 
                t_pack.vd_v = g_Foc._Vd_V;
                t_pack.iq_meas = g_Foc._Iq_A;
                t_pack.elec_angle = g_Foc._ElectricalAngle_Rad;
                t_pack.enc_offset = g_Foc._EncoderOffset_Rad;
                
                t_pack.foc_update_hz = 1.0f / dt_S;
                t_pack.i_alpha = g_Foc.i_alpha;
                t_pack.i_beta = g_Foc.i_beta;
                t_pack.i_d = g_Foc.i_d;
                t_pack.i_q = g_Foc.i_q;

                t_pack.iq_set = target._TargetIq_A;
                
                t_pack.i_u = SenseData._Iu_A;
                t_pack.i_v = SenseData._Iv_A;
                t_pack.i_w = SenseData._Iw_A;
                
                // t_pack.v_u = HwCmd.DutyPhU_unitless;
                // t_pack.v_v = HwCmd.DutyPhV_unitless;
                // t_pack.v_w = HwCmd.DutyPhW_unitless;
                t_pack.rotor_velocity = omega_est * 9.55; // convert to rpm
                (void)telemetry_try_push(&t_pack);
            }
        }
    }
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

    // configureZones();

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

    // --- NEW: Initialize the DMA ---
    adc_system->init_dma();
    



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
    measurements->printChannels();

    Telemetry::printf("\nCalibrating current sensors...\n");
    measurements->calibrateCurrentSensors();
    Telemetry::printf("Current sensor calibration complete.\n\n");


    g_FaultManager.Update();

    // 4. Configure Global Motor Limits
    g_MotorConfig._PolePairs_unitless = 5;
    g_MotorConfig._Ld_Henry = 0.000040f;
    g_MotorConfig._Lq_Henry = 0.000040f;
    g_MotorConfig._FluxLinkage_Wb = 0.0; // seems abt right from ~523.6 rads/sec @ 12v

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
    Telemetry::set_period_us(1000);  // 100 Hz (it's not lmao...)
    Telemetry::init();
    Telemetry::bindMeasurementSystem(*measurements);
    static TelemetryPacket empty = {0};
    telemetry_try_push(&empty);
    updateTel();
    g_Driver->setCarrierFrequency(4000.0f);

    // ------------------------------------------------------------------
    // HARDWARE DIAGNOSTIC: CURRENT SENSOR POLARITY CHECK
    // ------------------------------------------------------------------
    Telemetry::printf("\n--- DIAGNOSTIC: VERIFYING CURRENT SENSOR POLARITY ---\n");
    g_Driver->enable();

    // 1. Set a small, safe test voltage (1.5V)
    float test_voltage = 1.5f;
    measurements->update();
    float vdc = measurements->read("V_DC_BUS");
    if (vdc < 5.0f) vdc = 60.0f;  // Fallback to your config nominal

    Telemetry::printf("Applying %.2fV strictly to Phase U...\n", test_voltage);

    // 2. Manually calculate duty cycles to align the magnetic field EXACTLY to Phase U
    // Phase U gets positive voltage, V and W get half-negative to complete the circuit
    float duty_U = 0.5f + (test_voltage / vdc);
    float duty_V = 0.5f + (-test_voltage / 2.0f / vdc);
    float duty_W = 0.5f + (-test_voltage / 2.0f / vdc);

    // Apply the static voltages directly to the PWM driver
    g_Driver->setDutyCycles(duty_U, duty_V, duty_W);

    // 3. Wait a brief moment for the current to rise through the motor inductance
    sleep_ms(100);

    // 4. Read the resulting currents
    measurements->update();
    float i_u = measurements->read("I_PH_U");
    float i_w = measurements->read("I_PH_W");
    float i_v = -(i_u + i_w);  // Assuming wye-wound, sum of currents is 0

    Telemetry::printf("\n--- RESULTS ---\n");
    Telemetry::printf("  Phase U Current: %7.3f A  <-- MUST BE POSITIVE\n", i_u);
    Telemetry::printf("  Phase V Current: %7.3f A  <-- Should be negative (~ half of U)\n", i_v);
    Telemetry::printf("  Phase W Current: %7.3f A  <-- Should be negative (~ half of U)\n\n", i_w);

    // 5. Evaluate and warn
    if (i_u > 0.2f) {
        Telemetry::printf(">>> PASS: Phase U polarity is CORRECT.\n");
    } else if (i_u < -0.2f) {
        Telemetry::printf(">>> FATAL FAIL: Phase U is REVERSED!\n");
        Telemetry::printf("    This will cause a 100A positive feedback explosion.\n");
        Telemetry::printf("    Fix: Multiply sensor reading by -1 or flip sensor wiring.\n");
    } else {
        Telemetry::printf(">>> WARN: Current too low to determine polarity. Increase test_voltage.\n");
    }
    Telemetry::printf("-----------------------------------------------------\n\n");

    // 6. Safely turn off the drive before proceeding
    g_Driver->setDutyCycles(0.5f, 0.5f, 0.5f);
    g_Driver->disable();
    sleep_ms(1000);  // Pause so you can read the console

    // ------------------------------------------------------------------
    // SAFE CALIBRATION: current-limited open-loop (NO 100A spikes)
    // ------------------------------------------------------------------
    Telemetry::printf("\nCAL: Encoder offset calibration (CURRENT LIMITED)...\n");
    g_Driver->enable();

    auto wrap_0_2pi = [](float a) {
        const float TWO_PI = 6.283185307f;
        while (a >= TWO_PI) a -= TWO_PI;
        while (a < 0.0f) a += TWO_PI;
        return a;
    };

    auto wrap_pm_pi = [](float a) {
        const float PI = 3.1415926535f;
        const float TWO_PI = 6.283185307f;
        while (a > PI) a -= TWO_PI;
        while (a < -PI) a += TWO_PI;
        return a;
    };

    struct SinCosCal {
        float sin_mid = 0.0f;
        float cos_mid = 0.0f;
        float sin_gain = 1.0f;
        float cos_gain = 1.0f;
    };

    auto read_norm_sincos = [&](const SinCosCal& cal, float* s_out, float* c_out) {
        float s = measurements->read("ENCODER_SIN");
        float c = measurements->read("ENCODER_COS");
        s = (s - cal.sin_mid) * cal.sin_gain;
        c = (c - cal.cos_mid) * cal.cos_gain;
        float r = sqrtf(s * s + c * c);
        if (r > 1e-6f) {
            s /= r;
            c /= r;
        }
        *s_out = s;
        *c_out = c;
    };

    // Library-consistent open-loop voltage -> duty (centered SPWM), with voltage limiting
    auto set_vdq_openloop = [&](float Vd, float Vq, float theta_e, float Vdc, float max_mod) {
        tIPark ip = {};
        ip.m_dq2albe = tIPark_dq2albe;
        ip.fD = Vd;
        ip.fQ = Vq;
        ip.fSinAng = sinf(theta_e);
        ip.fCosAng = cosf(theta_e);
        ip.m_dq2albe(&ip);  // ip.fAl/ip.fBe

        tIFClarke ic = IF_CLARKE_DEFAULTS;
        ic.fAl = ip.fAl;
        ic.fBe = ip.fBe;
        ic.m_albe2abc(&ic);  // ic.fA/ic.fB/ic.fC (phase refs, volts)

        if (Vdc < 1e-3f) {
            g_Driver->setDutyCycles(0.5f, 0.5f, 0.5f);
            return;
        }

        const float Vlimit = 0.5f * Vdc * max_mod;  // SPWM capability (phase peak)
        float Va = ic.fA, Vb = ic.fB, Vc = ic.fC;

        float maxAbs = fabsf(Va);
        if (fabsf(Vb) > maxAbs) maxAbs = fabsf(Vb);
        if (fabsf(Vc) > maxAbs) maxAbs = fabsf(Vc);

        if (maxAbs > Vlimit && maxAbs > 1e-9f) {
            float k = Vlimit / maxAbs;
            Va *= k;
            Vb *= k;
            Vc *= k;
        }

        float du = 0.5f + (Va / Vdc);
        float dv = 0.5f + (Vb / Vdc);
        float dw = 0.5f + (Vc / Vdc);

        if (du < 0) du = 0;
        if (du > 1) du = 1;
        if (dv < 0) dv = 0;
        if (dv > 1) dv = 1;
        if (dw < 0) dw = 0;
        if (dw > 1) dw = 1;

        g_Driver->setDutyCycles(du, dv, dw);
    };

    // -------- Safety / limits --------
    const float I_LOCK_MAX_A = 15.0f;  // <= set to your safe calibration current
    const float I_TRIP_A = 50.0f;      // hard trip (instant stop) - set to safe hardware limit
    const float max_mod_cal = 0.70f;   // extra headroom for calibration
    const float theta_e_lock = 0.0f;

    // Use very small starting voltages.
    // (On low-R motors, even 1–2V can be plenty.)
    const float Vd_lock_min_V = 0.5f;
    const float Vd_lock_max_V = 5.0f;    // do NOT start with 10V
    const float Vd_ramp_V_per_s = 1.0f;  // slow ramp

    // These still sweep, but current-limited
    const float sweep_hz_e = 0.5f;
    const float sweep_time_s = 6.0f;

    auto read_currents = [&]() {
        float Idc = measurements->read("I_DC_MAIN");
        float Iu = measurements->read("I_PH_U");
        float Iw = measurements->read("I_PH_W");
        float Iv = -(Iu + Iw);
        // Use worst-case magnitude as a conservative limiter
        float Ipk = fabsf(Iu);
        if (fabsf(Iv) > Ipk) Ipk = fabsf(Iv);
        if (fabsf(Iw) > Ipk) Ipk = fabsf(Iw);
        return std::pair<float, float>(Idc, Ipk);  // (dc, phase_peak_est)
    };

    auto hard_stop = [&]() {
        g_Driver->setDutyCycles(0.5f, 0.5f, 0.5f);
        // If you prefer a hard gate-off:
        // g_Driver->emergencyStop();
    };

    // -------- Get Vdc --------
    measurements->update();
    float Vdc_meas = measurements->read("V_DC_BUS");
    // if (Vdc_meas < 5.0f) Vdc_meas = g_MotorConfig._DcBusVoltage_V;

    // -------- 1) Sweep to compute sin/cos min/max, but current-limited --------
    Telemetry::printf("CAL: Sweep for sin/cos min/max (current limited)...\n");
    SinCosCal sc{};
    float sin_min = 1e9f, sin_max = -1e9f, cos_min = 1e9f, cos_max = -1e9f;

    float Vd_cmd = Vd_lock_min_V;

    absolute_time_t t0 = get_absolute_time();
    absolute_time_t last = get_absolute_time();

    while (absolute_time_diff_us(t0, get_absolute_time()) < (int64_t)(sweep_time_s * 1e6f)) {
        updateTel();
        measurements->update();
        float dt = absolute_time_diff_us(last, get_absolute_time()) * 1e-6f;
        last = get_absolute_time();
        if (dt < 0) dt = 0;

        float t = absolute_time_diff_us(t0, get_absolute_time()) * 1e-6f;
        float theta_e = wrap_0_2pi(6.283185307f * sweep_hz_e * t);

        auto [Idc, Ipk] = read_currents();

        // Hard trip
        if (fabsf(Idc) > I_TRIP_A || Ipk > I_TRIP_A) {
            Telemetry::printf("CAL TRIP: Overcurrent! Idc=%f, Ipk=%f\n", Idc, Ipk);
            hard_stop();
            break;
        }

        // Simple limiter: ramp Vd up until we approach I_LOCK_MAX, ramp down if above
        if (Ipk < I_LOCK_MAX_A * 0.85f) {
            Vd_cmd += Vd_ramp_V_per_s * dt;
        } else if (Ipk > I_LOCK_MAX_A) {
            Vd_cmd -= 3.0f * Vd_ramp_V_per_s * dt;
        }
        if (Vd_cmd < Vd_lock_min_V) Vd_cmd = Vd_lock_min_V;
        if (Vd_cmd > Vd_lock_max_V) Vd_cmd = Vd_lock_max_V;

        set_vdq_openloop(Vd_cmd, 0.0f, theta_e, Vdc_meas, max_mod_cal);

        float s = measurements->read("ENCODER_SIN");
        float c = measurements->read("ENCODER_COS");
        if (s < sin_min) sin_min = s;
        if (s > sin_max) sin_max = s;
        if (c < cos_min) cos_min = c;
        if (c > cos_max) cos_max = c;

        sleep_us(600);
    }

    // Compute sin/cos normalization
    sc.sin_mid = 0.5f * (sin_max + sin_min);
    sc.cos_mid = 0.5f * (cos_max + cos_min);
    float sin_amp = 0.5f * (sin_max - sin_min);
    float cos_amp = 0.5f * (cos_max - cos_min);
    sc.sin_gain = (sin_amp > 1e-6f) ? (1.0f / sin_amp) : 1.0f;
    sc.cos_gain = (cos_amp > 1e-6f) ? (1.0f / cos_amp) : 1.0f;

    Telemetry::printf("CAL: sin[min,max]=[%f,%f] mid=%f gain=%f\n", sin_min, sin_max, sc.sin_mid, sc.sin_gain);
    Telemetry::printf("CAL: cos[min,max]=[%f,%f] mid=%f gain=%f\n", cos_min, cos_max, sc.cos_mid, sc.cos_gain);

    // -------- 2) Lock at theta_e_lock with current limit --------
    Telemetry::printf("CAL: Locking at theta_e=%f (current limited)...\n", theta_e_lock);

    // Re-use current-limited Vd_cmd from sweep, start low
    Vd_cmd = Vd_lock_min_V;
    absolute_time_t lock_end = delayed_by_ms(get_absolute_time(), 1200);
    last = get_absolute_time();

    while (absolute_time_diff_us(get_absolute_time(), lock_end) > 0) {
        updateTel();
        measurements->update();
        float dt = absolute_time_diff_us(last, get_absolute_time()) * 1e-6f;
        last = get_absolute_time();
        if (dt < 0) dt = 0;

        auto [Idc, Ipk] = read_currents();
        if (fabsf(Idc) > I_TRIP_A || Ipk > I_TRIP_A) {
            Telemetry::printf("CAL TRIP: Overcurrent during lock! Idc=%f, Ipk=%f\n", Idc, Ipk);
            hard_stop();
            break;
        }

        // keep Ipk near I_LOCK_MAX
        if (Ipk < I_LOCK_MAX_A * 0.90f) Vd_cmd += Vd_ramp_V_per_s * dt;
        if (Ipk > I_LOCK_MAX_A) Vd_cmd -= 4.0f * Vd_ramp_V_per_s * dt;

        if (Vd_cmd < Vd_lock_min_V) Vd_cmd = Vd_lock_min_V;
        if (Vd_cmd > Vd_lock_max_V) Vd_cmd = Vd_lock_max_V;

        set_vdq_openloop(Vd_cmd, 0.0f, theta_e_lock, Vdc_meas, max_mod_cal);
        sleep_us(400);
    }

    // -------- 3) Sample normalized sin/cos while continuing limited lock --------
    Telemetry::printf("CAL: Sampling...\n");
    float sum_sn = 0.0f, sum_cs = 0.0f;
    const int N = 1200;

    for (int i = 0; i < N; i++) {
        measurements->update();

        auto [Idc, Ipk] = read_currents();
        if (fabsf(Idc) > I_TRIP_A || Ipk > I_TRIP_A) {
            Telemetry::printf("CAL TRIP: Overcurrent during sampling! Idc=%f, Ipk=%f\n", Idc, Ipk);
            hard_stop();
            break;
        }

        // keep lock applied but don’t chase too hard during averaging
        if (Ipk > I_LOCK_MAX_A) Vd_cmd *= 0.98f;
        set_vdq_openloop(Vd_cmd, 0.0f, theta_e_lock, Vdc_meas, max_mod_cal);

        float sn, cs;
        read_norm_sincos(sc, &sn, &cs);
        sum_sn += sn;
        sum_cs += cs;

        sleep_us(400);
    }

    float mech_angle = atan2f(sum_sn / (float)N, sum_cs / (float)N);
    mech_angle = wrap_0_2pi(mech_angle);

    // With your FOC: elec = mech*pp + offset
    float elec_sign = -1.0f;            // flip to -1 if needed
    float phase_correction_rad = 0.0f;  // usually 0 with your Park, unless wiring/phase mapping differs

    float raw_elec = elec_sign * mech_angle * g_MotorConfig._PolePairs_unitless;
    float desired_elec = wrap_0_2pi(theta_e_lock + phase_correction_rad);

    g_Foc._EncoderOffset_Rad = wrap_0_2pi(desired_elec - raw_elec);



    Telemetry::printf("CAL DONE: Vd_used~%f V, mech=%f rad, offset=%f rad\n\n",
                      Vd_cmd, mech_angle, g_Foc._EncoderOffset_Rad);

    // release
    hard_stop();

























    adc_system->set_filtered_read(false);

    // --- NEW: Attach the DMA trigger to the PWM Driver ---
    g_Driver->setPwmWrapCallback([](){
        if (MAX2253x_MultiADC::instance) {
            MAX2253x_MultiADC::instance->start_async_read();
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
        if(delay > 1000) {
            g_Foc._EncoderOffset_Rad = ctx.encoderOffset;
            delay = 0;
        }
        delay++;
        

        // 2. Print active faults once per second (1,000,000 microseconds)
        uint64_t currentTime_uS = time_us_64();
        if ((currentTime_uS - lastFaultPrintTime_uS) >= 1000000) {
            lastFaultPrintTime_uS = currentTime_uS; // Reset the timer

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
