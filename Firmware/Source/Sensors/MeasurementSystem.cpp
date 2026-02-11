// ========================= MeasurementSystem.cpp =========================
#include "MeasurementSystem.h"
#include <algorithm>
#include <cstdio>
#include <cfloat>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// --- MeasurementChannel Implementation ---

MeasurementChannel::MeasurementChannel(const ChannelConfig& cfg)
    : m_config(cfg) {
    m_filtered_value = cfg.offset;  // Initialize to offset
    // Precompute things used every sample
    m_use_low_pass = (cfg.low_pass_factor > 0.0f && cfg.low_pass_factor < 1.0f);

    constexpr float MAX_ADC_VOLTAGE = 1.8f;
    m_fault_low  = 0.01f;
    m_fault_high = MAX_ADC_VOLTAGE - 0.01f;
}

static inline float clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}


void MeasurementChannel::update(float adc_voltage) {
    m_last_raw_voltage = adc_voltage;

    float physical_value = 0.0f;

    switch (m_config.type) {
        case SensorType::VOLTAGE_DIVIDER:
            physical_value = adc_voltage * m_config.scale + m_config.offset;
            break;

        case SensorType::BIPOLAR_CURRENT:
            physical_value = (adc_voltage - m_config.zero_offset_volts) * m_config.scale; // scale is A/V

            break;

        case SensorType::UNIPOLAR_CURRENT:
            physical_value = adc_voltage * m_config.scale + m_config.offset;
            break;

        case SensorType::TEMPERATURE:
            physical_value = adc_voltage * m_config.scale + m_config.offset;
            break;

        case SensorType::THROTTLE:
            physical_value = (adc_voltage - m_config.offset) * m_config.scale;
            physical_value = std::clamp(physical_value, 0.0f, 1.0f);
            break;

        case SensorType::DIRECT:
        default:
            physical_value = adc_voltage + m_config.offset;
            break;
    }

    // Apply low-pass filter if configured
    if (m_config.low_pass_factor < 1.0f && m_config.low_pass_factor > 0.0f) {
        m_filtered_value += (physical_value - m_filtered_value) * m_config.low_pass_factor;
    } else {
        m_filtered_value = physical_value;
    }

   // Fault detection
    m_faulted = (adc_voltage < m_fault_low || adc_voltage > m_fault_high);
}

void MeasurementChannel::calibrateZero(float samples) {
    (void)samples;
    m_accumulator = 0.0f;
    m_sample_count = 0;
}

// --- MeasurementSystem Implementation ---

MeasurementSystem::MeasurementSystem(MAX2253x_MultiADC& adc) : m_adc(adc) {}

// void MeasurementSystem::resetEncoderTracking() {
//     // m_encoder_cal_locked = false;
//     // m_encoder_sin_center_locked = 0.0f;
//     // m_encoder_cos_center_locked = 0.0f;
//     // m_encoder_sin_amp_locked = 1.0f;
//     // m_encoder_cos_amp_locked = 1.0f;
//     //    m_enc_Sxx = 0.0;
//     // m_enc_Sxy = 0.0;
//     // m_enc_Syy2 = 0.0;
//     // m_enc_stats_n = 0;
//     // m_enc_k_locked = 0.0f;
//     // m_enc_inv_y2_rms_locked = 1.0f;
// }

void MeasurementSystem::addChannel(const ChannelConfig& config) {
    auto ptr = std::make_unique<MeasurementChannel>(config);
    MeasurementChannel* raw = ptr.get();

    // Avoid operator[] (can default-construct then assign). Use emplace:
    m_channels.emplace(config.name, std::move(ptr));

    auto it = m_name_to_id.find(config.name);
    if (it == m_name_to_id.end()) {
        const uint16_t new_id = (uint16_t)(m_sensors.size() + 1);

        // Store name in stable vector (so pointers don't depend on unordered_map storage)
        m_sensor_names.push_back(config.name);
        std::string* stable_name = &m_sensor_names.back();

        m_name_to_id.emplace(*stable_name, new_id);
        m_sensors.push_back(SensorEntry{ new_id, stable_name, raw });
    } else {
        // If re-adding a channel with same name (unusual), update pointer
        const uint16_t id = it->second;
        if (id >= 1 && id <= m_sensors.size()) {
            m_sensors[id - 1].ch = raw;
        }
    }
    
     // Ensure per-device arrays are large enough
    const size_t dev = config.device_index;
    if (m_dev_chans.size() <= dev) {
        m_dev_chans.resize(dev + 1);
        m_dev_period_us.resize(dev + 1, PERIOD_1KHZ_US);
        m_dev_last_us.resize(dev + 1, 0);
        m_dev_cache.resize(dev + 1);
        for (auto& a : m_dev_cache) a = {0,0,0,0};
    }

    // Add channel to that device’s update list
    m_dev_chans[dev].push_back(DeviceChan{ raw, static_cast<uint8_t>(config.channel) });

    // Set device period as the minimum required by any channel on it
    const uint32_t p = period_for_channel(config);
    if (p < m_dev_period_us[dev]) m_dev_period_us[dev] = p;

    // Cache encoder pointers if you want (optional)
    if (config.name == "ENCODER_SIN") m_encoder_sin_ch = raw;
    if (config.name == "ENCODER_COS") m_encoder_cos_ch = raw;

    // Physical map (not hot)
    size_t max_dev = config.device_index + 1;
    if (m_physical_map.size() < max_dev * 4) {
        m_physical_map.resize(max_dev * 4, {0, 0});
    }
    size_t linear_idx = config.device_index * 4 + config.channel;
    if (linear_idx < m_physical_map.size()) {
        m_physical_map[linear_idx] = {config.device_index, config.channel};
    }
}


void MeasurementSystem::addChannels(const std::vector<ChannelConfig>& configs) {
    for (const auto& cfg : configs) {
        addChannel(cfg);
    }
}

void MeasurementSystem::update() {
     const uint32_t now = time_us_32();

    // For each device that has any channels mapped:
    for (size_t dev = 0; dev < m_dev_chans.size(); ++dev) {
        if (m_dev_chans[dev].empty()) continue;

        const uint32_t period = m_dev_period_us[dev];
        bool do_read = false;

        if ((uint32_t)(now - m_dev_last_us[dev]) >= m_dev_period_us[dev]) {
            do_read = true;
        }

        if (!do_read) continue;

        // Read this device only
        float v[4];
        m_adc.read_device_voltage_into(dev, v);

        m_dev_cache[dev][0] = v[0];
        m_dev_cache[dev][1] = v[1];
        m_dev_cache[dev][2] = v[2];
        m_dev_cache[dev][3] = v[3];
        m_dev_last_us[dev] = now;

        // Update only channels on this device
        for (const auto& dc : m_dev_chans[dev]) {
            dc.ch->update(m_dev_cache[dev][dc.chan]);
        }
    }
}

float MeasurementSystem::read(const std::string& channel_name) const {
    auto it = m_channels.find(channel_name);
    if (it != m_channels.end()) {
        return it->second->getValue();
    }
    return 0.0f;
}

float MeasurementSystem::getDCBusVoltage() const {
    const char* names[] = {"V_DC", "V_BUS", "HV_BUS", "BATTERY", "DC_BUS"};
    for (const char* name : names) {
        auto val = read(name);
        if (val != 0.0f) return val;
    }
    return 0.0f;
}

float MeasurementSystem::getBatteryVoltage() const {
    const char* names[] = {"V_BATTERY", "V_BAT", "BATTERY"};
    for (const char* name : names) {
        auto it = m_channels.find(name);
        if (it != m_channels.end()) {
            return it->second->getValue();
        }
    }
    return 0.0f;
}

float MeasurementSystem::getPhaseCurrent(uint8_t phase) const {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "I_PHASE_%c", 'A' + phase);
    return read(buf);
}

void MeasurementSystem::printChannels() const {
    std::printf("\n=== Measurement System Channels ===\n");
    std::printf("%-15s %-10s %-12s %-10s %-10s\n", "Name", "Device", "Type", "Raw(V)", "Physical");
    std::printf("---------------------------------------------------------\n");

    for (const auto& [name, ch] : m_channels) {
        const auto& cfg = ch->getConfig();
        const char* type_str = "UNKNOWN";
        switch (cfg.type) {
            case SensorType::VOLTAGE_DIVIDER:  type_str = "HV_VOLT"; break;
            case SensorType::BIPOLAR_CURRENT:  type_str = "BIP_CUR"; break;
            case SensorType::UNIPOLAR_CURRENT: type_str = "UNI_CUR"; break;
            case SensorType::TEMPERATURE:      type_str = "TEMP"; break;
            case SensorType::THROTTLE:         type_str = "THROTTLE"; break;
            case SensorType::DIRECT:           type_str = "DIRECT"; break;
        }

        std::printf("%-15s %-4zu/%-4u %-12s %10.6f   %8.3f\n",
                    name.c_str(), cfg.device_index, cfg.channel, type_str,
                    ch->getRawVoltage(), ch->getValue());
    }
    std::printf("\n");
}

bool MeasurementSystem::isChannelFaulted(const std::string& name) const {
    auto it = m_channels.find(name);
    if (it != m_channels.end()) {
        return it->second->isFaulted();
    }
    return true;
}

float MeasurementSystem::getRotorPositionDegrees() const {
    // Use cached pointers if available (fast), otherwise map lookup fallback.
    const MeasurementChannel* sin_ch = m_encoder_sin_ch;
    const MeasurementChannel* cos_ch = m_encoder_cos_ch;

    if (!sin_ch || !cos_ch) {
        auto sin_it = m_channels.find("ENCODER_SIN");
        auto cos_it = m_channels.find("ENCODER_COS");
        if (sin_it == m_channels.end() || cos_it == m_channels.end()) return NAN;
        sin_ch = sin_it->second.get();
        cos_ch = cos_it->second.get();
    }

    if (sin_ch->isFaulted() || cos_ch->isFaulted()) return NAN;

    const float sin_v = sin_ch->getRawVoltage();
    const float cos_v = cos_ch->getRawVoltage();

    // Fixed center+amp normalization using assumed raw range [0.154, 0.665]
    float sin_n = (sin_v - ENC_CENTER_V) * ENC_INV_AMP_V;
    float cos_n = (cos_v - ENC_CENTER_V) * ENC_INV_AMP_V;

    // Optional: clamp to avoid atan2 weirdness if you overshoot slightly
    // (harmless if your signals stay in range)
    sin_n = std::clamp(sin_n, -1.2f, 1.2f);
    cos_n = std::clamp(cos_n, -1.2f, 1.2f);

    float angle_rad = atan2f(sin_n, cos_n);
    float angle_deg = angle_rad * 180.0f / M_PI;
    if (angle_deg < 0.0f) angle_deg += 360.0f;
    return angle_deg;
}
