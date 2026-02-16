#ifndef COMMAND_CONTEXT_H
#define COMMAND_CONTEXT_H

#include <cstdint>

// Forward declarations
class CommutationManager;

// ======================
// Real-time status snapshot
// (owned by core1, read by core0)
// ======================
struct RtStatus {
    bool enabled;
    bool estop;

    float current_freq;
    float modulation_index;
    float carrier_hz;

    bool sync_mode;
    uint16_t pulses;

    bool manual_carrier_mode;
    float manual_carrier_hz;

    float ramp_rate;


    float debug_Vd;
    float debug_Vq;
    float debug_Iq_measured;
    float debug_Iq_error;
    float debug_angle_elec;

    float encoder_offset;
};

// ======================
// Function pointer types
// ======================
typedef void (*ctx_set_float_fn)(float);
typedef void (*ctx_set_bool_fn)(bool);
typedef void (*ctx_void_fn)();
typedef bool (*ctx_try_get_status_fn)(RtStatus*);

// ======================
// Command context (core0-safe)
// ======================
struct CommandContext {
    // Read-only shared config (must not be modified after core1 starts)
    CommutationManager* zone_mgr;

    // ---- Core0 → Core1 control ----
    ctx_set_float_fn set_ramp_rate;
    ctx_set_float_fn set_manual_carrier_hz;
    ctx_set_bool_fn  set_manual_carrier_mode;

    ctx_void_fn enable;
    ctx_void_fn disable;

    ctx_void_fn emergency_stop;
    ctx_void_fn clear_emergency_stop;

    // Optional
    ctx_set_float_fn set_target_frequency;
    ctx_set_float_fn setEncoderOffset;
    ctx_set_float_fn set_frequency_immediate;

    // ---- Core1 → Core0 telemetry ----
    ctx_try_get_status_fn try_get_status;
};

#endif // COMMAND_CONTEXT_H
