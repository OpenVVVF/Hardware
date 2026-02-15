#include "../CommandInterface.h"
#include "../CommandContext.h"
#include <Hardware.h>
#include <cstdio>

class CarrierCommand : public CommandInterface {
public:
 CarrierCommand()
      : CommandInterface("EN", "Enable output",
            ArgSpec{ .name = "freq",
            .unit = "Hz",
            .min = Hardware::Limits::Switching::MIN_HZ,
            .max = Hardware::Limits::Switching::MAX_HZ,
            .default_val = 2000,
            .required = true,
            .type = ArgSpec::FLOAT})
    {}

    void execute(const ArgValue* args, CommandContext& ctx) override {
        float carrier = args[0].f_val;

        ctx.set_manual_carrier_hz(carrier);
        ctx.set_manual_carrier_mode(true);

        printf("Manual carrier: %.1f Hz (AUTO mode OFF)\r\n", carrier);
    }
};

CommandInterface* makeCarrierCommand() {
    static CarrierCommand inst;
    return &inst;
}
