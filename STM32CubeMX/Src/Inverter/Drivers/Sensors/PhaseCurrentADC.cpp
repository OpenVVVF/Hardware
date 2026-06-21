#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"

#include "main.h"
#include "adc.h"
#include "tim.h"

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

    htim1.Instance->RCR = 1U;

    if (!calibrateOffsets()) {
        HAL_TIM_Base_Stop(&htim1);
        HAL_ADCEx_InjectedStop_IT(&hadc1);
        HAL_ADCEx_InjectedStop_IT(&hadc2);
        return false;
    }

    m_running = true;
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

    /* Restart the output filter so it begins from the calibrated zero point. */
    m_filter_sum_u = 0.0f;
    m_filter_sum_v = 0.0f;
    m_filter_idx = 0;
    m_filter_count = 0;
    for (size_t i = 0; i < FILTER_LEN; ++i) {
        m_filter_buf_u[i] = 0.0f;
        m_filter_buf_v[i] = 0.0f;
    }
    m_filtered_u = 0.0f;
    m_filtered_v = 0.0f;
    return true;
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

    updateFilter(m_iu - m_offset_u, m_iv - m_offset_v);
    m_new_data = true;
}

void PhaseCurrentADC::updateFilter(float raw_u, float raw_v) {
    if (m_filter_count < FILTER_LEN) {
        m_filter_buf_u[m_filter_count] = raw_u;
        m_filter_buf_v[m_filter_count] = raw_v;
        m_filter_sum_u += raw_u;
        m_filter_sum_v += raw_v;
        ++m_filter_count;
    } else {
        m_filter_sum_u -= m_filter_buf_u[m_filter_idx];
        m_filter_sum_v -= m_filter_buf_v[m_filter_idx];
        m_filter_buf_u[m_filter_idx] = raw_u;
        m_filter_buf_v[m_filter_idx] = raw_v;
        m_filter_sum_u += raw_u;
        m_filter_sum_v += raw_v;
        m_filter_idx = (m_filter_idx + 1) % FILTER_LEN;
    }

    m_filtered_u = m_filter_sum_u / static_cast<float>(m_filter_count);
    m_filtered_v = m_filter_sum_v / static_cast<float>(m_filter_count);
}

bool PhaseCurrentADC::sample(float& iu, float& iv, float& iw) {
    if (!m_new_data) {
        return false;
    }

    __disable_irq();
    iu = m_filtered_u;
    iv = m_filtered_v;
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
