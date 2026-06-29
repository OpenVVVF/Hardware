#include "../CommandInterface.h"
#include "../CommandContext.h"
#include <cstdio>

class EncoderOffsetCommand : public CommandInterface {
public:
     EncoderOffsetCommand()
      : CommandInterface("eo", "encoder offset",
            ArgSpec{
            .name = "offset",
            .unit = "arbitrary",
            .min = -10.0f,
            .max = 10.0f,
            .default_val = 0.0f,
            .required = true,
            .type = ArgSpec::FLOAT})
    {}

    void execute(const ArgValue* args, CommandContext& ctx) override {
        const float f = args[0].f_val;
        ctx.setEncoderOffset(f);
    }
};

CommandInterface* makeEncoderOffsetCommand() {
    static EncoderOffsetCommand inst;
    return &inst;
}
