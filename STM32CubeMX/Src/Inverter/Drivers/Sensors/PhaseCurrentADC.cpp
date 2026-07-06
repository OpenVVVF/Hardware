#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Drivers/Sensors/PoleEstimator.h"
#include "Inverter/Calibration/EncoderCycleCalibrator.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include "adc.h"
#include "tim.h"

#include <cstdio>
#include <cmath>

namespace Inverter {

static PhaseCurrentADC s_instance;

PhaseCurrentADC& phaseCurrentADC() {
    return s_instance;
}

static ADC_InjectionConfTypeDef makeInjectedConfig(uint32_t channel, uint32_t rank,
                                                    uint32_t trigger, uint32_t edge) {
    ADC_InjectionConfTypeDef cfg = {};
    cfg.InjectedChannel = channel;
    cfg.InjectedRank = rank;
    cfg.InjectedSamplingTime = ADC_SAMPLETIME_32CYCLES_5;
    cfg.InjectedSingleDiff = ADC_SINGLE_ENDED;
    cfg.InjectedOffsetNumber = ADC_OFFSET_NONE;
    cfg.InjectedOffset = 0;
    cfg.InjectedOffsetSignedSaturation = DISABLE;
    cfg.InjectedNbrOfConversion = 2;
    cfg.InjectedDiscontinuousConvMode = DISABLE;
    cfg.AutoInjectedConv = DISABLE;
    cfg.QueueInjectedContext = DISABLE;
    cfg.ExternalTrigInjecConv = trigger;
    cfg.ExternalTrigInjecConvEdge = edge;
    cfg.InjecOversamplingMode = ENABLE;
    cfg.InjecOversampling.Ratio = 16;
    cfg.InjecOversampling.RightBitShift = ADC_RIGHTBITSHIFT_4;
    return cfg;
}

bool PhaseCurrentADC::configureAdcChannels() {
    HAL_ADC_Stop(&hadc1);
    HAL_ADC_Stop(&hadc2);

    /* Scan mode must be enabled for the injected sequencer to use ranks 1 and 2. */
    hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc2.Init.ScanConvMode = ADC_SCAN_ENABLE;

    /* We want one interrupt per completed injected sequence (both ranks). */
    hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
    hadc2.Init.EOCSelection = ADC_EOC_SEQ_CONV;

    /* Disable injected context queue so software-trigger (slave) start works. */
    if (HAL_ADCEx_DisableInjectedQueue(&hadc1) != HAL_OK) {
        return false;
    }
    if (HAL_ADCEx_DisableInjectedQueue(&hadc2) != HAL_OK) {
        return false;
    }

    /* ADC1 injected master: U signal then V signal, triggered by TIM1_TRGO.
     * The reference channels are on ADC2 so each signal/reference pair is
     * sampled simultaneously (true differential measurement). */
    ADC_InjectionConfTypeDef inj1_r1 = makeInjectedConfig(
        ADC_CHANNEL_4, ADC_INJECTED_RANK_1,
        ADC_EXTERNALTRIGINJEC_T1_TRGO, ADC_EXTERNALTRIGINJECCONV_EDGE_RISING);
    ADC_InjectionConfTypeDef inj1_r2 = makeInjectedConfig(
        ADC_CHANNEL_3, ADC_INJECTED_RANK_2,
        ADC_EXTERNALTRIGINJEC_T1_TRGO, ADC_EXTERNALTRIGINJECCONV_EDGE_RISING);

    if (HAL_ADCEx_InjectedConfigChannel(&hadc1, &inj1_r1) != HAL_OK) {
        return false;
    }
    if (HAL_ADCEx_InjectedConfigChannel(&hadc1, &inj1_r2) != HAL_OK) {
        return false;
    }

    /* ADC2 injected slave: U reference then V reference, no external trigger.
     * It is hardware-slaved to ADC1 in injected-simultaneous mode. */
    ADC_InjectionConfTypeDef inj2_r1 = makeInjectedConfig(
        ADC_CHANNEL_8, ADC_INJECTED_RANK_1,
        ADC_INJECTED_SOFTWARE_START, ADC_EXTERNALTRIGINJECCONV_EDGE_NONE);
    ADC_InjectionConfTypeDef inj2_r2 = makeInjectedConfig(
        ADC_CHANNEL_7, ADC_INJECTED_RANK_2,
        ADC_INJECTED_SOFTWARE_START, ADC_EXTERNALTRIGINJECCONV_EDGE_NONE);

    if (HAL_ADCEx_InjectedConfigChannel(&hadc2, &inj2_r1) != HAL_OK) {
        return false;
    }
    if (HAL_ADCEx_InjectedConfigChannel(&hadc2, &inj2_r2) != HAL_OK) {
        return false;
    }

    /* Use injected-simultaneous dual mode only.  Regular groups remain independent,
     * so ADC2 regular can run the encoder DMA. */
    ADC_MultiModeTypeDef multimode = {};
    multimode.Mode = ADC_DUALMODE_INJECSIMULT;
    multimode.DualModeData = ADC_DUALMODEDATAFORMAT_DISABLED;
    multimode.TwoSamplingDelay = ADC_TWOSAMPLINGDELAY_5CYCLES;
    if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK) {
        return false;
    }

    return true;
}

bool PhaseCurrentADC::initTrigger() {
    /* Use TIM1 channel 4 to generate a narrow pulse around the bottom of the
     * center-aligned PWM triangle.  OC4REF is routed to TRGO and triggers the
     * ADC1 injected group.  ADC2 injected follows in dual mode. */
    static constexpr uint32_t PULSE_TICKS = 10U;

    TIM_OC_InitTypeDef sConfigOC = {};
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = PULSE_TICKS;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_OC_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_4) != HAL_OK) {
        return false;
    }

    MODIFY_REG(htim1.Instance->CR2, TIM_CR2_MMS, TIM_TRGO_OC4REF);
    return true;
}

bool PhaseCurrentADC::init() {
    if (!configureAdcChannels()) return false;
    if (!initTrigger()) return false;
    if (!configureAnalogWatchdog()) return false;

    HAL_NVIC_SetPriority(ADC_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(ADC_IRQn);

    return true;
}

bool PhaseCurrentADC::start() {
    if (m_running) return true;

    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        /* non-fatal: continue */
    }
    if (HAL_ADCEx_Calibration_Start(&hadc2, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        /* non-fatal: continue */
    }

    /* Start injected conversions: slave first, then master. */
    if (HAL_ADCEx_InjectedStart_IT(&hadc2) != HAL_OK) {
        return false;
    }
    if (HAL_ADCEx_InjectedStart_IT(&hadc1) != HAL_OK) {
        HAL_ADCEx_InjectedStop_IT(&hadc2);
        return false;
    }

    /* Start the TIM1 counter so its TRGO events trigger the injected group.
     * This does not enable PWM outputs; it just runs the timer. */
    if (HAL_TIM_Base_Start(&htim1) != HAL_OK) {
        HAL_ADCEx_InjectedStop_IT(&hadc1);
        HAL_ADCEx_InjectedStop_IT(&hadc2);
        return false;
    }

    /* Sanity check: the TIM1 counter must be moving. */
    const uint32_t cnt_before = htim1.Instance->CNT;
    for (volatile uint32_t i = 0; i < 10000U; ++i) {
        __NOP();
    }
    if (htim1.Instance->CNT == cnt_before) {
        HAL_TIM_Base_Stop(&htim1);
        HAL_ADCEx_InjectedStop_IT(&hadc1);
        HAL_ADCEx_InjectedStop_IT(&hadc2);
        return false;
    }

    if (!calibrateOffsets()) {
        HAL_TIM_Base_Stop(&htim1);
        HAL_ADCEx_InjectedStop_IT(&hadc1);
        HAL_ADCEx_InjectedStop_IT(&hadc2);
        return false;
    }

    m_running = true;
    Telemetry::printf("[CUR] start cal done U=%.3f V=%.3f",
                      static_cast<double>(m_offset_u),
                      static_cast<double>(m_offset_v));
    return true;
}

bool PhaseCurrentADC::stop() {
    if (!m_running) return true;
    HAL_TIM_Base_Stop(&htim1);
    HAL_ADCEx_InjectedStop_IT(&hadc1);
    HAL_ADCEx_InjectedStop_IT(&hadc2);
    m_running = false;
    return true;
}

bool PhaseCurrentADC::configureAnalogWatchdog() {
    if (m_hw_oc_threshold_a <= 0.0f) {
        /* Disabled: configure AWD in "none" mode to clear any previous window. */
        ADC_AnalogWDGConfTypeDef awd = {};
        awd.WatchdogNumber = ADC_ANALOGWATCHDOG_1;
        awd.WatchdogMode   = ADC_ANALOGWATCHDOG_NONE;
        awd.ITMode         = DISABLE;
        (void)HAL_ADC_AnalogWDGConfig(&hadc1, &awd);
        return true;
    }

    /* Compute the +/- raw ADC code corresponding to the requested current.
     * The LA37S600 sensor output is ratiometric around VREF/2 at 0 A. */
    constexpr float COUNTS_FULL = static_cast<float>((1U << ADC_BITS) - 1U);
    constexpr float COUNTS_PER_AMP = (DIVIDER * SENSITIVITY_VA * COUNTS_FULL) / ADC_VREF;
    constexpr float MID = COUNTS_FULL * 0.5f;

    const float delta = m_hw_oc_threshold_a * COUNTS_PER_AMP;
    float high_f = MID + delta;
    float low_f  = MID - delta;
    if (high_f > COUNTS_FULL) high_f = COUNTS_FULL;
    if (low_f  < 0.0f)       low_f  = 0.0f;

    ADC_AnalogWDGConfTypeDef awd = {};
    awd.WatchdogNumber = ADC_ANALOGWATCHDOG_1;
    awd.WatchdogMode   = ADC_ANALOGWATCHDOG_ALL_INJEC;
    awd.ITMode         = ENABLE;
    awd.HighThreshold  = static_cast<uint32_t>(high_f);
    awd.LowThreshold   = static_cast<uint32_t>(low_f);

    return HAL_ADC_AnalogWDGConfig(&hadc1, &awd) == HAL_OK;
}

bool PhaseCurrentADC::setHardwareOvercurrentThreshold(float amps) {
    if (amps < 0.0f) amps = 0.0f;

    /* The ADC watchdog is a safety-critical window: changing it while the
     * motor is running could create a glitch or a blind spot.  Require stop. */
    if (m_running) {
        return false;
    }

    m_hw_oc_threshold_a = amps;
    return configureAnalogWatchdog();
}

bool PhaseCurrentADC::recalibrateOffsets() {
    if (!m_running) {
        return false;
    }
    const bool ok = calibrateOffsets();
    if (ok) {
        Telemetry::printf("[CUR] recal done");
    }
    return ok;
}

float PhaseCurrentADC::countsToCurrent(uint32_t sig, uint32_t ref) const {
    const float lsb   = ADC_VREF / static_cast<float>((1U << ADC_BITS) - 1U);
    const float scale = lsb / (DIVIDER * SENSITIVITY_VA);
    return (static_cast<float>(sig) - static_cast<float>(ref)) * scale;
}

bool PhaseCurrentADC::calibrateOffsets() {
    constexpr uint32_t DISCARD_SAMPLES = 500;
    constexpr uint32_t AVG_SAMPLES     = 1000;

    m_new_data = false;
    for (uint32_t i = 0; i < DISCARD_SAMPLES; ++i) {
        while (!m_new_data) {
            __NOP();
        }
        m_new_data = false;
    }

    float sum_u = 0.0f;
    float sum_v = 0.0f;
    for (uint32_t i = 0; i < AVG_SAMPLES; ++i) {
        while (!m_new_data) {
            __NOP();
        }
        sum_u += m_iu;
        sum_v += m_iv;
        m_new_data = false;
    }

    m_offset_u = sum_u / static_cast<float>(AVG_SAMPLES);
    m_offset_v = sum_v / static_cast<float>(AVG_SAMPLES);

    /* Sanity-check the captured zero-current offset.  A residual this large
     * means the sensor/reference was still drifting during the capture. */
    constexpr float MAX_SANE_OFFSET_A = 50.0f;
    m_offset_valid = (std::fabs(m_offset_u) < MAX_SANE_OFFSET_A) &&
                     (std::fabs(m_offset_v) < MAX_SANE_OFFSET_A);
    if (!m_offset_valid) {
        Telemetry::printf("[CUR] WARNING: bad offset U=%.2f V=%.2f A; sensor not settled",
                          static_cast<double>(m_offset_u),
                          static_cast<double>(m_offset_v));
    }

    /* The synchronous output values are recomputed each ISR from (m_iu - offset),
     * so no separate filter state needs to be reset here. */
    return m_offset_valid;
}

void PhaseCurrentADC::onInjectedConversionComplete() {
    /* Read the simultaneously-sampled injected pairs.
     * ADC1 carries the signal channels, ADC2 carries the reference channels. */
    m_raw_u_sig = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_1);
    m_raw_v_sig = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_2);
    m_raw_u_ref = HAL_ADCEx_InjectedGetValue(&hadc2, ADC_INJECTED_RANK_1);
    m_raw_v_ref = HAL_ADCEx_InjectedGetValue(&hadc2, ADC_INJECTED_RANK_2);

    m_iu = countsToCurrent(m_raw_u_sig, m_raw_u_ref);
    m_iv = countsToCurrent(m_raw_v_sig, m_raw_v_ref);

    m_current_u = m_iu - m_offset_u;
    m_current_v = m_iv - m_offset_v;

    /* Software overcurrent protection.  Default threshold is very high so it
     * only trips when explicitly configured. */
    if (m_oc_threshold_a > 0.0f) {
        if (std::fabs(m_current_u) > m_oc_threshold_a ||
            std::fabs(m_current_v) > m_oc_threshold_a) {
            FaultManager::instance().raise(FaultSource::PhaseOvercurrent,
                                           FaultReason::PhaseOvercurrentSoftware);
        }
    }

    /* Feed the pole estimator at the ADC sample rate.  Pass raw encoder
     * sin/cos so the estimate does not depend on the encoder angle bounds. */
    PoleEstimator::instance().onSample(
        m_current_u, encoderADC().lastRawSin(), encoderADC().lastRawCos());

    /* Also feed the manual encoder-cycle calibrator. */
    EncoderCycleCalibrator::instance().onSample(
        encoderADC().lastRawSin(), encoderADC().lastRawCos());

    m_new_data = true;
}

bool PhaseCurrentADC::sample(float& iu, float& iv, float& iw) {
    if (!m_new_data) {
        return false;
    }

    __disable_irq();
    iu = m_current_u;
    iv = m_current_v;
    m_new_data = false;
    __enable_irq();

    iw = -(iu + iv);
    return true;
}

} // namespace Inverter

extern "C" void ADC_IRQHandler(void) {
    HAL_ADC_IRQHandler(&hadc1);
    HAL_ADC_IRQHandler(&hadc2);
}

extern "C" void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef* hadc) {
    if (hadc->Instance == ADC1) {
        Inverter::phaseCurrentADC().onInjectedConversionComplete();
    }
}

extern "C" void HAL_ADC_LevelOutOfWindowCallback(ADC_HandleTypeDef* hadc) {
    if (hadc != nullptr && hadc->Instance == ADC1) {
        Inverter::FaultManager::instance().raise(
            Inverter::FaultSource::AdcWatchdog,
            Inverter::FaultReason::AdcWatchdogTrip);
    }
}
