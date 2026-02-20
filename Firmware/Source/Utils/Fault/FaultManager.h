/**
***********************************************************************************
* @file    FaultManager.h
* @date    2026-02-20
* @brief   System-wide fault and warning orchestrator for the inverter. 
* Monitors subsystem reports, manages gate driver GPIO states on the Pico, 
* and controls the hardware reset state to protect the power stage.
***********************************************************************************
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>

/// Maximum number of distinct faults/warnings tracked simultaneously
#define MAX_ACTIVE_FAULTS 16

/// Maximum length of a fault description string (including null terminator)
#define FAULT_DESC_LENGTH 48

/**
 * @brief Classifies the required action and clearing mechanism for a report.
 */
enum class FaultSeverity {
    Warning,        ///< Logged only. Does not disable the drive or hardware.
    SelfClearing,   ///< Disables hardware. Clears automatically after timeout AND zero speed.
    Latched         ///< Disables hardware. Requires manual user acknowledgment to clear.
};

/**
 * @brief Tracks the state of an active fault or warning.
 */
struct FaultRecord {
    char          Description[FAULT_DESC_LENGTH]; ///< Human-readable fault description
    FaultSeverity Severity;                       ///< The severity classification
    float         TimeRemaining_S;                ///< Time left before a SelfClearing fault can reset
};

/**
 * @brief Manages inverter health, processes subsystem fault reports, and asserts 
 * physical safety lines on the Raspberry Pi Pico.
 */
class FaultManager {
public:
    FaultManager() = default;

    /**
     * @brief Initializes the hardware pins for the gate driver on the Pico.
     * @note  Hardcoded to Pico GPIOs: Ready (8), Reset (9), and Fault (26).
     */
    void Init();

    /**
     * @brief High-speed update loop for fault logic and GPIO monitoring.
     * @note  Calculates delta-time internally using the Pico's hardware timer.
     * Can be called safely from any high-frequency timer or background loop.
     */
    void Update();

    /**
     * @brief Updates the internal speed state used for evaluating safe-to-clear conditions.
     * @param _speed Current mechanical or electrical speed (e.g., in rad/s).
     */
    void SetSpeed(float _speed);

    /**
     * @brief Allows any subsystem to report an issue to the fault manager.
     * @param _description 32-character string describing the fault.
     * @param _severity    The severity level of the error.
     * @param _timeout_S   Time (in seconds) required before a SelfClearing fault evaluates for reset.
     */
    void ReportFault(const char* _description, FaultSeverity _severity, float _timeout_S = 0.0f);

    /**
     * @brief Manually clears all Latched faults, provided the underlying hardware 
     * error (like a desat event) is no longer actively asserted.
     */
    void AcknowledgeFaults();

    /**
     * @brief Evaluates if the system is currently in a hazardous state.
     * @return True if any SelfClearing or Latched faults are active. Warnings return false.
     */
    bool IsSystemFaulted() const;

    /**
     * @brief Retrieves a copy of the active faults list for telemetry or logging.
     * @param _outArray   Pointer to an array where records will be copied.
     * @param _maxRecords The maximum number of records _outArray can hold.
     * @return The actual number of active faults copied into the array.
     */
    uint8_t GetActiveFaults(FaultRecord* _outArray, uint8_t _maxRecords) const;

private:
    // --------------------------------------------------------
    // Hardware Configuration
    // --------------------------------------------------------
    static constexpr uint8_t ReadyPin_ = 8;  ///< Gate Driver Ready (Input, Active-Low = Not Ready)
    static constexpr uint8_t ResetPin_ = 9;  ///< Gate Driver Reset (Output, Active-Low = Reset)
    static constexpr uint8_t FaultPin_ = 26; ///< Gate Driver Fault (Input, Active-Low = Fault)

    // --------------------------------------------------------
    // Internal State
    // --------------------------------------------------------
    uint64_t LastUpdateTime_uS_ = 0;    ///< Timestamp of the last Update() call
    float    CurrentSpeed_      = 0.0f; ///< Cached system speed for self-clearing evaluation

    FaultRecord ActiveFaults_[MAX_ACTIVE_FAULTS]; ///< Array of currently active faults
    uint8_t     ActiveFaultCount_ = 0;            ///< Number of currently active faults

    // --------------------------------------------------------
    // Private Helpers
    // --------------------------------------------------------

    /**
     * @brief Reads physical GPIO states and reports faults if the gate driver signals an issue.
     */
    void ProcessHardwarePins();

    /**
     * @brief Updates the gate driver reset pin. Asserts reset (LOW) if system is faulted.
     */
    void EnforceHardwareSafety();

    /**
     * @brief Helper to remove a fault from the active list, shifting remaining elements.
     * @param _index The array index of the fault to remove.
     */
    void RemoveFault(uint8_t _index);
};