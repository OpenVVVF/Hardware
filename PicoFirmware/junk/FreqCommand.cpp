#include "../CommandInterface.h"
#include "../CommandContext.h"
#include "Hardware.h"
#include <cstdio>

class FreqCommand : public CommandInterface {
public:
     FreqCommand()
      : CommandInterface("F", "Set output frequency",
            ArgSpec{
            .name = "freq",
            .unit = "Hz",
            .min = Hardware::Limits::Fundamental::MIN_HZ,
            .max = Hardware::Limits::Fundamental::MAX_HZ,
            .default_val = 0.0f,
            .required = true,
            .type = ArgSpec::FLOAT})
    {}

    void execute(const ArgValue* args, CommandContext& ctx) override {
        const float f = args[0].f_val;

        RtStatus status{};
        const bool have_status = (ctx.try_get_status && ctx.try_get_status(&status));

        if (have_status && status.estop) {
            printf("Error: Emergency stop active, press E to enable\r\n");
            return;
        }

        if (ctx.set_target_frequency) {
            ctx.set_target_frequency(f);
        } else if (ctx.set_frequency_immediate) {
            ctx.set_frequency_immediate(f);
        } else {
            printf("Error: No frequency control hook available\r\n");
            return;
        }

        if (f != 0.0f && ctx.enable) {
            if (!have_status || !status.enabled) {
                ctx.enable();
            }
        }

        printf("Target freq: %.2f Hz\r\n", f);
    }
};

CommandInterface* makeFreqCommand() {
    static FreqCommand inst;
    return &inst;
}
