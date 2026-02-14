#include "RtBridge.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "hardware/sync.h"

#include "Switching/PWMDriver.h"

// -----------------------------
// Core0 -> Core1 command queue
// -----------------------------
enum class RtCmd : uint8_t {
    SET_RAMP_RATE,
    SET_MANUAL_CARRIER_HZ,
    SET_MANUAL_CARRIER_MODE,

    ENABLE,
    DISABLE,

    ESTOP,
    CLEAR_ESTOP,

    SET_TARGET_FREQ,
    SET_FREQ_IMMEDIATE,
};

struct RtMsg {
    RtCmd cmd;
    float f;      // float payload
    uint32_t u;   // int/bool payload
};

static queue_t g_q;

// -----------------------------
// Core1 -> Core0 status sharing (seqlock)
// -----------------------------
struct RtStatusShared {
    volatile uint32_t seq;
    RtStatus status;
};

static RtStatusShared g_shared;

static inline void status_write(const RtStatus& in) {
    g_shared.seq++;
    __dmb();
    g_shared.status = in;
    __dmb();
    g_shared.seq++;
}

static inline bool status_read(RtStatus* out) {
    uint32_t a = g_shared.seq;
    __dmb();
    RtStatus snap = g_shared.status;
    __dmb();
    uint32_t b = g_shared.seq;
    if (a != b || (a & 1u)) return false;
    *out = snap;
    return true;
}

// -----------------------------
// Shared (but effectively immutable) pointer
// -----------------------------
static CommutationManager* g_zone_mgr = nullptr;

// -----------------------------
// Core1-owned runtime variables
// -----------------------------
static PWMDriver* g_driver = nullptr;
static SPWMStrategy g_spwm;

static float g_ramp_rate = 5.0f; // Hz/s
static float g_manual_carrier_hz = 2000.0f;
static bool  g_manual_carrier_mode = false;

// Cache to avoid redundant PWM reprogramming
static float    last_carrier_hz = 0.0f;
static bool     last_sync_mode  = false;
static uint16_t last_pulses     = 0;

// -----------------------------
// Carrier update logic (core1)
// -----------------------------
static void updateCarrierFromZones_core1() {
    if (!g_driver || !g_driver->isEnabled()) return;

    if (g_manual_carrier_mode) {
        if (last_carrier_hz != g_manual_carrier_hz || last_sync_mode != false) {
            g_driver->setCarrierFrequency(g_manual_carrier_hz);
            g_driver->setSynchronousMode(false, 0);
            last_carrier_hz = g_manual_carrier_hz;
            last_sync_mode = false;
            last_pulses = 0;
        }
        return;
    }

    float current_freq = g_driver->getCurrentFrequency();
    ZoneConfig zone{};
    float sync_pulses_f = 0.0f;

    if (g_zone_mgr && g_zone_mgr->getZone(current_freq, &zone)) {
        float carrier = g_zone_mgr->calculateCarrier(current_freq, &zone, &sync_pulses_f);
        bool sync_mode = (zone.type == ZoneType::SYNC);
        uint16_t pulses = sync_mode ? (uint16_t)sync_pulses_f : 0;

        if (last_carrier_hz != carrier || last_sync_mode != sync_mode || last_pulses != pulses) {
            g_driver->setCarrierFrequency(carrier);
            g_driver->setSynchronousMode(sync_mode, pulses);
            last_carrier_hz = carrier;
            last_sync_mode = sync_mode;
            last_pulses = pulses;
        }
    }
}

// -----------------------------
// Core1: apply messages
// -----------------------------
static void apply_msg(const RtMsg& m) {
    switch (m.cmd) {
        case RtCmd::SET_RAMP_RATE:
            g_ramp_rate = m.f;
            break;

        case RtCmd::SET_MANUAL_CARRIER_HZ:
            g_manual_carrier_hz = m.f;
            break;

        case RtCmd::SET_MANUAL_CARRIER_MODE:
            g_manual_carrier_mode = (m.u != 0);
            break;

        case RtCmd::ENABLE:
            g_driver->enable();
            break;

        case RtCmd::DISABLE:
            g_driver->disable();
            break;

        case RtCmd::ESTOP:
            g_driver->emergencyStop();
            break;

        case RtCmd::CLEAR_ESTOP:
            g_driver->clearEmergency();
            break;

        case RtCmd::SET_TARGET_FREQ:
            // ramp_rate is core1-owned; every target set uses latest ramp
            g_driver->setTargetFrequency(m.f, g_ramp_rate);
            break;

        case RtCmd::SET_FREQ_IMMEDIATE:
            g_driver->setFrequencyImmediate(m.f);
            break;
    }
}

// -----------------------------
// Core1 loop
// -----------------------------
static void core1_entry() {
    // Create driver on core1 so all driver state lives on core1
    PWMDriver::Config cfg;
    cfg.min_duty_percent = 1.0f;
    cfg.max_duty_percent = 99.0f;

    static PWMDriver driver(cfg);
    g_driver = &driver;

    driver.setStrategy(&g_spwm);
    driver.setAutoModulation(true);
    driver.init(2000.0f);

    driver.enable();

    absolute_time_t next = make_timeout_time_us(1000);

    while (true) {
        // Drain control queue quickly
        RtMsg msg{};
        while (queue_try_remove(&g_q, &msg)) {
            apply_msg(msg);
        }

        // 1kHz tick (replace with tighter tick if you want)
        if (absolute_time_diff_us(get_absolute_time(), next) <= 0) {
            next = delayed_by_us(next, 1000);

            if (!driver.isEmergencyStopped()) {
                driver.update(0.001f);
                updateCarrierFromZones_core1();
            }

            // Publish status snapshot
            RtStatus st{};
            st.enabled = driver.isEnabled();
            st.estop = driver.isEmergencyStopped();
            st.current_freq = driver.getCurrentFrequency();
            st.modulation_index = driver.getModulationIndex();
            st.carrier_hz = driver.getCarrierFrequency();
            st.sync_mode = driver.isSynchronousMode();
            st.pulses = driver.getPulsesPerCycle();
            st.manual_carrier_mode = g_manual_carrier_mode;
            st.manual_carrier_hz = g_manual_carrier_hz;
            st.ramp_rate = g_ramp_rate;

            status_write(st);
        }

        tight_loop_contents();
    }
}

// -----------------------------
// Core0 enqueue helpers (used by CommandContext function pointers)
// -----------------------------
static inline void q_push_void(RtCmd c) {
    RtMsg m{c, 0.0f, 0};
    (void)queue_try_add(&g_q, &m);
}

static inline void q_push_f(RtCmd c, float v) {
    RtMsg m{c, v, 0};
    (void)queue_try_add(&g_q, &m);
}

static inline void q_push_b(RtCmd c, bool b) {
    RtMsg m{c, 0.0f, b ? 1u : 0u};
    (void)queue_try_add(&g_q, &m);
}

// These must be real function pointers (no capturing lambdas)
static void ctx_set_ramp_rate(float v)          { q_push_f(RtCmd::SET_RAMP_RATE, v); }
static void ctx_set_manual_carrier_hz(float v)  { q_push_f(RtCmd::SET_MANUAL_CARRIER_HZ, v); }
static void ctx_set_manual_carrier_mode(bool b) { q_push_b(RtCmd::SET_MANUAL_CARRIER_MODE, b); }

static void ctx_enable()  { q_push_void(RtCmd::ENABLE); }
static void ctx_disable() { q_push_void(RtCmd::DISABLE); }

static void ctx_estop()        { q_push_void(RtCmd::ESTOP); }
static void ctx_clear_estop()  { q_push_void(RtCmd::CLEAR_ESTOP); }

static void ctx_set_target_freq(float v)        { q_push_f(RtCmd::SET_TARGET_FREQ, v); }
static void ctx_set_freq_immediate(float v)     { q_push_f(RtCmd::SET_FREQ_IMMEDIATE, v); }

static bool ctx_try_get_status(RtStatus* out) { return status_read(out); }

// -----------------------------
// Public API
// -----------------------------
CommandContext RtBridge::initAndGetContext(CommutationManager* zone_mgr) {
    g_zone_mgr = zone_mgr;

    // queue capacity: tune as you like
    queue_init(&g_q, sizeof(RtMsg), 32);

    g_shared.seq = 0;

    multicore_launch_core1(core1_entry);

    CommandContext ctx{};
    ctx.zone_mgr = zone_mgr;

    ctx.set_ramp_rate = ctx_set_ramp_rate;
    ctx.set_manual_carrier_hz = ctx_set_manual_carrier_hz;
    ctx.set_manual_carrier_mode = ctx_set_manual_carrier_mode;

    ctx.enable = ctx_enable;
    ctx.disable = ctx_disable;

    ctx.emergency_stop = ctx_estop;
    ctx.clear_emergency_stop = ctx_clear_estop;

    ctx.set_target_frequency = ctx_set_target_freq;
    ctx.set_frequency_immediate = ctx_set_freq_immediate;

    ctx.try_get_status = ctx_try_get_status;

    return ctx;
}
