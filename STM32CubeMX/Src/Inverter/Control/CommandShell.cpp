#include "Inverter/Control/CommandShell.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Drivers/Sensors/PolePairEstimator.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include "usart.h"
#include "pwm.h"
#include "gate_driver.h"

#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace {

/* newlib-nano vsnprintf may not link %f support, so format floats manually. */
void fmtFloat2(char* buf, size_t cap, float v) {
    int whole = (int)v;
    int frac = (int)((v - whole) * 100.0f + 0.5f);
    if (frac < 0) frac = -frac;
    std::snprintf(buf, cap, "%d.%02d", whole, frac);
}

void fmtFloat3(char* buf, size_t cap, float v) {
    int whole = (int)v;
    int frac = (int)((v - whole) * 1000.0f + 0.5f);
    if (frac < 0) frac = -frac;
    std::snprintf(buf, cap, "%d.%03d", whole, frac);
}

} // namespace

namespace Inverter {

static CommandShell s_instance;

CommandShell& commandShell() {
    return s_instance;
}

bool CommandShell::init() {
    if (m_initialized) {
        return true;
    }

    m_rx_head = 0;
    m_rx_tail = 0;
    m_line_len = 0;
    m_initialized = true;

    HAL_StatusTypeDef status = HAL_UART_Receive_IT(&huart3, &m_rx_buf[0], 1U);
    if (status != HAL_OK) {
        Telemetry::log("print", "[SHELL] ERROR: HAL_UART_Receive_IT failed");
        return false;
    }

    Telemetry::log("print", "[SHELL] Commands: start f m | stop | freq f | mod m | status | clearfault | cal | raw | polepairs | encodercal start/stop | motorcal | help");
    return true;
}

void CommandShell::onRxComplete() {
    /* HAL advances pRxBuffPtr after writing, so the received byte is always
     * at the start of the buffer we passed (m_rx_buf[0]). */
    uint8_t b = m_rx_buf[0];

    size_t next = (m_rx_head + 1U) % RX_BUF_SIZE;
    if (next != m_rx_tail) {
        m_rx_buf[m_rx_head] = b;
        m_rx_head = next;
    }

    /* Restart reception immediately. */
    HAL_UART_Receive_IT(&huart3, &m_rx_buf[0], 1U);
}

void CommandShell::poll() {
    if (!m_initialized) {
        return;
    }

    while (true) {
        __disable_irq();
        bool empty = (m_rx_head == m_rx_tail);
        uint8_t b = empty ? 0U : m_rx_buf[m_rx_tail];
        if (!empty) {
            m_rx_tail = (m_rx_tail + 1U) % RX_BUF_SIZE;
        }
        __enable_irq();

        if (empty) {
            break;
        }

        /* Collect until newline or line buffer full. */
        if (b == '\r' || b == '\n') {
            if (m_line_len > 0) {
                m_line[m_line_len] = '\0';

                /* Make a local copy and reset the buffer before parsing. */
                char tmp[LINE_SIZE];
                std::strncpy(tmp, m_line, LINE_SIZE - 1);
                tmp[LINE_SIZE - 1] = '\0';

                m_line_len = 0;
                m_line[0] = '\0';

                /* Dispatch the command. */
                char* argv[8] = {nullptr};
                int argc = 0;

                char* p = tmp;
                while (argc < 8) {
                    while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
                    if (*p == '\0') break;
                    argv[argc++] = p;
                    while (*p && !std::isspace(static_cast<unsigned char>(*p))) ++p;
                    if (*p) {
                        *p = '\0';
                        ++p;
                    }
                }

                if (argc == 0) continue;

                /* Lowercase the command token. */
                for (char* c = argv[0]; *c; ++c) {
                    *c = static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
                }

                OpenLoopController& ol = openLoopController();

                if (std::strcmp(argv[0], "start") == 0) {
                    if (argc < 3) {
                        Telemetry::log("print", "[SHELL] usage: start <freq_hz> <mod_idx>");
                    } else {
                        float f = std::strtof(argv[1], nullptr);
                        float m = std::strtof(argv[2], nullptr);
                        ol.start(f, m);
                    }
                }
                else if (std::strcmp(argv[0], "stop") == 0) {
                    ol.stop();
                }
                else if (std::strcmp(argv[0], "freq") == 0) {
                    if (argc < 2) {
                        Telemetry::log("print", "[SHELL] usage: freq <freq_hz>");
                    } else {
                        float f = std::strtof(argv[1], nullptr);
                        ol.setFrequency(f);
                        char fbuf[16];
                        fmtFloat2(fbuf, sizeof(fbuf), f);
                        char msg[48];
                        std::snprintf(msg, sizeof(msg), "[SHELL] freq set to %s Hz", fbuf);
                        Telemetry::log("print", msg);
                    }
                }
                else if (std::strcmp(argv[0], "mod") == 0) {
                    if (argc < 2) {
                        Telemetry::log("print", "[SHELL] usage: mod <mod_idx>");
                    } else {
                        float m = std::strtof(argv[1], nullptr);
                        ol.setModulationIndex(m);
                        char mbuf[16];
                        fmtFloat3(mbuf, sizeof(mbuf), m);
                        char msg[48];
                        std::snprintf(msg, sizeof(msg), "[SHELL] mod set to %s", mbuf);
                        Telemetry::log("print", msg);
                    }
                }
                else if (std::strcmp(argv[0], "status") == 0) {
                    char fbuf[16], mbuf[16], msg[64];
                    fmtFloat2(fbuf, sizeof(fbuf), ol.frequencyHz());
                    fmtFloat3(mbuf, sizeof(mbuf), ol.modulationIndex());
                    std::snprintf(msg, sizeof(msg), "[SHELL] run=%s f=%s m=%s",
                                  ol.isRunning() ? "Y" : "N", fbuf, mbuf);
                    Telemetry::log("print", msg);
                    std::snprintf(msg, sizeof(msg), "[SHELL] ready=%s fault=%s",
                                  GateDriver_IsReady() ? "Y" : "N",
                                  GateDriver_IsFault() ? "Y" : "N");
                    Telemetry::log("print", msg);
                }
                else if (std::strcmp(argv[0], "clearfault") == 0) {
                    GateDriver_ResetPulse();
                    PWM_ClearFault();
                    Telemetry::log("print", "[SHELL] fault cleared");
                }
                else if (std::strcmp(argv[0], "cal") == 0) {
                    if (ol.isRunning()) {
                        Telemetry::log("print", "[SHELL] stop motor before calibrating");
                    } else if (phaseCurrentADC().recalibrateOffsets()) {
                        PhaseCurrentADC& adc = phaseCurrentADC();
                        char uoff[16], voff[16];
                        fmtFloat3(uoff, sizeof(uoff), adc.lastOffsetU());
                        fmtFloat3(voff, sizeof(voff), adc.lastOffsetV());
                        char msg[64];
                        std::snprintf(msg, sizeof(msg),
                                      "[SHELL] calibrated offsets U=%s V=%s", uoff, voff);
                        Telemetry::log("print", msg);
                    } else {
                        Telemetry::log("print", "[SHELL] calibration failed");
                    }
                }
                else if (std::strcmp(argv[0], "raw") == 0) {
                    PhaseCurrentADC& adc = phaseCurrentADC();
                    const int32_t u_diff = static_cast<int32_t>(adc.lastRawUSig()) -
                                           static_cast<int32_t>(adc.lastRawURef());
                    const int32_t v_diff = static_cast<int32_t>(adc.lastRawVSig()) -
                                           static_cast<int32_t>(adc.lastRawVRef());
                    char msg[64];
                    std::snprintf(msg, sizeof(msg),
                                  "[SHELL] raw U sig=%lu ref=%lu diff=%ld",
                                  adc.lastRawUSig(), adc.lastRawURef(), u_diff);
                    Telemetry::log("print", msg);
                    std::snprintf(msg, sizeof(msg),
                                  "[SHELL] raw V sig=%lu ref=%lu diff=%ld",
                                  adc.lastRawVSig(), adc.lastRawVRef(), v_diff);
                    Telemetry::log("print", msg);

                    char uoff[16], voff[16];
                    fmtFloat3(uoff, sizeof(uoff), adc.lastOffsetU());
                    fmtFloat3(voff, sizeof(voff), adc.lastOffsetV());
                    std::snprintf(msg, sizeof(msg),
                                  "[SHELL] offsets U=%s V=%s", uoff, voff);
                    Telemetry::log("print", msg);
                }
                else if (std::strcmp(argv[0], "polepairs") == 0) {
                    PolePairEstimator& pp = PolePairEstimator::instance();
                    char est[16], mech[16], elec[16];
                    fmtFloat3(est, sizeof(est), pp.estimate());
                    fmtFloat2(mech, sizeof(mech), pp.mechanicalCycles());
                    fmtFloat2(elec, sizeof(elec), pp.electricalCycles());
                    char msg[80];
                    std::snprintf(msg, sizeof(msg),
                                  "[SHELL] pp=%s mech=%s elec=%s", est, mech, elec);
                    Telemetry::log("print", msg);
                }
                else if (std::strcmp(argv[0], "encodercal") == 0) {
                    PolePairEstimator& pp = PolePairEstimator::instance();
                    if (argc < 2) {
                        Telemetry::log("print", "[SHELL] usage: encodercal start | stop");
                    } else if (std::strcmp(argv[1], "start") == 0) {
                        pp.startManualEncoderCal();
                        Telemetry::log("print", "[SHELL] encoder cal started; rotate shaft exactly 1 rev, then 'encodercal stop'");
                    } else if (std::strcmp(argv[1], "stop") == 0) {
                        pp.stopManualEncoderCal();
                        char cycles[16];
                        fmtFloat2(cycles, sizeof(cycles), pp.manualEncoderCycles());
                        char msg[96];
                        std::snprintf(msg, sizeof(msg),
                                      "[SHELL] encoder cycles in 1 rev = %s; true PP = pp_estimate * %s",
                                      cycles, cycles);
                        Telemetry::log("print", msg);
                    } else {
                        Telemetry::log("print", "[SHELL] usage: encodercal start | stop");
                    }
                }
                else if (std::strcmp(argv[0], "motorcal") == 0) {
                    if (ol.isRunning() && !ol.isCalibrating()) {
                        Telemetry::log("print", "[SHELL] stop the motor before starting motorcal");
                    } else {
                        ol.startCalibration();
                    }
                }
                else if (std::strcmp(argv[0], "help") == 0) {
                    Telemetry::log("print", "[SHELL] Commands: start f m | stop | freq f | mod m | status | clearfault | cal | raw | polepairs | encodercal start/stop | motorcal | help");
                }
                else {
                    /* Unknown but printable command: tell the user instead of
                     * silently dropping, so typos are obvious. */
                    Telemetry::log("print", "[SHELL] unknown command");
                }
            }
        }
        else if (m_line_len < LINE_SIZE - 1) {
            m_line[m_line_len++] = static_cast<char>(b);
        }
    }
}

} // namespace Inverter

extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart) {
    if (huart != nullptr && huart->Instance == USART3) {
        Inverter::commandShell().onRxComplete();
    }
}
