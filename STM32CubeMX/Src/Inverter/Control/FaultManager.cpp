#include "Inverter/Control/FaultManager.h"
#include "Inverter/Telemetry.h"
#include "Inverter/Drivers/GateDriver/gate_driver.h"
#include "Inverter/Drivers/PWM/pwm.h"

#include "main.h"
#include "tim.h"

#include <cctype>

namespace Inverter {

/* Out-of-class definition for pre-C++17 ODR. */
constexpr Inverter::FaultMeta Inverter::FaultManager::s_meta[];

const char* faultReasonString(FaultReason r) {
    switch (r) {
        case FaultReason::Unspecified:          return "unspecified";
        case FaultReason::UserInjected:         return "injected by user";
        case FaultReason::DesatBreak:           return "DESAT break (/FLT low)";
        case FaultReason::GateDriverNotReady:   return "gate driver not ready";
        case FaultReason::PhaseOvercurrentSoftware: return "software overcurrent";
        case FaultReason::AdcWatchdogTrip:      return "ADC1 injected out of window";
        case FaultReason::AdcHalError:          return "ADC HAL error";
        case FaultReason::UartHalError:         return "UART error";
        case FaultReason::EncoderAmplitudeLow:  return "encoder magnitude collapsed";
        case FaultReason::EncoderAtRail:        return "encoder signal at rail";
        case FaultReason::EncoderDmaError:      return "encoder ADC DMA error";
        case FaultReason::EncoderSampleTimeout: return "no encoder sample";
        case FaultReason::CanBusOff:            return "FDCAN2 bus-off";
        case FaultReason::CanErrorPassive:      return "FDCAN2 error-passive";
        case FaultReason::CanErrorLogOverflow:  return "FDCAN2 error-log overflow";
        case FaultReason::CanHalError:          return "FDCAN2 HAL error";
        case FaultReason::VosNotReady:          return "VOS not ready";
        case FaultReason::PvdTriggered:         return "PVD triggered";
        case FaultReason::AvdTriggered:         return "AVD triggered";
        case FaultReason::Max22530CrcMismatch:  return "CRC mismatch";
        case FaultReason::Max22530SpiFrameError:return "SPI frame/CRC error";
        case FaultReason::Max22530SpiDmaError:  return "SPI DMA error";
        case FaultReason::Max22530AdcDiagnostic:return "ADC functionality diagnostic";
        case FaultReason::Max22530FieldLoss:    return "field-side data loss";
        case FaultReason::Max22530Overvoltage:  return "comparator high threshold";
        case FaultReason::Max22530Undervoltage: return "comparator low threshold";
        case FaultReason::FramInitIdMismatch:   return "device ID mismatch";
        case FaultReason::FramReadFailed:       return "read failed";
        case FaultReason::FramWriteFailed:      return "write failed";
        case FaultReason::FramCommandFailed:    return "command failed";
        case FaultReason::Count:                break;
    }
    return "unknown";
}

FaultManager& FaultManager::instance() {
    static FaultManager s_instance;
    return s_instance;
}

const FaultMeta* FaultManager::metaFor(FaultSource src) {
    const uint32_t bit = static_cast<uint32_t>(src);
    for (size_t i = 0; i < metaCount(); ++i) {
        if (static_cast<uint32_t>(s_meta[i].source) == bit) {
            return &s_meta[i];
        }
    }
    return nullptr;
}

const FaultMeta* FaultManager::metaTable() {
    return s_meta;
}

FaultSource FaultManager::sourceFromName(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return FaultSource::None;
    }
    for (size_t i = 0; i < metaCount(); ++i) {
        const char* a = name;
        const char* b = s_meta[i].name;
        while (*a && *b &&
               std::tolower(static_cast<unsigned char>(*a)) ==
               std::tolower(static_cast<unsigned char>(*b))) {
            ++a; ++b;
        }
        if (*a == '\0' && *b == '\0') {
            return s_meta[i].source;
        }
    }
    return FaultSource::None;
}

void FaultManager::raise(FaultSource src, FaultReason reason) {
    const uint32_t bits = static_cast<uint32_t>(src);
    if (bits == 0) {
        return;
    }

    __disable_irq();
    const uint32_t old = m_active;
    m_active |= bits;
    const uint32_t newly = m_active & ~old;
    m_pending_log |= newly;

    uint32_t b = newly;
    while (b != 0) {
        const int idx = __builtin_ctz(b);
        m_reason[idx] = reason;
        b &= b - 1U;
    }
    __enable_irq();
}

void FaultManager::clear(FaultSource src) {
    const uint32_t bits = static_cast<uint32_t>(src);
    if (bits == 0) {
        return;
    }

    __disable_irq();
    m_active &= ~bits;
    __enable_irq();
}

void FaultManager::clearAll() {
    __disable_irq();
    m_active = 0;
    m_pending_log = 0;
    m_safety_executed = false;
    for (size_t i = 0; i < SOURCE_COUNT; ++i) {
        m_reason[i] = FaultReason::Unspecified;
    }
    __enable_irq();
}

bool FaultManager::isActive(FaultSource mask) const {
    __disable_irq();
    const bool active = (m_active & static_cast<uint32_t>(mask)) != 0;
    __enable_irq();
    return active;
}

bool FaultManager::isSeverityActive(FaultSeverity severity) const {
    const uint32_t flags = activeFlags();
    if (flags == 0) {
        return false;
    }
    for (size_t i = 0; i < metaCount(); ++i) {
        if (s_meta[i].severity == severity &&
            (flags & static_cast<uint32_t>(s_meta[i].source)) != 0) {
            return true;
        }
    }
    return false;
}

uint32_t FaultManager::activeFlags() const {
    __disable_irq();
    const uint32_t flags = m_active;
    __enable_irq();
    return flags;
}

void FaultManager::printSummary() {
    const uint32_t flags = activeFlags();

    if (flags == 0) {
        Telemetry::printf("[FAULT] none active");
        return;
    }

    for (size_t i = 0; i < metaCount(); ++i) {
        const auto& m = s_meta[i];
        if ((flags & static_cast<uint32_t>(m.source)) != 0) {
            const int idx = __builtin_ctz(static_cast<uint32_t>(m.source));
            const char* reason = faultReasonString(m_reason[idx]);
            char sev_char = '?';
            switch (m.severity) {
                case FaultSeverity::Warning:  sev_char = 'W'; break;
                case FaultSeverity::High:     sev_char = 'H'; break;
                case FaultSeverity::Critical: sev_char = 'C'; break;
            }
            if (m_reason[idx] != FaultReason::Unspecified) {
                Telemetry::printf("[FAULT][%c][%s] %s: %s (%s)",
                                  sev_char, m.category, m.name, m.description, reason);
            } else {
                Telemetry::printf("[FAULT][%c][%s] %s: %s",
                                  sev_char, m.category, m.name, m.description);
            }
        }
    }
}

void FaultManager::service() {
    __disable_irq();
    const uint32_t pending = m_pending_log;
    m_pending_log = 0;
    __enable_irq();

    if (pending == 0) {
        return;
    }

    for (size_t i = 0; i < metaCount(); ++i) {
        const auto& m = s_meta[i];
        if ((pending & static_cast<uint32_t>(m.source)) != 0) {
            const int idx = __builtin_ctz(static_cast<uint32_t>(m.source));
            const char* reason = faultReasonString(m_reason[idx]);
            char sev_char = '?';
            switch (m.severity) {
                case FaultSeverity::Warning:  sev_char = 'W'; break;
                case FaultSeverity::High:     sev_char = 'H'; break;
                case FaultSeverity::Critical: sev_char = 'C'; break;
            }
            if (m_reason[idx] != FaultReason::Unspecified) {
                Telemetry::printf("[FAULT][%c][%s] %s triggered: %s",
                                  sev_char, m.category, m.name, reason);
            } else {
                Telemetry::printf("[FAULT][%c][%s] %s triggered",
                                  sev_char, m.category, m.name);
            }
        }
    }
}

void FaultManager::executeSafetyActions() {
    if (!isSeverityActive(FaultSeverity::Critical)) {
        /* Reset the one-shot so the next critical fault logs/shuts down again. */
        __disable_irq();
        m_safety_executed = false;
        __enable_irq();
        return;
    }

    if (m_safety_executed) {
        return;
    }
    m_safety_executed = true;

    /* Triple-redundant shutdown:
     * 1. Force TIM1 break -> hardware disables all PWM outputs (MOE clear).
     * 2. Assert gate-driver reset line.
     * 3. Turn off gate-driver power rail. */
    if (TIM1 != nullptr) {
        TIM1->EGR |= TIM_EGR_BG;
    }
    PWM_StopSPWM();
    GateDriver_DisableOutputs();
    GateDriver_EnablePower(false);

    Telemetry::printf("[SAFETY] Critical fault -> PWM break, gate driver reset, power off");
}

void FaultManager::testFault(FaultSource src, FaultReason reason) {
    raise(src, reason);
}

} // namespace Inverter
