#pragma once
#include <cstdint>
#include <cstddef>

class MeasurementSystem;

namespace Telemetry {

static constexpr uint32_t MAGIC   = 0x544C4D31u; // "TLM1"
static constexpr uint8_t  VERSION = 1;

enum MsgType : uint8_t {
    MSG_DATA   = 1,  // (id,value) pairs
    MSG_DEFINE = 2,  // key definitions
};

// Configure periodic send rate (microseconds)
void set_period_us(uint32_t period_us);
uint32_t get_period_us();

// Optional: how many sensor samples we try to include per MSG_DATA frame (chunking).
// 0 = auto (fill frame as much as possible).
void set_sensor_chunk_limit(uint16_t max_sensors_per_frame);
uint16_t get_sensor_chunk_limit();

// Initialize telemetry protocol/state (queues, seq, registries). No MeasurementSystem needed.
void init();

bool printf(const char* fmt, ...);

// Bind MeasurementSystem and register its exposed sensors (queues DEFINE entries).
// Safe to call after Telemetry::init(), and safe to call again if sensors change.
void bindMeasurementSystem(const MeasurementSystem& ms);

// Dynamic logging (extra values beyond MeasurementSystem sensors)
bool log(const char* key, float value);
bool log(const char* key, const char* value); // small strings (capped)

// Sends frames if due. Uses stored MeasurementSystem pointer from bindMeasurementSystem().
// Returns true if at least one frame was written.
bool updateSensors();

// Backwards-compatible helper: binds (if needed) then updates.
bool updateSensors(const MeasurementSystem& ms);

} // namespace Telemetry
