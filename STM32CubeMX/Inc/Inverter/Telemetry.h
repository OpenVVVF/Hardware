#pragma once

#ifdef __cplusplus
  #include <cstdint>
  #include <cstddef>
#else
  #include <stdint.h>
  #include <stddef.h>
#endif

#include "../stm32h7xx_hal.h"

#ifndef TELEMETRY_HAS_MEASUREMENT_SYSTEM
#define TELEMETRY_HAS_MEASUREMENT_SYSTEM 0
#endif

#ifdef __cplusplus

#if TELEMETRY_HAS_MEASUREMENT_SYSTEM
class MeasurementSystem;
#else
class MeasurementSystem {};
#endif

namespace Telemetry {

static constexpr uint32_t MAGIC   = 0x544C4D31u; // "TLM1"
static constexpr uint8_t  VERSION = 1;

enum MsgType : uint8_t {
    MSG_DATA   = 1,
    MSG_DEFINE = 2,
};

void set_period_us(uint32_t period_us);
uint32_t get_period_us();

void set_sensor_chunk_limit(uint16_t max_sensors_per_frame);
uint16_t get_sensor_chunk_limit();

void init();
void init(UART_HandleTypeDef* uart);

bool printf(const char* fmt, ...);

bool log(const char* key, float value);
bool log(const char* key, const char* value);

bool updateSensors();

#if TELEMETRY_HAS_MEASUREMENT_SYSTEM
void bindMeasurementSystem(const MeasurementSystem& ms);
bool updateSensors(const MeasurementSystem& ms);
#else
inline void bindMeasurementSystem(const MeasurementSystem&) {}
inline bool updateSensors(const MeasurementSystem&) { return updateSensors(); }
#endif

void onUartTxComplete(UART_HandleTypeDef* huart);

bool txBusy();
size_t txBytesQueued();

} // namespace Telemetry

#endif // __cplusplus
