#include "../CommandInterface.h"
#include "../CommandContext.h"
#include "Hardware.h"
#include <cstdio>

class ImmediateCommand : public CommandInterface {
public:
    ImmediateCommand()
      : CommandInterface("I", "Set frequency immediately (no ramp)",
            ArgSpec{
            .name = "freq",
            .unit = "Hz",
            .min = -200.0f,
            .max = 200.0f,
            .default_val = 0.0f,
            .required = true,
            .type = ArgSpec::FLOAT
        })
    {}

    void execute(const ArgValue* args, CommandContext& ctx) override {
        const float f = args[0].f_val;

        RtStatus status{};
        const bool have_status = (ctx.try_get_status && ctx.try_get_status(&status));

        if (have_status && status.estop) {
            printf("Error: Emergency stop active\r\n");
            return;
        }

        if (ctx.set_frequency_immediate) {
            ctx.set_frequency_immediate(f);
        } else if (ctx.set_target_frequency) {
            ctx.set_target_frequency(f);
        } else {
            printf("Error: No frequency control hook available\r\n");
            return;
        }
        if (f != 0.0f && ctx.enable) {
            if (!have_status || !status.enabled) {
                ctx.enable();
            }
        }

        printf("Immediate: %.2f Hz\r\n", f);
    }
};

CommandInterface* makeImmediateCommand() {
    static ImmediateCommand inst;
    return &inst;
}