#include "Inverter/Control/CommandShell.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Calibration/PoleCalibrator.h"
#include "Inverter/Calibration/EncoderOffsetCalibrator.h"
#include "Inverter/Calibration/ResistanceCalibrator.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Drivers/Sensors/PoleEstimator.h"
#include "Inverter/Drivers/Logging/SupplyMonitor.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include "usart.h"
#include "pwm.h"
#include "gate_driver.h"

#include <cctype>
#include <cstring>
#include <cstdlib>

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
        Telemetry::printf("[SHELL] ERROR: HAL_UART_Receive_IT failed");
        return false;
    }

    Telemetry::printf("[SHELL] Commands: start f m | stop | freq f | mod m | status | clearfault | fault list/clear/test | cal | raw | vzero | maxcfg ov/uv/thresholds/status/filterclear/raw/filtered | ocset amps | hwocset amps | supply status | poles | encodercal start/stop | calpoles | encoffset start <poles> <enc_cycles> [breakaway_mod] | rescal start [uv|uw|vw] <bus_pct> [max_a] | rescal ictrl [uv|uw|vw] <current_a> [oc_limit_a] | rescal stop | rescal status | help");
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
                        Telemetry::printf("[SHELL] usage: start <freq_hz> <mod_idx>");
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
                        Telemetry::printf("[SHELL] usage: freq <freq_hz>");
                    } else {
                        float f = std::strtof(argv[1], nullptr);
                        ol.setFrequency(f);
                        Telemetry::printf("[SHELL] freq set to %.2f Hz", f);
                    }
                }
                else if (std::strcmp(argv[0], "mod") == 0) {
                    if (argc < 2) {
                        Telemetry::printf("[SHELL] usage: mod <mod_idx>");
                    } else {
                        float m = std::strtof(argv[1], nullptr);
                        ol.setModulationIndex(m);
                        Telemetry::printf("[SHELL] mod set to %.3f", m);
                    }
                }
                else if (std::strcmp(argv[0], "status") == 0) {
                    Telemetry::printf("[SHELL] run=%s f=%.2f m=%.3f",
                                      ol.isRunning() ? "Y" : "N",
                                      ol.frequencyHz(), ol.modulationIndex());
                    Telemetry::printf("[SHELL] ready=%s gd_fault=%s",
                                      GateDriver_IsReady() ? "Y" : "N",
                                      GateDriver_IsFault() ? "Y" : "N");
                    FaultManager::instance().printSummary();
                }
                else if (std::strcmp(argv[0], "clearfault") == 0) {
                    /* Re-enable gate-driver power so the board can be started again. */
                    GateDriver_EnablePower(true);
                    HAL_Delay(50);

                    /* Assert reset to guarantee the NCD57100 DESAT fault latch is
                     * cleared, then release it so /RDY and /FLT can be read. */
                    GateDriver_DisableOutputs();
                    HAL_Delay(10);
                    GateDriver_EnableOutputs();
                    HAL_Delay(10);

                    PWM_ClearBreakFlag();
                    PWM_ClearFault();
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

                    bool ready = GateDriver_IsReady();
                    bool fault = GateDriver_IsFault();
                    uint32_t bdtr = TIM1->BDTR;
                    Telemetry::printf("[SHELL] clearfault done | ready=%s fault=%s MOE=%lu",
                                      ready ? "Y" : "N",
                                      fault ? "Y" : "N",
                                      (bdtr >> 15) & 1UL);
                }
                else if (std::strcmp(argv[0], "cal") == 0) {
                    if (ol.isRunning()) {
                        Telemetry::printf("[SHELL] stop motor before calibrating");
                    } else if (phaseCurrentADC().recalibrateOffsets()) {
                        PhaseCurrentADC& adc = phaseCurrentADC();
                        Telemetry::printf("[SHELL] calibrated offsets U=%.3f V=%.3f",
                                          adc.lastOffsetU(), adc.lastOffsetV());
                    } else {
                        Telemetry::printf("[SHELL] calibration failed");
                    }
                }
                else if (std::strcmp(argv[0], "raw") == 0) {
                    PhaseCurrentADC& adc = phaseCurrentADC();
                    const int32_t u_diff = static_cast<int32_t>(adc.lastRawUSig()) -
                                           static_cast<int32_t>(adc.lastRawURef());
                    const int32_t v_diff = static_cast<int32_t>(adc.lastRawVSig()) -
                                           static_cast<int32_t>(adc.lastRawVRef());
                    Telemetry::printf("[SHELL] raw U sig=%lu ref=%lu diff=%ld",
                                      adc.lastRawUSig(), adc.lastRawURef(), u_diff);
                    Telemetry::printf("[SHELL] raw V sig=%lu ref=%lu diff=%ld",
                                      adc.lastRawVSig(), adc.lastRawVRef(), v_diff);

                    Telemetry::printf("[SHELL] offsets U=%.3f V=%.3f",
                                      adc.lastOffsetU(), adc.lastOffsetV());
                }
                else if (std::strcmp(argv[0], "vzero") == 0) {
                    DcLinkVoltageSensor& vdc = dcLinkVoltageSensor();
                    if (vdc.zeroCalibrate()) {
                        Telemetry::printf("[SHELL] Vdc zero calibrated: %.3f V", vdc.voltage());
                    } else {
                        Telemetry::printf("[SHELL] Vdc zero cal failed (no sample)");
                    }
                }
                else if (std::strcmp(argv[0], "poles") == 0) {
                    PoleEstimator& poles = PoleEstimator::instance();
                    Telemetry::printf("[SHELL] poles=%.3f mech=%.2f elec=%.2f",
                                      poles.estimate(), poles.mechanicalCycles(), poles.electricalCycles());
                }
                else if (std::strcmp(argv[0], "encodercal") == 0) {
                    PoleEstimator& poles = PoleEstimator::instance();
                    if (argc < 2) {
                        Telemetry::printf("[SHELL] usage: encodercal start | stop");
                    } else if (std::strcmp(argv[1], "start") == 0) {
                        poles.startManualEncoderCal();
                        Telemetry::printf("[SHELL] encoder cal started; rotate shaft exactly 1 rev, then 'encodercal stop'");
                    } else if (std::strcmp(argv[1], "stop") == 0) {
                        poles.stopManualEncoderCal();
                        const float cycles = poles.manualEncoderCycles();
                        Telemetry::printf("[SHELL] encoder cycles in 1 rev = %.2f; true poles = poles_estimate * %.2f",
                                          cycles, cycles);
                    } else {
                        Telemetry::printf("[SHELL] usage: encodercal start | stop");
                    }
                }
                else if (std::strcmp(argv[0], "calpoles") == 0) {
                    PoleCalibrator& cal = PoleCalibrator::instance();
                    if (ol.isRunning()) {
                        Telemetry::printf("[SHELL] stop the motor before starting calpoles");
                    } else if (cal.isActive()) {
                        Telemetry::printf("[SHELL] calibration already running");
                    } else {
                        cal.start();
                    }
                }
                else if (std::strcmp(argv[0], "encoffset") == 0) {
                    EncoderOffsetCalibrator& cal = EncoderOffsetCalibrator::instance();
                    if (ol.isRunning()) {
                        Telemetry::printf("[SHELL] stop the motor before starting encoffset");
                    } else if (cal.isActive()) {
                        Telemetry::printf("[SHELL] calibration already running");
                    } else if (argc < 2) {
                        Telemetry::printf("[SHELL] usage: encoffset start <poles> [breakaway_mod] | status");
                    } else if (std::strcmp(argv[1], "status") == 0) {
                        Telemetry::printf("[SHELL] encoffset: samples=%d avg=%.3f deg",
                                          cal.sampleCount(),
                                          static_cast<double>(cal.averageOffset()));
                    } else if (std::strcmp(argv[1], "start") == 0) {
                        if (argc < 4) {
                            Telemetry::printf("[SHELL] usage: encoffset start <poles> <enc_cycles> [breakaway_mod]");
                        } else {
                            const float poles = std::strtof(argv[2], nullptr);
                            const float enc_cycles = std::strtof(argv[3], nullptr);
                            const float breakaway_mod = (argc >= 5) ? std::strtof(argv[4], nullptr) : 0.0f;
                            cal.start(poles, enc_cycles, breakaway_mod);
                        }
                    } else {
                        Telemetry::printf("[SHELL] usage: encoffset start <poles> <enc_cycles> [breakaway_mod] | status");
                    }
                }
                else if (std::strcmp(argv[0], "rescal") == 0) {
                    if (argc < 2 || std::strcmp(argv[1], "status") == 0) {
                        ResistanceCalibrator& rc = ResistanceCalibrator::instance();
                        Telemetry::printf("[SHELL] rescal: R_uv=%.3f R_uw=%.3f R_vw=%.3f avg=%.3f ohm",
                                          rc.lastResult(ResistanceCalibrator::Pair::UV),
                                          rc.lastResult(ResistanceCalibrator::Pair::UW),
                                          rc.lastResult(ResistanceCalibrator::Pair::VW),
                                          rc.lastAverage());
                    } else if (std::strcmp(argv[1], "start") == 0) {
                        if (argc < 3) {
                            Telemetry::printf("[SHELL] usage: rescal start [uv|uw|vw] <bus_pct> [max_a]");
                        } else {
                            float bus_pct = 0.0f;
                            float max_a = 50.0f;
                            ResistanceCalibrator::Pair pair = ResistanceCalibrator::Pair::UV;
                            bool run_all = true;

                            if (std::strcmp(argv[2], "uv") == 0 ||
                                std::strcmp(argv[2], "uw") == 0 ||
                                std::strcmp(argv[2], "vw") == 0) {
                                if (argc < 4) {
                                    Telemetry::printf("[SHELL] usage: rescal start [uv|uw|vw] <bus_pct> [max_a]");
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

                            if (!resistanceCalibrator().start(bus_pct, pair, run_all, 15000U, max_a)) {
                                Telemetry::printf("[SHELL] rescal start failed");
                            }
                        }
                    } else if (std::strcmp(argv[1], "stop") == 0) {
                        resistanceCalibrator().stop();
                    } else if (std::strcmp(argv[1], "ictrl") == 0) {
                        if (argc < 3) {
                            Telemetry::printf("[SHELL] usage: rescal ictrl [uv|uw|vw] <current_a> [oc_limit_a]");
                        } else {
                            float current_a = 0.0f;
                            float oc_limit_a = 0.0f;
                            ResistanceCalibrator::Pair pair = ResistanceCalibrator::Pair::UV;
                            bool run_all = true;

                            if (std::strcmp(argv[2], "uv") == 0 ||
                                std::strcmp(argv[2], "uw") == 0 ||
                                std::strcmp(argv[2], "vw") == 0) {
                                if (argc < 4) {
                                    Telemetry::printf("[SHELL] usage: rescal ictrl [uv|uw|vw] <current_a> [oc_limit_a]");
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
                                current_a = std::strtof(argv[3], nullptr);
                                if (argc >= 5) {
                                    oc_limit_a = std::strtof(argv[4], nullptr);
                                }
                            } else {
                                current_a = std::strtof(argv[2], nullptr);
                                if (argc >= 4) {
                                    oc_limit_a = std::strtof(argv[3], nullptr);
                                }
                            }

                            if (!resistanceCalibrator().startCurrentCtrl(current_a, pair, run_all, 15000U, oc_limit_a)) {
                                Telemetry::printf("[SHELL] rescal ictrl failed");
                            }
                        }
                    } else {
                        Telemetry::printf("[SHELL] usage: rescal start [uv|uw|vw] <bus_pct> [max_a] | rescal ictrl [uv|uw|vw] <current_a> [oc_limit_a] | rescal stop | rescal status");
                    }
                }
                else if (std::strcmp(argv[0], "fault") == 0) {
                    if (argc < 2) {
                        Telemetry::printf("[SHELL] usage: fault list | clear | test <name>");
                    } else if (std::strcmp(argv[1], "list") == 0) {
                        FaultManager::instance().printSummary();
                    } else if (std::strcmp(argv[1], "clear") == 0) {
                        FaultManager::instance().clearAll();
                        Telemetry::printf("[SHELL] latched faults cleared");
                    } else if (std::strcmp(argv[1], "test") == 0) {
                        if (argc < 3) {
                            Telemetry::printf("[SHELL] usage: fault test <FaultName>");
                        } else {
                            FaultSource src = FaultManager::sourceFromName(argv[2]);
                            if (src == FaultSource::None) {
                                Telemetry::printf("[SHELL] unknown fault name; use 'fault list' to see names");
                            } else {
                                FaultManager::instance().testFault(src);
                                Telemetry::printf("[SHELL] injected test fault");
                            }
                        }
                    } else {
                        Telemetry::printf("[SHELL] usage: fault list | clear | test <name>");
                    }
                }
                else if (std::strcmp(argv[0], "ocset") == 0) {
                    if (argc < 2) {
                        Telemetry::printf("[SHELL] usage: ocset <amps>  (0 disables)");
                    } else {
                        float amps = std::strtof(argv[1], nullptr);
                        if (amps < 0.0f) amps = 0.0f;
                        phaseCurrentADC().setOvercurrentThreshold(amps);
                        Telemetry::printf("[SHELL] phase overcurrent threshold set to %.3f A", amps);
                    }
                }
                else if (std::strcmp(argv[0], "hwocset") == 0) {
                    if (argc < 2) {
                        Telemetry::printf("[SHELL] usage: hwocset <amps>  (0 disables ADC watchdog)");
                    } else {
                        float amps = std::strtof(argv[1], nullptr);
                        if (amps < 0.0f) amps = 0.0f;
                        if (phaseCurrentADC().setHardwareOvercurrentThreshold(amps)) {
                            Telemetry::printf("[SHELL] hardware overcurrent threshold set to %.3f A", amps);
                        } else {
                            Telemetry::printf("[SHELL] failed to set hardware overcurrent threshold (stop motor first)");
                        }
                    }
                }
                else if (std::strcmp(argv[0], "supply") == 0) {
                    if (argc < 2 || std::strcmp(argv[1], "status") != 0) {
                        Telemetry::printf("[SHELL] usage: supply status");
                    } else {
                        supplyMonitorPrintStatus();
                    }
                }
                else if (std::strcmp(argv[0], "maxcfg") == 0) {
                    DcLinkVoltageSensor& vdc = dcLinkVoltageSensor();
                    MAX22530& adc = vdc.adc();

                    if (argc < 2) {
                        Telemetry::printf("[SHELL] usage: maxcfg ov <V> | uv <V> | thresholds | status | filterclear | raw | filtered");
                    } else if (std::strcmp(argv[1], "ov") == 0) {
                        if (argc < 3) {
                            Telemetry::printf("[SHELL] usage: maxcfg ov <volts>");
                        } else {
                            float v = std::strtof(argv[2], nullptr);
                            if (vdc.setOvervoltageThreshold(v)) {
                                Telemetry::printf("[SHELL] Vbus OV threshold set to %.3f V", v);
                            } else {
                                Telemetry::printf("[SHELL] failed to set OV threshold");
                            }
                        }
                    } else if (std::strcmp(argv[1], "uv") == 0) {
                        if (argc < 3) {
                            Telemetry::printf("[SHELL] usage: maxcfg uv <volts>");
                        } else {
                            float v = std::strtof(argv[2], nullptr);
                            if (vdc.setUndervoltageThreshold(v)) {
                                Telemetry::printf("[SHELL] Vbus UV threshold set to %.3f V", v);
                            } else {
                                Telemetry::printf("[SHELL] failed to set UV threshold");
                            }
                        }
                    } else if (std::strcmp(argv[1], "status") == 0) {
                        uint16_t cout_status = 0;
                        (void)adc.getComparatorStatus(cout_status);
                        uint16_t couthi = 0, coutlo = 0;
                        (void)adc.readComparatorThreshold(0, couthi, coutlo);
                        Telemetry::printf("[SHELL] MAX int_status=0x%04X cout_status=0x%04X int_enable=0x%04X",
                                          adc.lastInterruptStatus(), cout_status, adc.lastInterruptEnable());
                        Telemetry::printf("[SHELL] MAX ch1 thresholds: HI=0x%03X LO=0x%03X",
                                          couthi, coutlo);
                        Telemetry::printf("[SHELL] MAX irq=%lu dma=%lu err=%lu fail=%lu crc_err=%lu",
                                          static_cast<unsigned long>(adc.irqCount()),
                                          static_cast<unsigned long>(adc.dmaCompleteCount()),
                                          static_cast<unsigned long>(adc.dmaErrorCount()),
                                          static_cast<unsigned long>(adc.dmaStartFailCount()),
                                          static_cast<unsigned long>(adc.crcErrorCount()));
                    } else if (std::strcmp(argv[1], "thresholds") == 0) {
                        Telemetry::printf("[SHELL] Vbus OV=%.3f V UV=%.3f V (scaled)",
                                          vdc.overvoltageThreshold(), vdc.undervoltageThreshold());
                    } else if (std::strcmp(argv[1], "filterclear") == 0) {
                        if (adc.clearFilter(0)) {
                            Telemetry::printf("[SHELL] MAX channel 1 filter cleared");
                        } else {
                            Telemetry::printf("[SHELL] MAX filter clear failed");
                        }
                    } else if (std::strcmp(argv[1], "raw") == 0) {
                        const uint16_t counts = adc.readRawCounts(0);
                        const float v = adc.readRawVoltage(0);
                        Telemetry::printf("[SHELL] MAX raw ch1 = 0x%03X (%lu counts), %.3f V",
                                          counts, static_cast<unsigned long>(counts), v);
                    } else if (std::strcmp(argv[1], "filtered") == 0) {
                        const uint16_t counts = adc.readFilteredCounts(0);
                        const float v = adc.readFilteredVoltage(0);
                        Telemetry::printf("[SHELL] MAX filtered ch1 = 0x%03X (%lu counts), %.3f V",
                                          counts, static_cast<unsigned long>(counts), v);
                    } else {
                        Telemetry::printf("[SHELL] usage: maxcfg ov <V> | uv <V> | thresholds | status | filterclear | raw | filtered");
                    }
                }
                else if (std::strcmp(argv[0], "help") == 0) {
                    Telemetry::printf("[SHELL] Commands: start f m | stop | freq f | mod m | status | clearfault | fault list/clear/test | cal | raw | vzero | maxcfg ov/uv/thresholds/status/filterclear/raw/filtered | ocset amps | hwocset amps | supply status | poles | encodercal start/stop | calpoles | encoffset start <poles> <enc_cycles> [breakaway_mod] | rescal start [uv|uw|vw] <bus_pct> [max_a] | rescal ictrl [uv|uw|vw] <current_a> [oc_limit_a] | rescal stop | rescal status | help");
                }
                else {
                    /* Unknown but printable command: tell the user instead of
                     * silently dropping, so typos are obvious. */
                    Telemetry::printf("[SHELL] unknown command");
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
