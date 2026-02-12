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
    zone_mgr.addAsyncFixed(0.0f, 2000.0f, 12000.0f);
}

int main() {
    stdio_init_all();
    sleep_ms(500);


    configureZones();
    CommandContext ctx = RtBridge::initAndGetContext(&zone_mgr);

    CommandManager::instance().setContext(ctx);
    initializeCommands();

    SerialProcessor serial_proc;
    Telemetry::init_default_sensors();
    Telemetry::set_period_us(10000); // 100 Hz
    printf("\r\n3-Phase SPWM Controller\r\n");
    printf("Type 'HELP' or 'h' for commands\r\n");

    static MAX2253x_MultiADC adc_instance({13, 14, 15, 22});
    adc_system = &adc_instance;

    if (!adc_system->init()) {
        printf("FATAL: ADC initialization failed!\n");
        return -1;
    }

    static MeasurementSystem ms_instance(*adc_system);
    measurements = &ms_instance;

    const std::vector<ChannelConfig> channel_map = {
        //{device_index, channel, type, scale, offset, low_pass, name, zero_offset}
        {0, 0, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 0.1f, "V_PH_W", 0.0f},  // Phase W
        {0, 1, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 0.1f, "V_PH_V", 0.0f},  // Phase V
        {0, 2, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 0.1f, "V_PH_U", 0.0f},  // Phase U
        {0, 3, SensorType::VOLTAGE_DIVIDER, 1500.0f, 0.0f, 1.0f, "V_DC_BUS", 0.0f}, // DC Link bus

        {2, 2, SensorType::DIRECT, 1.0f, 0.0f, 1.0f, "ENCODER_SIN", 0.0f}, // Encoder sine
        {2, 1, SensorType::DIRECT, 1.0f, 0.0f, 1.0f, "ENCODER_COS", 0.0f},  // Encoder cosine

        {1, 3, SensorType::BIPOLAR_CURRENT, -1204.8193f, 0.0f, 1.0f, "I_DC_MAIN", 0.410f},
        {1, 1, SensorType::BIPOLAR_CURRENT, 1204.8193f, 0.0f, 1.0f, "I_PH_U", 0.410f},
        {1, 0, SensorType::BIPOLAR_CURRENT, 1204.8193f, 0.0f, 1.0f, "I_PH_W", 0.410f}
        //current v index 1, channel 2
        
    };

    measurements->addChannels(channel_map);
    measurements->update();
    measurements->printChannels();

    printf("\nCalibrating current sensors...\n");
    measurements->calibrateCurrentSensors();
    printf("Current sensor calibration complete.\n\n");

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


        Telemetry::send_frame(*measurements, ctx, sensor_rate_khz, true);

        serial_proc.poll();

       if (absolute_time_diff_us(last_telemetry, get_absolute_time()) > 1000000) {
            float v_dc = measurements->read("V_DC_BUS");
            float v_u  = measurements->read("V_PH_U");
            float v_v  = measurements->read("V_PH_V");
            float v_w  = measurements->read("V_PH_W");

            float i_u  = measurements->read("I_PH_U");
            float i_w  = measurements->read("I_PH_W");


            float i_dc_main = measurements->read("I_DC_MAIN");

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
            
            Telemetry::log("LoopHz", updateCounter / 1000);
            updateCounter = 0;
            // measurements->printChannels();
            last_telemetry = get_absolute_time();
        }

        // ---- Status print using core1 snapshot
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
    }
}   