// main.cpp (DROP-IN replacement for multicore RT bridge)
//
// Core1: PWMDriver + update loop + carrier selection (RtBridge)
// Core0: commands + serial + measurements + printing from RtStatus snapshot
//
// Notes:
// - zone_mgr is configured on core0 BEFORE RtBridge starts, then treated as read-only.
// - Remove ALL direct PWMDriver usage from core0.

#include "pico/stdlib.h"
#include <vector>

#include "Switching/CommutationManager.h"
#include "Hardware.h"

#include "RtBridge.h"                // <-- your new bridge
#include "Command/CommandContext.h"
#include "Command/CommandManager.h"
#include "Command/SerialProcessor.h"
#include "Command/CommandInitializer.h"

#include "Sensors/MAX2253x.h"
#include "Sensors/MeasurementSystem.h"
#include "Telemetry.h"

static CommutationManager zone_mgr;

// Measurement system instances
static MAX2253x_MultiADC* adc_system = nullptr;
static MeasurementSystem* measurements = nullptr;

static void configureZones() {
    zone_mgr.clearZones();

    // zone_mgr.addAsyncFixed(0.0f, 10.0f, 2000.0f);
    // zone_mgr.addAsyncRamp(10.0f, 15.0f, 2000.0f, 4000.0f);
    // zone_mgr.addAsyncFixed(15.0f, 20.0f, 4000.0f);


    zone_mgr.addAsyncFixed(0.0f, 2000.0f, 12000.0f);

    // zone_mgr.addRCFM(0.0f, 2000.0f, 1200.0f, 200.0f);
    // -- alstom wmata 2000/3000/6000 switching pattern
    // zone_mgr.addAsyncFixed(0.0f, 8.0f, 1235.0f);
    // zone_mgr.addAsyncFixed(8.0f, 17.0f, 1190.0f);
    // zone_mgr.addAsyncFixed(17.0f, 20.0f, 1210.0f);
    // zone_mgr.addAsyncFixed(20.0f, 25.0f, 1235.0f);
    // zone_mgr.addAsyncFixed(25.0f, 30.0f, 1460.0f);
    // zone_mgr.addAsyncFixed(30.0f, 33.0f, 1210.0f);
    // zone_mgr.addAsyncFixed(33.0f, 50.0f, 1230.0f);
    // zone_mgr.addAsyncFixed(50.0f, 1000.0f, 1190.0f);
}

static const char* zoneTypeToStr(ZoneType t) {
    switch (t) {
        case ZoneType::ASYNC_FIXED: return "ASYNC-FIX";
        case ZoneType::ASYNC_RAMP:  return "ASYNC-RMP";
        case ZoneType::SYNC:        return "SYNC";
        case ZoneType::RCFM:        return "RCFM";
        default:                    return "DEF";
    }
}

int main() {
    stdio_init_all();
    sleep_ms(500);

    // ---------------------------
    // Configure zones on core0
    // (must be "read-only" after RT starts)
    // ---------------------------
    configureZones();

    // ---------------------------
    // Start core1 RT loop + get safe CommandContext (core0 only)
    // ---------------------------
    CommandContext ctx = RtBridge::initAndGetContext(&zone_mgr);

    CommandManager::instance().setContext(ctx);
    initializeCommands();

    SerialProcessor serial_proc;

    printf("\r\n3-Phase SPWM Controller\r\n");
    printf("Type 'HELP' or 'h' for commands\r\n");

    // ---------------------------
    // ADC + Measurement System Setup (core0)
    // ---------------------------
    std::vector<uint8_t> cs_pins = {13, 14, 15, 22}; // 3 devices = 12 channels total

    static MAX2253x_MultiADC adc_instance(cs_pins);
    adc_system = &adc_instance;

    if (!adc_system->init()) {
        printf("FATAL: ADC initialization failed!\n");
        return -1;
    }

    static MeasurementSystem ms_instance(*adc_system);
    measurements = &ms_instance;

    const std::vector<ChannelConfig> channel_map = {
        // Device 0 (CS=13): High Voltage and Auxiliary
        {0, 0, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 0.1f, "V_PH_W", 0.0f},  // Phase W
        {0, 1, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 0.1f, "V_PH_V", 0.0f},  // Phase V
        {0, 2, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 0.1f, "V_PH_U", 0.0f},  // Phase U
        {0, 3, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_DC_BUS", 0.0f}, // DC Link bus

        // Device 2 (CS=15): Encoder signals (filtered for clean angle)
        {2, 2, SensorType::DIRECT, 0.0f, 0.0f, 0.05f, "ENCODER_SIN", 0.0f}, // Encoder sine
        {2, 1, SensorType::DIRECT, 0.0f, 0.0f, 0.05f, "ENCODER_COS", 0.0f},  // Encoder cosine
    
        // //phase v
        // {1, 1, SensorType::DIRECT, 0.0f, 0.0f, 0.05f, "NEW2", 0.0f},
        // //phase u
        // {1, 2, SensorType::DIRECT, 0.0f, 0.0f, 0.05f, "NEW3", 0.0f},


        
        
    };

    ChannelConfig dc_main_current {
        .device_index = 1,
        .channel = 3,
        .type = SensorType::BIPOLAR_CURRENT,
        .scale = -1204.8193f,     // A/V  (because -0.83mV per amp)
        .offset = 0.0f,           // unused for BIPOLAR_CURRENT in your code
        .low_pass_factor = 1.0f,  // pick 0.1-0.3 for smoothing; 1.0 = no filter
        .name = "I_DC_MAIN",
        .zero_offset_volts = 0.410f
};

ChannelConfig u_current {
        .device_index = 1,
        .channel = 1,
        .type = SensorType::BIPOLAR_CURRENT,
        .scale = -1204.8193f,    
        .offset = 0.0f,         
        .low_pass_factor = 1.0f,  
        .name = "I_PH_U",
        .zero_offset_volts = 0.410f
};
// ChannelConfig v_current {
//         .device_index = 1,
//         .channel = 2,
//         .type = SensorType::BIPOLAR_CURRENT,
//         .scale = -1204.8193f,   
//         .offset = 0.0f,           
//         .low_pass_factor = 1.0f,
//         .name = "I_PH_V",
//         .zero_offset_volts = 0.0f
// };
ChannelConfig w_current {
        .device_index = 1,
        .channel = 0,
        .type = SensorType::BIPOLAR_CURRENT,
        .scale = -1204.8193f,
        .offset = 0.0f,
        .low_pass_factor = 1.0f,
        .name = "I_PH_W",
        .zero_offset_volts = 0.410f
};
    measurements->addChannels(channel_map);
    measurements->addChannel(dc_main_current);
    measurements->addChannel(u_current);
     measurements->addChannel(w_current);
    // measurements->addChannel(v_current);
    
    // Initial update to populate values
    measurements->update();
    measurements->printChannels();

    printf("\nCalibrating current sensors...\n");
    sleep_ms(100);
    // measurements->calibrateCurrentSensors();
    float sum = 0.0f;
    float i_u_sum = 0.0f, i_w_sum = 0.0f;
const int N = 200;

for (int i = 0; i < N; i++) {
    measurements->update();                 // get fresh ADC values
    sum += measurements->readRawVoltage("I_DC_MAIN");
    i_u_sum += measurements->readRawVoltage("I_PH_U");
    i_w_sum += measurements->readRawVoltage("I_PH_W");
    sleep_ms(2);
}


measurements->setZeroOffsetVolts("I_DC_MAIN",  sum / N);
measurements->setZeroOffsetVolts("I_PH_U", i_u_sum / N);
measurements->setZeroOffsetVolts("I_PH_W", i_w_sum / N);
    printf("Current sensor calibration complete.\n\n");

    // Optional: decide whether to start enabled from core0.
    // RtBridge core1 currently calls driver.enable() at startup; if you prefer core0 control,
    // remove that enable from RtBridge and uncomment this:
    // if (ctx.enable) ctx.enable();

    absolute_time_t last_print = get_absolute_time();
    absolute_time_t last_telemetry = get_absolute_time();
    unsigned long updateCounter = 0;
    while (true) {
        // ---- Measurements (core0) ----
        measurements->update();
        updateCounter += 1;
        // ---- Binary telemetry at >=100 Hz (core0) ----
static uint32_t last_rate_us = time_us_32();
static uint32_t updates_in_window = 0;
updates_in_window++;

uint32_t now_us = time_us_32();
float sensor_rate_khz = 0.0f;
if ((uint32_t)(now_us - last_rate_us) >= 1000000u) {
    sensor_rate_khz = (float)updates_in_window / 1000.0f; // updates/sec -> kHz
    updates_in_window = 0;
    last_rate_us = now_us;
}

// send frame (default 100 Hz). Set to 5000us if you want 200 Hz, etc.
Telemetry::send_frame(*measurements, ctx, sensor_rate_khz);

        
        
        // ---- Serial commands (core0) ----
        serial_proc.poll();

        // ---- Telemetry output (every 500ms) ----
       if (absolute_time_diff_us(last_telemetry, get_absolute_time()) > 1000000) {
    float v_dc = measurements->read("V_DC_BUS");
    float v_u  = measurements->read("V_PH_U");
    float v_v  = measurements->read("V_PH_V");
    float v_w  = measurements->read("V_PH_W");

    float i_u  = measurements->read("I_PH_U");
    // float i_v  = measurements->read("I_PH_V");
    float i_w  = measurements->read("I_PH_W");
    

    float i_dc_main = measurements->read("I_DC_MAIN");   // <-- main current (A)

    float enc_sin   = measurements->read("ENCODER_SIN");
    float enc_cos   = measurements->read("ENCODER_COS");
    float rotor_pos = measurements->getRotorPositionDegrees();
    printf("\r\n=== Telemetry ===\r\n");
    printf("DC Bus: %6.1fV | I_DC_MAIN: %7.1fA | V_U: %5.1fV | V_V: %5.1fV | V_W: %5.1fV\r\n",
           v_dc, i_dc_main, v_u, v_v, v_w);
    printf("I_PH_U: %7.1fA  |   I_PH_W: %7.1fA\r\n",
          i_u, i_w);
    printf("SIN: %5.5fV | COS: %5.5fV | Rotor: %6.1f°\r\n", enc_sin, enc_cos, rotor_pos);
    printf("Sensor sample rate: %luKHz\r\n", updateCounter / 1000);
    updateCounter = 0;
        // measurements->printChannels();
    last_telemetry = get_absolute_time();
}


        // ---- Status print using core1 snapshot (every 500ms) ----
        if (absolute_time_diff_us(last_print, get_absolute_time()) > 1000000) {
            RtStatus st{};
            const bool have = (ctx.try_get_status && ctx.try_get_status(&st));

            if (!have) {
                printf("RT STATUS: unavailable\r\n");
            } else if (st.estop) {
                printf("EMERGENCY STOP ACTIVE\r\n");
            } else if (!st.enabled) {
                printf("IDLE: Gates LOW, freq=0\r\n");
            } else {
                if (st.manual_carrier_mode) {
                    printf("MANUAL CARRIER F:%6.2fHz Mod:%3.0f%% Car:%4.0fHz [AUTO OFF]\r\n",
                           st.current_freq, st.modulation_index * 100.0f, st.carrier_hz);
                } else {
                    ZoneConfig zone{};
                    const char* zone_str = "DEF";
                    if (zone_mgr.getZone(st.current_freq, &zone)) {
                        zone_str = zoneTypeToStr(zone.type);
                    }

                    printf("%s-%s F:%6.2fHz Mod:%3.0f%% n:%2u Car:%4.0fHz\r\n",
                           zone_str, st.sync_mode ? "SYNC" : "ASYNC",
                           st.current_freq, st.modulation_index * 100.0f,
                           (unsigned)st.pulses, st.carrier_hz);
                }
            }

            last_print = get_absolute_time();
        }

        // sleep_ms(1);
    }
}   