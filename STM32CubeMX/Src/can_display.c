/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can_display.c
  * @brief   Energica display CAN command abstraction layer
  ******************************************************************************
  */
/* USER CODE END Header */

#include "can_display.h"
#include "fdcan.h"
#include "mcp2221a_driver.h"
#include <string.h>

/* Private defines -----------------------------------------------------------*/
#define CAN_ID_101  0x101U
#define CAN_ID_102  0x102U
#define CAN_ID_104  0x104U
#define CAN_ID_109  0x109U
#define CAN_ID_10A  0x10AU
#define CAN_ID_10B  0x10BU
#define CAN_ID_200  0x200U

/* Screen lookup tables ------------------------------------------------------*/
static const uint8_t screen_prefix[5][2] = {
    [DISP_SCREEN_IDLE]   = {0x21, 0x14},
    [DISP_SCREEN_MENU]   = {0x3E, 0x3C},
    [DISP_SCREEN_ARROW]  = {0x21, 0x0F},
    [DISP_SCREEN_RIDE]   = {0x16, 0x14},
    [DISP_SCREEN_CHARGE] = {0x65, 0x64},
};

static const uint8_t screen_102[5][8] = {
    [DISP_SCREEN_IDLE]   = {0x00, 0x32, 0x00, 0x44, 0x1B, 0xFF, 0x17, 0x00},
    [DISP_SCREEN_MENU]   = {0x82, 0x30, 0x42, 0x44, 0xC1, 0xFF, 0x11, 0x00},
    [DISP_SCREEN_ARROW]  = {0x00, 0x32, 0x00, 0x44, 0x1B, 0xFF, 0x17, 0x00},
    [DISP_SCREEN_RIDE]   = {0x00, 0x20, 0x00, 0x44, 0x1B, 0xFF, 0x11, 0x00},
    [DISP_SCREEN_CHARGE] = {0x00, 0x32, 0x00, 0x44, 0x1B, 0xFF, 0x17, 0x00},
};

/* Private helpers -----------------------------------------------------------*/
static HAL_StatusTypeDef CAN_Display_SendFrame(uint32_t can_id, const uint8_t *data, uint8_t dlc)
{
    FDCAN_TxHeaderTypeDef header = {0};
    header.Identifier          = can_id;
    header.IdType              = FDCAN_STANDARD_ID;
    header.TxFrameType         = FDCAN_DATA_FRAME;
    header.DataLength          = dlc;
    header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    header.BitRateSwitch       = FDCAN_BRS_OFF;
    header.FDFormat            = FDCAN_CLASSIC_CAN;
    header.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    header.MessageMarker       = 0;

    HAL_StatusTypeDef status = HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan2, &header, (uint8_t *)data);

    if (status != HAL_OK)
    {
        MCP2221A_Printf("[CAN ERR] 0x%03lX HAL=%d FIFO=%lu\r\n",
                         can_id, (int)status,
                         (unsigned long)HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan2));
    }

    return status;
}

/* Documentation says the display needs at least 4 back-to-back identical
   transmissions of a frame ID before it registers the change. */
#define DISPLAY_FRAME_REPEAT  4U

static void CAN_Display_SendFrameRepeated(uint32_t can_id, const uint8_t *data, uint8_t dlc)
{
    for (uint8_t i = 0; i < DISPLAY_FRAME_REPEAT; i++)
    {
        /* Wait for TX FIFO space so we don't drop frames */
        while (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan2) == 0)
        {
            /* busy-wait; at 500 kbps a frame is ~260 us */
        }
        CAN_Display_SendFrame(can_id, data, dlc);
    }
}

void CAN_Display_LogStatus(void)
{
    FDCAN_ProtocolStatusTypeDef status;
    HAL_FDCAN_GetProtocolStatus(&hfdcan2, &status);

    uint32_t free = HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan2);
    uint32_t psr = hfdcan2.Instance->PSR;
    uint32_t ecr = hfdcan2.Instance->ECR;

    MCP2221A_Printf(
        "[CAN STAT] BusOff=%u ErrPass=%u Warn=%u TxFIFO=%lu "
        "TEC=%lu REC=%lu PSR=0x%02lX\r\n",
        (unsigned int)status.BusOff,
        (unsigned int)status.ErrorPassive,
        (unsigned int)status.Warning,
        (unsigned long)free,
        (unsigned long)((ecr >> 16) & 0xFF),
        (unsigned long)((ecr >> 8) & 0x7F),
        (unsigned long)(psr & 0xFF));
}

/* Exported functions --------------------------------------------------------*/

void CAN_Display_Init(void)
{
    /* FDCAN1 is already initialized and started in MX_FDCAN1_Init */
}

void CAN_Display_GetDefaultState(DisplayState *state)
{
    memset(state, 0, sizeof(DisplayState));
    state->soc          = 50;
    state->range_miles_x100 = 10000;  /* 100.00 mi */
    state->mode         = DISP_MODE_SPORT;
    state->batt_status  = 0;
    state->temp         = 0x1A;
    state->odo_miles    = 0;
    state->speed_mph    = 0;
    state->eff_wh_x100  = 128000;     /* 1280.00 Wh/mi */
    state->screen       = DISP_SCREEN_IDLE;
    state->switch_countdown = 0;
    state->switch_screen = DISP_SCREEN_IDLE;
    state->use_raw_10a  = false;
    state->use_raw_10b  = false;
    /* Default speed raw bytes for 0x104 */
    state->raw_104_b4_b7[0] = 0x03;
    state->raw_104_b4_b7[1] = 0x80;
    state->raw_104_b4_b7[2] = 0x01;
    state->raw_104_b4_b7[3] = 0x40;
}

void CAN_Display_SendWakeup(void)
{
    const uint8_t hb[8] = {0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    for (int i = 0; i < 4; i++)
    {
        CAN_Display_SendFrame(CAN_ID_101, hb, 8);
        HAL_Delay(10);
    }
}

void CAN_Display_SendCycle(const DisplayState *state)
{
    uint8_t payload[8];
    const uint8_t *prefix;
    const uint8_t *p102;

    /* Determine active screen (switch overrides main for 2 cycles) */
    if (state->switch_countdown > 0)
    {
        prefix = screen_prefix[state->switch_screen];
        p102   = screen_102[state->switch_screen];
    }
    else
    {
        prefix = screen_prefix[state->screen];
        p102   = screen_102[state->screen];
    }

    /* 0x101 - heartbeat / prefix  (×4 back-to-back) */
    payload[0] = prefix[0];
    payload[1] = prefix[1];
    payload[2] = 0x04;
    payload[3] = 0x00;
    payload[4] = 0x4B;
    payload[5] = 0x00;
    payload[6] = 0x00;
    payload[7] = 0x00;
    CAN_Display_SendFrameRepeated(CAN_ID_101, payload, 8);

    /* 0x102 - screen state  (×4 back-to-back) */
    CAN_Display_SendFrameRepeated(CAN_ID_102, p102, 8);

    /* 0x10A - SOC / range (or raw override)  (×4 back-to-back) */
    if (state->use_raw_10a)
    {
        CAN_Display_SendFrameRepeated(CAN_ID_10A, state->raw_10a, 8);
    }
    else
    {
        uint8_t soc_b = (state->soc > 100) ? 100 : state->soc;
        uint16_t range_units = state->range_miles_x100;
        if (range_units > 65535) range_units = 65535;
        payload[0] = 0x96;
        payload[1] = 0x20;
        payload[2] = soc_b;
        payload[3] = (uint8_t)(range_units & 0xFF);
        payload[4] = (uint8_t)((range_units >> 8) & 0xFF);
        payload[5] = 0x00;
        payload[6] = 0x00;
        payload[7] = 0x00;
        CAN_Display_SendFrameRepeated(CAN_ID_10A, payload, 8);
    }

    /* 0x109 - mode / battery status  (×4 back-to-back) */
    payload[0] = 0x01;
    payload[1] = 0x01;
    payload[2] = 0x00;
    payload[3] = 0x0F;
    payload[4] = 0xFF;
    payload[5] = 0xFF;
    payload[6] = (uint8_t)(state->mode & 0xFF);
    payload[7] = state->batt_status;
    CAN_Display_SendFrameRepeated(CAN_ID_109, payload, 8);

    /* 0x200 - temperature  (×4 back-to-back) */
    payload[0] = 0x12;
    payload[1] = 0x47;
    payload[2] = 0x64;
    payload[3] = state->temp;
    payload[4] = 0x0B;
    payload[5] = 0xB1;
    payload[6] = 0xFF;
    payload[7] = 0xF8;
    CAN_Display_SendFrameRepeated(CAN_ID_200, payload, 8);

    /* 0x104 - odometer / speed  (×4 back-to-back) */
    /* Convert miles to km tenths: ((miles + 0.5) / 0.621371) * 10 */
    uint64_t odo_km_tenths = (uint64_t)(((double)(state->odo_miles + 0.5) / 0.621371) * 10.0);
    payload[0] = (uint8_t)((odo_km_tenths >> 0) & 0xFF);
    payload[1] = (uint8_t)((odo_km_tenths >> 8) & 0xFF);
    payload[2] = (uint8_t)((odo_km_tenths >> 16) & 0xFF);
    payload[3] = (uint8_t)((odo_km_tenths >> 24) & 0xFF);
    payload[4] = state->raw_104_b4_b7[0];
    payload[5] = state->raw_104_b4_b7[1];
    payload[6] = state->raw_104_b4_b7[2];
    payload[7] = state->raw_104_b4_b7[3];
    CAN_Display_SendFrameRepeated(CAN_ID_104, payload, 8);

    /* 0x10B - efficiency (only if explicitly set, matching Python default)  (×4 back-to-back) */
    if (state->use_raw_10b)
    {
        CAN_Display_SendFrameRepeated(CAN_ID_10B, state->raw_10b, 8);
    }
}

/* Convenience setters -------------------------------------------------------*/

void CAN_Display_SetSOC(DisplayState *state, uint8_t soc)
{
    state->soc = (soc > 100) ? 100 : soc;
}

void CAN_Display_SetRange(DisplayState *state, uint16_t range_miles_x100)
{
    state->range_miles_x100 = range_miles_x100;
}

void CAN_Display_SetMode(DisplayState *state, DisplayMode mode)
{
    state->mode = mode;
}

void CAN_Display_SetBatteryStatus(DisplayState *state, uint8_t status)
{
    state->batt_status = status;
}

void CAN_Display_SetTemp(DisplayState *state, uint8_t temp)
{
    state->temp = temp;
}

void CAN_Display_SetOdo(DisplayState *state, uint32_t odo_miles)
{
    state->odo_miles = odo_miles;
}

void CAN_Display_SetSpeed(DisplayState *state, uint16_t speed_mph)
{
    state->speed_mph = speed_mph;
    if (speed_mph >= 199)
    {
        state->raw_104_b4_b7[0] = 0xFF;
        state->raw_104_b4_b7[1] = 0xFF;
        state->raw_104_b4_b7[2] = 0xFF;
        state->raw_104_b4_b7[3] = 0xFF;
    }
    else
    {
        uint16_t val = (uint16_t)(speed_mph * 8.2);
        if (val > 65534) val = 65534;
        state->raw_104_b4_b7[0] = 0x00;
        state->raw_104_b4_b7[1] = 0x00;
        state->raw_104_b4_b7[2] = (uint8_t)(val & 0xFF);
        state->raw_104_b4_b7[3] = (uint8_t)((val >> 8) & 0xFF);
    }
}

void CAN_Display_SetEfficiency(DisplayState *state, uint16_t eff_wh_x100)
{
    state->eff_wh_x100 = eff_wh_x100;
    uint16_t eff_raw = (uint16_t)((double)eff_wh_x100 / 16.0); /* /0.16 */
    if (eff_raw > 65535) eff_raw = 65535;
    state->raw_10b[0] = 0x7D;
    state->raw_10b[1] = 0x00;
    state->raw_10b[2] = (uint8_t)(eff_raw & 0xFF);
    state->raw_10b[3] = (uint8_t)((eff_raw >> 8) & 0xFF);
    state->raw_10b[4] = 0x00;
    state->raw_10b[5] = 0x7F;
    state->raw_10b[6] = 0xFF;
    state->raw_10b[7] = 0x7F;
    state->use_raw_10b = true;
}

void CAN_Display_SetScreen(DisplayState *state, DisplayScreen screen)
{
    state->screen = screen;
    state->switch_countdown = 0;
}

void CAN_Display_SwitchScreen(DisplayState *state, DisplayScreen screen)
{
    state->switch_screen = screen;
    state->switch_countdown = 2;
}

/* Raw payload overrides -----------------------------------------------------*/

void CAN_Display_SetRaw10A(DisplayState *state, const uint8_t data[8])
{
    memcpy(state->raw_10a, data, 8);
    state->use_raw_10a = true;
}

void CAN_Display_ClearRaw10A(DisplayState *state)
{
    state->use_raw_10a = false;
}

void CAN_Display_SetRaw10B(DisplayState *state, const uint8_t data[8])
{
    memcpy(state->raw_10b, data, 8);
    state->use_raw_10b = true;
}

void CAN_Display_ClearRaw10B(DisplayState *state)
{
    state->use_raw_10b = false;
}

void CAN_Display_SetRaw104(DisplayState *state, const uint8_t b4, const uint8_t b5,
                           const uint8_t b6, const uint8_t b7)
{
    state->raw_104_b4_b7[0] = b4;
    state->raw_104_b4_b7[1] = b5;
    state->raw_104_b4_b7[2] = b6;
    state->raw_104_b4_b7[3] = b7;
}
