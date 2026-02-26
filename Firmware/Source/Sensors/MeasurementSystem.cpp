// ========================= MeasurementSystem.cpp =========================
#include "MeasurementSystem.h"
#include <algorithm>
#include <cstdio>
#include <cfloat>
#include <math.h>

MeasurementChannel::MeasurementChannel(const ChannelConfig& cfg)
    : m_config(cfg) {
    m_filtered_value = cfg.offset;  
    m_use_low_pass = (cfg.low_pass_factor > 0.0f && cfg.low_pass_factor < 1.0f);

    constexpr float MAX_ADC_VOLTAGE = 1.8f;
    m_fault_low  = 0.01f;
    m_fault_high = MAX_ADC_VOLTAGE - 0.01f;
}

static inline float clamp(float x) {
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
            physical_value = (adc_voltage - m_config.zero_offset_volts) * m_config.scale; 
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

    if (m_config.low_pass_factor < 1.0f && m_config.low_pass_factor > 0.0f) {
        m_filtered_value += (physical_value - m_filtered_value) * m_config.low_pass_factor;
    } else {
        m_filtered_value = physical_value;
    }
}

MeasurementSystem::MeasurementSystem(MAX2253x_MultiADC& adc) : m_adc(adc) {}

void MeasurementSystem::addChannel(const ChannelConfig& config) {
    auto ptr = std::make_unique<MeasurementChannel>(config);
    MeasurementChannel* raw = ptr.get();

    m_channels.emplace(config.name, std::move(ptr));

    auto it = m_name_to_id.find(config.name);
    if (it == m_name_to_id.end()) {
        const uint16_t new_id = static_cast<uint16_t>(m_sensors.size() + 1);
        m_name_to_id.emplace(config.name, new_id);
        m_sensors.push_back(SensorEntry{ new_id, config.name, raw });
    } else {
        const uint16_t id = it->second;
        if (id >= 1 && id <= m_sensors.size()) {
            m_sensors[id - 1].ch = raw;
        }
    }

    const size_t dev = config.device_index;
    if (m_dev_chans.size() <= dev) {
        m_dev_chans.resize(dev + 1);
        m_dev_period_us.resize(dev + 1, PERIOD_1KHZ_US);
        m_dev_last_us.resize(dev + 1, 0);
        m_dev_cache.resize(dev + 1);
        for (auto& a : m_dev_cache) a = {0,0,0,0};
    }

    m_dev_chans[dev].push_back(DeviceChan{ raw, static_cast<uint8_t>(config.channel) });

    const uint32_t p = period_for_channel(config);
    if (p < m_dev_period_us[dev]) m_dev_period_us[dev] = p;

    if (config.name == "ENCODER_SIN") m_encoder_sin_ch = raw;
    if (config.name == "ENCODER_COS") m_encoder_cos_ch = raw;

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
    for (size_t dev = 0; dev < m_dev_chans.size(); ++dev) {
        if (m_dev_chans[dev].empty()) continue;

        bool do_read = false;
        if ((uint32_t)(now - m_dev_last_us[dev]) >= m_dev_period_us[dev]) {
            do_read = true;
        }
        if (!do_read) continue;

        float v[4];
        m_adc.read_device_voltage_into(dev, v);

        m_dev_cache[dev][0] = v[0];
        m_dev_cache[dev][1] = v[1];
        m_dev_cache[dev][2] = v[2];
        m_dev_cache[dev][3] = v[3];
        m_dev_last_us[dev] = now;

        for (const auto& dc : m_dev_chans[dev]) {
            dc.ch->update(m_dev_cache[dev][dc.chan]);
        }
    }
}

bool MeasurementSystem::update_from_dma() {
    if (!m_adc.is_async_ready()) {
        return false; 
    }
    

    
    m_adc.process_async_data();
    

    for (size_t dev = 0; dev < m_dev_chans.size(); ++dev) {
        if (m_dev_chans[dev].empty()) continue;
        const float* v = m_adc.get_cached_device_voltage(dev);
        for (const auto& dc : m_dev_chans[dev]) {
            dc.ch->update(v[dc.chan]); 
        }
    }
    return true; 
}

float MeasurementSystem::read(const std::string& channel_name) const {
    auto it = m_channels.find(channel_name);
    if (it != m_channels.end()) {
        return it->second->getValue();
    }
    return 0.0f;
}

void MeasurementSystem::calibrateCurrentSensors() {
    std::unordered_map<MeasurementChannel*, float> volts;
    for (const auto& [name, ch] : m_channels) {
        const auto& cfg = ch->getConfig();
        if (cfg.type == SensorType::BIPOLAR_CURRENT || cfg.type == SensorType::UNIPOLAR_CURRENT) {
            volts.emplace(ch.get(), 0.0f);
        }
    }
    const int samples = 200;
    for (int i = 1; i <= samples; i++) {
        update();
        for (auto& [channel, v] : volts) {
            v += channel->getRawVoltage();
            if(i == samples) channel->setZeroOffsetVolts(v / samples);
        }
        sleep_ms(2);
    }
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

float MeasurementSystem::getRotorPositionDegrees() const {
    const MeasurementChannel* sin_ch = m_encoder_sin_ch;
    const MeasurementChannel* cos_ch = m_encoder_cos_ch;

    if (!sin_ch || !cos_ch) {
        auto sin_it = m_channels.find("ENCODER_SIN");
        auto cos_it = m_channels.find("ENCODER_COS");
        if (sin_it == m_channels.end() || cos_it == m_channels.end()) return NAN;
        sin_ch = sin_it->second.get();
        cos_ch = cos_it->second.get();
    }

    const float sin_v = sin_ch->getRawVoltage();
    const float cos_v = cos_ch->getRawVoltage();

    float sin_n = (sin_v - ENC_CENTER_V) * ENC_INV_AMP_V;
    float cos_n = (cos_v - ENC_CENTER_V) * ENC_INV_AMP_V;

    sin_n = std::clamp(sin_n, -1.2f, 1.2f);
    cos_n = std::clamp(cos_n, -1.2f, 1.2f);

    float angle_rad = atan2f(sin_n, cos_n);
    float angle_deg = angle_rad * 180.0f / M_PI;
    if (angle_deg < 0.0f) angle_deg += 360.0f;
    return angle_deg;
}

float MeasurementSystem::getRotorOmegaMechanicalRadPerSec(float dt_s) const {
    if (dt_s <= 0.0f) return NAN;

    const float deg = getRotorPositionDegrees();
    if (!isfinite(deg)) return NAN;

    if (!isfinite(m_prev_deg)) {
        m_prev_deg = deg;
        m_omega_m_rad_s = 0.0f;
        return m_omega_m_rad_s;
    }

    const float ddeg = wrapDeltaDeg(deg - m_prev_deg);
    m_prev_deg = deg;

    const float drad = ddeg * (float)M_PI / 180.0f;
    m_omega_m_rad_s = drad / dt_s;
    return m_omega_m_rad_s;
}