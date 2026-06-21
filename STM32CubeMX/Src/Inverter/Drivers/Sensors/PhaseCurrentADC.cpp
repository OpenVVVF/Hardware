#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"

#include "main.h"
#include "adc.h"
#include "tim.h"
#include "dma.h"

namespace Inverter {

static PhaseCurrentADC s_instance;
static DMA_HandleTypeDef hdma_adc1;
static DMA_HandleTypeDef hdma_adc2;

/* DMA cannot read DTCMRAM on H7; place the ADC DMA buffers in AXI SRAM.
 * Each TIM1 trigger starts a 2-channel scan on ADC1 and ADC2.  The two ADCs
 * are triggered from the same TIM1_TRGO edge, and each ADC has its own DMA
 * stream reading its own DR.  This avoids the STM32H7 dual-mode CDR packing
 * mis-alignment that was causing the reference channels to drift by ~20 counts.
 *
 *   ADC1 buffer: [U_sig, V_sig]
 *   ADC2 buffer: [U_ref, V_ref]
 */
static uint16_t s_adc1_dma_buffer[2] __attribute__((section(".dma_buffers")));
static uint16_t s_adc2_dma_buffer[2] __attribute__((section(".dma_buffers")));

PhaseCurrentADC& phaseCurrentADC() {
    return s_instance;
}

bool PhaseCurrentADC::configureAdcChannels() {
    HAL_ADC_Stop(&hadc1);
    HAL_ADC_Stop(&hadc2);

    ADC_ChannelConfTypeDef sConfig = {};
    sConfig.SamplingTime = ADC_SAMPLETIME_32CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    sConfig.OffsetSignedSaturation = DISABLE;
    sConfig.OffsetSign = ADC3_OFFSET_SIGN_NEGATIVE;

    /* ADC1 sequence: U current signal (CH4), then V current signal (CH3). */
    sConfig.Channel = ADC_CHANNEL_4;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
        return false;
    }
    sConfig.Channel = ADC_CHANNEL_3;
    sConfig.Rank = ADC_REGULAR_RANK_2;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
        return false;
    }

    /* ADC2 sequence: U current reference (CH8), then V current reference (CH7).
     * Ranks are paired with ADC1 so index 0 = U and index 1 = V. */
    sConfig.Channel = ADC_CHANNEL_8;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK) {
        return false;
    }
    sConfig.Channel = ADC_CHANNEL_7;
    sConfig.Rank = ADC_REGULAR_RANK_2;
    if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK) {
        return false;
    }

    /* Enable 2-rank scan sequences (discontinuous mode disabled). */
    MODIFY_REG(hadc1.Instance->SQR1, ADC_SQR1_L, 1U);
    MODIFY_REG(hadc2.Instance->SQR1, ADC_SQR1_L, 1U);
    CLEAR_BIT(hadc1.Instance->CFGR, ADC_CFGR_DISCEN | ADC_CFGR_DISCNUM);
    CLEAR_BIT(hadc2.Instance->CFGR, ADC_CFGR_DISCEN | ADC_CFGR_DISCNUM);

    /* Fix CubeMX oversampling: 16x average instead of 16x sum. */
    MODIFY_REG(hadc1.Instance->CFGR2, ADC_CFGR2_OVSS, ADC_RIGHTBITSHIFT_4);
    MODIFY_REG(hadc2.Instance->CFGR2, ADC_CFGR2_OVSS, ADC_RIGHTBITSHIFT_4);

    /* Use circular DMA for each ADC's own data register.  HAL_ADC_Start_DMA()
     * writes this value into the DMNGT bits. */
    hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;
    hadc2.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;

    /* Run ADC1 and ADC2 independently.  Both still trigger from the same
     * TIM1_TRGO edge, so the signal/reference pairs are sampled together,
     * but each ADC result is read from its own DR instead of the packed CDR. */
    ADC_MultiModeTypeDef multimode = {};
    multimode.Mode = ADC_MODE_INDEPENDENT;
    if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK) {
        return false;
    }

    /* Route both ADCs to the same TIM1 trigger source. */
    MODIFY_REG(hadc1.Instance->CFGR, ADC_CFGR_EXTEN | ADC_CFGR_EXTSEL,
               ADC_EXTERNALTRIGCONVEDGE_RISING | ADC_EXTERNALTRIG_T1_TRGO);
    MODIFY_REG(hadc2.Instance->CFGR, ADC_CFGR_EXTEN | ADC_CFGR_EXTSEL,
               ADC_EXTERNALTRIGCONVEDGE_RISING | ADC_EXTERNALTRIG_T1_TRGO);

    return true;
}

static bool initSingleDma(DMA_HandleTypeDef* hdma, DMA_Stream_TypeDef* instance,
                          uint32_t request, IRQn_Type irqn) {
    hdma->Instance = instance;
    hdma->Init.Request = request;
    hdma->Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma->Init.PeriphInc = DMA_PINC_DISABLE;
    hdma->Init.MemInc = DMA_MINC_ENABLE;
    hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma->Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma->Init.Mode = DMA_CIRCULAR;
    hdma->Init.Priority = DMA_PRIORITY_HIGH;
    hdma->Init.FIFOMode = DMA_FIFOMODE_DISABLE;

    if (HAL_DMA_Init(hdma) != HAL_OK) {
        return false;
    }

    HAL_NVIC_SetPriority(irqn, 5, 0);
    HAL_NVIC_EnableIRQ(irqn);

    return true;
}

bool PhaseCurrentADC::initDma() {
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    if (!initSingleDma(&hdma_adc1, DMA1_Stream1, DMA_REQUEST_ADC1,
                       DMA1_Stream1_IRQn)) {
        return false;
    }
    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

    if (!initSingleDma(&hdma_adc2, DMA2_Stream0, DMA_REQUEST_ADC2,
                       DMA2_Stream0_IRQn)) {
        return false;
    }
    __HAL_LINKDMA(&hadc2, DMA_Handle, hdma_adc2);

    return true;
}

bool PhaseCurrentADC::initTrigger() {
    /* Use TIM1 channel 4 to generate a narrow pulse around the bottom of the
     * center-aligned PWM triangle.  This gives a deterministic, once-per-period
     * ADC trigger at the quiet point where the low-side shunts conduct.
     *
     * TIM1_CH4 is not routed to a pin (CC4E stays 0); only the internal OC4REF
     * signal is used.  PWM mode 1 with CCR4 = PULSE_TICKS makes OC4REF high
     * while CNT < PULSE_TICKS, so there is a single rising edge just before the
     * counter underflows (CNT = 0). */
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

    /* Route OC4REF to TRGO.  Both ADCs trigger on the rising edge of TRGO. */
    MODIFY_REG(htim1.Instance->CR2, TIM_CR2_MMS, TIM_TRGO_OC4REF);

    return true;
}

bool PhaseCurrentADC::init() {
    if (!configureAdcChannels()) return false;
    if (!initDma()) return false;
    if (!initTrigger()) return false;
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

    if (HAL_ADC_Start_DMA(&hadc1, reinterpret_cast<uint32_t*>(s_adc1_dma_buffer), 2) != HAL_OK) {
        return false;
    }
    if (HAL_ADC_Start_DMA(&hadc2, reinterpret_cast<uint32_t*>(s_adc2_dma_buffer), 2) != HAL_OK) {
        HAL_ADC_Stop_DMA(&hadc1);
        return false;
    }

    /* Start the TIM1 counter so its TRGO events trigger the ADCs.
     * This does not enable PWM outputs; it just runs the timer. */
    if (HAL_TIM_Base_Start(&htim1) != HAL_OK) {
        HAL_ADC_Stop_DMA(&hadc1);
        HAL_ADC_Stop_DMA(&hadc2);
        return false;
    }

    /* Sanity check: the TIM1 counter must be moving. */
    const uint32_t cnt_before = htim1.Instance->CNT;
    for (volatile uint32_t i = 0; i < 10000U; ++i) {
        __NOP();
    }
    if (htim1.Instance->CNT == cnt_before) {
        HAL_TIM_Base_Stop(&htim1);
        HAL_ADC_Stop_DMA(&hadc1);
        HAL_ADC_Stop_DMA(&hadc2);
        return false;
    }

    /* In center-aligned mode the TIM1 update event fires at both the overflow
     * and the underflow.  The ADC trigger now comes from OC4REF near the bottom
     * of the triangle, so the ADC sampling point is deterministic.  We still set
     * RCR=1 so the TIM1 update interrupt (used by SPWM) runs only once per PWM
     * period; writing RCR after the counter starts aligns that update to the
     * underflow. */
    htim1.Instance->RCR = 1U;

    /* Measure and subtract the residual zero-current offset of each phase.
     * This removes board-level divider mismatch. */
    if (!calibrateOffsets()) {
        HAL_TIM_Base_Stop(&htim1);
        HAL_ADC_Stop_DMA(&hadc1);
        HAL_ADC_Stop_DMA(&hadc2);
        return false;
    }

    m_running = true;
    return true;
}

bool PhaseCurrentADC::stop() {
    if (!m_running) return true;
    HAL_TIM_Base_Stop(&htim1);
    HAL_ADC_Stop_DMA(&hadc1);
    HAL_ADC_Stop_DMA(&hadc2);
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

    /* Let the RCR=1 preload take effect and skip the first few samples in case
     * they were taken before the trigger aligned to the PWM underflow. */
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

void PhaseCurrentADC::onDmaHalfComplete() {
    /* Index 0: U signal and U reference, captured on the same trigger. */
    m_raw_u_sig = s_adc1_dma_buffer[0];
    m_raw_u_ref = s_adc2_dma_buffer[0];
    m_iu = countsToCurrent(m_raw_u_sig, m_raw_u_ref);
}

void PhaseCurrentADC::onDmaComplete() {
    /* Index 1: V signal and V reference, captured on the same trigger. */
    m_raw_v_sig = s_adc1_dma_buffer[1];
    m_raw_v_ref = s_adc2_dma_buffer[1];
    m_iv = countsToCurrent(m_raw_v_sig, m_raw_v_ref);

    /* Apply offset correction and update the output noise filter. */
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

    /* Read the filtered results atomically w.r.t. the DMA ISR. */
    __disable_irq();
    iu = m_filtered_u;
    iv = m_filtered_v;
    m_new_data = false;
    __enable_irq();

    iw = -(iu + iv);
    return true;
}

} // namespace Inverter

extern "C" void DMA1_Stream1_IRQHandler(void) {
    HAL_DMA_IRQHandler(&Inverter::hdma_adc1);
}

extern "C" void DMA2_Stream0_IRQHandler(void) {
    HAL_DMA_IRQHandler(&Inverter::hdma_adc2);
}

extern "C" void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc) {
    if (hadc->Instance == ADC1) {
        Inverter::phaseCurrentADC().onDmaHalfComplete();
    }
}

extern "C" void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
    if (hadc->Instance == ADC1) {
        Inverter::phaseCurrentADC().onDmaComplete();
    }
}
