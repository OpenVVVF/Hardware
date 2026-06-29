#pragma once
#include "CommandContext.h"
#include <cstdio>

// Best-effort: set a float param
static inline void ctx_set_ramp(CommandContext& ctx, float r) {
    if (ctx.set_ramp_rate) ctx.set_ramp_rate(r);
}

static inline void ctx_set_manual_carrier(CommandContext& ctx, bool enable, float hz = 0.0f) {
    if (ctx.set_manual_carrier_mode) ctx.set_manual_carrier_mode(enable);
    if (enable && ctx.set_manual_carrier_hz) ctx.set_manual_carrier_hz(hz);
}

static inline bool ctx_get_status(CommandContext& ctx, RtStatus* out) {
    return (ctx.try_get_status && ctx.try_get_status(out));
}

static inline bool ctx_is_estop(CommandContext& ctx) {
    RtStatus st{};
    return ctx_get_status(ctx, &st) ? st.estop : false;
}

static inline bool ctx_is_enabled(CommandContext& ctx) {
    RtStatus st{};
    return ctx_get_status(ctx, &st) ? st.enabled : false;
}
