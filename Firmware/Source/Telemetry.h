#pragma once
#include <cstdint>
#include <cstddef>

class MeasurementSystem;
struct CommandContext;

namespace Telemetry {

static constexpr uint32_t MAGIC = 0x544C4D31u; // "TLM1"
static constexpr uint8_t  VERSION = 1;

enum MsgType : uint8_t {
    MSG_DATA   = 1,  // (id,value) pairs
    MSG_DEFINE = 2,  // new key definitions
};

// Configure telemetry periodic send rate
void set_period_us(uint32_t period_us);
uint32_t get_period_us();

// Optional: call once at boot to define known sensor keys upfront (so DATA is tiny)
void init(const MeasurementSystem& ms); // uses a built-in list you define in .cpp

// Dynamic logging
bool log(const char* key, float value);
bool log(const char* key, const char* value); // small strings (capped)

// Sends frames if due. Frame may contain queued logs + (optionally) sensor snapshot.
// Returns true if it wrote at least one frame.
bool updateSensors(const MeasurementSystem& ms);

} // namespace Telemetry
