#include "Inverter/Control/CommandShell.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Calibration/PolePairCalibrator.h"
#include "Inverter/Calibration/ResistanceCalibrator.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Drivers/Sensors/PolePairEstimator.h"
#include "Inverter/Drivers/Logging/SupplyMonitor.h"
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

    /* Clear any stale error/idle flags left from the power-up / debugger
     * transient before unmasking the UART interrupt. */
    __HAL_UART_CLEAR_FLAG(&huart3, UART_CLEAR_PEF | UART_CLEAR_FEF |
                                  UART_CLEAR_NEF | UART_CLEAR_OREF |
                                  UART_CLEAR_IDLEF);
    HAL_NVIC_ClearPendingIRQ(USART3_IRQn);

    HAL_StatusTypeDef status = HAL_UART_Receive_IT(&huart3, &m_rx_buf[0], 1U);
    if (status != HAL_OK) {
        Telemetry::log("print", "[SHELL] ERROR: HAL_UART_Receive_IT failed");
        return false;
    }

    Telemetry::log("print", "[SHELL] Commands: start f m | stop | freq f | mod m | status | clearfault | fault list/clear/test | cal | raw | vzero | maxcfg ov/uv/thresholds/status/filterclear/raw/filtered | ocset amps | hwocset amps | supply status | polepairs | encodercal start/stop | calpolepairs | rescal start [uv|uw|vw] <bus_pct> [max_a] | rescal status | help");
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

void CommandShell::recover() {
    if (!m_initialized) return;

    /* Clear error/idle flags and restart reception. */
    __HAL_UART_CLEAR_FLAG(&huart3, UART_CLEAR_PEF | UART_CLEAR_FEF |
                                  UART_CLEAR_NEF | UART_CLEAR_OREF |
                                  UART_CLEAR_IDLEF);
    HAL_NVIC_ClearPendingIRQ(USART3_IRQn);
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
                    char fbuf[16], mbuf[16], msg[80];
                    fmtFloat2(fbuf, sizeof(fbuf), ol.frequencyHz());
                    fmtFloat3(mbuf, sizeof(mbuf), ol.modulationIndex());
                    std::snprintf(msg, sizeof(msg), "[SHELL] run=%s f=%s m=%s",
                                  ol.isRunning() ? "Y" : "N", fbuf, mbuf);
                    Telemetry::log("print", msg);
                    std::snprintf(msg, sizeof(msg), "[SHELL] ready=%s gd_fault=%s",
                                  GateDriver_IsReady() ? "Y" : "N",
                                  GateDriver_IsFault() ? "Y" : "N");
                    Telemetry::log("print", msg);
                    FaultManager::instance().printSummary();
                }
                else if (std::strcmp(argv[0], "clearfault") == 0) {
                    /* Re-enable gate-driver power so the board can be started again,
                     * but keep the gate-driver outputs disabled and the TIM1 master
                     * output off.  Switching must only resume after an explicit
                     * 'start' command. */
                    GateDriver_EnablePower(true);
                    HAL_Delay(50);
                    GateDriver_DisableOutputs();
                    PWM_ClearBreakFlag();
                    FaultManager::instance().clearAll();

                    /* The isolated ADC may have a latched comparator interrupt and/or
                     * a stale rolling-average filter from the fault event.  Clear both
                     * and drop any pending EXTI1 interrupt so the old event does not
                     * immediately re-trigger the latched fault. */
                    MAX22530& adc = dcLinkVoltageSensor().adc();
                    (void)adc.clearInterruptStatus();
                    (void)adc.clearFilter(0);
                    __HAL_GPIO_EXTI_CLEAR_IT(VSENSE_ISO_ADC_INTERRUPT_Pin);
                    HAL_NVIC_ClearPendingIRQ(EXTI1_IRQn);

                    Telemetry::log("print", "[SHELL] faults cleared; gate driver powered but outputs disabled");
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
                else if (std::strcmp(argv[0], "vzero") == 0) {
                    DcLinkVoltageSensor& vdc = dcLinkVoltageSensor();
                    if (vdc.zeroCalibrate()) {
                        char vbuf[16];
                        fmtFloat3(vbuf, sizeof(vbuf), vdc.voltage());
                        char msg[48];
                        std::snprintf(msg, sizeof(msg), "[SHELL] Vdc zero calibrated: %s V", vbuf);
                        Telemetry::log("print", msg);
                    } else {
                        Telemetry::log("print", "[SHELL] Vdc zero cal failed (no sample)");
                    }
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
                else if (std::strcmp(argv[0], "calpolepairs") == 0) {
                    PolePairCalibrator& cal = PolePairCalibrator::instance();
                    if (ol.isRunning()) {
                        Telemetry::log("print", "[SHELL] stop the motor before starting calpolepairs");
                    } else if (cal.isActive()) {
                        Telemetry::log("print", "[SHELL] calibration already running");
                    } else {
                        cal.start();
                    }
                }
                else if (std::strcmp(argv[0], "rescal") == 0) {
                    if (argc < 2 || std::strcmp(argv[1], "status") == 0) {
                        ResistanceCalibrator& rc = ResistanceCalibrator::instance();
                        char uv[16], uw[16], vw[16], avg[16];
                        fmtFloat3(uv, sizeof(uv), rc.lastResult(ResistanceCalibrator::Pair::UV));
                        fmtFloat3(uw, sizeof(uw), rc.lastResult(ResistanceCalibrator::Pair::UW));
                        fmtFloat3(vw, sizeof(vw), rc.lastResult(ResistanceCalibrator::Pair::VW));
                        fmtFloat3(avg, sizeof(avg), rc.lastAverage());
                        char msg[128];
                        std::snprintf(msg, sizeof(msg),
                                      "[SHELL] rescal: R_uv=%s R_uw=%s R_vw=%s avg=%s ohm",
                                      uv, uw, vw, avg);
                        Telemetry::log("print", msg);
                    } else if (std::strcmp(argv[1], "start") == 0) {
                        if (argc < 3) {
                            Telemetry::log("print", "[SHELL] usage: rescal start [uv|uw|vw] <bus_pct> [max_a]");
                        } else {
                            float bus_pct = 0.0f;
                            float max_a = 50.0f;
                            ResistanceCalibrator::Pair pair = ResistanceCalibrator::Pair::UV;
                            bool run_all = true;

                            if (std::strcmp(argv[2], "uv") == 0 ||
                                std::strcmp(argv[2], "uw") == 0 ||
                                std::strcmp(argv[2], "vw") == 0) {
                                if (argc < 4) {
                                    Telemetry::log("print", "[SHELL] usage: rescal start [uv|uw|vw] <bus_pct> [max_a]");
                                    continue;
                                }
                                run_all = false;
                                if (std::strcmp(argv[2], "uv") == 0) {
                                    pair = ResistanceCalibrator::Pair::UV;
                                } else if (std::strcmp(argv[2], "uw") == 0) {
                                    pair = ResistanceCalibrator::Pair::UW;
                                } else {
                                    pair = ResistanceCalibrator::Pair::VW;
                                }
                                bus_pct = std::strtof(argv[3], nullptr);
                                if (argc >= 5) {
                                    max_a = std::strtof(argv[4], nullptr);
                                }
                            } else {
                                bus_pct = std::strtof(argv[2], nullptr);
                                if (argc >= 4) {
                                    max_a = std::strtof(argv[3], nullptr);
                                }
                            }

                            if (!resistanceCalibrator().start(bus_pct, pair, run_all, 5000U, max_a)) {
                                Telemetry::log("print", "[SHELL] rescal start failed");
                            }
                        }
                    } else {
                        Telemetry::log("print", "[SHELL] usage: rescal start [uv|uw|vw] <bus_pct> [max_a] | rescal status");
                    }
                }
                else if (std::strcmp(argv[0], "fault") == 0) {
                    if (argc < 2) {
                        Telemetry::log("print", "[SHELL] usage: fault list | clear | test <name>");
                    } else if (std::strcmp(argv[1], "list") == 0) {
                        FaultManager::instance().printSummary();
                    } else if (std::strcmp(argv[1], "clear") == 0) {
                        FaultManager::instance().clearAll();
                        Telemetry::log("print", "[SHELL] latched faults cleared");
                    } else if (std::strcmp(argv[1], "test") == 0) {
                        if (argc < 3) {
                            Telemetry::log("print", "[SHELL] usage: fault test <FaultName>");
                        } else {
                            FaultSource src = FaultManager::sourceFromName(argv[2]);
                            if (src == FaultSource::None) {
                                Telemetry::log("print", "[SHELL] unknown fault name; use 'fault list' to see names");
                            } else {
                                FaultManager::instance().testFault(src);
                                Telemetry::log("print", "[SHELL] injected test fault");
                            }
                        }
                    } else {
                        Telemetry::log("print", "[SHELL] usage: fault list | clear | test <name>");
                    }
                }
                else if (std::strcmp(argv[0], "ocset") == 0) {
                    if (argc < 2) {
                        Telemetry::log("print", "[SHELL] usage: ocset <amps>  (0 disables)");
                    } else {
                        float amps = std::strtof(argv[1], nullptr);
                        if (amps < 0.0f) amps = 0.0f;
                        phaseCurrentADC().setOvercurrentThreshold(amps);
                        char abuf[16];
                        fmtFloat3(abuf, sizeof(abuf), amps);
                        char msg[64];
                        std::snprintf(msg, sizeof(msg), "[SHELL] phase overcurrent threshold set to %s A", abuf);
                        Telemetry::log("print", msg);
                    }
                }
                else if (std::strcmp(argv[0], "hwocset") == 0) {
                    if (argc < 2) {
                        Telemetry::log("print", "[SHELL] usage: hwocset <amps>  (0 disables ADC watchdog)");
                    } else {
                        float amps = std::strtof(argv[1], nullptr);
                        if (amps < 0.0f) amps = 0.0f;
                        if (phaseCurrentADC().setHardwareOvercurrentThreshold(amps)) {
                            char abuf[16];
                            fmtFloat3(abuf, sizeof(abuf), amps);
                            char msg[80];
                            std::snprintf(msg, sizeof(msg), "[SHELL] hardware overcurrent threshold set to %s A", abuf);
                            Telemetry::log("print", msg);
                        } else {
                            Telemetry::log("print", "[SHELL] failed to set hardware overcurrent threshold (stop motor first)");
                        }
                    }
                }
                else if (std::strcmp(argv[0], "supply") == 0) {
                    if (argc < 2 || std::strcmp(argv[1], "status") != 0) {
                        Telemetry::log("print", "[SHELL] usage: supply status");
                    } else {
                        supplyMonitorPrintStatus();
                    }
                }
                else if (std::strcmp(argv[0], "maxcfg") == 0) {
                    DcLinkVoltageSensor& vdc = dcLinkVoltageSensor();
                    MAX22530& adc = vdc.adc();

                    if (argc < 2) {
                        Telemetry::log("print", "[SHELL] usage: maxcfg ov <V> | uv <V> | thresholds | status | filterclear | raw | filtered");
                    } else if (std::strcmp(argv[1], "ov") == 0) {
                        if (argc < 3) {
                            Telemetry::log("print", "[SHELL] usage: maxcfg ov <volts>");
                        } else {
                            float v = std::strtof(argv[2], nullptr);
                            if (vdc.setOvervoltageThreshold(v)) {
                                char vbuf[16];
                                fmtFloat3(vbuf, sizeof(vbuf), v);
                                char msg[64];
                                std::snprintf(msg, sizeof(msg), "[SHELL] Vbus OV threshold set to %s V", vbuf);
                                Telemetry::log("print", msg);
                            } else {
                                Telemetry::log("print", "[SHELL] failed to set OV threshold");
                            }
                        }
                    } else if (std::strcmp(argv[1], "uv") == 0) {
                        if (argc < 3) {
                            Telemetry::log("print", "[SHELL] usage: maxcfg uv <volts>");
                        } else {
                            float v = std::strtof(argv[2], nullptr);
                            if (vdc.setUndervoltageThreshold(v)) {
                                char vbuf[16];
                                fmtFloat3(vbuf, sizeof(vbuf), v);
                                char msg[64];
                                std::snprintf(msg, sizeof(msg), "[SHELL] Vbus UV threshold set to %s V", vbuf);
                                Telemetry::log("print", msg);
                            } else {
                                Telemetry::log("print", "[SHELL] failed to set UV threshold");
                            }
                        }
                    } else if (std::strcmp(argv[1], "status") == 0) {
                        uint16_t cout_status = 0;
                        (void)adc.getComparatorStatus(cout_status);
                        uint16_t couthi = 0, coutlo = 0;
                        (void)adc.readComparatorThreshold(0, couthi, coutlo);
                        char msg[80];
                        std::snprintf(msg, sizeof(msg), "[SHELL] MAX int_status=0x%04X cout_status=0x%04X int_enable=0x%04X",
                                      adc.lastInterruptStatus(), cout_status, adc.lastInterruptEnable());
                        Telemetry::log("print", msg);
                        std::snprintf(msg, sizeof(msg), "[SHELL] MAX ch1 thresholds: HI=0x%03X LO=0x%03X",
                                      couthi, coutlo);
                        Telemetry::log("print", msg);
                        std::snprintf(msg, sizeof(msg), "[SHELL] MAX irq=%lu dma=%lu err=%lu fail=%lu crc_err=%lu",
                                      static_cast<unsigned long>(adc.irqCount()),
                                      static_cast<unsigned long>(adc.dmaCompleteCount()),
                                      static_cast<unsigned long>(adc.dmaErrorCount()),
                                      static_cast<unsigned long>(adc.dmaStartFailCount()),
                                      static_cast<unsigned long>(adc.crcErrorCount()));
                        Telemetry::log("print", msg);
                    } else if (std::strcmp(argv[1], "thresholds") == 0) {
                        char ov[16], uv[16];
                        fmtFloat3(ov, sizeof(ov), vdc.overvoltageThreshold());
                        fmtFloat3(uv, sizeof(uv), vdc.undervoltageThreshold());
                        char msg[80];
                        std::snprintf(msg, sizeof(msg), "[SHELL] Vbus OV=%s V UV=%s V (scaled)", ov, uv);
                        Telemetry::log("print", msg);
                    } else if (std::strcmp(argv[1], "filterclear") == 0) {
                        if (adc.clearFilter(0)) {
                            Telemetry::log("print", "[SHELL] MAX channel 1 filter cleared");
                        } else {
                            Telemetry::log("print", "[SHELL] MAX filter clear failed");
                        }
                    } else if (std::strcmp(argv[1], "raw") == 0) {
                        const uint16_t counts = adc.readRawCounts(0);
                        const float v = adc.readRawVoltage(0);
                        char vbuf[16];
                        fmtFloat3(vbuf, sizeof(vbuf), v);
                        char msg[80];
                        std::snprintf(msg, sizeof(msg), "[SHELL] MAX raw ch1 = 0x%03X (%lu counts), %s V",
                                      counts, static_cast<unsigned long>(counts), vbuf);
                        Telemetry::log("print", msg);
                    } else if (std::strcmp(argv[1], "filtered") == 0) {
                        const uint16_t counts = adc.readFilteredCounts(0);
                        const float v = adc.readFilteredVoltage(0);
                        char vbuf[16];
                        fmtFloat3(vbuf, sizeof(vbuf), v);
                        char msg[80];
                        std::snprintf(msg, sizeof(msg), "[SHELL] MAX filtered ch1 = 0x%03X (%lu counts), %s V",
                                      counts, static_cast<unsigned long>(counts), vbuf);
                        Telemetry::log("print", msg);
                    } else {
                        Telemetry::log("print", "[SHELL] usage: maxcfg ov <V> | uv <V> | thresholds | status | filterclear | raw | filtered");
                    }
                }
                else if (std::strcmp(argv[0], "help") == 0) {
                    Telemetry::log("print", "[SHELL] Commands: start f m | stop | freq f | mod m | status | clearfault | fault list/clear/test | cal | raw | vzero | maxcfg ov/uv/thresholds/status/filterclear/raw/filtered | ocset amps | hwocset amps | supply status | polepairs | encodercal start/stop | calpolepairs | rescal start [uv|uw|vw] <bus_pct> [max_a] | rescal status | help");
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
