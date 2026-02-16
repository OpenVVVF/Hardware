// ========================= MeasurementSystem.h =========================
#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <cmath>
#include <cfloat>  // For FLT_MAX
#include "MAX2253x.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>


enum class SensorType {
    VOLTAGE_DIVIDER,      // HV bus, battery voltage (unipolar)
    BIPOLAR_CURRENT,      // Shunt with offset (e.g., 0.5V = 0A, 2.5V = +Imax)
    UNIPOLAR_CURRENT,     // Shunt single-ended (0V = 0A)
    TEMPERATURE,          // NTC or analog temp sensor
    THROTTLE,             // 0-5V or 0-3.3V potentiometer
    DIRECT                // Raw 0-1.8V, no scaling
};

struct ChannelConfig {
    size_t device_index;      // Which MAX2253x chip (0, 1, 2...)
    uint8_t channel;          // Which channel on that chip (0-3)
    SensorType type;
    float scale;              // Multiplier (V/divider_ratio or A/V_sensitivity)
    float offset;             // Additive offset (volts or amps)
    float low_pass_factor;    // 0.0-1.0 for IIR filter (1.0 = no filter)
    std::string name;         // Human readable name

    // For current sensors: what represents zero current (typically 0.5 or 0.9V for isolated amps)
    float zero_offset_volts;
};

class MeasurementChannel {
public:
    MeasurementChannel(const ChannelConfig& cfg);

    void setZeroOffsetVolts(float v) { m_config.zero_offset_volts = v; }

    // Update with new raw ADC voltage reading (0-1.8V for MAX2253x)
    void update(float adc_voltage);

    // Get physical value (volts, amps, degrees, etc)
    float getValue() const { return m_filtered_value; }
    float getRawVoltage() const { return m_last_raw_voltage; }
    uint16_t getRawADC() const { return m_last_raw_adc; }

    const std::string& getName() const { return m_config.name; }
    const ChannelConfig& getConfig() const { return m_config; }

private:
    ChannelConfig m_config;
    float m_last_raw_voltage = 0.0f;
    uint16_t m_last_raw_adc = 0;
    float m_filtered_value = 0.0f;
    float m_accumulator = 0.0f;  // For calibration
    uint32_t m_sample_count = 0;
    // In MeasurementChannel class:
    bool m_use_low_pass = false;
    float m_fault_low = 0.01f;
    float m_fault_high = 1.79f; // 1.8 - 0.01

};

class MeasurementSystem {
public:

    explicit MeasurementSystem(MAX2253x_MultiADC& adc);

    struct DeviceChan {
        MeasurementChannel* ch;
        uint8_t chan; // 0..3
    };

    static constexpr uint32_t PERIOD_FAST_US = 0;     // every loop
    static constexpr uint32_t PERIOD_5KHZ_US = 200;   // 5 kHz
    static constexpr uint32_t PERIOD_1KHZ_US = 1000;  // 1 kHz
    static constexpr uint32_t PERIOD_18KHZ_US = 55;   // 1e6 / 18000 ≈ 55.56us
    
    
    std::vector<std::vector<DeviceChan>> m_dev_chans;   // [device] -> channels on that device
    std::vector<uint32_t> m_dev_period_us;              // [device] -> desired period
    std::vector<uint32_t> m_dev_last_us;                // [device] -> last read time_us_32()
    std::vector<std::array<float,4>> m_dev_cache;       // [device] -> last volta
    
    static inline uint32_t period_for_channel(const ChannelConfig& cfg) {
        // Currents: cap at ~18 kHz
        if (cfg.type == SensorType::BIPOLAR_CURRENT || cfg.type == SensorType::UNIPOLAR_CURRENT) {
            return PERIOD_18KHZ_US;
        }
        if (cfg.type == SensorType::VOLTAGE_DIVIDER) {
            return PERIOD_5KHZ_US;   // 200us
        }
        return PERIOD_1KHZ_US;       // 1000us
    }


// Cached encoder channel pointers (avoid map find in loop)
MeasurementChannel* m_encoder_sin_ch = nullptr;
MeasurementChannel* m_encoder_cos_ch = nullptr;
    // Register channels - call this once at startup
    void addChannel(const ChannelConfig& config);
    void addChannels(const std::vector<ChannelConfig>& configs);

    // Read physical values
    float read(const std::string& channel_name) const;
    float read(size_t device_idx, uint8_t channel) const;

    // Batch update - call in your main loop
    void update();

    // Diagnostics
    void printChannels() const;

    float getRotorOmegaMechanicalRadPerSec(float dt_s) const;
    // Zero calibration for current sensors
    void calibrateCurrentSensors();
    static inline float wrapDeltaDeg(float delta_deg) {
    // result in (-180, 180]
    delta_deg = fmodf(delta_deg + 180.0f, 360.0f);
    if (delta_deg < 0.0f) delta_deg += 360.0f;
    return delta_deg - 180.0f;
}


    // --- Sin/Cos Encoder Methods ---
    float getRotorPositionDegrees() const; // Get angle (0-360°)

    bool setZeroOffsetVolts(const std::string& name, float v) {
        auto it = m_channels.find(name);
        if (it == m_channels.end()) return false;
        it->second->setZeroOffsetVolts(v);
        return true;
    }
    float readRawVoltage(const std::string& channel_name) const {
        auto it = m_channels.find(channel_name);
        if (it != m_channels.end()) return it->second->getRawVoltage();
        return NAN;
    }


    

private:
    mutable float m_prev_deg = NAN;
    mutable float m_omega_m_rad_s = 0.0f;
    MAX2253x_MultiADC& m_adc;
    std::unordered_map<std::string, std::unique_ptr<MeasurementChannel>> m_channels;
    std::vector<std::pair<size_t, uint8_t>> m_physical_map;  // Reverse lookup



    // --- Fixed sin/cos encoder calibration (raw volts) ---
    static constexpr float ENC_RAW_MIN_V = 0.154f;
    static constexpr float ENC_RAW_MAX_V = 0.665f;

    // Center and amplitude derived from fixed min/max
    static constexpr float ENC_CENTER_V  = (ENC_RAW_MIN_V + ENC_RAW_MAX_V) * 0.5f;
    static constexpr float ENC_AMP_V     = (ENC_RAW_MAX_V - ENC_RAW_MIN_V) * 0.5f;
    static constexpr float ENC_INV_AMP_V = 1.0f / ENC_AMP_V;


    public:
    // --- Telemetry / external read-only sensor registry ---
    struct SensorEntry {
        uint16_t id;                 // stable within a boot (or stable across boots if you keep channel add order fixed)
        std::string name;            // owned storage (NO dangling pointers)
        MeasurementChannel* ch;      // pointer owned by MeasurementSystem via unique_ptr in m_channels
    };

    // Telemetry can iterate this in init() to register names, and in update() to read values.
    const std::vector<SensorEntry>& sensors() const { return m_sensors; }
    size_t sensorCount() const { return m_sensors.size(); }

    // Optional convenience: O(1) access by id (since ids are 1..N)
    const SensorEntry* findSensorById(uint16_t id) const {
        if (id == 0 || id > m_sensors.size()) return nullptr;
        return &m_sensors[id - 1];
    }

private:
    std::unordered_map<std::string, uint16_t> m_name_to_id;
    std::vector<SensorEntry> m_sensors;

};
