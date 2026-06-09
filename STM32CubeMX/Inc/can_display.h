/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can_display.h
  * @brief   Energica display CAN command abstraction layer
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __CAN_DISPLAY_H__
#define __CAN_DISPLAY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Exported types ------------------------------------------------------------*/

typedef enum {
    DISP_SCREEN_IDLE = 0,
    DISP_SCREEN_MENU,
    DISP_SCREEN_ARROW,
    DISP_SCREEN_RIDE,
    DISP_SCREEN_CHARGE,
} DisplayScreen;

typedef enum {
    DISP_MODE_SPORT = 0,
    DISP_MODE_ECO   = 1,
    DISP_MODE_URBAN = 2,
    DISP_MODE_RAIN  = 3,
} DisplayMode;

typedef struct {
    uint8_t  soc;               /* 0-100 */
    uint16_t range_miles_x100;  /* range in 0.01 mile units, 0-65535 */
    DisplayMode mode;
    uint8_t  batt_status;       /* 0-255 */
    uint8_t  temp;              /* 0-255 (default 0x1A) */
    uint32_t odo_miles;
    uint16_t speed_mph;
    uint32_t eff_wh_x100;       /* efficiency in 0.01 Wh/mi */

    DisplayScreen screen;
    uint8_t switch_countdown;   /* temporary screen switch countdown */
    DisplayScreen switch_screen;

    bool use_raw_10a;
    uint8_t raw_10a[8];

    bool use_raw_10b;
    uint8_t raw_10b[8];

    uint8_t raw_104_b4_b7[4];
} DisplayState;

/* Exported functions --------------------------------------------------------*/

void CAN_Display_Init(void);
void CAN_Display_GetDefaultState(DisplayState *state);

void CAN_Display_SendWakeup(void);
void CAN_Display_SendCycle(const DisplayState *state);
void CAN_Display_LogStatus(void);

/* Convenience setters (updates a state object) -----------------------------*/
void CAN_Display_SetSOC(DisplayState *state, uint8_t soc);
void CAN_Display_SetRange(DisplayState *state, uint16_t range_miles_x100);
void CAN_Display_SetMode(DisplayState *state, DisplayMode mode);
void CAN_Display_SetBatteryStatus(DisplayState *state, uint8_t status);
void CAN_Display_SetTemp(DisplayState *state, uint8_t temp);
void CAN_Display_SetOdo(DisplayState *state, uint32_t odo_miles);
void CAN_Display_SetSpeed(DisplayState *state, uint16_t speed_mph);
void CAN_Display_SetEfficiency(DisplayState *state, uint16_t eff_wh_x100);
void CAN_Display_SetScreen(DisplayState *state, DisplayScreen screen);
void CAN_Display_SwitchScreen(DisplayState *state, DisplayScreen screen);

/* Raw payload overrides -----------------------------------------------------*/
void CAN_Display_SetRaw10A(DisplayState *state, const uint8_t data[8]);
void CAN_Display_ClearRaw10A(DisplayState *state);
void CAN_Display_SetRaw10B(DisplayState *state, const uint8_t data[8]);
void CAN_Display_ClearRaw10B(DisplayState *state);
void CAN_Display_SetRaw104(DisplayState *state, const uint8_t b4, const uint8_t b5,
                           const uint8_t b6, const uint8_t b7);

#ifdef __cplusplus
}
#endif

#endif /* __CAN_DISPLAY_H__ */
