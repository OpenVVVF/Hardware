#pragma once

#include <cstdint>
#include <cmath>

#include "Hardware.h"


enum class ZoneType {
    ASYNC_FIXED,
    ASYNC_RAMP,
    SYNC,
    RCFM  // Random Carrier Frequency Modulation
};
static const char* zoneTypeToStr(ZoneType t) {
    switch (t) {
        case ZoneType::ASYNC_FIXED: return "ASYNC-FIX";
        case ZoneType::ASYNC_RAMP:  return "ASYNC-RMP";
        case ZoneType::SYNC:        return "SYNC";
        case ZoneType::RCFM:        return "RCFM";
        default:                    return "DEF";
    }
}

struct ZoneConfig {
    float freq_start;
    float freq_end;
    ZoneType type;
    float param1;        // FIXED: carrier_hz, RAMP: carrier_start_hz, SYNC: pulses_per_cycle, RCFM: center_carrier_hz
    float param2;        // RAMP: carrier_end_hz, RCFM: dither_hz (half-range, i.e., +/- value)
    
    float carrierFreq() const { return param1; }
    float rampStart() const { return param1; }
    float rampEnd() const { return param2; }
    uint16_t pulsesPerCycle() const { return static_cast<uint16_t>(param1); }
    float rcfmCenter() const { return param1; }
    float rcfmDither() const { return param2; }
};

class CommutationManager {
private:
    static constexpr int MAX_ZONES = Hardware::Commutation::MAX_ZONES;
    static constexpr float DEFAULT_CARRIER = Hardware::Commutation::DEFAULT_HZ;
    static constexpr float MIN_CARRIER = Hardware::Limits::Switching::MIN_HZ;
    static constexpr float MAX_CARRIER = Hardware::Limits::Switching::MAX_HZ;
    
    ZoneConfig zones[MAX_ZONES];
    int zone_count = 0;
    
    // RCFM random number generator state (LCG)
    mutable uint32_t rng_state_ = 0xACE1u;  // Seed

public:
    CommutationManager();
    
    void clearZones();
    void addAsyncFixed(float start_freq, float end_freq, float carrier_hz);
    void addAsyncRamp(float start_freq, float end_freq, 
                     float carrier_start, float carrier_end);
    void addSync(float start_freq, float end_freq, uint16_t pulses_per_cycle);
    
    // RCFM: center_carrier is nominal frequency, dither_hz is +/- range (e.g., 200Hz for +/-200Hz)
    void addRCFM(float start_freq, float end_freq, float center_carrier, float dither_hz);
    
    bool getZone(float freq, ZoneConfig* zone) const;
    float calculateCarrier(float freq, const ZoneConfig* zone, 
                          float* sync_pulses = nullptr) const;
    
    int getZoneCount() const { return zone_count; }
    bool isConfigured() const { return zone_count > 0; }
    
    float getCarrierForFrequency(float freq, float* sync_pulses = nullptr) const;
    
    // RNG utility
    void setRngSeed(uint32_t seed) { rng_state_ = seed; }

private:
    // Generate random float in range [-1.0, 1.0]
    float randomFloat() const;



};