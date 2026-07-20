#include "Inverter/Drivers/Storage/MotorConfigStore.h"
#include "Inverter/Drivers/Storage/FramStore.h"
#include "Inverter/Calibration/MotorCalibration.h"
#include "Inverter/Calibration/InductanceCalibrator.h"
#include "Inverter/Control/FocControlManager.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Telemetry.h"

#include <cmath>
#include <cstring>

namespace Inverter {
namespace MotorConfigStore {

namespace {

MotorConfigData s_working = {
    static_cast<uint32_t>(MotorType::PmsmIpm),
    0.0f,               /* pole_count */
    1.0f,               /* encoder_cycles_per_rev */
    0.0f,               /* encoder_offset_deg */
    1.0f,               /* encoder_sign */
    0.0f, 0.0f, 0.0f,   /* r_phase uv/uw/vw */
    0.0f,               /* flux_linkage_wb (reserved) */
    0.0f, 0.0f,         /* ld/lq (reserved) */
    0.03f, 10.0f,       /* pi_kp / pi_ki (match FocControlManager defaults) */
    0, 0, 0, 0,         /* encoder bounds: unset until learned */
    0, 0,               /* ind_n_points / reserved */
    {0}, {0}, {0}, {0}, /* ind_bias_ld_a / ind_ld_h / ind_bias_lq_a / ind_lq_h */
};
bool s_stored = false;
CY15B102Q_HandleTypeDef* s_fram_dev = nullptr;

/** Fill a MotorConfigData from the runtime sources, preserving fields that
 * have no runtime representation (type, flux, ld, lq) from the base. */
MotorConfigData capture(const MotorConfigData& base) {
    MotorConfigData d = base;
    const MotorCalibration& mc = motorCalibration();
    d.pole_count = mc.pole_count;
    d.encoder_cycles_per_rev = mc.encoder_cycles_per_rev;
    d.encoder_offset_deg = mc.encoder_offset_deg;
    d.encoder_sign = mc.encoder_sign;
    d.r_phase_uv_ohm = mc.r_phase_uv;
    d.r_phase_uw_ohm = mc.r_phase_uw;
    d.r_phase_vw_ohm = mc.r_phase_vw;
    d.pi_kp = focControlManager().kp();
    d.pi_ki = focControlManager().ki();
    /* Encoder amplitude bounds are learned dynamically as the rotor turns;
     * persist them so FOC commutates correctly from the first electrical
     * cycle after a reboot (the hardcoded fallback bounds distort the angle
     * enough to detent-lock the rotor).  Keep the previously stored bounds
     * if none have been learned yet this boot. */
    if (encoderADC().learnedBoundsActive()) {
        d.enc_sin_min = encoderADC().sinMin();
        d.enc_sin_max = encoderADC().sinMax();
        d.enc_cos_min = encoderADC().cosMin();
        d.enc_cos_max = encoderADC().cosMax();
    }
    /* Measured inductances: take the runtime values when a calibration has
     * produced them, otherwise keep what was already stored. */
    if (mc.ld_henry > 0.0f) d.ld_henry = mc.ld_henry;
    if (mc.lq_henry > 0.0f) d.lq_henry = mc.lq_henry;
    InductanceCalibrator& ic = inductanceCalibrator();
    if (ic.isDone() && ic.pointCount() > 0) {
        d.ind_n_points = static_cast<uint16_t>(ic.pointCount());
        for (int i = 0; i < ic.pointCount() && i < 8; ++i) {
            d.ind_bias_ld_a[i] = ic.biasLdPoint(i);
            d.ind_ld_h[i] = ic.ldPoint(i);
            d.ind_bias_lq_a[i] = ic.biasLqPoint(i);
            d.ind_lq_h[i] = ic.lqPoint(i);
        }
    }
    return d;
}

/** Plausibility check before a stored config is allowed to mark the runtime
 * calibration valid. */
bool sane(const MotorConfigData& d) {
    if (d.pole_count < 1.0f || d.pole_count > 128.0f) return false;
    if (d.encoder_cycles_per_rev <= 0.0f || d.encoder_cycles_per_rev > 64.0f) return false;
    if (std::fabs(d.encoder_sign) < 0.5f) return false;
    if (std::fabs(d.encoder_offset_deg) > 360.0f) return false;
    if (d.r_phase_uv_ohm <= 0.0f || d.r_phase_uv_ohm > 0.5f) return false;
    if (d.r_phase_uw_ohm <= 0.0f || d.r_phase_uw_ohm > 0.5f) return false;
    if (d.r_phase_vw_ohm <= 0.0f || d.r_phase_vw_ohm > 0.5f) return false;
    return true;
}

} // namespace

void init(CY15B102Q_HandleTypeDef* fram_dev) {
    FramStore::init(fram_dev);
    s_fram_dev = fram_dev;

    if (loadFromFram()) {
        if (applyToRuntime()) {
            Telemetry::printf("[CFG] motor config loaded from FRAM: type=%s poles=%.0f "
                              "offset=%.3f deg sign=%.0f R=%.4f ohm kp=%.4f ki=%.3f",
                              typeName(static_cast<MotorType>(s_working.motor_type)),
                              static_cast<double>(s_working.pole_count),
                              static_cast<double>(s_working.encoder_offset_deg),
                              static_cast<double>(s_working.encoder_sign),
                              static_cast<double>(s_working.r_phase_uv_ohm),
                              static_cast<double>(s_working.pi_kp),
                              static_cast<double>(s_working.pi_ki));
        } else {
            Telemetry::printf("[CFG] FRAM motor config failed sanity check; ignoring it");
            s_stored = false;
        }
    } else {
        Telemetry::printf("[CFG] no motor config in FRAM; using built-in defaults");
    }
}

bool hasStored() {
    return s_stored;
}

CY15B102Q_HandleTypeDef* framDev() {
    return s_fram_dev;
}

const MotorConfigData& working() {
    return s_working;
}

bool saveFromRuntime() {
    s_working = capture(s_working);
    if (!FramStore::save(FramStore::NODE_MOTOR_CONFIG, MOTOR_CONFIG_VERSION,
                         &s_working, sizeof(s_working))) {
        Telemetry::printf("[CFG] ERROR: failed to write motor config to FRAM");
        return false;
    }
    s_stored = true;
    return true;
}

bool applyToRuntime() {
    if (!sane(s_working)) {
        return false;
    }

    MotorCalibration& mc = motorCalibration();
    mc.pole_count = s_working.pole_count;
    mc.encoder_cycles_per_rev = s_working.encoder_cycles_per_rev;
    mc.encoder_offset_deg = s_working.encoder_offset_deg;
    mc.encoder_sign = (s_working.encoder_sign >= 0.0f) ? 1.0f : -1.0f;
    mc.r_phase_uv = s_working.r_phase_uv_ohm;
    mc.r_phase_uw = s_working.r_phase_uw_ohm;
    mc.r_phase_vw = s_working.r_phase_vw_ohm;
    mc.r_phase_avg = (mc.r_phase_uv + mc.r_phase_uw + mc.r_phase_vw) / 3.0f;
    mc.timestamp_ms = 0U; /* unknown for a stored config */
    if (s_working.ld_henry > 0.0f) mc.ld_henry = s_working.ld_henry;
    if (s_working.lq_henry > 0.0f) mc.lq_henry = s_working.lq_henry;
    mc.valid = true;

    if (s_working.pi_kp >= 0.0f) focControlManager().setKp(s_working.pi_kp);
    if (s_working.pi_ki >= 0.0f) focControlManager().setKi(s_working.pi_ki);

    /* Seed the learned encoder bounds if a plausible set was stored.  They
     * keep expanding from real samples afterwards, so this only ever helps
     * the angle normalization converge before the first revolution. */
    if (s_working.enc_sin_max > s_working.enc_sin_min &&
        static_cast<uint32_t>(s_working.enc_sin_max - s_working.enc_sin_min) >= 5000U &&
        s_working.enc_cos_max > s_working.enc_cos_min &&
        static_cast<uint32_t>(s_working.enc_cos_max - s_working.enc_cos_min) >= 5000U) {
        encoderADC().setBounds(s_working.enc_sin_min, s_working.enc_sin_max,
                               s_working.enc_cos_min, s_working.enc_cos_max);
    }
    return true;
}

bool loadFromFram() {
    MotorConfigData d;
    uint16_t version = 0;
    if (!FramStore::load(FramStore::NODE_MOTOR_CONFIG, &d, sizeof(d), &version)) {
        s_stored = false;
        return false;
    }
    if (version == 0U || version > MOTOR_CONFIG_VERSION) {
        Telemetry::printf("[CFG] motor config version %u unsupported (max %u); ignoring",
                          static_cast<unsigned>(version),
                          static_cast<unsigned>(MOTOR_CONFIG_VERSION));
        s_stored = false;
        return false;
    }
    s_working = d;
    s_stored = true;
    return true;
}

bool clear() {
    if (!FramStore::erase(FramStore::NODE_MOTOR_CONFIG)) {
        return false;
    }
    s_stored = false;
    return true;
}

void dump() {
    MotorConfigData stored;
    uint16_t version = 0;
    const bool in_fram = FramStore::load(FramStore::NODE_MOTOR_CONFIG,
                                         &stored, sizeof(stored), &version);
    s_stored = in_fram;

    Telemetry::printf("[CFG] ---- stored in FRAM ----");
    if (!in_fram) {
        Telemetry::printf("[CFG]   (no valid record)");
    } else {
        Telemetry::printf("[CFG]   version       = %u", static_cast<unsigned>(version));
        Telemetry::printf("[CFG]   type          = %s (%lu)",
                          typeName(static_cast<MotorType>(stored.motor_type)),
                          static_cast<unsigned long>(stored.motor_type));
        Telemetry::printf("[CFG]   poles         = %.2f", static_cast<double>(stored.pole_count));
        Telemetry::printf("[CFG]   enc_cycles    = %.3f", static_cast<double>(stored.encoder_cycles_per_rev));
        Telemetry::printf("[CFG]   enc_offset    = %.3f deg", static_cast<double>(stored.encoder_offset_deg));
        Telemetry::printf("[CFG]   enc_sign      = %.0f", static_cast<double>(stored.encoder_sign));
        Telemetry::printf("[CFG]   r_phase uv/uw/vw = %.4f / %.4f / %.4f ohm",
                          static_cast<double>(stored.r_phase_uv_ohm),
                          static_cast<double>(stored.r_phase_uw_ohm),
                          static_cast<double>(stored.r_phase_vw_ohm));
        Telemetry::printf("[CFG]   flux_linkage  = %.5f Wb (reserved)", static_cast<double>(stored.flux_linkage_wb));
        Telemetry::printf("[CFG]   ld / lq       = %.2e / %.2e H",
                          static_cast<double>(stored.ld_henry),
                          static_cast<double>(stored.lq_henry));
        if (stored.ind_n_points > 0U) {
            for (uint16_t i = 0; i < stored.ind_n_points && i < 8U; ++i) {
                if (stored.ind_ld_h[i] > 0.0f) {
                    Telemetry::printf("[CFG]     Ld(%5.1f A) = %7.1f uH",
                                      static_cast<double>(stored.ind_bias_ld_a[i]),
                                      static_cast<double>(stored.ind_ld_h[i] * 1.0e6f));
                }
                if (stored.ind_lq_h[i] > 0.0f) {
                    Telemetry::printf("[CFG]     Lq(%5.1f A) = %7.1f uH",
                                      static_cast<double>(stored.ind_bias_lq_a[i]),
                                      static_cast<double>(stored.ind_lq_h[i] * 1.0e6f));
                }
            }
        } else {
            Telemetry::printf("[CFG]   (no Ld/Lq curve stored)");
        }
        Telemetry::printf("[CFG]   pi kp / ki    = %.4f / %.3f",
                          static_cast<double>(stored.pi_kp),
                          static_cast<double>(stored.pi_ki));
        if (stored.enc_sin_max > stored.enc_sin_min) {
            Telemetry::printf("[CFG]   enc bounds    = sin [%u, %u] cos [%u, %u]",
                              stored.enc_sin_min, stored.enc_sin_max,
                              stored.enc_cos_min, stored.enc_cos_max);
        } else {
            Telemetry::printf("[CFG]   enc bounds    = (not stored)");
        }
    }

    const MotorCalibration& mc = motorCalibration();
    Telemetry::printf("[CFG] ---- runtime (MotorCalibration) ----");
    Telemetry::printf("[CFG]   valid         = %s", mc.valid ? "yes" : "no");
    Telemetry::printf("[CFG]   poles         = %.2f", static_cast<double>(mc.pole_count));
    Telemetry::printf("[CFG]   enc_cycles    = %.3f", static_cast<double>(mc.encoder_cycles_per_rev));
    Telemetry::printf("[CFG]   enc_offset    = %.3f deg", static_cast<double>(mc.encoder_offset_deg));
    Telemetry::printf("[CFG]   enc_sign      = %.0f", static_cast<double>(mc.encoder_sign));
    Telemetry::printf("[CFG]   r_phase avg   = %.4f ohm", static_cast<double>(mc.r_phase_avg));
    Telemetry::printf("[CFG]   pi kp / ki    = %.4f / %.3f",
                      static_cast<double>(focControlManager().kp()),
                      static_cast<double>(focControlManager().ki()));
}

bool setField(const char* name, float value) {
    if (name == nullptr) return false;

    /* Calibration fields: update runtime, then re-save everything. */
    MotorCalibration& mc = motorCalibration();
    bool touches_cal = true;
    if (std::strcmp(name, "poles") == 0) {
        if (value < 1.0f || value > 128.0f) return false;
        mc.pole_count = value;
    } else if (std::strcmp(name, "enc_cycles") == 0) {
        if (value <= 0.0f || value > 64.0f) return false;
        mc.encoder_cycles_per_rev = value;
    } else if (std::strcmp(name, "offset") == 0) {
        if (std::fabs(value) > 360.0f) return false;
        mc.encoder_offset_deg = value;
    } else if (std::strcmp(name, "sign") == 0) {
        if (std::fabs(value) < 0.5f) return false;
        mc.encoder_sign = (value >= 0.0f) ? 1.0f : -1.0f;
    } else if (std::strcmp(name, "r_uv") == 0 || std::strcmp(name, "r_uw") == 0 ||
               std::strcmp(name, "r_vw") == 0) {
        if (value <= 0.0f || value > 0.5f) return false;
        if (std::strcmp(name, "r_uv") == 0) mc.r_phase_uv = value;
        else if (std::strcmp(name, "r_uw") == 0) mc.r_phase_uw = value;
        else mc.r_phase_vw = value;
        mc.r_phase_avg = (mc.r_phase_uv + mc.r_phase_uw + mc.r_phase_vw) / 3.0f;
    } else {
        touches_cal = false;
    }

    if (touches_cal) {
        mc.valid = true;
        return saveFromRuntime();
    }

    /* PI gains: update runtime (applies to the controller), then save. */
    if (std::strcmp(name, "kp") == 0) {
        if (value < 0.0f) return false;
        focControlManager().setKp(value);
        return saveFromRuntime();
    }
    if (std::strcmp(name, "ki") == 0) {
        if (value < 0.0f) return false;
        focControlManager().setKi(value);
        return saveFromRuntime();
    }

    /* Reserved fields: working copy only, then save. */
    if (std::strcmp(name, "flux") == 0) {
        s_working.flux_linkage_wb = value;
    } else if (std::strcmp(name, "ld") == 0) {
        s_working.ld_henry = value;
    } else if (std::strcmp(name, "lq") == 0) {
        s_working.lq_henry = value;
    } else if (std::strcmp(name, "type") == 0) {
        if (value < 0.0f || value > static_cast<float>(MotorType::SlipRing)) return false;
        s_working.motor_type = static_cast<uint32_t>(value);
    } else {
        return false;
    }

    s_working = capture(s_working); /* keep calibration/gain fields in sync */
    if (!FramStore::save(FramStore::NODE_MOTOR_CONFIG, MOTOR_CONFIG_VERSION,
                         &s_working, sizeof(s_working))) {
        return false;
    }
    s_stored = true;
    return true;
}

bool setType(const char* name) {
    bool ok = false;
    MotorType t = typeFromName(name, &ok);
    if (!ok) return false;
    s_working.motor_type = static_cast<uint32_t>(t);
    s_working = capture(s_working);
    if (!FramStore::save(FramStore::NODE_MOTOR_CONFIG, MOTOR_CONFIG_VERSION,
                         &s_working, sizeof(s_working))) {
        return false;
    }
    s_stored = true;
    return true;
}

const char* typeName(MotorType t) {
    switch (t) {
        case MotorType::PmsmIpm:   return "pmsm_ipm";
        case MotorType::PmsmSpm:   return "pmsm_spm";
        case MotorType::Induction: return "induction";
        case MotorType::SynRel:    return "synrel";
        case MotorType::Brushed:   return "brushed";
        case MotorType::SlipRing:  return "slipring";
        case MotorType::Unknown:
        default:                   return "unknown";
    }
}

MotorType typeFromName(const char* name, bool* ok) {
    if (ok != nullptr) *ok = true;
    if (name == nullptr) { if (ok) *ok = false; return MotorType::Unknown; }
    if (std::strcmp(name, "pmsm_ipm") == 0)  return MotorType::PmsmIpm;
    if (std::strcmp(name, "pmsm_spm") == 0)  return MotorType::PmsmSpm;
    if (std::strcmp(name, "induction") == 0) return MotorType::Induction;
    if (std::strcmp(name, "synrel") == 0)    return MotorType::SynRel;
    if (std::strcmp(name, "brushed") == 0)   return MotorType::Brushed;
    if (std::strcmp(name, "slipring") == 0)  return MotorType::SlipRing;
    if (std::strcmp(name, "unknown") == 0)   return MotorType::Unknown;
    if (ok != nullptr) *ok = false;
    return MotorType::Unknown;
}

} // namespace MotorConfigStore
} // namespace Inverter
