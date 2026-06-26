#pragma once

#include <cstdint>
#include <cstddef>

namespace Inverter {

/**
 * @brief Central latched fault sources for the inverter.
 *
 * Sources can be raised from interrupt context (e.g. EXTI/DMA callbacks) and
 * are latched until explicitly cleared.  Use isActive() in safety-critical
 * paths; the main loop can print/clear faults via the shell.
 */
enum class FaultSource : uint32_t {
    None             = 0,
    GateDriver       = 1u << 0,
    PwmBreak         = 1u << 1,
    Max22530Ov       = 1u << 2,   /**< Vbus overvoltage (MAX22530 comparator) */
    Max22530Uv       = 1u << 3,   /**< Vbus undervoltage (MAX22530 comparator) */
    Max22530Adc      = 1u << 4,   /**< MAX22530 ADC functionality diagnostic error */
    Max22530Comm     = 1u << 5,   /**< MAX22530 SPI framing or internal CRC error */
    Max22530Field    = 1u << 6,   /**< MAX22530 field-side data-loss fault */
    PhaseOvercurrent = 1u << 7,   /**< Phase current above safe limit */
    AdcError         = 1u << 8,   /**< ADC HAL/overrun/queue error */
    UartError        = 1u << 9,   /**< USART3 shell/telemetry error */
};

constexpr FaultSource operator|(FaultSource a, FaultSource b) {
    return static_cast<FaultSource>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr FaultSource operator&(FaultSource a, FaultSource b) {
    return static_cast<FaultSource>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

class FaultManager {
public:
    static FaultManager& instance();

    /**
     * @brief Raise one or more fault sources.
     *
     * Safe to call from ISR context.
     */
    void raise(FaultSource src, const char* reason = nullptr);

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

private:
    FaultManager() = default;

    volatile uint32_t m_active = 0;
    volatile uint32_t m_pending_log = 0;
};

} // namespace Inverter
