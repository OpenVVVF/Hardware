#include <vector>
#include "pico/stdlib.h"
#include "Switching/CommutationManager.h"
#include "Command/CommandContext.h"
#include "Command/CommandManager.h"
#include "Command/SerialProcessor.h"
#include "Command/CommandInitializer.h"
#include "Sensors/MAX2253x.h"
#include "Sensors/MeasurementSystem.h"
#include "Telemetry.h"
#include "Hardware.h"
#include "RtBridge.h"

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

    Telemetry::set_period_us(10000); // 100 Hz
    Telemetry::init(); 
    Telemetry::bindMeasurementSystem(*measurements);

    absolute_time_t last_print = get_absolute_time();
    absolute_time_t last_telemetry = get_absolute_time();
    unsigned long updateCounter = 0;
    while (true) {
        // ---- Measurements (core0) ----
        measurements->update();
        updateCounter += 1;

        Telemetry::updateSensors();
        Telemetry::log("ROTOR_DEG", measurements->getRotorPositionDegrees());
        serial_proc.poll();

    //    if (absolute_time_diff_us(last_telemetry, get_absolute_time()) > 10000) {
    //         // Telemetry::log("LoopHz", );
    //         updateCounter = 0;
    //         // measurements->printChannels();
    //         last_telemetry = get_absolute_time();
    //     }

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