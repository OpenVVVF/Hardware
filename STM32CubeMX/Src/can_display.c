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

static void CAN_Display_SendLogFrame(uint32_t can_id, const uint8_t *data)
{
    while (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan2) == 0)
    {
        /* wait for TX FIFO space */
    }
    CAN_Display_SendFrame(can_id, data, 8);
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

/* Replay the exact custom.csv log the user captured. */
void CAN_Display_SendWakeup(void)
{
    /* 0x101 × 4 */
    const uint8_t f1[8] = {0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    CAN_Display_SendLogFrame(0x101, f1);
    CAN_Display_SendLogFrame(0x101, f1);
    CAN_Display_SendLogFrame(0x101, f1);
    CAN_Display_SendLogFrame(0x101, f1);

    /* 0x631 × 9 */
    const uint8_t f631_1[8] = {0x01, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02};
    const uint8_t f631_2[8] = {0x02, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02};
    const uint8_t f631_3[8] = {0x05, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02};
    const uint8_t f631_4[8] = {0x03, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02};
    CAN_Display_SendLogFrame(0x631, f631_1);
    CAN_Display_SendLogFrame(0x631, f631_2);
    CAN_Display_SendLogFrame(0x631, f631_3);
    CAN_Display_SendLogFrame(0x631, f631_4);
    CAN_Display_SendLogFrame(0x631, f631_4);
    CAN_Display_SendLogFrame(0x631, f631_4);
    CAN_Display_SendLogFrame(0x631, f631_4);
    CAN_Display_SendLogFrame(0x631, f631_4);
    CAN_Display_SendLogFrame(0x631, f631_4);

    /* 0x10B × 4 */
    const uint8_t f10b_1[8] = {0x7D, 0x00, 0x40, 0x1F, 0xFF, 0x7F, 0xFF, 0x7F};
    const uint8_t f10b_2[8] = {0x7D, 0x00, 0x40, 0x1F, 0x00, 0x7F, 0xFF, 0x7F};
    CAN_Display_SendLogFrame(0x10B, f10b_1);
    CAN_Display_SendLogFrame(0x10B, f10b_1);
    CAN_Display_SendLogFrame(0x10B, f10b_1);
    CAN_Display_SendLogFrame(0x10B, f10b_2);

    /* 0x104 × 4 */
    const uint8_t f104_1[8] = {0xEC, 0x7A, 0x0C, 0x00, 0x03, 0x80, 0x01, 0x40};
    const uint8_t f104_2[8] = {0xFF, 0xFF, 0x0A, 0x00, 0x00, 0x00, 0xFF, 0xFF};
    CAN_Display_SendLogFrame(0x104, f104_1);
    CAN_Display_SendLogFrame(0x104, f104_1);
    CAN_Display_SendLogFrame(0x104, f104_1);
    CAN_Display_SendLogFrame(0x104, f104_2);

    /* 0x101 */
    const uint8_t f101_2[8] = {0x21, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    CAN_Display_SendLogFrame(0x101, f101_2);

    /* 0x102 */
    const uint8_t f102[8] = {0x82, 0x30, 0x42, 0x44, 0x00, 0xFF, 0x11, 0x00};
    CAN_Display_SendLogFrame(0x102, f102);
}

void CAN_Display_SendCycle(const DisplayState *state)
{
    (void)state;
    /* Nothing — we replay the exact log once in SendWakeup().
       If you want a repeating cycle, add frames here. */
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
