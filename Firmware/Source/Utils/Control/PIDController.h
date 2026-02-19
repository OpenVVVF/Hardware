/**
***********************************************************************************
* @file    PIDController.h
* @date    2026-02-19
* @brief   Generic PID Controller with Back-Calculation Anti-Windup.
***********************************************************************************
*/

#pragma once

class PidController {
public:
    float Kp_ = 0.0f;
    float Ki_ = 0.0f;
    float Kd_ = 0.0f;
    float MaxOutput_ = 0.0f;
    float MinOutput_ = 0.0f;
    float AntiWindupGain_ = 0.0f; ///< Typically set to 1.0 / Kp_

    PidController() = default;

    void Reset();

    /**
    * @brief Executes the PID calculation.
    * @param _Error The setpoint error.
    * @param _Feedforward Direct output addition (subject to limits).
    * @param _dt_S Loop delta time in seconds.
    * @return Bounded output signal.
    */
    float Update(float _Error, float _Feedforward, float _dt_S);

private:
    float Integral_ = 0.0f;
    float PrevError_ = 0.0f;
};