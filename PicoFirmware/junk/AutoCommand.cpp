#include "../CommandInterface.h"
#include "../CommandContext.h"
#include <cstdio>

#include "Command/RtCtxHelpers.h"

class AutoCommand : public CommandInterface {
public:
    AutoCommand() : CommandInterface("A", "Return to automatic zone based carrier") {}

    void execute(const ArgValue*, CommandContext& ctx) override {
        ctx_set_manual_carrier(ctx, false);
        printf("AUTO mode: Zone-based carrier active\r\n");
    }
};

CommandInterface* makeAutoCommand() {
    static AutoCommand inst;
    return &inst;
}