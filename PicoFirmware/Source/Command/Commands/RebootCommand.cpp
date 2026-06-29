#include "hardware/watchdog.h"
#include "../CommandInterface.h"
#include "../CommandContext.h"
#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

class RebootCommand : public CommandInterface {
public:
    RebootCommand() : CommandInterface("reboot", "Reboot system normally") {}
    
    void execute(const ArgValue* args, CommandContext& ctx) override {
        Telemetry::printf("Rebooting Pico...");
        const uint DP = 24;
        const uint DM = 25;

        // Take over USB pins as GPIO and drive low
        gpio_init(DP); gpio_set_dir(DP, GPIO_OUT); gpio_put(DP, 0);
        gpio_init(DM); gpio_set_dir(DM, GPIO_OUT); gpio_put(DM, 0);

        sleep_ms(500);

        // Release pins back to USB controller
        gpio_set_function(DP, GPIO_FUNC_USB);
        gpio_set_function(DM, GPIO_FUNC_USB);
        watchdog_reboot(0, 0, 10);      // 10 ms
        watchdog_enable(10, false); 
        while (true) tight_loop_contents();
    }
};

CommandInterface* makeRebootCommand() {
    static RebootCommand inst;
    return &inst;
}