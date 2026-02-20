/**
***********************************************************************************
* @file    FaultManager.cpp
* @date    2026-02-20
* @brief   Implementation of the FaultManager for Raspberry Pi Pico. Handles 
* hardware timers, GPIOs, and the core state machine for clearing faults.
***********************************************************************************
*/

#include "FaultManager.h"
#include "pico/stdlib.h"
#include <string.h>
#include <math.h>

void FaultManager::Init() {
    // 1. Initialize Gate Driver Ready Pin (Input)
    gpio_init(ReadyPin_);
    gpio_set_dir(ReadyPin_, GPIO_IN);
    
    // 2. Initialize Gate Driver Fault Pin (Input)
    gpio_init(FaultPin_);
    gpio_set_dir(FaultPin_, GPIO_IN);

    // 3. Initialize Gate Driver Reset Pin (Output)
    gpio_init(ResetPin_);
    gpio_set_dir(ResetPin_, GPIO_OUT);
    
    // Hold chips in reset immediately upon boot for safety. 
    // They will remain disabled until the first Update() proves the system is clear.
    gpio_put(ResetPin_, 0); 
    
    // Seed the timer for the first dt calculation
    LastUpdateTime_uS_ = time_us_64();
}

void FaultManager::Update() {
    // 1. Calculate internal delta-time (dt_S)
    uint64_t currentTime_uS = time_us_64();
    float dt_S = (float)(currentTime_uS - LastUpdateTime_uS_) / 1000000.0f;
    LastUpdateTime_uS_ = currentTime_uS;

    // 2. Poll hardware safety lines
    ProcessHardwarePins();

    // 3. Process Self-Clearing Faults
    // Note: Iterating backwards ensures that removing an element (which shifts the 
    // array down) doesn't cause us to accidentally skip the next element in the loop.
    for (int i = ActiveFaultCount_ - 1; i >= 0; i--) {
        if (ActiveFaults_[i].Severity == FaultSeverity::SelfClearing) {
            
            // Decrement the timer
            ActiveFaults_[i].TimeRemaining_S -= dt_S;
            
            // Evaluate clearing conditions: timeout expired AND motor has stopped
            // We use a small epsilon (0.1f) to account for noise around zero speed.
            if (ActiveFaults_[i].TimeRemaining_S <= 0.0f && fabs(CurrentSpeed_) < 0.1f) {
                RemoveFault(i);
            }
        }
    }

    // 4. Actuate the hardware based on current state
    EnforceHardwareSafety();
}

void FaultManager::SetSpeed(float _speed) {
    CurrentSpeed_ = _speed;
}

void FaultManager::ReportFault(const char* _description, FaultSeverity _severity, float _timeout_S) {
    // 1. Prevent duplicate logging by checking existing descriptions
    for (uint8_t i = 0; i < ActiveFaultCount_; i++) {
        if (strncmp(ActiveFaults_[i].Description, _description, FAULT_DESC_LENGTH) == 0) {
            
            // If the fault is already active, we just refresh the timeout timer
            // so a bouncing fault condition doesn't clear prematurely.
            if (_severity == FaultSeverity::SelfClearing) {
                ActiveFaults_[i].TimeRemaining_S = _timeout_S;
            }
            return; 
        }
    }

    // 2. Add new fault if space is available
    if (ActiveFaultCount_ < MAX_ACTIVE_FAULTS) {
        FaultRecord& newFault = ActiveFaults_[ActiveFaultCount_];
        
        // Safely copy string and explicitly enforce null termination
        strncpy(newFault.Description, _description, FAULT_DESC_LENGTH - 1);
        newFault.Description[FAULT_DESC_LENGTH - 1] = '\0';
        
        newFault.Severity = _severity;
        newFault.TimeRemaining_S = _timeout_S;
        
        ActiveFaultCount_++;
    }
}

void FaultManager::AcknowledgeFaults() {
    // Iterate backwards safely, clearing anything marked as Latched.
    for (int i = ActiveFaultCount_ - 1; i >= 0; i--) {
        if (ActiveFaults_[i].Severity == FaultSeverity::Latched) {
            RemoveFault(i);
        }
    }
}

bool FaultManager::IsSystemFaulted() const {
    for (uint8_t i = 0; i < ActiveFaultCount_; i++) {
        // Any fault that isn't just a warning constitutes a system-level fault
        if (ActiveFaults_[i].Severity != FaultSeverity::Warning) {
            return true;
        }
    }
    return false;
}

uint8_t FaultManager::GetActiveFaults(FaultRecord* _outArray, uint8_t _maxRecords) const {
    if (!_outArray) return 0;
    
    // Prevent buffer overflows by copying the minimum of requested vs available
    uint8_t copyCount = (ActiveFaultCount_ < _maxRecords) ? ActiveFaultCount_ : _maxRecords;
    
    for (uint8_t i = 0; i < copyCount; i++) {
        _outArray[i] = ActiveFaults_[i];
    }
    
    return copyCount;
}

void FaultManager::ProcessHardwarePins() {
    // Standard industrial logic levels: Active-Low means a fault/not-ready condition exists.
    // 0 = Fault Occurred / Driver Not Ready
    bool gateFaultActive = (gpio_get(FaultPin_) == 0);
    bool gateNotReady    = (gpio_get(ReadyPin_) == 0);

    // Hard hardware fault (e.g., desaturation / overcurrent). Requires user intervention.
    if (gateFaultActive) {
        ReportFault("HW: Desat/Gate Fault", FaultSeverity::Latched);
    }
    
    // Soft hardware fault (e.g., undervoltage lockout on boot). Usually self-clearing.
    if (gateNotReady) {
        ReportFault("HW: Gate Drv Not Ready", FaultSeverity::SelfClearing, 1.0f);
    }
}

void FaultManager::EnforceHardwareSafety() {
    // Active-Low Reset Logic:
    // If faulted -> write 0 to pull Reset LOW (Disable chips).
    // If healthy -> write 1 to drive Reset HIGH (Enable chips).
    if (IsSystemFaulted()) {
        gpio_put(ResetPin_, 0); 
    } else {
        gpio_put(ResetPin_, 1); 
    }
}

void FaultManager::RemoveFault(uint8_t _index) {
    if (_index >= ActiveFaultCount_) return;

    // Shift remaining faults down one slot to fill the newly opened gap
    for (uint8_t i = _index; i < ActiveFaultCount_ - 1; i++) {
        ActiveFaults_[i] = ActiveFaults_[i + 1];
    }
    
    ActiveFaultCount_--;
}