#pragma once

#include <cstdint>
#include <cstddef>

namespace Inverter {

/**
 * @brief PWM-synchronous phase-current ADC for FOC.
 *
 * Uses ADC1+ADC2 in dual-mode injected-simultaneous conversion, triggered by
 * TIM1 TRGO and read from the injected data registers in the ADC ISR.
 *
 * ADC1 injected sequence: U signal (CH4) -> V signal (CH3)
 * ADC2 injected sequence: U reference (CH8) -> V reference (CH7)
 *
 * The two ADCs sample simultaneously rank-by-rank, so U signal/ref and V
 * signal/ref are captured together (true differential measurement).
 * W current is computed as -(iu + iv).
 *
 * ADC2's regular group remains free for other uses (e.g. the encoder DMA).
 */
class PhaseCurrentADC {
public:
    PhaseCurrentADC() = default;

    /**
     * @brief Initialize hardware: ADC injected channels, TIM1 TRGO, ADC IRQ.
     *
     * Must be called after MX_ADC1_Init(), MX_ADC2_Init(), MX_TIM1_Init()
     * and MX_DMA_Init() have run.
     */
    bool init();

    /**
     * @brief Start timer-triggered injected conversions and run a one-shot
     * zero-current offset calibration.  The motor must be at standstill with
     * no phase current.
     */
    bool start();

    /**
     * @brief Stop conversions.
     */
    bool stop();

    /**
     * @brief Re-run the zero-current offset calibration.
     *
     * Only safe when the motor is stopped and no phase current is flowing.
     * Returns false if the ADC is not running.
     */
    bool recalibrateOffsets();

    /**
     * @brief Convert the latest raw samples to amperes.
     *
     * @param[out] iu  Phase U current in A.
     * @param[out] iv  Phase V current in A.
     * @param[out] iw  Phase W current in A (computed).
     * @return true if a new sample pair was available since the last call.
     */
    bool sample(float& iu, float& iv, float& iw);

    /**
     * @brief Called from the ADC ISR when an injected sequence completes.
     */
    void onInjectedConversionComplete();

    /**
     * @brief Diagnostic read-back of the latest raw ADC counts.
     */
    uint32_t lastRawUSig() const { return m_raw_u_sig; }
    uint32_t lastRawVSig() const { return m_raw_v_sig; }
    uint32_t lastRawURef() const { return m_raw_u_ref; }
    uint32_t lastRawVRef() const { return m_raw_v_ref; }
    float    lastOffsetU() const { return m_offset_u; }
    float    lastOffsetV() const { return m_offset_v; }

    /**
     * @brief Latest synchronous current samples (after offset subtraction).
     *
     * These are the same values a future FOC loop would consume: one sample
     * taken at the PWM bottom for each PWM period, not a time-averaged value.
     */
    float    lastU() const { return m_current_u; }
    float    lastV() const { return m_current_v; }

    /**
     * @brief Set the phase-current overcurrent threshold [A].
     *
     * Default is very high (effectively disabled).  The check is applied to
     * the absolute value of U and V currents in the ADC ISR.
     */
    void setOvercurrentThreshold(float amps) { m_oc_threshold_a = amps; }
    float overcurrentThreshold() const { return m_oc_threshold_a; }

    /**
     * @brief Set the hardware ADC analog-watchdog overcurrent threshold [A].
     *
     * A value of 0 disables the watchdog.  The watchdog window is centered on
     * the mid-scale ADC code and watches both injected channels on ADC1.
     * Reconfiguration requires the ADC to be stopped, so this must be called
     * while the motor is not running.
     * @return true on success, false if the ADC could not be reconfigured.
     */
    bool setHardwareOvercurrentThreshold(float amps);
    float hardwareOvercurrentThreshold() const { return m_hw_oc_threshold_a; }

    /**
     * @brief Configure the ADC analog watchdog from the stored threshold.
     */
    bool configureAnalogWatchdog();

private:
    bool configureAdcChannels();
    bool initTrigger();
    bool calibrateOffsets();
    float countsToCurrent(uint32_t sig, uint32_t ref) const;

    static constexpr uint32_t ADC_BITS        = 16;
    static constexpr float    ADC_VREF        = 3.3f;
    static constexpr float    DIVIDER         = 2.0f / 3.0f;
    static constexpr float    SENSITIVITY_VA  = 1.042e-3f; /**< LA37S600. */

    volatile uint32_t m_raw_u_sig = 0;
    volatile uint32_t m_raw_v_sig = 0;
    volatile uint32_t m_raw_u_ref = 0;
    volatile uint32_t m_raw_v_ref = 0;
    volatile float    m_iu = 0.0f;
    volatile float    m_iv = 0.0f;
    float             m_offset_u = 0.0f;
    float             m_offset_v = 0.0f;

    /* Latest synchronous current after offset subtraction.  No additional
     * time-domain averaging is applied so this is exactly what a FOC loop
     * running at the PWM sample rate would see. */
    volatile float    m_current_u = 0.0f;
    volatile float    m_current_v = 0.0f;

    float             m_oc_threshold_a = 1000.0f; /**< default: effectively disabled */
    float             m_hw_oc_threshold_a = 0.0f; /**< 0 = ADC watchdog disabled */

    volatile bool     m_new_data = false;
    bool              m_running = false;
};

/**
 * @brief Global instance used by the ADC ISR.
 */
PhaseCurrentADC& phaseCurrentADC();

} // namespace Inverter
