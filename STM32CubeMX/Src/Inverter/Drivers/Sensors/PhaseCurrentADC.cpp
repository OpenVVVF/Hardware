#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"

#include "main.h"
#include "adc.h"
#include "tim.h"
#include "dma.h"

namespace Inverter {

static PhaseCurrentADC s_instance;
static DMA_HandleTypeDef hdma_adc1;

/* DMA cannot read DTCMRAM on H7; place the ADC DMA buffer in AXI SRAM.
 * Scan-mode dual simultaneous sampling produces two 32-bit words per PWM period:
 *   word 0 = U_sig (ADC1) + U_ref (ADC2)
 *   word 1 = V_sig (ADC1) + V_ref (ADC2) */
static uint32_t s_adc_dma_buffer[2] __attribute__((section(".dma_buffers")));

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
     * Paired by rank with ADC1, so word 0 = U_sig + U_ref and word 1 = V_sig + V_ref,
     * both sampled simultaneously in dual mode. */
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

    /* Enable circular DMA requests from ADC1 (master).  The DMA source will be
     * the common data register once dual-mode packing is enabled below. */
    MODIFY_REG(hadc1.Instance->CFGR, ADC_CFGR_DMNGT, ADC_CONVERSIONDATA_DMA_CIRCULAR);

    /* Pack ADC1 result in the lower 16 bits and ADC2 result in the upper 16
     * bits of the common regular data register. */
    ADC_MultiModeTypeDef multimode = {};
    multimode.Mode = ADC_DUALMODE_REGSIMULT;
    multimode.DualModeData = ADC_DUALMODEDATAFORMAT_32_10_BITS;
    multimode.TwoSamplingDelay = ADC_TWOSAMPLINGDELAY_5CYCLES;
    if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK) {
        return false;
    }

    return true;
}

bool PhaseCurrentADC::initDma() {
    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_adc1.Instance = DMA1_Stream1;
    hdma_adc1.Init.Request = DMA_REQUEST_ADC1;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK) {
        return false;
    }

    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);

    return true;
}

bool PhaseCurrentADC::initTrigger() {
    /* TIM1 already runs the PWM.  Set its master-mode TRGO to update event. */
    MODIFY_REG(htim1.Instance->CR2, TIM_CR2_MMS, TIM_TRGO_UPDATE);

    /* ADC1 triggers on rising edge of TIM1 TRGO.  ADC2 follows via dual mode. */
    MODIFY_REG(hadc1.Instance->CFGR,
               ADC_CFGR_EXTEN | ADC_CFGR_EXTSEL,
               ADC_EXTERNALTRIGCONVEDGE_RISING | ADC_EXTERNALTRIG_T1_TRGO);

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

    if (HAL_ADCEx_MultiModeStart_DMA(&hadc1, s_adc_dma_buffer, 2) != HAL_OK) {
        return false;
    }

    /* Start the TIM1 counter so its TRGO events trigger the ADC.
     * This does not enable PWM outputs; it just runs the timer. */
    if (HAL_TIM_Base_Start(&htim1) != HAL_OK) {
        HAL_ADCEx_MultiModeStop_DMA(&hadc1);
        return false;
    }

    /* Sanity check: the TIM1 counter must be moving. */
    const uint32_t cnt_before = htim1.Instance->CNT;
    for (volatile uint32_t i = 0; i < 10000U; ++i) {
        __NOP();
    }
    if (htim1.Instance->CNT == cnt_before) {
        HAL_TIM_Base_Stop(&htim1);
        HAL_ADCEx_MultiModeStop_DMA(&hadc1);
        return false;
    }

    /* In center-aligned mode the update event (TRGO) fires at both the overflow
     * and the underflow, so the ADC would sample at the top and the bottom of
     * the PWM triangle and the two sets of samples would get mixed together.
     * Set the repetition counter to 1 so only one update event is generated per
     * full PWM period.  Writing RCR after the counter has started aligns the
     * update event to the underflow (the quiet point where the low-side shunts
     * are conducting). */
    htim1.Instance->RCR = 1U;

    /* Measure and subtract the residual zero-current offset of each phase.
     * This removes board-level divider mismatch (notably the ~5 A offset on U). */
    if (!calibrateOffsets()) {
        HAL_TIM_Base_Stop(&htim1);
        HAL_ADCEx_MultiModeStop_DMA(&hadc1);
        return false;
    }

    m_running = true;
    return true;
}

bool PhaseCurrentADC::stop() {
    if (!m_running) return true;
    HAL_TIM_Base_Stop(&htim1);
    HAL_ADCEx_MultiModeStop_DMA(&hadc1);
    m_running = false;
    return true;
}

float PhaseCurrentADC::countsToCurrent(uint32_t sig, uint32_t ref) const {
    const float lsb   = ADC_VREF / static_cast<float>((1U << ADC_BITS) - 1U);
    const float scale = lsb / (DIVIDER * SENSITIVITY_VA);
    return (static_cast<float>(sig) - static_cast<float>(ref)) * scale;
}

bool PhaseCurrentADC::calibrateOffsets() {
    constexpr uint32_t DISCARD_SAMPLES = 50;
    constexpr uint32_t AVG_SAMPLES     = 500;

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
    return true;
}

void PhaseCurrentADC::onDmaHalfComplete() {
    /* Word 0: U signal + U reference, sampled simultaneously. */
    const uint32_t word = s_adc_dma_buffer[0];
    m_raw_u_sig = word & 0xFFFFU;
    m_raw_u_ref = (word >> 16) & 0xFFFFU;
    m_iu = countsToCurrent(m_raw_u_sig, m_raw_u_ref);
}

void PhaseCurrentADC::onDmaComplete() {
    /* Word 1: V signal + V reference, sampled simultaneously. */
    const uint32_t word = s_adc_dma_buffer[1];
    m_raw_v_sig = word & 0xFFFFU;
    m_raw_v_ref = (word >> 16) & 0xFFFFU;
    m_iv = countsToCurrent(m_raw_v_sig, m_raw_v_ref);
    m_new_data = true;
}

bool PhaseCurrentADC::sample(float& iu, float& iv, float& iw) {
    if (!m_new_data) {
        return false;
    }

    iu = m_iu - m_offset_u;
    iv = m_iv - m_offset_v;
    iw = -(iu + iv);

    m_new_data = false;
    return true;
}

} // namespace Inverter

extern "C" void DMA1_Stream1_IRQHandler(void) {
    HAL_DMA_IRQHandler(&Inverter::hdma_adc1);
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
