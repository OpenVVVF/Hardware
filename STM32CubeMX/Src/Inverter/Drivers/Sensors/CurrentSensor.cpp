#include "Inverter/Drivers/Sensors/CurrentSensor.h"

#include <cstring>

namespace Inverter {

void CurrentSensor::init(const CurrentSensorConfig& cfg) {
    cfg_ = cfg;

    /* If the caller left reference.hadc as nullptr, use the output ADC twice. */
    if (cfg_.reference.hadc == nullptr) {
        cfg_.reference.hadc = cfg_.output.hadc;
    }
}

bool CurrentSensor::configureChannel(ADC_HandleTypeDef* hadc, uint32_t channel) {
    if (hadc == nullptr) {
        return false;
    }

    /* ADC must be stopped before reconfiguring a regular channel. */
    HAL_ADC_Stop(hadc);

    ADC_ChannelConfTypeDef sConfig = {};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    sConfig.OffsetSignedSaturation = DISABLE;
    sConfig.OffsetSign = ADC3_OFFSET_SIGN_NEGATIVE;

    /* ADC3 has its own set of sampling-time constants on H72/H73. */
    if (hadc->Instance == ADC3) {
        sConfig.SamplingTime = ADC3_SAMPLETIME_24CYCLES_5;
    } else {
        sConfig.SamplingTime = ADC_SAMPLETIME_32CYCLES_5;
    }

    return HAL_ADC_ConfigChannel(hadc, &sConfig) == HAL_OK;
}

uint32_t CurrentSensor::readSingleAdc(ADC_HandleTypeDef* hadc) {
    if (HAL_ADC_Start(hadc) != HAL_OK) {
        return 0;
    }
    HAL_ADC_PollForConversion(hadc, 10);
    const uint32_t value = HAL_ADC_GetValue(hadc);
    HAL_ADC_Stop(hadc);
    return value;
}

float CurrentSensor::rawToVoltage(uint32_t raw) const {
    const float lsb = cfg_.vref_mcu / static_cast<float>((1U << cfg_.adc_bits) - 1U);
    return static_cast<float>(raw) * lsb;
}

bool CurrentSensor::read(float& current_a, float& vout_v, float& vref_v) {
    const bool dual_mode = (cfg_.reference.hadc != nullptr) &&
                           (cfg_.reference.hadc != cfg_.output.hadc);

    uint32_t raw_out = 0;
    uint32_t raw_ref = 0;

    if (dual_mode) {
        /* Simultaneous sampling: configure both ADCs, start slave, then master. */
        if (!configureChannel(cfg_.output.hadc, cfg_.output.channel)) {
            return false;
        }
        if (!configureChannel(cfg_.reference.hadc, cfg_.reference.channel)) {
            return false;
        }

        /* Start the slave first, then the master triggers both conversions. */
        HAL_ADC_Start(cfg_.reference.hadc);
        if (HAL_ADC_Start(cfg_.output.hadc) != HAL_OK) {
            HAL_ADC_Stop(cfg_.reference.hadc);
            return false;
        }

        HAL_ADC_PollForConversion(cfg_.output.hadc, 10);
        HAL_ADC_PollForConversion(cfg_.reference.hadc, 10);

        raw_out = HAL_ADC_GetValue(cfg_.output.hadc);
        raw_ref = HAL_ADC_GetValue(cfg_.reference.hadc);

        HAL_ADC_Stop(cfg_.output.hadc);
        HAL_ADC_Stop(cfg_.reference.hadc);
    } else {
        /* Sequential sampling on the same ADC. */
        if (!configureChannel(cfg_.output.hadc, cfg_.output.channel)) {
            return false;
        }
        raw_out = readSingleAdc(cfg_.output.hadc);

        if (!configureChannel(cfg_.output.hadc, cfg_.reference.channel)) {
            return false;
        }
        raw_ref = readSingleAdc(cfg_.output.hadc);
    }

    vout_v = rawToVoltage(raw_out) / cfg_.voltage_divider;
    vref_v = rawToVoltage(raw_ref) / cfg_.voltage_divider;

    const float diff_v = vout_v - vref_v;
    current_a = diff_v / cfg_.sensitivity_va;

    return true;
}

} // namespace Inverter
