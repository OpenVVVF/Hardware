#pragma once

#include <cmath>

namespace Inverter {

static constexpr float FOC_PI = 3.14159265358979323846f;
static constexpr float FOC_TWO_PI = 2.0f * FOC_PI;
static constexpr float FOC_INV_SQRT3 = 0.5773502691896258f;
static constexpr float FOC_SQRT3 = 1.7320508075688772f;

/**
 * @brief Amplitude-invariant forward Clarke transform (ABC -> Alpha/Beta).
 *
 * Matches the convention used in PicoFirmware's vector_transfs.cpp:
 *   ialpha = iu
 *   ibeta  = (iv - iw) / sqrt(3)
 */
inline void clarkeAbcToAlphaBeta(float iu, float iv, float iw,
                                  float& ialpha, float& ibeta) {
    (void)iw;
    ialpha = iu;
    ibeta = (iv - iw) * FOC_INV_SQRT3;
}

/**
 * @brief Forward Park transform (Alpha/Beta -> D/Q).
 */
inline void parkAlphaBetaToDq(float ialpha, float ibeta,
                               float sin_theta, float cos_theta,
                               float& id, float& iq) {
    id = ialpha * cos_theta + ibeta * sin_theta;
    iq = ibeta * cos_theta - ialpha * sin_theta;
}

/**
 * @brief Inverse Park transform (D/Q -> Alpha/Beta).
 */
inline void inverseParkDqToAlphaBeta(float vd, float vq,
                                      float sin_theta, float cos_theta,
                                      float& valpha, float& vbeta) {
    valpha = vd * cos_theta - vq * sin_theta;
    vbeta = vq * cos_theta + vd * sin_theta;
}

/**
 * @brief Inverse Clarke transform (Alpha/Beta -> ABC).
 */
inline void inverseClarkeAlphaBetaToAbc(float valpha, float vbeta,
                                         float& va, float& vb, float& vc) {
    va = valpha;
    vb = -0.5f * valpha + 0.5f * FOC_SQRT3 * vbeta;
    vc = -0.5f * valpha - 0.5f * FOC_SQRT3 * vbeta;
}

/**
 * @brief Wrap an angle to [0, 2*pi).
 */
inline float wrapAngle2Pi(float rad) {
    rad = fmodf(rad, FOC_TWO_PI);
    if (rad < 0.0f) {
        rad += FOC_TWO_PI;
    }
    return rad;
}

/**
 * @brief Wrap an angle to [-pi, pi).
 */
inline float wrapAnglePi(float rad) {
    rad = fmodf(rad + FOC_PI, FOC_TWO_PI);
    if (rad < 0.0f) {
        rad += FOC_TWO_PI;
    }
    return rad - FOC_PI;
}

} // namespace Inverter
