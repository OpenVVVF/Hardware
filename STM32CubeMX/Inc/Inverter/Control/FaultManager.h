#pragma once

#include <cstdint>
#include <cstddef>

namespace Inverter {

/**
 * @brief Severity levels for inverter faults.
 */
enum class FaultSeverity {
    Warning  = 0, /**< Logged only.                                           */
    High     = 1, /**< Blocks start(); running motor continues (Option A).    */
    Critical = 2, /**< Triggers triple-redundant shutdown instantly.          */
};

/**
 * @brief Central latched fault sources for the inverter.
 *
 * Sources can be raised from interrupt context (e.g. EXTI/DMA callbacks) and
 * are latched until explicitly cleared.  Use isActive() / isSeverityActive()
 * in safety-critical paths; the main loop prints/clears faults via the shell.
 */
enum class FaultSource : uint32_t {
    None             = 0,
    GateDriver       = 1u << 0,   /**< Legacy generic gate-driver fault.      */
    PwmBreak         = 1u << 1,   /**< TIM1 hardware break (DESAT).           */
    Max22530Ov       = 1u << 2,   /**< Vbus overvoltage (MAX22530 comparator) */
    Max22530Uv       = 1u << 3,   /**< Vbus undervoltage (MAX22530 comparator)*/
    Max22530Adc      = 1u << 4,   /**< MAX22530 ADC functionality diagnostic  */
    Max22530Comm     = 1u << 5,   /**< MAX22530 SPI framing/internal CRC error*/
    Max22530Field    = 1u << 6,   /**< MAX22530 field-side data-loss fault    */
    PhaseOvercurrent = 1u << 7,   /**< Phase current above safe limit         */
    AdcError         = 1u << 8,   /**< ADC HAL/overrun/queue error            */
    UartError        = 1u << 9,   /**< USART3 shell/telemetry error           */
    GateDriverUvlo   = 1u << 10,  /**< Gate-driver supply UVLO (/RDY low)     */
    EncoderDma       = 1u << 11,  /**< Encoder ADC DMA error                  */
    EncoderAmplitude = 1u << 12,  /**< Encoder sin/cos amplitude collapsed    */
    EncoderOutOfRange= 1u << 13,  /**< Encoder signal stuck at rail           */
    EncoderTimeout   = 1u << 14,  /**< No new encoder sample                  */
    CanBusOff        = 1u << 15,  /**< FDCAN bus-off                          */
    CanErrorPassive  = 1u << 16,  /**< FDCAN error-passive                    */
    CanProtocolError = 1u << 17,  /**< FDCAN protocol error                   */
    SupplyPvd        = 1u << 18,  /**< VDD supply dip (PVD)                   */
    SupplyAvd        = 1u << 19,  /**< VDDA supply dip (AVD)                  */
    SupplyVosrdy     = 1u << 20,  /**< Voltage scaling ready lost             */
    FramComm         = 1u << 21,  /**< SPI4 / F-RAM communication error       */
    AdcWatchdog      = 1u << 22,  /**< ADC analog watchdog overcurrent        */
};

constexpr FaultSource operator|(FaultSource a, FaultSource b) {
    return static_cast<FaultSource>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr FaultSource operator&(FaultSource a, FaultSource b) {
    return static_cast<FaultSource>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/**
 * @brief Typed reason code carried with a fault raise.
 *
 * Avoids copying runtime strings from interrupt context; the printable string is
 * resolved only when the fault is logged.
 */
enum class FaultReason : uint8_t {
    Unspecified = 0,
    UserInjected,
    DesatBreak,
    GateDriverNotReady,
    PhaseOvercurrentSoftware,
    AdcWatchdogTrip,
    AdcHalError,
    UartHalError,
    EncoderAmplitudeLow,
    EncoderAtRail,
    EncoderDmaError,
    EncoderSampleTimeout,
    CanBusOff,
    CanErrorPassive,
    CanErrorLogOverflow,
    CanHalError,
    VosNotReady,
    PvdTriggered,
    AvdTriggered,
    Max22530CrcMismatch,
    Max22530SpiFrameError,
    Max22530SpiDmaError,
    Max22530AdcDiagnostic,
    Max22530FieldLoss,
    Max22530Overvoltage,
    Max22530Undervoltage,
    FramInitIdMismatch,
    FramReadFailed,
    FramWriteFailed,
    FramCommandFailed,
    Count
};

/** @brief Human-readable string for a fault reason code. */
const char* faultReasonString(FaultReason r);

/**
 * @brief Static metadata for each fault source.
 */
struct FaultMeta {
    FaultSource   source;
    const char*   name;
    const char*   category;
    const char*   description;
    FaultSeverity severity;
};

class FaultManager {
public:
    static FaultManager& instance();

    /**
     * @brief Raise one or more fault sources.
     *
     * Safe to call from ISR context.  The @p reason code is stored per source
     * and resolved to a string later by service().
     */
    void raise(FaultSource src, FaultReason reason = FaultReason::Unspecified);

    /**
     * @brief Clear one or more fault sources.
     *
     * Safe to call from ISR context.
     */
    void clear(FaultSource src);

    /** @brief Clear all latched faults. */
    void clearAll();

    /**
     * @brief Return true if any source in the mask is active.
     *
     * @param mask Defaults to all sources.
     */
    bool isActive(FaultSource mask = static_cast<FaultSource>(~0u)) const;

    /** @brief Return true if any active fault has the given severity. */
    bool isSeverityActive(FaultSeverity severity) const;

    /** @brief Raw bit mask of currently active fault sources. */
    uint32_t activeFlags() const;

    /** @brief Emit all active faults to telemetry as "print" messages. */
    void printSummary();

    /**
     * @brief Log any newly-raised faults to telemetry.
     *
     * Call this from the main loop.  It avoids logging from interrupt context.
     */
    void service();

    /**
     * @brief Perform safety actions for active Critical faults.
     *
     * Call once per main-loop iteration after service().  This forces a TIM1
     * software break, asserts the gate-driver reset line, and turns off the
     * gate-driver power rail.
     */
    void executeSafetyActions();

    /**
     * @brief Inject a fault for testing.
     *
     * Call from main-loop / shell context only.
     */
    void testFault(FaultSource src, FaultReason reason = FaultReason::UserInjected);

    /** @brief Look up a fault source by its short name (case-insensitive). */
    static FaultSource sourceFromName(const char* name);

    /** @brief Metadata lookup. */
    static const FaultMeta* metaFor(FaultSource src);
    static const FaultMeta* metaTable();
    static constexpr size_t metaCount();

private:
    FaultManager() = default;

    static constexpr size_t SOURCE_COUNT = 32U;

    volatile uint32_t m_active = 0;
    volatile uint32_t m_pending_log = 0;
    volatile bool     m_safety_executed = false;

    FaultReason       m_reason[SOURCE_COUNT] = {};

    static constexpr FaultMeta s_meta[] = {
        { FaultSource::GateDriver,       "GateDriver",       "Gate Drive",   "legacy generic gate-driver fault",         FaultSeverity::Critical },
        { FaultSource::PwmBreak,         "PwmBreak",         "Gate Drive",   "hardware DESAT break event",               FaultSeverity::Critical },
        { FaultSource::Max22530Ov,       "Max22530Ov",       "Voltage Sense","Vbus overvoltage (MAX22530 comparator)",   FaultSeverity::Critical },
        { FaultSource::Max22530Uv,       "Max22530Uv",       "Voltage Sense","Vbus undervoltage (MAX22530 comparator)",  FaultSeverity::Critical },
        { FaultSource::Max22530Adc,      "Max22530Adc",      "Isolated ADC", "MAX22530 ADC functionality diagnostic",    FaultSeverity::High     },
        { FaultSource::Max22530Comm,     "Max22530Comm",     "Isolated ADC", "MAX22530 SPI framing/internal CRC error",  FaultSeverity::High     },
        { FaultSource::Max22530Field,    "Max22530Field",    "Isolated ADC", "MAX22530 field-side data-loss",            FaultSeverity::High     },
        { FaultSource::PhaseOvercurrent, "PhaseOvercurrent", "Current Sense","phase current above safe limit",           FaultSeverity::Critical },
        { FaultSource::AdcError,         "AdcError",         "STM32 ADC",    "ADC HAL/overrun/queue error",              FaultSeverity::High     },
        { FaultSource::UartError,        "UartError",        "Telemetry",    "USART3 shell/telemetry error",             FaultSeverity::Warning  },
        { FaultSource::GateDriverUvlo,   "GateDriverUvlo",   "Gate Drive",   "gate-driver supply UVLO (/RDY low)",       FaultSeverity::Critical },
        { FaultSource::EncoderDma,       "EncoderDma",       "Encoder",      "encoder ADC DMA error",                    FaultSeverity::High     },
        { FaultSource::EncoderAmplitude, "EncoderAmplitude", "Encoder",      "encoder sin/cos amplitude collapsed",      FaultSeverity::Warning  },
        { FaultSource::EncoderOutOfRange,"EncoderOutOfRange","Encoder",      "encoder signal stuck at rail",             FaultSeverity::Warning  },
        { FaultSource::EncoderTimeout,   "EncoderTimeout",   "Encoder",      "no new encoder sample",                    FaultSeverity::High     },
        { FaultSource::CanBusOff,        "CanBusOff",        "CAN",          "FDCAN bus-off",                            FaultSeverity::Critical },
        { FaultSource::CanErrorPassive,  "CanErrorPassive",  "CAN",          "FDCAN error-passive",                      FaultSeverity::High     },
        { FaultSource::CanProtocolError, "CanProtocolError", "CAN",          "FDCAN protocol error",                     FaultSeverity::High     },
        { FaultSource::SupplyPvd,        "SupplyPvd",        "Supply",       "VDD supply dip (PVD)",                     FaultSeverity::Critical },
        { FaultSource::SupplyAvd,        "SupplyAvd",        "Supply",       "VDDA supply dip (AVD)",                    FaultSeverity::Critical },
        { FaultSource::SupplyVosrdy,     "SupplyVosrdy",     "Supply",       "voltage scaling ready lost",               FaultSeverity::Critical },
        { FaultSource::FramComm,         "FramComm",         "Storage",      "SPI4 / F-RAM communication error",         FaultSeverity::High     },
        { FaultSource::AdcWatchdog,      "AdcWatchdog",      "STM32 ADC",    "ADC analog watchdog overcurrent",          FaultSeverity::Critical },
    };
};

constexpr size_t FaultManager::metaCount() {
    return sizeof(s_meta) / sizeof(s_meta[0]);
}

} // namespace Inverter
