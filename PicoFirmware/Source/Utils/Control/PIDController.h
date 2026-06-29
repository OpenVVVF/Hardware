/**
***********************************************************************************
* @file    PIDController.h
* @date    2026-02-20
* @brief   Generic PID Controller with Back-Calculation Anti-Windup and 
* optional built-in execution decimation.
***********************************************************************************
*/

#pragma once

#include <stdint.h>

class PidController {
public:
    float Kp_ = 0.0f;
    float Ki_ = 0.0f;
    float Kd_ = 0.0f;
    float MaxOutput_ = 0.0f;
    float MinOutput_ = 0.0f;
    float AntiWindupGain_ = 0.0f; ///< Typically set to 1.0 / Kp_

    /**
    * @brief Controls how many ticks must pass before the PID evaluates.
    * Default is 1 (evaluates every update tick).
    */
    uint32_t DecimationFactor_ = 1;

    PidController() = default;

    void Reset();

    /**
    * @brief Executes the PID calculation, automatically accumulating dt 
    * and returning the cached output during skipped ticks.
    * @param _Error The setpoint error.
    * @param _Feedforward Direct output addition (subject to limits).
    * @param _dt_S Loop delta time in seconds.
    * @return Bounded output signal (live or cached depending on decimation).
    */
    float Update(float _Error, float _Feedforward, float _dt_S);

private:
    float Integral_ = 0.0f;
    float PrevError_ = 0.0f;

    // Multi-rate execution tracking
    uint32_t TickCounter_ = 0;
    float AccumulatedDt_S_ = 0.0f;
    float CachedOutput_ = 0.0f;
};