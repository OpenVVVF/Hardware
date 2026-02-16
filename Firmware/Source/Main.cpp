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
    ctx.setEncoderOffset = [](float val) {g_Foc._EncoderOffset_Rad = val; };
    
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
        {0, 0, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_PH_W", 0.0f},
        {0, 1, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_PH_V", 0.0f},
        {0, 2, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_PH_U", 0.0f},
        {0, 3, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_DC_BUS", 0.0f},
        {2, 2, SensorType::DIRECT, 1.0f, 0.0f, 1.0f, "ENCODER_SIN", 0.0f},
        {2, 1, SensorType::DIRECT, 1.0f, 0.0f, 1.0f, "ENCODER_COS", 0.0f},
        {1, 3, SensorType::BIPOLAR_CURRENT, -1204.8193f, 0.0f, 1.0f, "I_DC_MAIN", 0.410f},
        {1, 1, SensorType::BIPOLAR_CURRENT, 1204.8193f, 0.0f, 0.1f, "I_PH_U", 0.410f},
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
    CurrentCmd._IqCmd_A = 2.0f;
    g_Foc.ApplyCurrentLimits(CurrentCmd);

    // 5. Initialize Telemetry
    Telemetry::set_period_us(10000); // 100 Hz
    Telemetry::init(); 
    Telemetry::bindMeasurementSystem(*measurements);




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
const float I_LOCK_MAX_A   = 8.0f;   // <= set to your safe calibration current
const float I_TRIP_A       = 20.0f;  // hard trip (instant stop) - set to safe hardware limit
const float max_mod_cal    = 0.70f;  // extra headroom for calibration
const float theta_e_lock   = 0.0f;

// Use very small starting voltages.
// (On low-R motors, even 1–2V can be plenty.)
const float Vd_lock_min_V  = 0.3f;
const float Vd_lock_max_V  = 3.0f;   // do NOT start with 10V
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




    // 6. Timing Variables for the Main Loop
    absolute_time_t last_print = get_absolute_time();
    absolute_time_t next_foc_tick = get_absolute_time(); // Track 1kHz control loop

    absolute_time_t old_t = get_absolute_time();

    float dt_S = 0.0f;

    // ----------------------------------------------------------------------
    // Unified Run Loop
    // ----------------------------------------------------------------------
    while (true) {
        // Fast asynchronous updates
        measurements->update();
        Telemetry::updateSensors();
        Telemetry::log("ROTOR_DEG", measurements->getRotorPositionDegrees());
        serial_proc.poll();

        dt_S = (float)(get_absolute_time()-old_t)/1'000'000.0f;
        old_t = get_absolute_time();

        Telemetry::log("LOOP_ITERATION_TIMES_S", dt_S);

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
            SenseData._EncoderVelocity_RadPerSec = measurements->getRotorOmegaMechanicalRadPerSec(dt_S);

            g_Foc._DaxisController_.fDtSec = dt_S;
            g_Foc._QaxisController_.fDtSec = dt_S;
            


           // ------------------------------------------------------------------
            // TRUE PLL TRACKING OBSERVER (1kHz)
            // ------------------------------------------------------------------
            static float theta_est = 0.0f;
            static float omega_est = 0.0f;

            float raw_adc_rad = measurements->getRotorPositionDegrees() * 0.01745329251f;

            // Tuning parameters for ~100 rad/s bandwidth
            const float Kp_pll = 200.0f;
            const float Ki_pll = 2000.0f;
            const float dt = dt_S; // 1kHz loop

            // 1. Calculate Error (Shortest path)
            float error = raw_adc_rad - theta_est;
            while (error > 3.14159265f) error -= 6.283185307f;
            while (error < -3.14159265f) error += 6.283185307f;

            // 2. PI Controller to estimate velocity
            omega_est += Ki_pll * error * dt;

            // 3. Integrate to estimate angle
            theta_est += (omega_est + Kp_pll * error) * dt;

            // 4. Wrap the final estimated angle cleanly
            while (theta_est >= 6.283185307f) theta_est -= 6.283185307f;
            while (theta_est < 0.0f) theta_est += 6.283185307f;

            // Feed FOC
            SenseData._EncoderPosition_Rad = theta_est;
            SenseData._EncoderVelocity_RadPerSec = omega_est;


            // Pass the smoothly extrapolated angle to the FOC controller
            // SenseData._EncoderPosition_Rad = smoothed_angle_rad;
            
            Telemetry::log("SMOOTH_DEG", theta_est * 57.29577f);
            Telemetry::log("ENCODER_OFFSET", g_Foc._EncoderOffset_Rad);




            g_Foc.UpdateSensors(SenseData);

            // 2. Execute Modulation if Active
            if (g_Driver && !g_Driver->isEmergencyStopped() && g_Driver->isEnabled()) {
                FocOutput FOC_Out = g_Foc.UpdateVoltages();
                
                PhaseVoltages TargetDutyCycles;
                GenerateSpwm(FOC_Out, 0.95f, TargetDutyCycles);
                 
                g_Driver->setDutyCycles(TargetDutyCycles._Du_unitless, TargetDutyCycles._Dv_unitless, TargetDutyCycles._Dw_unitless);
                updateCarrierFromZones();
            }



            RtStatus st{};
            const bool have = (ctx.try_get_status && ctx.try_get_status(&st));

            Telemetry::log("DEBUG_VQ", st.debug_Vq);
            Telemetry::log("DEBUG_VD", st.debug_Vd);
            Telemetry::log("DEBUG_AngleElec", st.debug_angle_elec);
            Telemetry::log("DEBUG_IQ_MEASURED", st.debug_Iq_measured);

        }



        // ------------------------------------------------------------------
        // 1Hz Console Status Prints
        // ------------------------------------------------------------------
        if (absolute_time_diff_us(last_print, get_absolute_time()) > 1000000) {

            RtStatus st{};
            const bool have = (ctx.try_get_status && ctx.try_get_status(&st));

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