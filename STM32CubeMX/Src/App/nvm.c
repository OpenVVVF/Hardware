/**
  ******************************************************************************
  * @file    nvm.c
  * @brief   F-RAM-backed non-volatile memory manager.
  ******************************************************************************
  */

#include "nvm.h"
#include <string.h>
#include <stdio.h>

static struct {
    CY15B102Q_HandleTypeDef *fram;
    NVM_SystemHeader_t header;
    uint32_t last_persisted_total_ms;
    uint32_t last_persist_time_ms;
    bool initialised;
} s_nvm;

/* CRC16-CCITT (initial 0xFFFF, polynomial 0x1021). */
static uint16_t NVM_Crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000U) {
                crc = (crc << 1) ^ 0x1021U;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static uint16_t NVM_SystemHeaderCrc(const NVM_SystemHeader_t *hdr)
{
    return NVM_Crc16((const uint8_t *)hdr, sizeof(*hdr) - sizeof(hdr->crc) - sizeof(hdr->reserved));
}

static uint16_t NVM_MotorParamsCrc(const NVM_MotorParams_t *params)
{
    return NVM_Crc16((const uint8_t *)params, sizeof(*params) - sizeof(params->crc) - sizeof(params->reserved));
}

static bool NVM_LoadSystemHeader(void)
{
    NVM_SystemHeader_t hdr;
    CY15B102Q_Read(s_nvm.fram, NVM_ADDR_SYSTEM_HEADER, (uint8_t *)&hdr, sizeof(hdr));

    if (hdr.magic == NVM_SYSTEM_MAGIC) {
        uint16_t crc = NVM_SystemHeaderCrc(&hdr);
        if (crc == hdr.crc) {
            s_nvm.header = hdr;
            return true;
        }
    }

    /* Try legacy 12-byte layout (magic + total_ms + boot_count, no CRC). */
    uint8_t legacy[12];
    CY15B102Q_Read(s_nvm.fram, NVM_ADDR_SYSTEM_HEADER, legacy, sizeof(legacy));
    uint32_t legacy_magic;
    memcpy(&legacy_magic, &legacy[0], sizeof(legacy_magic));
    if (legacy_magic == NVM_SYSTEM_MAGIC) {
        memcpy(&s_nvm.header.total_ms, &legacy[4], sizeof(s_nvm.header.total_ms));
        memcpy(&s_nvm.header.boot_count, &legacy[8], sizeof(s_nvm.header.boot_count));
        s_nvm.header.magic = NVM_SYSTEM_MAGIC;
        s_nvm.header.crc = NVM_SystemHeaderCrc(&s_nvm.header);
        return true;
    }

    return false;
}

static void NVM_SaveSystemHeader(void)
{
    s_nvm.header.crc = NVM_SystemHeaderCrc(&s_nvm.header);
    CY15B102Q_Write(s_nvm.fram, NVM_ADDR_SYSTEM_HEADER, (const uint8_t *)&s_nvm.header, sizeof(s_nvm.header));
    s_nvm.last_persisted_total_ms = s_nvm.header.total_ms;
}

bool NVM_Init(CY15B102Q_HandleTypeDef *fram)
{
    s_nvm.fram = fram;
    s_nvm.initialised = true;

    memset(&s_nvm.header, 0, sizeof(s_nvm.header));
    s_nvm.last_persisted_total_ms = 0;
    s_nvm.last_persist_time_ms = 0;

    bool valid = NVM_LoadSystemHeader();
    if (!valid) {
        s_nvm.header.magic = NVM_SYSTEM_MAGIC;
        s_nvm.header.total_ms = 0;
        s_nvm.header.boot_count = 0;
    }

    s_nvm.header.boot_count++;
    NVM_SaveSystemHeader();
    return valid;
}

void NVM_Update(CY15B102Q_HandleTypeDef *fram, uint32_t elapsed_ms)
{
    (void)fram; /* s_nvm.fram is used internally */

    if (!s_nvm.initialised) {
        return;
    }

    s_nvm.header.total_ms += elapsed_ms;

    /* Persist once per second or if more than 1 s has accumulated since last save. */
    if ((s_nvm.header.total_ms - s_nvm.last_persisted_total_ms) >= 1000U) {
        NVM_SaveSystemHeader();
    }
}

uint32_t NVM_GetTotalMs(void)
{
    return s_nvm.header.total_ms;
}

uint32_t NVM_GetBootCount(void)
{
    return s_nvm.header.boot_count;
}

void NVM_Format(char *buf, size_t len)
{
    uint32_t total_ms = s_nvm.header.total_ms;
    uint32_t hours = total_ms / 3600000U;
    uint32_t minutes = (total_ms / 60000U) % 60U;
    uint32_t seconds = (total_ms / 1000U) % 60U;
    uint32_t ms = total_ms % 1000U;
    snprintf(buf, len, "%02lu:%02lu:%02lu.%03lu",
             (unsigned long)hours,
             (unsigned long)minutes,
             (unsigned long)seconds,
             (unsigned long)ms);
}

bool NVM_ReadMotorParams(CY15B102Q_HandleTypeDef *fram, NVM_MotorParams_t *params)
{
    NVM_MotorParams_t tmp;
    CY15B102Q_Read(fram, NVM_ADDR_MOTOR_PARAMS, (uint8_t *)&tmp, sizeof(tmp));

    uint16_t crc = NVM_MotorParamsCrc(&tmp);
    if (crc != tmp.crc) {
        return false;
    }

    *params = tmp;
    return true;
}

bool NVM_WriteMotorParams(CY15B102Q_HandleTypeDef *fram, const NVM_MotorParams_t *params)
{
    NVM_MotorParams_t tmp = *params;
    tmp.crc = NVM_MotorParamsCrc(&tmp);
    CY15B102Q_Write(fram, NVM_ADDR_MOTOR_PARAMS, (const uint8_t *)&tmp, sizeof(tmp));
    return true;
}
