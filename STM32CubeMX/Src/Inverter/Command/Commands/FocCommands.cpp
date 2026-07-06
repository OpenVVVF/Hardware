#include "Inverter/Command/CommandInterface.h"
#include "Inverter/Command/CommandContext.h"
#include "Inverter/Command/CommandManager.h"
#include "Inverter/Control/FocControlManager.h"
#include "Inverter/Control/FocController.h"
#include "Inverter/Telemetry.h"

#include <cstring>
#include <cstdlib>
#include <strings.h>

using Inverter::FocControlManager;
using Inverter::focControlManager;

/**
 * @brief Single `foc <subcommand> [args...]` dispatcher.
 *
 * Supported forms:
 *   foc start <iq_a> [id_a]
 *   foc stop
 *   foc id <id_a>
 *   foc iq <iq_a>
 *   foc kp <kp>
 *   foc ki <ki>
 *   foc vlim <v_v>
 *   foc offset <delta_mech_deg>
 *   foc status
 */
class FocCommand : public CommandInterface {
public:
    FocCommand()
      : CommandInterface("foc", "FOC control: start/stop/id/iq/kp/ki/vlim/status",
            {ArgSpec{"subcommand", "", 0.0f, 0.0f, 0.0f, true, ArgSpec::STRING},
             ArgSpec{"value1", "", -1000.0f, 1000.0f, 0.0f, false, ArgSpec::FLOAT},
             ArgSpec{"value2", "", -1000.0f, 1000.0f, 0.0f, false, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        const char* sub = args[0].s_val;
        float v1 = args[1].present ? args[1].f_val : 0.0f;
        float v2 = args[2].present ? args[2].f_val : 0.0f;

        if (strcasecmp(sub, "start") == 0) {
            focControlManager().start(v1, v2);
        } else if (strcasecmp(sub, "stop") == 0) {
            focControlManager().stop();
        } else if (strcasecmp(sub, "id") == 0) {
            focControlManager().setId(v1);
            Telemetry::printf("[SHELL] FOC Id set to %.2f A", static_cast<double>(v1));
        } else if (strcasecmp(sub, "iq") == 0) {
            focControlManager().setIq(v1);
            Telemetry::printf("[SHELL] FOC Iq set to %.2f A", static_cast<double>(v1));
        } else if (strcasecmp(sub, "kp") == 0) {
            focControlManager().setKp(v1);
        } else if (strcasecmp(sub, "ki") == 0) {
            focControlManager().setKi(v1);
        } else if (strcasecmp(sub, "vlim") == 0) {
            focControlManager().setVoltageLimit(v1);
        } else if (strcasecmp(sub, "offset") == 0) {
            focControlManager().adjustEncoderOffset(v1);
        } else if (strcasecmp(sub, "status") == 0) {
            printStatus();
        } else {
            Telemetry::printf("[SHELL] Unknown FOC subcommand '%s'. Use: start/stop/id/iq/kp/ki/vlim/offset/status", sub);
        }
    }

private:
    void printStatus() const {
        FocControlManager& mgr = focControlManager();
        const Inverter::FocController& ctrl = mgr.controller();
        Telemetry::printf("[SHELL] FOC run=%s", mgr.isRunning() ? "Y" : "N");
        Telemetry::printf("[SHELL] id_cmd=%.2f iq_cmd=%.2f",
                          static_cast<double>(mgr.setpoints().id_a),
                          static_cast<double>(mgr.setpoints().iq_a));
        Telemetry::printf("[SHELL] id=%.2f iq=%.2f vd=%.2f vq=%.2f",
                          static_cast<double>(ctrl.Id_A),
                          static_cast<double>(ctrl.Iq_A),
                          static_cast<double>(ctrl.Vd_V),
                          static_cast<double>(ctrl.Vq_V));
        Telemetry::printf("[SHELL] valpha=%.2f vbeta=%.2f elec_angle=%.3f",
                          static_cast<double>(ctrl.Valpha_V),
                          static_cast<double>(ctrl.Vbeta_V),
                          static_cast<double>(ctrl.ElectricalAngle_Rad));
        Telemetry::printf("[SHELL] iu=%.3f iv=%.3f iw=%.3f missed=%u",
                          static_cast<double>(mgr.lastIuA()),
                          static_cast<double>(mgr.lastIvA()),
                          static_cast<double>(mgr.lastIwA()),
                          static_cast<unsigned>(mgr.missedCurrentSamples()));
    }
};

static FocCommand sFocCmd;

void registerFocCommands(CommandManager& mgr) {
    mgr.registerCommand(&sFocCmd);
}
