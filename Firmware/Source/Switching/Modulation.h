/**
 ***********************************************************************************
 * @file    Modulation.h
 * @date    2026-02-15
 * @brief   SPWM/SVM modulation functions using vector_transfs.h.
 ***********************************************************************************
 */

#pragma once

#include "FOC.h"
#include "SwitchingStructs.h"

/**
 * @brief Generate SPWM duty cycles from FOC output
 * @param FocOut FOC output containing Valpha, Vbeta, Vdc
 * @param MaxModulation Max modulation index (e.g., 0.95)
 * @param Duties Output duty cycles [0.0, 1.0]
 */
void GenerateSpwm(const FocOutput& FocOut, float MaxModulation, PhaseVoltages& Duties);

/**
 * @brief Generate SVPWM duty cycles from FOC output
 * @param FocOut FOC output containing Valpha, Vbeta, Vdc
 * @param MaxModulation Max modulation index (e.g., 0.95 relative to linear SVPWM limit)
 * @param Duties Output duty cycles [0.0, 1.0]
 */
void GenerateSvm(const FocOutput& FocOut, float MaxModulation, PhaseVoltages& Duties);