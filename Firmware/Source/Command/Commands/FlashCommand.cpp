#ifndef UNIT_TEST
#include "pico/bootrom.h"
#endif

#include "../CommandInterface.h"
#include "../CommandContext.h"
#include <cstdio>

class FlashCommand : public CommandInterface {
public:
    FlashCommand() : CommandInterface("flash", "Reboot system in flasher mode") {}
    
    void execute(const ArgValue* args, CommandContext& ctx) override {
#ifndef UNIT_TEST
        reset_usb_boot(0, 0);
#endif
    }
};

CommandInterface* makeFlashCommand() {
    static FlashCommand inst;
    return &inst;
}