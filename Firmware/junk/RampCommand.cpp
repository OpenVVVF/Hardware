#include "../CommandInterface.h"
#include "../CommandContext.h"
#include "Hardware.h"
#include <cstdio>

#include "Command/RtCtxHelpers.h"

class RampCommand : public CommandInterface {
public:
    RampCommand()
      : CommandInterface("R", "Set ramp rate",
            ArgSpec{
            .name = "rate",
            .unit = "Hz/s",
            .min = 0.1f,
            .max = Hardware::Limits::Fundamental::MAX_RAMP_HZ_S,
            .default_val = 5.0f,
            .required = true,
            .type = ArgSpec::FLOAT
        })
    {}

    void execute(const ArgValue* args, CommandContext& ctx) override {
        const float r = args[0].f_val;
        ctx_set_ramp(ctx, r);

        printf("Ramp rate: %.2f Hz/s\r\n", r);
    }
};

CommandInterface* makeRampCommand() {
    static RampCommand inst;
    return &inst;
}