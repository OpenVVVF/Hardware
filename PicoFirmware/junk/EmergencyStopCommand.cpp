#include "../CommandInterface.h"
#include "../CommandContext.h"
#include <cstdio>

class EmergencyStopCommand : public CommandInterface {
public:
    EmergencyStopCommand() : CommandInterface("STOP", "Emergency stop (immediate)") {}
    
    void execute(const ArgValue* /*args*/, CommandContext& ctx) override {
        if (ctx.emergency_stop) {
            ctx.emergency_stop();
            printf("EMERGENCY STOP\r\n");
        } else {
            printf("Error: No emergency stop hook available\r\n");
        }
    }
};

CommandInterface* makeEmergencyStopCommand() {
    static EmergencyStopCommand inst;
    return &inst;
}