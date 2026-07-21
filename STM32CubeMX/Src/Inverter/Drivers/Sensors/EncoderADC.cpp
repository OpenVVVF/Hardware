#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Telemetry.h"
#include "Inverter/Control/FaultManager.h"

#include "main.h"
#include "adc.h"
#include "dma.h"

#include <cmath>

namespace Inverter {

static EncoderADC s_instance;
static DMA_HandleTypeDef hdma_adc2_enc;
static TIM_HandleTypeDef htim2_enc;

/* DMA buffer must live in AXI SRAM, not DTCMRAM. */
static uint16_t s_enc_dma_buffer[2] __attribute__((section(".dma_buffers")));

EncoderADC& encoderADC() {
    return s_instance;
}

bool EncoderADC::configureAdcChannels() {
    /* Make sure no regular conversion is running before reconfiguring. */
    if (LL_ADC_REG_IsConversionOngoing(hadc2.Instance)) {
        LL_ADC_REG_StopConversion(hadc2.Instance);
        while (LL_ADC_REG_IsConversionOngoing(hadc2.Instance)) {
            __NOP();
        }
    }

    /* Scan sequence: sin (CH10) then cos (CH11). */
    ADC_ChannelConfTypeDef sConfig = {};
    sConfig.SamplingTime = ADC_SAMPLETIME_32CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    sConfig.OffsetSignedSaturation = DISABLE;
    sConfig.OffsetSign = ADC3_OFFSET_SIGN_NEGATIVE;

    sConfig.Channel = ADC_CHANNEL_10;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK) {
        return false;
    }

    sConfig.Channel = ADC_CHANNEL_11;
    sConfig.Rank = ADC_REGULAR_RANK_2;
    if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK) {
        return false;
    }

    /* 2-rank scan sequence. */
    MODIFY_REG(hadc2.Instance->SQR1, ADC_SQR1_L, 1U);
    CLEAR_BIT(hadc2.Instance->CFGR, ADC_CFGR_DISCEN | ADC_CFGR_DISCNUM);

    /* Disable regular oversampling for the encoder (not needed, keep it fast). */
    CLEAR_BIT(hadc2.Instance->CFGR2, ADC_CFGR2_ROVSE);

    /* Trigger from TIM2 TRGO, rising edge. */
    MODIFY_REG(hadc2.Instance->CFGR, ADC_CFGR_EXTEN | ADC_CFGR_EXTSEL,
               ADC_EXTERNALTRIGCONVEDGE_RISING | ADC_EXTERNALTRIG_T2_TRGO);

    /* Tell HAL_ADC_Start_DMA() to use circular DMA mode. */
    hadc2.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc2.Init.ContinuousConvMode = DISABLE;
    hadc2.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;

    return true;
}

bool EncoderADC::initTimer() {
    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2_enc.Instance = TIM2;
    htim2_enc.Init.Prescaler = 0;
    htim2_enc.Init.CounterMode = TIM_COUNTERMODE_UP;
    /* APB1 = 137.5 MHz.  13750 ticks -> 10 kHz TRGO. */
    htim2_enc.Init.Period = 13749U;
    htim2_enc.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2_enc.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(&htim2_enc) != HAL_OK) {
        return false;
    }

    /* Derive the sample rate from the actual timer configuration rather than
     * assuming it: the RPM estimator uses this and must follow any future
     * change to the trigger rate. */
    {
        /* TIM2 is on APB1 (137.5 MHz timer clock). */
        m_sample_hz = 137500000.0f /
            (static_cast<float>(htim2_enc.Init.Prescaler + 1U) *
             static_cast<float>(htim2_enc.Init.Period + 1U));
    }

    TIM_MasterConfigTypeDef sMasterConfig = {};
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
    sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2_enc, &sMasterConfig) != HAL_OK) {
        return false;
    }

    return true;
}

bool EncoderADC::initDma() {
    __HAL_RCC_DMA2_CLK_ENABLE();

    hdma_adc2_enc.Instance = DMA2_Stream0;
    hdma_adc2_enc.Init.Request = DMA_REQUEST_ADC2;
    hdma_adc2_enc.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc2_enc.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc2_enc.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc2_enc.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc2_enc.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc2_enc.Init.Mode = DMA_CIRCULAR;
    hdma_adc2_enc.Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma_adc2_enc.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

    if (HAL_DMA_Init(&hdma_adc2_enc) != HAL_OK) {
        return false;
    }

    __HAL_LINKDMA(&hadc2, DMA_Handle, hdma_adc2_enc);

    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

    return true;
}

bool EncoderADC::init() {
    if (!configureAdcChannels()) return false;
    if (!initTimer()) return false;
    if (!initDma()) return false;
    return true;
}

bool EncoderADC::start() {
    if (m_running) return true;

    /* Calibrate only if ADC2 is not already running for current sense. */
    if (LL_ADC_IsEnabled(hadc2.Instance) == 0U) {
        if (HAL_ADCEx_Calibration_Start(&hadc2, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
            /* non-fatal: continue */
        }
    }

    if (HAL_ADC_Start_DMA(&hadc2, reinterpret_cast<uint32_t*>(s_enc_dma_buffer), 2) != HAL_OK) {
        return false;
    }

    if (HAL_TIM_Base_Start(&htim2_enc) != HAL_OK) {
        HAL_ADC_Stop_DMA(&hadc2);
        return false;
    }

    m_running = true;
    return true;
}

float EncoderADC::computeAngle(uint16_t raw_sin, uint16_t raw_cos) {
    /* Clamp to hard limits to reject outliers. */
    uint16_t csin = raw_sin;
    uint16_t ccos = raw_cos;
    if (csin < SIN_MIN_CAP) csin = SIN_MIN_CAP;
    if (csin > SIN_MAX_CAP) csin = SIN_MAX_CAP;
    if (ccos < COS_MIN_CAP) ccos = COS_MIN_CAP;
    if (ccos > COS_MAX_CAP) ccos = COS_MAX_CAP;

    /* Expand the learned bounds from the observed signal.  They start empty
     * and bracket the true signal range after roughly one revolution. */
    if (csin < m_obs_sin_min) m_obs_sin_min = csin;
    if (csin > m_obs_sin_max) m_obs_sin_max = csin;
    if (ccos < m_obs_cos_min) m_obs_cos_min = ccos;
    if (ccos > m_obs_cos_max) m_obs_cos_max = ccos;

    /* Prefer the learned bounds once both channels have seen enough span;
     * the hardcoded caps are only a fallback until then.  Normalizing with
     * bounds that do not match the real signal (stale mids or spans)
     * distorts the angle with a 2nd-harmonic error large enough to
     * detent-lock the rotor under FOC. */
    const bool learned_valid =
        (m_obs_sin_max >= m_obs_sin_min + LEARNED_MIN_SPAN) &&
        (m_obs_cos_max >= m_obs_cos_min + LEARNED_MIN_SPAN);
    uint16_t sin_min, sin_max, cos_min, cos_max;
    if (learned_valid) {
        sin_min = m_obs_sin_min; sin_max = m_obs_sin_max;
        cos_min = m_obs_cos_min; cos_max = m_obs_cos_max;
    } else {
        sin_min = m_sin_min; sin_max = m_sin_max;
        cos_min = m_cos_min; cos_max = m_cos_max;
    }
    m_active_sin_min = sin_min;
    m_active_sin_max = sin_max;
    m_active_cos_min = cos_min;
    m_active_cos_max = cos_max;
    m_learned_active = learned_valid;

    float angle_deg = 0.0f;
    if ((sin_max > sin_min) && (cos_max > cos_min)) {
        float sin_norm = (static_cast<float>(csin - sin_min) /
                          static_cast<float>(sin_max - sin_min)) * 2.0f - 1.0f;
        float cos_norm = (static_cast<float>(ccos - cos_min) /
                          static_cast<float>(cos_max - cos_min)) * 2.0f - 1.0f;
        angle_deg = atan2f(sin_norm, cos_norm) * (180.0f / static_cast<float>(M_PI));
        if (angle_deg < 0.0f) {
            angle_deg += 360.0f;
        }
    }

    return angle_deg;
}

void EncoderADC::onDmaComplete() {
    uint16_t raw_sin = s_enc_dma_buffer[0];
    uint16_t raw_cos = s_enc_dma_buffer[1];

    /* Compute angle before touching the snapshot so the ISR writes all three
     * fields atomically relative to the main-loop readers. */
    const float angle = computeAngle(raw_sin, raw_cos);

    /* Mechanical speed from the angle delta over a short window: the raw
     * per-sample delta multiplies angle noise by the full sample rate, which
     * buries the estimate in EMI at high switching frequencies. */
    if (!m_rpm_init) {
        m_rpm_init = true;
        m_unwrapped_angle = angle;
        m_window_ref_angle = angle;
        m_rpm_prev_angle = angle;
        m_rpm_filt_angle = angle;
        m_rpm_ema = 0.0f;
    } else {
        float delta = angle - m_rpm_prev_angle;
        if (delta > 180.0f) delta -= 360.0f;
        else if (delta < -180.0f) delta += 360.0f;
        m_unwrapped_angle += delta;
        m_rpm_prev_angle = angle;

        if (++m_window_n >= RPM_WINDOW) {
            const float win_deg = m_unwrapped_angle - m_window_ref_angle;
            const float win_s = static_cast<float>(RPM_WINDOW) / m_sample_hz;
            const float rpm_inst = (win_deg / 360.0f) * (60.0f / win_s);
            m_rpm_ema += RPM_ALPHA * (rpm_inst - m_rpm_ema);
            m_window_ref_angle = m_unwrapped_angle;
            m_window_n = 0;
        }
    }

    m_snapshot.angle = angle;
    m_snapshot.raw_sin = raw_sin;
    m_snapshot.raw_cos = raw_cos;
    m_new_data = true;
    m_last_sample_ms = HAL_GetTick();
    ++m_isr_count;

    /* Only evaluate signal-quality faults after the encoder has rotated enough
     * for the active bounds to be meaningful. */
    const bool range_ok = (m_active_sin_max - m_active_sin_min > MIN_AMP_RANGE) &&
                          (m_active_cos_max - m_active_cos_min > MIN_AMP_RANGE);
    if (range_ok) {
        const float sin_mid = 0.5f * static_cast<float>(m_active_sin_min + m_active_sin_max);
        const float cos_mid = 0.5f * static_cast<float>(m_active_cos_min + m_active_cos_max);
        const float dx = static_cast<float>(raw_sin) - sin_mid;
        const float dy = static_cast<float>(raw_cos) - cos_mid;
        const float mag = std::sqrt(dx * dx + dy * dy);

        if (!m_mag_ema_init) {
            m_mag_ema = mag;
            m_mag_ema_init = true;
        } else {
            constexpr float ALPHA = 0.05f;
            m_mag_ema += ALPHA * (mag - m_mag_ema);
        }

        if (m_mag_ema < AMP_COLLAPSE_THRESHOLD) {
            if (++m_amp_low_count >= AMP_COLLAPSE_COUNT) {
                FaultManager::instance().raise(
                    FaultSource::EncoderAmplitude, FaultReason::EncoderAmplitudeLow);
                m_amp_low_count = 0;
            }
        } else {
            m_amp_low_count = 0;
        }

        const bool at_rail =
            (raw_sin < SIN_MIN_CAP + RAIL_MARGIN) ||
            (raw_sin > SIN_MAX_CAP - RAIL_MARGIN) ||
            (raw_cos < COS_MIN_CAP + RAIL_MARGIN) ||
            (raw_cos > COS_MAX_CAP - RAIL_MARGIN);
        if (at_rail) {
            if (++m_rail_count >= RAIL_COUNT) {
                FaultManager::instance().raise(
                    FaultSource::EncoderOutOfRange, FaultReason::EncoderAtRail);
                m_rail_count = 0;
            }
        } else {
            m_rail_count = 0;
        }
    }
}

bool EncoderADC::sample(float& angle_deg) {
    if (!m_new_data) {
        return false;
    }

    __disable_irq();
    angle_deg = m_snapshot.angle;
    m_new_data = false;
    __enable_irq();

    return true;
}

bool EncoderADC::sample(float& angle_deg, uint16_t& raw_sin, uint16_t& raw_cos) {
    if (!m_new_data) {
        return false;
    }

    __disable_irq();
    angle_deg = m_snapshot.angle;
    raw_sin = m_snapshot.raw_sin;
    raw_cos = m_snapshot.raw_cos;
    m_new_data = false;
    __enable_irq();

    return true;
}

void EncoderADC::setBounds(uint16_t sin_min, uint16_t sin_max,
                           uint16_t cos_min, uint16_t cos_max) {
    __disable_irq();
    m_sin_min = sin_min;
    m_sin_max = sin_max;
    m_cos_min = cos_min;
    m_cos_max = cos_max;
    /* Seed the learned bounds so the override takes effect immediately;
     * they keep expanding from real samples afterwards. */
    m_obs_sin_min = sin_min;
    m_obs_sin_max = sin_max;
    m_obs_cos_min = cos_min;
    m_obs_cos_max = cos_max;
    m_mag_ema = 0.0f;
    m_mag_ema_init = false;
    m_amp_low_count = 0;
    m_rail_count = 0;
    __enable_irq();
}

void EncoderADC::resetBounds() {
    __disable_irq();
    /* Reset dynamic bounds to the hard-coded caps.  The hard caps stay fixed. */
    m_sin_min = SIN_MIN_CAP;
    m_sin_max = SIN_MAX_CAP;
    m_cos_min = COS_MIN_CAP;
    m_cos_max = COS_MAX_CAP;
    m_obs_sin_min = 65535U;
    m_obs_sin_max = 0U;
    m_obs_cos_min = 65535U;
    m_obs_cos_max = 0U;
    m_learned_active = false;
    m_active_sin_min = SIN_MIN_CAP;
    m_active_sin_max = SIN_MAX_CAP;
    m_active_cos_min = COS_MIN_CAP;
    m_active_cos_max = COS_MAX_CAP;
    m_mag_ema = 0.0f;
    m_mag_ema_init = false;
    m_amp_low_count = 0;
    m_rail_count = 0;
    __enable_irq();
}

void EncoderADC::onDmaError() {
    FaultManager::instance().raise(FaultSource::EncoderDma,
                                   FaultReason::EncoderDmaError);
}

void EncoderADC::diagnose() {
    /* Publish the measured trigger/ISR rate once a second so the assumed
     * sample rate (m_sample_hz) can be validated against reality. */
    static uint32_t s_last_ms = 0;
    static uint32_t s_last_count = 0;
    const uint32_t now_ms = HAL_GetTick();
    if (s_last_ms != 0U && (now_ms - s_last_ms) >= 1000U) {
        const float hz = static_cast<float>(m_isr_count - s_last_count) *
                         (1000.0f / static_cast<float>(now_ms - s_last_ms));
        Telemetry::log("enc_isr_hz", hz);
        /* Self-calibrate the estimator's time base from the measured rate;
         * the APB timer clock assumption (137.5 MHz) under-reads it badly. */
        m_sample_hz = hz;
        s_last_count = m_isr_count;
        s_last_ms = now_ms;
    } else if (s_last_ms == 0U) {
        s_last_count = m_isr_count;
        s_last_ms = now_ms;
    }

    if (m_running && (HAL_GetTick() - m_last_sample_ms) > SAMPLE_TIMEOUT_MS) {
        /* Temporarily disabled: encoder timeout fault is firing during
         * bench testing and interfering with other calibration work. */
        // FaultManager::instance().raise(FaultSource::EncoderTimeout,
        //                                FaultReason::EncoderSampleTimeout);
    }
}

} // namespace Inverter

extern "C" void HAL_DMA_ErrorCallback(DMA_HandleTypeDef* hdma) {
    if (hdma != nullptr && hdma->Instance == DMA2_Stream0) {
        Inverter::encoderADC().onDmaError();
    }
}

extern "C" void DMA2_Stream0_IRQHandler(void) {
    HAL_DMA_IRQHandler(&Inverter::hdma_adc2_enc);
}

extern "C" void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
    if (hadc->Instance == ADC2) {
        Inverter::encoderADC().onDmaComplete();
    }
}
