/**
  ******************************************************************************
  * @file    fault_manager.h
  * @brief   System-wide fault and warning orchestrator.
  *          Ported from the Pico firmware to C11.
  ******************************************************************************
  */

#pragma once

#include "Control/common_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FAULT_HW_GATE_FAULT      "HW: Desat/Gate Fault"
#define FAULT_HW_GATE_NOT_READY  "HW: Gate Drv Not Ready"

typedef struct {
    FaultRecord_t ActiveFaults[MAX_ACTIVE_FAULTS];
    uint8_t ActiveFaultCount;

    bool SystemFaultedCache;
    bool HwGateFaultActive;
    bool HwGateNotReadyActive;

    uint32_t LastUpdateTime_ms;
    float CurrentSpeed;
} FaultManager_t;

/**
 * @brief  Initialise the fault manager state.
 */
void FaultManager_Init(FaultManager_t *fm);

/**
 * @brief  High-speed update loop: polls gate driver pins, decrements
 *         self-clearing timers, and updates the hardware safety state.
 */
void FaultManager_Update(FaultManager_t *fm);

/**
 * @brief  Provide the current motor speed so self-clearing faults can wait
 *         for the motor to stop before clearing.
 */
void FaultManager_SetSpeed(FaultManager_t *fm, float speed);

/**
 * @brief  Report a fault from any subsystem.
 */
void FaultManager_ReportFault(FaultManager_t *fm, const char *description,
                              FaultSeverity_t severity, float timeout_S);

/**
 * @brief  Clear all latched faults (useful from a console command).
 */
void FaultManager_AcknowledgeFaults(FaultManager_t *fm);

/**
 * @brief  Copy active faults into the caller-supplied array.
 * @return Number of faults copied.
 */
uint8_t FaultManager_GetActiveFaults(const FaultManager_t *fm,
                                     FaultRecord_t *outArray,
                                     uint8_t maxRecords);

/**
 * @brief  True if any SelfClearing or Latched fault is active.
 */
static inline bool FaultManager_IsSystemFaulted(const FaultManager_t *fm)
{
    return fm->SystemFaultedCache;
}

#ifdef __cplusplus
}
#endif
