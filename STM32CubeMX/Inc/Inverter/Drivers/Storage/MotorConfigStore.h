#pragma once

#include "cy15b102q_driver.h"

#include <cstdint>

namespace Inverter {

/**
 * @brief Motor type taxonomy.  Stored in FRAM so future firmware can branch
 *        per motor family; only PmsmIpm has control support today.
 */
enum class MotorType : uint32_t {
    Unknown   = 0,
    PmsmIpm   = 1,  /**< Interior permanent magnet (current hardware). */
    PmsmSpm   = 2,  /**< Surface permanent magnet (future). */
    Induction = 3,  /**< Induction machine (future). */
    SynRel    = 4,  /**< Synchronous reluctance (future). */
    Brushed   = 5,  /**< Brushed DC (future). */
    SlipRing  = 6,  /**< Wound-rotor synchronous (future). */
};

/**
 * @brief Persistent motor configuration payload (FRAM schema version 1).
 *
 * Plain-old-data, no pointers: this exact byte image goes into F-RAM.
 * Bump MOTOR_CONFIG_VERSION and append fields at the END when extending.
 */
struct MotorConfigData {
    uint32_t motor_type;          /**< MotorType. */
    float pole_count;             /**< Total rotor pole count. */
    float encoder_cycles_per_rev; /**< Encoder electrical cycles per mech rev. */
    float encoder_offset_deg;     /**< Encoder offset, mechanical degrees. */
    float encoder_sign;           /**< +1 or -1. */
    float r_phase_uv_ohm;         /**< Per-phase resistance, UV pair [ohm]. */
    float r_phase_uw_ohm;         /**< Per-phase resistance, UW pair [ohm]. */
    float r_phase_vw_ohm;         /**< Per-phase resistance, VW pair [ohm]. */
    float flux_linkage_wb;        /**< Reserved (PM flux linkage), 0 for now. */
    float ld_henry;               /**< Reserved (d-axis inductance), 0 for now. */
    float lq_henry;               /**< Reserved (q-axis inductance), 0 for now. */
    float pi_kp;                  /**< Current-loop proportional gain. */
    float pi_ki;                  /**< Current-loop integral gain. */
};

constexpr uint16_t MOTOR_CONFIG_VERSION = 1U;

/**
 * @brief Motor configuration persistence: FRAM <-> runtime.
 *
 * The runtime sources of truth remain MotorCalibration (calibration results)
 * and FocControlManager (PI gains); this module snapshots them into F-RAM and
 * restores them on boot.  Fields without a runtime consumer yet (motor_type,
 * flux linkage, Ld/Lq) live in a RAM working copy so they round-trip.
 */
namespace MotorConfigStore {

/**
 * @brief Initialise the store and auto-load the motor config if present.
 *        Call once at boot after the F-RAM device is initialised.
 */
void init(CY15B102Q_HandleTypeDef* fram_dev);

/** True if a valid motor config record exists in FRAM (read at last check). */
bool hasStored();

/** The F-RAM device the store is bound to (for diagnostics). */
CY15B102Q_HandleTypeDef* framDev();

/** The RAM working copy (as last loaded/saved). */
const MotorConfigData& working();

/** Snapshot runtime state (MotorCalibration + FOC PI gains) into FRAM. */
bool saveFromRuntime();

/** Push the working copy into MotorCalibration and the FOC PI gains. */
bool applyToRuntime();

/** Re-read the FRAM record into the working copy (does not apply). */
bool loadFromFram();

/** Invalidate the FRAM record. */
bool clear();

/** Print the stored and runtime configurations over telemetry. */
void dump();

/**
 * @brief Set one field and persist it.
 *
 * Calibration/gain fields update the runtime source first (so they take
 * effect immediately), then the whole config is re-saved.  Reserved fields
 * (flux/ld/lq) update only the working copy.  Accepts: type (numeric or
 * name via setType), poles, enc_cycles, offset, sign, r_uv, r_uw, r_vw,
 * flux, ld, lq, kp, ki.
 */
bool setField(const char* name, float value);

/** Set the motor type by name (pmsm_ipm, pmsm_spm, induction, synrel,
 *  brushed, slipring, unknown) and persist it. */
bool setType(const char* name);

const char* typeName(MotorType t);
MotorType typeFromName(const char* name, bool* ok);

} // namespace MotorConfigStore
} // namespace Inverter
