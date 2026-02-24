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
#include <cfloat>  
#include "MAX2253x.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

enum class SensorType {
    VOLTAGE_DIVIDER,      
    BIPOLAR_CURRENT,      
    UNIPOLAR_CURRENT,     
    TEMPERATURE,          
    THROTTLE,             
    DIRECT                
};

struct ChannelConfig {
    size_t device_index;      
    uint8_t channel;          
    SensorType type;
    float scale;              
    float offset;             
    float low_pass_factor;    
    std::string name;         
    float zero_offset_volts;
};

class MeasurementChannel {
public:
    MeasurementChannel(const ChannelConfig& cfg);
    void setZeroOffsetVolts(float v) { m_config.zero_offset_volts = v; }
    void update(float adc_voltage);

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
    float m_accumulator = 0.0f;  
    uint32_t m_sample_count = 0;
    bool m_use_low_pass = false;
    float m_fault_low = 0.01f;
    float m_fault_high = 1.79f; 
};

class MeasurementSystem {
public:
    explicit MeasurementSystem(MAX2253x_MultiADC& adc);

    struct DeviceChan {
        MeasurementChannel* ch;
        uint8_t chan; 
    };

    static constexpr uint32_t PERIOD_FAST_US = 0;     
    static constexpr uint32_t PERIOD_5KHZ_US = 200;   
    static constexpr uint32_t PERIOD_1KHZ_US = 1000;  
    static constexpr uint32_t PERIOD_18KHZ_US = 55;   
    
    std::vector<std::vector<DeviceChan>> m_dev_chans;   
    std::vector<uint32_t> m_dev_period_us;              
    std::vector<uint32_t> m_dev_last_us;                
    std::vector<std::array<float,4>> m_dev_cache;       
    
    static inline uint32_t period_for_channel(const ChannelConfig& cfg) {
        if (cfg.type == SensorType::BIPOLAR_CURRENT || cfg.type == SensorType::UNIPOLAR_CURRENT) {
            return PERIOD_18KHZ_US;
        }
        if (cfg.type == SensorType::VOLTAGE_DIVIDER) {
            return PERIOD_5KHZ_US;   
        }
        return PERIOD_1KHZ_US;       
    }

    MeasurementChannel* m_encoder_sin_ch = nullptr;
    MeasurementChannel* m_encoder_cos_ch = nullptr;

    void addChannel(const ChannelConfig& config);
    void addChannels(const std::vector<ChannelConfig>& configs);

    float read(const std::string& channel_name) const;
    float read(size_t device_idx, uint8_t channel) const;

    // --- CPU Polling update (use during startup/calibration) ---
    void update();
    
    // --- DMA Asynchronous update (use in your fast FOC loop) ---
    bool update_from_dma();

    void printChannels() const;
    float getRotorOmegaMechanicalRadPerSec(float dt_s) const;
    void calibrateCurrentSensors();

    static inline float wrapDeltaDeg(float delta_deg) {
        delta_deg = fmodf(delta_deg + 180.0f, 360.0f);
        if (delta_deg < 0.0f) delta_deg += 360.0f;
        return delta_deg - 180.0f;
    }

    float getRotorPositionDegrees() const; 

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
    std::vector<std::pair<size_t, uint8_t>> m_physical_map;  

    static constexpr float ENC_RAW_MIN_V = 0.154f;
    static constexpr float ENC_RAW_MAX_V = 0.665f;
    static constexpr float ENC_CENTER_V  = (ENC_RAW_MIN_V + ENC_RAW_MAX_V) * 0.5f;
    static constexpr float ENC_AMP_V     = (ENC_RAW_MAX_V - ENC_RAW_MIN_V) * 0.5f;
    static constexpr float ENC_INV_AMP_V = 1.0f / ENC_AMP_V;

public:
    struct SensorEntry {
        uint16_t id;                 
        std::string name;            
        MeasurementChannel* ch;      
    };

    const std::vector<SensorEntry>& sensors() const { return m_sensors; }
    size_t sensorCount() const { return m_sensors.size(); }

    const SensorEntry* findSensorById(uint16_t id) const {
        if (id == 0 || id > m_sensors.size()) return nullptr;
        return &m_sensors[id - 1];
    }

private:
    std::unordered_map<std::string, uint16_t> m_name_to_id;
    std::vector<SensorEntry> m_sensors;
};