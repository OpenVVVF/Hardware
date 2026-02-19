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
