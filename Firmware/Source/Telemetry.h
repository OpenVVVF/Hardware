#pragma once
#include <cstdint>
#include <cstddef>

class MeasurementSystem;
struct CommandContext;
struct RtStatus;

namespace Telemetry {

static constexpr uint32_t MAGIC = 0x544C4D31u; // "TLM1"
static constexpr uint8_t  VERSION = 1;
static constexpr uint8_t  MSG_TELEMETRY = 1;

// Send one telemetry frame (COBS + CRC) to stdio (USB CDC).
// Returns true if it wrote a frame.
bool send_frame(const MeasurementSystem& ms,
                const CommandContext& ctx,
                float sensor_rate_khz);

// Optional: set/override telemetry rate limiter in microseconds
void set_period_us(uint32_t period_us);
uint32_t get_period_us();

} // namespace Telemetry
