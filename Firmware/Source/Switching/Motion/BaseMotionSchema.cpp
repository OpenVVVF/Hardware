/**
***********************************************************************************
* @file    BaseMotionSchema.cpp
* @date    2026-02-19
* @brief   Implementation of base motion controller logic.
***********************************************************************************
*/

#include "BaseMotionSchema.h"

void MotionController::SetMotorConfig(const MotorConfig& _MotorConfig) {
    MotorConfig_ = _MotorConfig; 
}