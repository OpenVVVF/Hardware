#include "Inverter/Command/CommandInterface.h"
#include "Inverter/Command/CommandManager.h"
#include "Inverter/Command/CommandContext.h"
#include "Inverter/Drivers/Storage/MotorConfigStore.h"
#include "Inverter/Drivers/Storage/FramStore.h"
#include "Inverter/Telemetry.h"

#include <cstdio>
#include <cstring>

namespace {

class MotorCfgCommand : public CommandInterface {
public:
    MotorCfgCommand()
      : CommandInterface("motorcfg",
            "Motor config in FRAM: dump/save/load/clear/set/type",
            {ArgSpec{"subcommand", "", 0.0f, 0.0f, 0.0f, true, ArgSpec::STRING},
             ArgSpec{"arg1", "", -1.0e9f, 1.0e9f, 0.0f, false, ArgSpec::STRING},
             ArgSpec{"arg2", "", -1.0e9f, 1.0e9f, 0.0f, false, ArgSpec::FLOAT}}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        const char* sub = args[0].s_val;

        if (strcasecmp(sub, "dump") == 0) {
            Inverter::MotorConfigStore::dump();
            return;
        }

        if (strcasecmp(sub, "raw") == 0) {
            /* Debug: hammer the slot with raw reads and report how each one
             * fails, plus the driver's SPI error counter delta. */
            CY15B102Q_HandleTypeDef* dev = Inverter::MotorConfigStore::framDev();
            const uint32_t addr = Inverter::FramStore::slotAddress(Inverter::FramStore::NODE_MOTOR_CONFIG);
            const uint32_t err0 = CY15B102Q_GetErrorCount();
            int ok = 0, bad_magic = 0, bad_crc = 0;
            for (int i = 0; i < 20; ++i) {
                uint8_t hdr[16];
                CY15B102Q_Read(dev, addr, hdr, sizeof(hdr));
                uint32_t magic = 0, crc_stored = 0;
                uint16_t len = 0;
                memcpy(&magic, hdr, 4);
                memcpy(&len, hdr + 8, 2);
                memcpy(&crc_stored, hdr + 12, 4);
                if (magic != 0x464E4F44UL || len != 52U) { bad_magic++; continue; }
                uint8_t payload[52];
                CY15B102Q_Read(dev, addr + 16, payload, sizeof(payload));
                uint32_t crc_calc = Inverter::FramStore::debugCrc(1, 1, 52, 0, payload);
                if (crc_calc != crc_stored) { bad_crc++; continue; }
                ok++;
            }
            Telemetry::printf("[CFG] 20 reads: ok=%d bad_hdr=%d bad_crc=%d spi_err_delta=%lu",
                              ok, bad_magic, bad_crc,
                              (unsigned long)(CY15B102Q_GetErrorCount() - err0));
            return;
        }

        if (strcasecmp(sub, "save") == 0) {
            if (Inverter::MotorConfigStore::saveFromRuntime()) {
                Telemetry::printf("[CFG] motor config saved to FRAM");
            }
            return;
        }

        if (strcasecmp(sub, "load") == 0) {
            if (!Inverter::MotorConfigStore::loadFromFram()) {
                Telemetry::printf("[CFG] no valid motor config in FRAM");
                return;
            }
            if (Inverter::MotorConfigStore::applyToRuntime()) {
                Telemetry::printf("[CFG] motor config applied from FRAM");
            } else {
                Telemetry::printf("[CFG] stored config failed sanity check; not applied");
            }
            return;
        }

        if (strcasecmp(sub, "clear") == 0) {
            if (Inverter::MotorConfigStore::clear()) {
                Telemetry::printf("[CFG] motor config erased from FRAM");
            } else {
                Telemetry::printf("[CFG] ERROR: erase failed");
            }
            return;
        }

        if (strcasecmp(sub, "set") == 0) {
            if (!args[1].present || !args[2].present) {
                Telemetry::printf("[CFG] usage: motorcfg set <field> <value>");
                printFields();
                return;
            }
            if (Inverter::MotorConfigStore::setField(args[1].s_val, args[2].f_val)) {
                Telemetry::printf("[CFG] %s = %g (saved to FRAM)",
                                  args[1].s_val, static_cast<double>(args[2].f_val));
            } else {
                Telemetry::printf("[CFG] ERROR: unknown field or bad value '%s'", args[1].s_val);
                printFields();
            }
            return;
        }

        if (strcasecmp(sub, "type") == 0) {
            if (!args[1].present) {
                Telemetry::printf("[CFG] usage: motorcfg type <pmsm_ipm|pmsm_spm|induction|synrel|brushed|slipring|unknown>");
                return;
            }
            if (Inverter::MotorConfigStore::setType(args[1].s_val)) {
                Telemetry::printf("[CFG] motor type = %s (saved to FRAM)", args[1].s_val);
            } else {
                Telemetry::printf("[CFG] ERROR: unknown motor type '%s'", args[1].s_val);
            }
            return;
        }

        Telemetry::printf("[CFG] unknown subcommand '%s'. Use: dump/save/load/clear/set/type", sub);
    }

private:
    static void printFields() {
        Telemetry::printf("[CFG] fields: poles enc_cycles offset sign r_uv r_uw r_vw "
                          "kp ki flux ld lq type");
    }
};

MotorCfgCommand sMotorCfgCmd;

} // namespace

void registerMotorConfigCommands(CommandManager& mgr) {
    mgr.registerCommand(&sMotorCfgCmd);
}
