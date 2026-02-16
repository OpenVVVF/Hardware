/**
  ***********************************************************************************
  * @file    Modulation.h
  * @date    2026-02-15
  * @brief   SPWM/SVM modulation functions using vector_transfs.h.
  ***********************************************************************************
  */

  #pragma once

  #include "FOC.h"
  #include "space_vector_transfs/vector_transfs.h"
  

  // ur mom >:) I AM TIRED. PLEASE WORK.
  struct PhaseVoltages {

    float _Du_unitless; // float from 0-1
    float _Dv_unitless; // float from 0-1
    float _Dw_unitless; // float from 0-1

  };

  /**
   * @brief Generate SPWM duty cycles from FOC output
   * @param FocOut FOC output containing Valpha, Vbeta, Vdc
   * @param MaxModulation Max modulation index (e.g., 0.95)
   * @param Duties Output duty cycles [0.0, 1.0]
   */
  inline void GenerateSpwm(const FocOutput& FocOut, float MaxModulation, PhaseVoltages& Duties) {
      tIFClarke Iclarke = IF_CLARKE_DEFAULTS;
      
      // Inverse Clarke: Alpha/Beta -> ABC
      Iclarke.fAl = FocOut._Valpha_V;
      Iclarke.fBe = FocOut._Vbeta_V;
      Iclarke.m_albe2abc(&Iclarke);
      
      // Normalize to DC bus voltage
      float Vmax = FocOut._Vdc_V * MaxModulation;
      float Scale = (Vmax > 0.0f) ? (0.5f / Vmax) : 0.0f;
      
      // Center at 0.5 duty cycle
      Duties._Du_unitless = 0.5f + Iclarke.fA * Scale;
      Duties._Dv_unitless = 0.5f + Iclarke.fB * Scale;
      Duties._Dw_unitless = 0.5f + Iclarke.fC * Scale;
      
      // Clamp to [0, 1]
      if (Duties._Du_unitless < 0.0f) Duties._Du_unitless = 0.0f;
      if (Duties._Du_unitless > 1.0f) Duties._Du_unitless = 1.0f;
      if (Duties._Dv_unitless < 0.0f) Duties._Dv_unitless = 0.0f;
      if (Duties._Dv_unitless > 1.0f) Duties._Dv_unitless = 1.0f;
      if (Duties._Dw_unitless < 0.0f) Duties._Dw_unitless = 0.0f;
      if (Duties._Dw_unitless > 1.0f) Duties._Dw_unitless = 1.0f;
  }
  
  /**
   * @brief Placeholder for future SVM implementation
   * @param FocOut FOC output containing Valpha, Vbeta, Vdc, angle
   * @param MaxModulation Max modulation index
   * @param Duties Output duty cycles [0.0, 1.0]
   */
  inline void GenerateSvm(const FocOutput& FocOut, float MaxModulation, PhaseVoltages& Duties) {
      // TODO: Implement SVM using FocOut._Valpha_V, FocOut._Vbeta_V, FocOut._ElectricalAngle_Rad
      // For now, fall back to SPWM
      GenerateSpwm(FocOut, MaxModulation, Duties);
  }