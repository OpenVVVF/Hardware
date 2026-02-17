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

#include "ThreadSafeQueue.h"

// Bring in FOC and Switching headers that used to be hidden in RtBridge
#include "Switching/FOC.h"
#include "Switching/PWMDriver.h"
#include "Switching/Modulation.h"



#include "pico/multicore.h"
#include "ThreadSafeQueue.h"

// ----------------------------------------------------------------------
// Inter-Core Communication Structures
// ----------------------------------------------------------------------

// Core 0 -> Core 1: Just send raw torque targets!
ThreadSafeQueue<CurrentCommand> tx_queue;

// Core 1 -> Core 0: FOC State Only
struct TelemetryPacket {
    float raw_adc_rad;
    float theta_est;
    float vq_v;
    float vd_v;
    float iq_meas;
    float elec_angle;
    float enc_offset;
    float foc_update_hz;

    float i_u;
    float i_v;
    float i_w;

    float i_alpha;
    float i_beta;

    float i_d;
    float i_q;


};

ThreadSafeQueue<TelemetryPacket> rx_queue;


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


void core1_entry() {
    absolute_time_t next_foc_tick = get_absolute_time();
    absolute_time_t old_t = get_absolute_time();

    float theta_est = 0.0f;
    float omega_est = 0.0f;
    const float Kp_pll = 200.0f;
    const float Ki_pll = 2000.0f;

    while (true) {
        // 1. UPDATE TARGET TORQUE (Dead simple)
        CurrentCommand new_cmd;
        if (tx_queue.try_pop(new_cmd)) {
            // Update the FOC targets immediately
            g_Foc.ApplyCurrentLimits(new_cmd); 
        }

        // 2. STRICT 2KHZ FOC TICK
        if (absolute_time_diff_us(get_absolute_time(), next_foc_tick) <= 0) {
            float dt_S = (float)absolute_time_diff_us(old_t, get_absolute_time()) / 1000000.0f;
            old_t = get_absolute_time();
            next_foc_tick = delayed_by_us(next_foc_tick, 400); 

            measurements->update();

            SensorData SenseData;
            SenseData._Idc_A = measurements->read("I_DC_MAIN");
            SenseData._Iu_A  = measurements->read("I_PH_U");
            SenseData._Iw_A  = measurements->read("I_PH_W");
            SenseData._Iv_A  = -(SenseData._Iu_A + SenseData._Iw_A);
            SenseData._DcBusVoltage_V = measurements->read("V_DC_BUS");

            
            // PLL Tracking Observer
            float raw_adc_rad = measurements->getRotorPositionDegrees() * 0.01745329251f;
            float error = raw_adc_rad - theta_est;
            while (error > 3.14159265f) error -= 6.283185307f;
            while (error < -3.14159265f) error += 6.283185307f;

            omega_est += Ki_pll * error * dt_S;
            theta_est += (omega_est + Kp_pll * error) * dt_S;
            while (theta_est >= 6.283185307f) theta_est -= 6.283185307f;
            while (theta_est < 0.0f) theta_est += 6.283185307f;


            SenseData._EncoderPosition_Rad = theta_est;
            SenseData._EncoderVelocity_RadPerSec = omega_est;

            g_Foc._DaxisController_.fDtSec = dt_S;
            g_Foc._QaxisController_.fDtSec = dt_S;
            g_Foc.UpdateSensors(SenseData);

            // Execute modulation
            if (g_Driver && !g_Driver->isEmergencyStopped() && g_Driver->isEnabled()) {
                FocOutput FOC_Out = g_Foc.UpdateVoltages();
                PhaseVoltages TargetDuty;
                GenerateSpwm(FOC_Out, 0.95f, TargetDuty);
                g_Driver->setDutyCycles(TargetDuty._Du_unitless, TargetDuty._Dv_unitless, TargetDuty._Dw_unitless);
            }

            // 3. SEND TELEMETRY
            TelemetryPacket t_pack;
            t_pack.raw_adc_rad = raw_adc_rad;
            t_pack.theta_est = theta_est;
            t_pack.vq_v = g_Foc._Vq_V;
            t_pack.vd_v = g_Foc._Vd_V;
            t_pack.iq_meas = g_Foc._Iq_A;
            t_pack.elec_angle = g_Foc._ElectricalAngle_Rad;
            t_pack.enc_offset = g_Foc._EncoderOffset_Rad;
            t_pack.foc_update_hz=1.0f/dt_S;
            t_pack.i_alpha =  g_Foc.i_alpha;
            t_pack.i_beta = g_Foc.i_beta;
            t_pack.i_d =  g_Foc.i_d;
            t_pack.i_q =  g_Foc.i_q;
            t_pack.i_u =  SenseData._Iu_A;
            t_pack.i_v =  SenseData._Iv_A;
            t_pack.i_w =  SenseData._Iw_A;
            rx_queue.push(t_pack);
        }
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

    // // 2. Setup Command Context (Using captureless lambdas to cast to C function pointers)
    // CommandContext ctx{};
    // ctx.zone_mgr = &zone_mgr;
    // ctx.set_ramp_rate = [](float val) { g_RampRate = val; };
    // ctx.set_manual_carrier_hz = [](float val) { g_ManualCarrierHz = val; };
    // ctx.set_manual_carrier_mode = [](bool val) { g_ManualCarrierMode = val; };
    // ctx.enable = []() { if (g_Driver) g_Driver->enable(); };
    // ctx.disable = []() { if (g_Driver) g_Driver->disable(); };
    // ctx.emergency_stop = []() { if (g_Driver) g_Driver->emergencyStop(); };
    // ctx.clear_emergency_stop = []() { if (g_Driver) g_Driver->clearEmergency(); };
    // ctx.set_target_frequency = [](float val) { if (g_Driver) g_Driver->setTargetFrequency(val, g_RampRate); };
    // ctx.set_frequency_immediate = [](float val) { if (g_Driver) g_Driver->setFrequencyImmediate(val); };
    // ctx.setEncoderOffset = [](float val) {g_Foc._EncoderOffset_Rad = val; };
    
    // // Status polling callback bypasses the seqlock now
    // ctx.try_get_status = [](RtStatus* st) -> bool {
    //     if (!g_Driver) return false;
    //     st->enabled = g_Driver->isEnabled();
    //     st->estop = g_Driver->isEmergencyStopped();
    //     st->current_freq = g_Driver->getCurrentFrequency();
    //     st->modulation_index = g_Driver->getModulationIndex();
    //     st->carrier_hz = g_Driver->getCarrierFrequency();
    //     st->sync_mode = g_Driver->isSynchronousMode();
    //     st->pulses = g_Driver->getPulsesPerCycle();
    //     st->manual_carrier_mode = g_ManualCarrierMode;
    //     st->manual_carrier_hz = g_ManualCarrierHz;
    //     st->ramp_rate = g_RampRate;
    //     st->debug_Vd = g_Foc._Vd_V;
    //     st->debug_Vq = g_Foc._Vq_V;
    //     st->debug_Iq_measured = g_Foc._Iq_A;
    //     st->debug_angle_elec = g_Foc._ElectricalAngle_Rad;
    //     return true;
    // };

    // CommandManager::instance().setContext(ctx);
    // initializeCommands();
    // SerialProcessor serial_proc;

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
        {0, 0, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_PH_W", 0.0f},
        {0, 1, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_PH_V", 0.0f},
        {0, 2, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_PH_U", 0.0f},
        {0, 3, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_DC_BUS", 0.0f},
        {2, 2, SensorType::DIRECT, 1.0f, 0.0f, 1.0f, "ENCODER_SIN", 0.0f},
        {2, 1, SensorType::DIRECT, 1.0f, 0.0f, 1.0f, "ENCODER_COS", 0.0f},
        {1, 3, SensorType::BIPOLAR_CURRENT, -1204.8193f, 0.0f, 1.0f, "I_DC_MAIN", 0.410f},
        {1, 1, SensorType::BIPOLAR_CURRENT, -1204.8193f, 0.0f, 0.1f, "I_PH_U", 0.410f},
        {1, 0, SensorType::BIPOLAR_CURRENT, 1204.8193f, 0.0f, 0.1f, "I_PH_W", 0.410f}
    };

    measurements->addChannels(channel_map);
    measurements->update();
    measurements->printChannels();

    printf("\nCalibrating current sensors...\n");
    measurements->calibrateCurrentSensors();
    printf("Current sensor calibration complete.\n\n");

    // 4. Configure FOC
    MotorConfig C; 
    C._PolePairs_unitless = 5;
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
    Telemetry::set_period_us(1000); // 100 Hz (it's not lmao...)
    Telemetry::init(); 
    Telemetry::bindMeasurementSystem(*measurements);




// ------------------------------------------------------------------
    // HARDWARE DIAGNOSTIC: CURRENT SENSOR POLARITY CHECK
    // ------------------------------------------------------------------
    printf("\n--- DIAGNOSTIC: VERIFYING CURRENT SENSOR POLARITY ---\n");
    g_Driver->enable();

    // 1. Set a small, safe test voltage (1.5V)
    float test_voltage = 1.5f;
    measurements->update();
    float vdc = measurements->read("V_DC_BUS");
    if (vdc < 5.0f) vdc = 60.0f; // Fallback to your config nominal

    printf("Applying %.2fV strictly to Phase U...\n", test_voltage);

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
    float i_v = -(i_u + i_w); // Assuming wye-wound, sum of currents is 0

    printf("\n--- RESULTS ---\n");
    printf("  Phase U Current: %7.3f A  <-- MUST BE POSITIVE\n", i_u);
    printf("  Phase V Current: %7.3f A  <-- Should be negative (~ half of U)\n", i_v);
    printf("  Phase W Current: %7.3f A  <-- Should be negative (~ half of U)\n\n", i_w);

    // 5. Evaluate and warn
    if (i_u > 0.2f) {
        printf(">>> PASS: Phase U polarity is CORRECT.\n");
    } else if (i_u < -0.2f) {
        printf(">>> FATAL FAIL: Phase U is REVERSED!\n");
        printf("    This will cause a 100A positive feedback explosion.\n");
        printf("    Fix: Multiply sensor reading by -1 or flip sensor wiring.\n");
    } else {
        printf(">>> WARN: Current too low to determine polarity. Increase test_voltage.\n");
    }
    printf("-----------------------------------------------------\n\n");

    // 6. Safely turn off the drive before proceeding
    g_Driver->setDutyCycles(0.5f, 0.5f, 0.5f);
    g_Driver->disable();
    sleep_ms(1000); // Pause so you can read the console



// ------------------------------------------------------------------
// SAFE CALIBRATION: current-limited open-loop (NO 100A spikes)
// ------------------------------------------------------------------
printf("\nCAL: Encoder offset calibration (CURRENT LIMITED)...\n");
g_Driver->enable();

auto wrap_0_2pi = [](float a) {
    const float TWO_PI = 6.283185307f;
    while (a >= TWO_PI) a -= TWO_PI;
    while (a <  0.0f)   a += TWO_PI;
    return a;
};

auto wrap_pm_pi = [](float a) {
    const float PI = 3.1415926535f;
    const float TWO_PI = 6.283185307f;
    while (a >  PI) a -= TWO_PI;
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
    float r = sqrtf(s*s + c*c);
    if (r > 1e-6f) { s /= r; c /= r; }
    *s_out = s; *c_out = c;
};

// Library-consistent open-loop voltage -> duty (centered SPWM), with voltage limiting
auto set_vdq_openloop = [&](float Vd, float Vq, float theta_e, float Vdc, float max_mod) {
    tIPark ip = {};
    ip.m_dq2albe = tIPark_dq2albe;
    ip.fD = Vd;
    ip.fQ = Vq;
    ip.fSinAng = sinf(theta_e);
    ip.fCosAng = cosf(theta_e);
    ip.m_dq2albe(&ip); // ip.fAl/ip.fBe

    tIFClarke ic = IF_CLARKE_DEFAULTS;
    ic.fAl = ip.fAl;
    ic.fBe = ip.fBe;
    ic.m_albe2abc(&ic); // ic.fA/ic.fB/ic.fC (phase refs, volts)

    if (Vdc < 1e-3f) { g_Driver->setDutyCycles(0.5f,0.5f,0.5f); return; }

    const float Vlimit = 0.5f * Vdc * max_mod; // SPWM capability (phase peak)
    float Va = ic.fA, Vb = ic.fB, Vc = ic.fC;

    float maxAbs = fabsf(Va);
    if (fabsf(Vb) > maxAbs) maxAbs = fabsf(Vb);
    if (fabsf(Vc) > maxAbs) maxAbs = fabsf(Vc);

    if (maxAbs > Vlimit && maxAbs > 1e-9f) {
        float k = Vlimit / maxAbs;
        Va *= k; Vb *= k; Vc *= k;
    }

    float du = 0.5f + (Va / Vdc);
    float dv = 0.5f + (Vb / Vdc);
    float dw = 0.5f + (Vc / Vdc);

    if (du < 0) du = 0; if (du > 1) du = 1;
    if (dv < 0) dv = 0; if (dv > 1) dv = 1;
    if (dw < 0) dw = 0; if (dw > 1) dw = 1;

    g_Driver->setDutyCycles(du, dv, dw);
};

// -------- Safety / limits --------
const float I_LOCK_MAX_A   = 15.0f;   // <= set to your safe calibration current
const float I_TRIP_A       = 50.0f;  // hard trip (instant stop) - set to safe hardware limit
const float max_mod_cal    = 0.70f;  // extra headroom for calibration
const float theta_e_lock   = 0.0f;

// Use very small starting voltages.
// (On low-R motors, even 1–2V can be plenty.)
const float Vd_lock_min_V  = 0.5f;
const float Vd_lock_max_V  = 5.0f;   // do NOT start with 10V
const float Vd_ramp_V_per_s = 1.0f;  // slow ramp

// These still sweep, but current-limited
const float sweep_hz_e     = 0.5f;
const float sweep_time_s   = 6.0f;

auto read_currents = [&](){
    float Idc = measurements->read("I_DC_MAIN");
    float Iu  = measurements->read("I_PH_U");
    float Iw  = measurements->read("I_PH_W");
    float Iv  = -(Iu + Iw);
    // Use worst-case magnitude as a conservative limiter
    float Ipk = fabsf(Iu);
    if (fabsf(Iv) > Ipk) Ipk = fabsf(Iv);
    if (fabsf(Iw) > Ipk) Ipk = fabsf(Iw);
    return std::pair<float,float>(Idc, Ipk); // (dc, phase_peak_est)
};

auto hard_stop = [&](){
    g_Driver->setDutyCycles(0.5f,0.5f,0.5f);
    // If you prefer a hard gate-off:
    // g_Driver->emergencyStop();
};

// -------- Get Vdc --------
measurements->update();
float Vdc_meas = measurements->read("V_DC_BUS");
if (Vdc_meas < 5.0f) Vdc_meas = C._DcBusVoltage_V;

// -------- 1) Sweep to compute sin/cos min/max, but current-limited --------
printf("CAL: Sweep for sin/cos min/max (current limited)...\n");
SinCosCal sc{};
float sin_min=1e9f, sin_max=-1e9f, cos_min=1e9f, cos_max=-1e9f;

float Vd_cmd = Vd_lock_min_V;

absolute_time_t t0 = get_absolute_time();
absolute_time_t last = get_absolute_time();

while (absolute_time_diff_us(t0, get_absolute_time()) < (int64_t)(sweep_time_s * 1e6f)) {
    measurements->update();
    float dt = absolute_time_diff_us(last, get_absolute_time()) * 1e-6f;
    last = get_absolute_time();
    if (dt < 0) dt = 0;

    float t = absolute_time_diff_us(t0, get_absolute_time()) * 1e-6f;
    float theta_e = wrap_0_2pi(6.283185307f * sweep_hz_e * t);

    auto [Idc, Ipk] = read_currents();

    // Hard trip
    if (fabsf(Idc) > I_TRIP_A || Ipk > I_TRIP_A) {
        printf("CAL TRIP: Overcurrent! Idc=%f, Ipk=%f\n", Idc, Ipk);
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
sc.sin_mid  = 0.5f*(sin_max + sin_min);
sc.cos_mid  = 0.5f*(cos_max + cos_min);
float sin_amp = 0.5f*(sin_max - sin_min);
float cos_amp = 0.5f*(cos_max - cos_min);
sc.sin_gain = (sin_amp > 1e-6f) ? (1.0f / sin_amp) : 1.0f;
sc.cos_gain = (cos_amp > 1e-6f) ? (1.0f / cos_amp) : 1.0f;

printf("CAL: sin[min,max]=[%f,%f] mid=%f gain=%f\n", sin_min, sin_max, sc.sin_mid, sc.sin_gain);
printf("CAL: cos[min,max]=[%f,%f] mid=%f gain=%f\n", cos_min, cos_max, sc.cos_mid, sc.cos_gain);

// -------- 2) Lock at theta_e_lock with current limit --------
printf("CAL: Locking at theta_e=%f (current limited)...\n", theta_e_lock);

// Re-use current-limited Vd_cmd from sweep, start low
Vd_cmd = Vd_lock_min_V;
absolute_time_t lock_end = delayed_by_ms(get_absolute_time(), 1200);
last = get_absolute_time();

while (absolute_time_diff_us(get_absolute_time(), lock_end) > 0) {
    measurements->update();
    float dt = absolute_time_diff_us(last, get_absolute_time()) * 1e-6f;
    last = get_absolute_time();
    if (dt < 0) dt = 0;

    auto [Idc, Ipk] = read_currents();
    if (fabsf(Idc) > I_TRIP_A || Ipk > I_TRIP_A) {
        printf("CAL TRIP: Overcurrent during lock! Idc=%f, Ipk=%f\n", Idc, Ipk);
        hard_stop();
        break;
    }

    // keep Ipk near I_LOCK_MAX
    if (Ipk < I_LOCK_MAX_A * 0.90f) Vd_cmd += Vd_ramp_V_per_s * dt;
    if (Ipk > I_LOCK_MAX_A)         Vd_cmd -= 4.0f * Vd_ramp_V_per_s * dt;

    if (Vd_cmd < Vd_lock_min_V) Vd_cmd = Vd_lock_min_V;
    if (Vd_cmd > Vd_lock_max_V) Vd_cmd = Vd_lock_max_V;

    set_vdq_openloop(Vd_cmd, 0.0f, theta_e_lock, Vdc_meas, max_mod_cal);
    sleep_us(400);
}

// -------- 3) Sample normalized sin/cos while continuing limited lock --------
printf("CAL: Sampling...\n");
float sum_sn=0.0f, sum_cs=0.0f;
const int N = 1200;

for (int i = 0; i < N; i++) {
    measurements->update();

    auto [Idc, Ipk] = read_currents();
    if (fabsf(Idc) > I_TRIP_A || Ipk > I_TRIP_A) {
        printf("CAL TRIP: Overcurrent during sampling! Idc=%f, Ipk=%f\n", Idc, Ipk);
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
float elec_sign = -1.0f;          // flip to -1 if needed
float phase_correction_rad = 0.0f; // usually 0 with your Park, unless wiring/phase mapping differs

float raw_elec = elec_sign * mech_angle * C._PolePairs_unitless;
float desired_elec = wrap_0_2pi(theta_e_lock + phase_correction_rad);

g_Foc._EncoderOffset_Rad = wrap_0_2pi(desired_elec - raw_elec);

g_Foc.Reset();
CurrentCmd._IdCmd_A = 0.0f;
CurrentCmd._IqCmd_A = 2.0f;
g_Foc.ApplyCurrentLimits(CurrentCmd);

printf("CAL DONE: Vd_used~%f V, mech=%f rad, offset=%f rad\n\n",
       Vd_cmd, mech_angle, g_Foc._EncoderOffset_Rad);

// release
hard_stop();


    g_Foc._EncoderOffset_Rad = 3.8f; // WE KNOW THIS IS BEST FOR NOW USE CAL DURING MOTOR DETECTION, THEN STORE THOSE VALUES AND LOAD THEM WHEN NEEDED!!!!




    // Local struct to hold the FOC state safely (No static variables!)
    RtStatus current_status{};

    // Setup Command Context
    CommandContext ctx{};
    ctx.zone_mgr = &zone_mgr;
    ctx.set_ramp_rate = [](float val) { g_RampRate = val; };
    ctx.set_manual_carrier_hz = [](float val) { g_ManualCarrierHz = val; };
    ctx.set_manual_carrier_mode = [](bool val) { g_ManualCarrierMode = val; };
    
    // // Commands route to Core 1
    // ctx.enable = []() { tx_queue.push({CmdType::ENABLE, 0.0f}); };
    // ctx.disable = []() { tx_queue.push({CmdType::DISABLE, 0.0f}); };
    // ctx.emergency_stop = []() { tx_queue.push({CmdType::ESTOP, 0.0f}); };
    // ctx.clear_emergency_stop = []() { tx_queue.push({CmdType::CLEAR_ESTOP, 0.0f}); };
    // ctx.set_target_frequency = [](float val) { tx_queue.push({CmdType::SET_FREQ, val}); };
    // ctx.setEncoderOffset = [](float val) { tx_queue.push({CmdType::SET_ENC_OFFSET, val}); };

    // // Status callback captures our local struct by reference
    // ctx.try_get_status = [&current_status](RtStatus* st) -> bool {
    //     if (!g_Driver) return false;
    //     st->enabled = g_Driver->isEnabled();
    //     st->estop = g_Driver->isEmergencyStopped();
    //     st->current_freq = g_Driver->getCurrentFrequency();
    //     st->modulation_index = g_Driver->getModulationIndex();
    //     st->carrier_hz = g_Driver->getCarrierFrequency();
    //     st->sync_mode = g_Driver->isSynchronousMode();
    //     st->pulses = g_Driver->getPulsesPerCycle();
    //     st->manual_carrier_mode = g_ManualCarrierMode;
    //     st->manual_carrier_hz = g_ManualCarrierHz;
    //     st->ramp_rate = g_RampRate;
        
    //     // Grab the FOC data we sync'd from the queue
    //     st->debug_Vd = current_status.debug_Vd;
    //     st->debug_Vq = current_status.debug_Vq;
    //     st->debug_Iq_measured = current_status.debug_Iq_measured;
    //     st->debug_angle_elec = current_status.debug_angle_elec;
    //     return true;
    // };

    CommandManager::instance().setContext(ctx);
    initializeCommands();
    SerialProcessor serial_proc;

    // Initialize telemetry and bind the measurement system so it can grab hardware data
    Telemetry::init(); 
    Telemetry::bindMeasurementSystem(*measurements);


    // -> LAUNCH CORE 1 <-
    multicore_launch_core1(core1_entry);

    absolute_time_t last_print = get_absolute_time();

    // Local variables just so we have something to print to the console at 1Hz
    float print_vq = 0.0f;
    float print_vd = 0.0f;
    float print_iq = 0.0f;

    while (true) {
        // (Optional: You can eventually put simple serial string parsing here 
        // to push new CurrentCommands to tx_queue. Example:)
        // if (serial_read == "set iq 5.0") { tx_queue.push({0.0f, 5.0f}); }

        // 1. Drain the telemetry queue from Core 1
        TelemetryPacket tp;
        while (rx_queue.try_pop(tp)) {
            // Save latest for console print
            print_vq = tp.vq_v;
            print_vd = tp.vd_v;
            print_iq = tp.iq_meas;

            // Push to Telemetry
            Telemetry::log("CORE1_LOOP_HZ", tp.foc_update_hz);
            Telemetry::log("RAW_ADC_RAD", tp.raw_adc_rad);
            Telemetry::log("THETA_EST_RAD", tp.theta_est);
            Telemetry::log("ELEC_ANGLE", tp.elec_angle);
            Telemetry::log("ENC_OFFSET", tp.enc_offset);
            Telemetry::log("DEBUG_VQ", tp.vq_v);
            Telemetry::log("DEBUG_VD", tp.vd_v);
            // Telemetry::log("DEBUG_IQ_MEAS", tp.iq_meas);


            Telemetry::log("DEBUG_I_ALPHA", tp.i_alpha);
            Telemetry::log("DEBUG_I_BETA", tp.i_beta);

            Telemetry::log("DEBUG_I_D", tp.i_d);
            Telemetry::log("DEBUG_I_Q", tp.i_q);

            Telemetry::log("FAKE_I_U", tp.i_u);
            Telemetry::log("FAKE_I_V", tp.i_v);
            Telemetry::log("FAKE_I_W", tp.i_w);



        }

        // 2. Dispatch telemetry frames
        Telemetry::updateSensors(); 

        // 3. Simple 1Hz console print
        if (absolute_time_diff_us(last_print, get_absolute_time()) > 1000000) {
            printf("FOC State | Vd: %.2f | Vq: %.2f | Iq_meas: %.2f\r\n", print_vd, print_vq, print_iq);
            last_print = get_absolute_time();
        }
    }
}