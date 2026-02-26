/**
***********************************************************************************
* @file    DriveManager.cpp
* @date    2026-02-19
* @brief   Implementation of the rate-decimated control pipeline.
***********************************************************************************
*/

#include "CalibrationManager.h"
#include "Hardware.h"


bool CalibrationManager::Update(FaultManager* _FaultManager, PWMDriver* _Driver, const SensorData& _Sensors, float _DT) {

    

    // Several modes

    switch (m_mode) {

        // -- ENCODER OFFSET -- //
        // encoder offset calibration (lock rotor to U phase, measure encoder has stopped changing, 
        // then sample value, perhaps then move rotor and lock to u phase again, maybe wrap to electrical and wrap, across all?)
        case CalibrationMode::ENCODER_OFFSET: {
            float dt = _DT; 
        
            switch (m_encoderCalib.CurrentState) {
                
                case EncoderCalibrationContext::State::INIT: {
                    m_encoderCalib.CurrentPid.Kp_ = 0.003f; 
                    m_encoderCalib.CurrentPid.Ki_ = 0.1f;
                    m_encoderCalib.CurrentPid.MaxOutput_ = 0.05f; 
                    m_encoderCalib.CurrentPid.MinOutput_ = -0.05f;
                    m_encoderCalib.CurrentPid.Reset();
                    
                    m_encoderCalib.Timer_sec = 0.0f;
                    
                    _Driver->enable();
                    m_encoderCalib.CurrentState = EncoderCalibrationContext::State::WAIT_SETTLE;
                    break;
                }
        
                case EncoderCalibrationContext::State::WAIT_SETTLE: {
                    m_encoderCalib.Timer_sec += dt;
        
                    float currentError = _Sensors._Iu_A - m_encoderCalib.TargetAlignCurrent_A ;
                    float dutyModulation = 0.02;//m_encoderCalib.CurrentPid.Update(currentError, 0.0f, dt);
        
                    HardwareCommand cmd;
                    cmd.SwitchingFrequency_Hz = 8000.0f; 
                    cmd.DutyPhU_unitless = 0.5f + dutyModulation;
                    cmd.DutyPhV_unitless = 0.5f - (dutyModulation / 2.0f);
                    cmd.DutyPhW_unitless = 0.5f - (dutyModulation / 2.0f);
                    _Driver->SetHardwareCommand(cmd);
        
                    bool timeElapsed = (m_encoderCalib.Timer_sec >= m_encoderCalib.SettleTime_sec);
                    bool rotorStopped = (std::abs(_Sensors._EncoderVelocity_RadPerSec) < m_encoderCalib.VelocityThreshold);
        
                    if (timeElapsed && rotorStopped) {
                        // Move to sample phase, reset accumulator
                        m_encoderCalib.CurrentState = EncoderCalibrationContext::State::SAMPLE;
                        m_encoderCalib.Accumulator = 0.0f;
                        m_encoderCalib.SampleCount = 0;
                    } else if (timeElapsed && !rotorStopped) {
                        _Driver->SetNeutralDutycycle();
                        // _FaultManager->TriggerFault(Fault::CALIBRATION_TIMEOUT);
                    }
                    break;
                }
        
                case EncoderCalibrationContext::State::SAMPLE: {
                    m_encoderCalib.Timer_sec += dt;

                    // MUST KEEP HOLDING THE ROTOR WITH PID WHILE SAMPLING!
                    float currentError = _Sensors._Iu_A - m_encoderCalib.TargetAlignCurrent_A ;
                    float dutyModulation = 0.02;//m_encoderCalib.CurrentPid.Update(currentError, 0.0f, dt);
                    
                    HardwareCommand cmd;
                    cmd.SwitchingFrequency_Hz = 8000.0f; 
                    cmd.DutyPhU_unitless = 0.5f + dutyModulation;
                    cmd.DutyPhV_unitless = 0.5f - (dutyModulation / 2.0f);
                    cmd.DutyPhW_unitless = 0.5f - (dutyModulation / 2.0f);
                    _Driver->SetHardwareCommand(cmd);

                    // Accumulate encoder readings
                    m_encoderCalib.Accumulator += _Sensors._EncoderPosition_Rad;
                    m_encoderCalib.SampleCount++;

                    // Check if sampling duration is complete
                    if (m_encoderCalib.Timer_sec >= (m_encoderCalib.SettleTime_sec + m_encoderCalib.SampleTime_sec)) {
                        _Driver->SetNeutralDutycycle();
                        
                        // 1. Calculate Average
                        float avgMechRad = m_encoderCalib.Accumulator / static_cast<float>(m_encoderCalib.SampleCount);
                        
                        // 2. Wrap it so it's consistent regardless of which pole pair it snapped to
                        float mechPitch_Rad = EncoderCalibrationContext::TWO_PI / m_encoderCalib.PolePairs;
                        
                        // Using double fmodf to safely handle any negative angles from the encoder
                        m_encoderCalib.MeasuredOffset_Rad = fmodf(fmodf(avgMechRad, mechPitch_Rad) + mechPitch_Rad, mechPitch_Rad);
                        
                        m_encoderCalib.CurrentState = EncoderCalibrationContext::State::DONE;
                    }
                    break;
                }
        
                case EncoderCalibrationContext::State::DONE: {
                    m_encoderCalib.CurrentState = EncoderCalibrationContext::State::INIT; 
                    m_mode = CalibrationMode::IDLE; 
                    break;
                }
            }
            break;
        }



    // encoder min/max calibration (run motor open loop at a voltage until we see 2 full sin/cos mechanical revolutions, then find min/max, offsets)



    // lr calibration (resistance, flux+torque inductances, etc.)

    // flux linkage later


    }

    return false;
}