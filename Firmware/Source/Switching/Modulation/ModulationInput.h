/***********************************************************************************
* @file    ModulationInput.h
* @date    2026-02-18
* @brief   Input data structure for modulation schemes.
*
*          Contains all required input parameters for calculating PWM commands,
*          including stationary frame voltages, DC bus voltage, and electrical
*          state variables (angle and velocity).
***********************************************************************************/

#pragma once



/**
 * @brief Input snapshot required by all modulation schemes.
 */
 struct ModulationInput {
    float Valpha_V;        // Stationary Frame Voltage Alpha
    float Vbeta_V;         // Stationary Frame Voltage Beta
    float Vdc_V;           // DC Bus Voltage
    float Theta_Rad;       // Electrical Angle (0 - 2PI)
    float Omega_RadPerSec; // Electrical Velocity
};
