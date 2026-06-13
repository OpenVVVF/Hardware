/**
  ******************************************************************************
  * @file    fault_manager.c
  * @brief   Fault manager implementation.
  ******************************************************************************
  */

#include "fault_manager.h"
#include "gate_driver.h"
#include "main.h"
#include <string.h>
#include <math.h>

/* Internal helpers */
static void FaultManager_RemoveFault(FaultManager_t *fm, uint8_t index);
static void FaultManager_UpdateFaultCache(FaultManager_t *fm);

void FaultManager_Init(FaultManager_t *fm)
{
    memset(fm, 0, sizeof(*fm));
    fm->LastUpdateTime_ms = HAL_GetTick();

    /* Hold gate driver in reset until the first Update() proves the system is clear. */
    HAL_GPIO_WritePin(GATE_DRIVER_RESET_GPIO_Port, GATE_DRIVER_RESET_Pin, GPIO_PIN_RESET);
}

void FaultManager_Update(FaultManager_t *fm)
{
    uint32_t now_ms = HAL_GetTick();
    float dt_S = (float)(now_ms - fm->LastUpdateTime_ms) * 0.001f;
    fm->LastUpdateTime_ms = now_ms;

    if (dt_S < 0.0f) {
        dt_S = 0.0f;
    }

    /* Poll hardware safety lines. */
    bool gate_fault = GateDriver_IsFault();
    bool gate_ready = GateDriver_IsReady();

    if (gate_fault && !fm->HwGateFaultActive) {
        FaultManager_ReportFault(fm, FAULT_HW_GATE_FAULT, FAULT_SEVERITY_LATCHED, 0.0f);
        fm->HwGateFaultActive = true;
    } else if (!gate_fault) {
        fm->HwGateFaultActive = false;
    }

    if (!gate_ready && !fm->HwGateNotReadyActive) {
        FaultManager_ReportFault(fm, FAULT_HW_GATE_NOT_READY, FAULT_SEVERITY_SELF_CLEARING, 1.0f);
        fm->HwGateNotReadyActive = true;
    } else if (gate_ready) {
        fm->HwGateNotReadyActive = false;
    }

    /* Process self-clearing faults. */
    if (fm->ActiveFaultCount > 0) {
        for (int i = fm->ActiveFaultCount - 1; i >= 0; i--) {
            if (fm->ActiveFaults[i].Severity == FAULT_SEVERITY_SELF_CLEARING) {
                fm->ActiveFaults[i].TimeRemaining_S -= dt_S;
                if (fm->ActiveFaults[i].TimeRemaining_S <= 0.0f && fabsf(fm->CurrentSpeed) < 0.1f) {
                    FaultManager_RemoveFault(fm, (uint8_t)i);
                }
            }
        }
    }

    /* Actuate hardware safety line. */
    if (fm->SystemFaultedCache) {
        HAL_GPIO_WritePin(GATE_DRIVER_RESET_GPIO_Port, GATE_DRIVER_RESET_Pin, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(GATE_DRIVER_RESET_GPIO_Port, GATE_DRIVER_RESET_Pin, GPIO_PIN_SET);
    }
}

void FaultManager_SetSpeed(FaultManager_t *fm, float speed)
{
    fm->CurrentSpeed = speed;
}

void FaultManager_ReportFault(FaultManager_t *fm, const char *description,
                              FaultSeverity_t severity, float timeout_S)
{
    /* Prevent duplicate descriptions. */
    for (uint8_t i = 0; i < fm->ActiveFaultCount; i++) {
        if (strncmp(fm->ActiveFaults[i].Description, description, FAULT_DESC_LENGTH) == 0) {
            if (severity == FAULT_SEVERITY_SELF_CLEARING) {
                fm->ActiveFaults[i].TimeRemaining_S = timeout_S;
            }
            return;
        }
    }

    if (fm->ActiveFaultCount < MAX_ACTIVE_FAULTS) {
        FaultRecord_t *newFault = &fm->ActiveFaults[fm->ActiveFaultCount];
        strncpy(newFault->Description, description, FAULT_DESC_LENGTH - 1);
        newFault->Description[FAULT_DESC_LENGTH - 1] = '\0';
        newFault->Severity = severity;
        newFault->TimeRemaining_S = timeout_S;
        fm->ActiveFaultCount++;

        if (severity != FAULT_SEVERITY_WARNING) {
            fm->SystemFaultedCache = true;
        }
    }
}

void FaultManager_AcknowledgeFaults(FaultManager_t *fm)
{
    for (int i = fm->ActiveFaultCount - 1; i >= 0; i--) {
        if (fm->ActiveFaults[i].Severity == FAULT_SEVERITY_LATCHED) {
            FaultManager_RemoveFault(fm, (uint8_t)i);
        }
    }
}

uint8_t FaultManager_GetActiveFaults(const FaultManager_t *fm,
                                     FaultRecord_t *outArray,
                                     uint8_t maxRecords)
{
    if (!outArray || maxRecords == 0) {
        return 0;
    }

    uint8_t copyCount = (fm->ActiveFaultCount < maxRecords) ? fm->ActiveFaultCount : maxRecords;
    for (uint8_t i = 0; i < copyCount; i++) {
        outArray[i] = fm->ActiveFaults[i];
    }
    return copyCount;
}

void FaultManager_RemoveFault(FaultManager_t *fm, uint8_t index)
{
    if (index >= fm->ActiveFaultCount) {
        return;
    }

    for (uint8_t i = index; i < fm->ActiveFaultCount - 1; i++) {
        fm->ActiveFaults[i] = fm->ActiveFaults[i + 1];
    }

    fm->ActiveFaultCount--;
    FaultManager_UpdateFaultCache(fm);
}

void FaultManager_UpdateFaultCache(FaultManager_t *fm)
{
    fm->SystemFaultedCache = false;
    for (uint8_t i = 0; i < fm->ActiveFaultCount; i++) {
        if (fm->ActiveFaults[i].Severity != FAULT_SEVERITY_WARNING) {
            fm->SystemFaultedCache = true;
            break;
        }
    }
}
