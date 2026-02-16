#pragma once

#include <variant>
#include <functional>
#include <atomic>

#include "Command/CommandContext.h"
#include "Switching/CommutationManager.h"
#include "Switching/FOC.h"
#include "ThreadSafeQueue.h"
#include "Switching/PWMDriver.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"

// Forward declare if PWMDriver header is heavy
class PWMDriver; 

class RtBridge {

public:
    // -----------------------------
    // Definitions
    // -----------------------------

    enum class RtCmd : uint8_t {
        Enable,
        Disable,
        Estop,
        ClearEstop
    };

    struct SetRampRate         { float Value; };
    struct SetManualCarrierHz  { float Value; };
    struct SetManualCarrierMode{ bool Enable; };
    struct SetTargetFreq       { float Value; };
    struct SetFreqImmediate    { float Value; };
    struct InitZoneManager     { CommutationManager* Manager; };

    using StructVariant = std::variant<
        RtCmd,
        MotorConfig,
        SensorData,
        CurrentCommand,
        SetRampRate,
        SetManualCarrierHz,
        SetManualCarrierMode,
        SetTargetFreq,
        SetFreqImmediate,
        InitZoneManager
    >;

    // -----------------------------
    // Shared State Structure
    // -----------------------------
    struct RtStatusShared {
        volatile uint32_t Seq;
        RtStatus Status;
    };

    // -----------------------------
    // Constructor / Initialization
    // -----------------------------
    RtBridge() = default;

    // Launches Core 1 RT loop and returns a Core 0 safe context.
    CommandContext InitAndGetContext(CommutationManager* _Manager);

    // -----------------------------
    // Member Variables (Instance State)
    // -----------------------------
    ThreadSafeQueue<StructVariant> m_Queue;
    RtStatusShared m_SharedStatus {};


private:

    // Core 1 Owned Runtime Variables
    CommutationManager* m_Manager = nullptr;
    PWMDriver* m_Driver = nullptr;
    SPWMStrategy* m_Strategy = nullptr;
    FocController* FOCController_ = nullptr;

    // SPWMStrategy m_SpwmStrategy;

    float m_RampRate = 5.0f; 
    float m_ManualCarrierHz = 2000.0f;
    bool  m_ManualCarrierMode = false;

    // Cache for PWM reprogramming
    float    m_LastCarrierHz = 0.0f;
    bool     m_LastSyncMode  = false;
    uint16_t m_LastPulses    = 0;

    // -----------------------------
    // Helper for std::visit
    // -----------------------------
    template<class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
    template<class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

    // -----------------------------
    // Internal Logic Methods
    // -----------------------------
    void UpdateCarrierFromZones();
    void Core1Loop();

    // Status Read/Write (Seqlock)
    void StatusWrite(const RtStatus& _Status);
    bool StatusRead(RtStatus* _OutStatus);

    // -----------------------------
    // Static Trampoline (Adapter for C API)
    // -----------------------------
    static RtBridge* s_Instance; // The single instance pointer for the trampoline
    static void Core1EntryTrampoline(); // The C-style function pointer target

    // Context Helpers (Static functions to bind to CommandContext)
    // We use static bindings because CommandContext expects function pointers,
    // so we use the s_Instance pointer to route to the correct object.
    static void CtxSetRampRate(float _Value);
    static void CtxSetManualCarrierHz(float _Value);
    static void CtxSetManualCarrierMode(bool _Enable);
    static void CtxEnable();
    static void CtxDisable();
    static void CtxEstop();
    static void CtxClearEstop();
    static void CtxSetTargetFreq(float _Value);
    static void CtxSetFreqImmediate(float _Value);
    static bool CtxTryGetStatus(RtStatus* _OutStatus);
};