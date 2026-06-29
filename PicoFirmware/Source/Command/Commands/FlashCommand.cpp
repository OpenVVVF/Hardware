#include "pico/bootrom.h"
#include "../CommandInterface.h"
#include "../CommandContext.h"
#include <cstdio>

class FlashCommand : public CommandInterface {
public:
    FlashCommand() : CommandInterface("flash", "Reboot system in flasher mode") {}
    
    void execute(const ArgValue* args, CommandContext& ctx) override {
        Telemetry::printf("Flashing Pico...");
        reset_usb_boot(0, 0);
    }
};

CommandInterface* makeFlashCommand() {
    static FlashCommand inst;
    return &inst;
}