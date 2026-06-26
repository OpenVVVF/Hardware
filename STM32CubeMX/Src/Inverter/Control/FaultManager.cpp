#include "Inverter/Control/FaultManager.h"
#include "Inverter/Telemetry.h"

#include <cstdio>
#include <cstring>

namespace Inverter {

namespace {

struct Entry {
    FaultSource src;
    const char* name;
};

static const Entry entries[] = {
    { FaultSource::GateDriver,       "GateDriver" },
    { FaultSource::PwmBreak,         "PwmBreak" },
    { FaultSource::Max22530Ov,       "Max22530Ov" },
    { FaultSource::Max22530Uv,       "Max22530Uv" },
    { FaultSource::Max22530Adc,      "Max22530Adc" },
    { FaultSource::Max22530Comm,     "Max22530Comm" },
    { FaultSource::Max22530Field,    "Max22530Field" },
    { FaultSource::PhaseOvercurrent, "PhaseOvercurrent" },
    { FaultSource::AdcError,         "AdcError" },
    { FaultSource::UartError,        "UartError" },
};

} // namespace

FaultManager& FaultManager::instance() {
    static FaultManager s_instance;
    return s_instance;
}

void FaultManager::raise(FaultSource src, const char* reason) {
    const uint32_t bits = static_cast<uint32_t>(src);
    if (bits == 0) {
        return;
    }

    __disable_irq();
    const uint32_t old = m_active;
    m_active |= bits;
    const uint32_t newly = m_active & ~old;
    /* Defer telemetry logging to the main loop unless a reason string was
     * supplied from a main-thread context. */
    if (newly != 0 && (reason == nullptr || reason[0] == '\0')) {
        m_pending_log |= newly;
    }
    __enable_irq();

    if (reason != nullptr && reason[0] != '\0' && newly != 0) {
        char msg[80];
        std::snprintf(msg, sizeof(msg), "[FAULT] %s", reason);
        Telemetry::log("print", msg);
    }
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
    __enable_irq();
}

bool FaultManager::isActive(FaultSource mask) const {
    __disable_irq();
    const bool active = (m_active & static_cast<uint32_t>(mask)) != 0;
    __enable_irq();
    return active;
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
        Telemetry::log("print", "[FAULT] none active");
        return;
    }

    for (const auto& e : entries) {
        if ((flags & static_cast<uint32_t>(e.src)) != 0) {
            char msg[64];
            std::snprintf(msg, sizeof(msg), "[FAULT] %s", e.name);
            Telemetry::log("print", msg);
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

    for (const auto& e : entries) {
        if ((pending & static_cast<uint32_t>(e.src)) != 0) {
            char msg[64];
            std::snprintf(msg, sizeof(msg), "[FAULT] %s triggered", e.name);
            Telemetry::log("print", msg);
        }
    }
}

} // namespace Inverter
