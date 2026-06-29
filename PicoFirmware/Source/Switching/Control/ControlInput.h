/**
***********************************************************************************
* @file    ControlInput.h
* @date    2026-02-18
* @brief   Common set of structs used across many switching applications.
***********************************************************************************
*/

#pragma once

#include "../SwitchingCommon.h"


struct DriveCommand {
    // --- Current / Torque Domain (FOC) ---
    float _IqCmd_A;               // Commanded torque. Negative = reverse torque/braking.
    float _IdCmd_A;               // Commanded flux. Negative = Field Weakening!

    float _VdFeedforward_V;       // Feedforward term of flux voltage
    float _VqFeedforward_V;       // Feedforward term of torque voltage

    // --- Velocity Domain (V/Hz) ---
    float _VelocityCmd_RadPerSec; // Commanded speed. Negative = reverse direction.   
};