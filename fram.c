/* =============================================================
 *  fram.c  —  MSP430FR2155 FRAM 영구 저장 드라이버
 *
 *  Rev History
 *  -----------
 *  REV 0.3  2026-04-26  Factory/User Cal 2-tier 구조 추가
 *  REV 0.2  2026-04-24  Initial
 * =============================================================*/

#include "fram.h"
#include <string.h>

#define FRAM_PTR    ((volatile FRAM_Data_t *)FRAM_BASE_ADDR)

static FRAM_Data_t g_fram;

/* ─── 내부: Write Protect ────────────────────────────────── */
static void FRAM_WriteEnable(void)
{
    SYSCFG0 = FRWPPW | PFWP;
}

static void FRAM_WriteDisable(void)
{
    SYSCFG0 = FRWPPW | DFWP | PFWP;
}

/* ─── 내부: 기본값 초기화 ────────────────────────────────── */
static void FRAM_SetDefaults(void)
{
    g_fram.magic         = FRAM_MAGIC;
    g_fram.slave_id      = FRAM_SLAVE_ID_DEFAULT;
    g_fram.reserved0     = 0x00u;
    g_fram.fzero_raw     = 0;
    g_fram.fspan_raw     = 0;
    g_fram.fspan_ppm_x10 = FRAM_SPAN_PPM_DEFAULT;
    g_fram.zero_raw      = 0;
    g_fram.span_raw      = 0;
    g_fram.span_ppm_x10  = FRAM_SPAN_PPM_DEFAULT;
    g_fram.cal_flags     = 0x00u;
    g_fram.reserved1     = 0x00u;
    g_fram.tiacn_val     = 0x000Cu;
    g_fram.crc16         = 0x0000u;
}

/* ═══════════════════════════════════════════════════════════
 *  CRC16/ARC (Modbus RTU 표준)
 * ═══════════════════════════════════════════════════════════*/
uint16_t FRAM_CRC16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i, j;

    for (i = 0u; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (j = 0u; j < 8u; j++) {
            if (crc & 0x0001u) {
                crc = (crc >> 1u) ^ 0xA001u;
            } else {
                crc >>= 1u;
            }
        }
    }
    return crc;
}

/* ═══════════════════════════════════════════════════════════
 *  공개 API
 * ═══════════════════════════════════════════════════════════*/

uint8_t FRAM_Load(void)
{
    uint16_t calc_crc;
    uint16_t stored_crc;

    memcpy(&g_fram, (const void *)FRAM_PTR, sizeof(FRAM_Data_t));

    if (g_fram.magic != FRAM_MAGIC) {
        FRAM_SetDefaults();
        FRAM_Save();
        return FRAM_ERR_INVALID;
    }

    calc_crc   = FRAM_CRC16((const uint8_t *)&g_fram,
                             sizeof(FRAM_Data_t) - sizeof(uint16_t));
    stored_crc = g_fram.crc16;

    if (calc_crc != stored_crc) {
        FRAM_SetDefaults();
        FRAM_Save();
        return FRAM_ERR_CRC;
    }

    if (g_fram.slave_id < FRAM_SLAVE_ID_MIN ||
        g_fram.slave_id > FRAM_SLAVE_ID_MAX) {
        g_fram.slave_id = FRAM_SLAVE_ID_DEFAULT;
        FRAM_Save();
    }

    return FRAM_OK;
}

uint8_t FRAM_Save(void)
{
    volatile FRAM_Data_t *p = FRAM_PTR;

    g_fram.crc16 = FRAM_CRC16((const uint8_t *)&g_fram,
                               sizeof(FRAM_Data_t) - sizeof(uint16_t));

    FRAM_WriteEnable();

    p->magic         = g_fram.magic;
    p->slave_id      = g_fram.slave_id;
    p->reserved0     = g_fram.reserved0;
    p->fzero_raw     = g_fram.fzero_raw;
    p->fspan_raw     = g_fram.fspan_raw;
    p->fspan_ppm_x10 = g_fram.fspan_ppm_x10;
    p->zero_raw      = g_fram.zero_raw;
    p->span_raw      = g_fram.span_raw;
    p->span_ppm_x10  = g_fram.span_ppm_x10;
    p->cal_flags     = g_fram.cal_flags;
    p->reserved1     = g_fram.reserved1;
    p->tiacn_val     = g_fram.tiacn_val;
    p->crc16         = g_fram.crc16;

    FRAM_WriteDisable();

    return FRAM_OK;
}

uint8_t FRAM_Reset(void)
{
    FRAM_SetDefaults();
    return FRAM_Save();
}

/* ═══════════════════════════════════════════════════════════
 *  교정 API
 * ═══════════════════════════════════════════════════════════*/

/**
 * Factory Zero 교정: fzero_raw 저장 + zero_raw 동기화
 */
void FRAM_SetFactoryZero(int16_t raw)
{
    g_fram.fzero_raw = raw;
    g_fram.zero_raw  = raw;   /* User zero도 동기화 */
    g_fram.cal_flags |= (FRAM_CAL_FZERO_DONE | FRAM_CAL_ZERO_DONE);
    FRAM_Save();
}

/**
 * Factory Span 교정: fspan 저장 + span 동기화
 */
void FRAM_SetFactorySpan(int16_t raw, uint16_t ppm_x10)
{
    g_fram.fspan_raw     = raw;
    g_fram.fspan_ppm_x10 = ppm_x10;
    g_fram.span_raw      = raw;       /* User span도 동기화 */
    g_fram.span_ppm_x10  = ppm_x10;
    g_fram.cal_flags |= (FRAM_CAL_FSPAN_DONE | FRAM_CAL_SPAN_DONE);
    FRAM_Save();
}

/**
 * User Zero 교정: zero_raw만 갱신 (Factory cal 유지)
 */
void FRAM_SetUserZero(int16_t raw)
{
    g_fram.zero_raw = raw;
    g_fram.cal_flags |= FRAM_CAL_ZERO_DONE;
    FRAM_Save();
}

/**
 * User Span 교정: span_raw/ppm 갱신 (Factory cal 유지)
 */
void FRAM_SetUserSpan(int16_t raw, uint16_t ppm_x10)
{
    g_fram.span_raw     = raw;
    g_fram.span_ppm_x10 = ppm_x10;
    g_fram.cal_flags |= FRAM_CAL_SPAN_DONE;
    FRAM_Save();
}

/**
 * RECAL: Factory cal → User cal 복원
 */
void FRAM_RecalFromFactory(void)
{
    g_fram.zero_raw     = g_fram.fzero_raw;
    g_fram.span_raw     = g_fram.fspan_raw;
    g_fram.span_ppm_x10 = g_fram.fspan_ppm_x10;

    if (g_fram.cal_flags & FRAM_CAL_FZERO_DONE) {
        g_fram.cal_flags |= FRAM_CAL_ZERO_DONE;
    } else {
        g_fram.cal_flags &= (uint8_t)(~FRAM_CAL_ZERO_DONE);
    }

    if (g_fram.cal_flags & FRAM_CAL_FSPAN_DONE) {
        g_fram.cal_flags |= FRAM_CAL_SPAN_DONE;
    } else {
        g_fram.cal_flags &= (uint8_t)(~FRAM_CAL_SPAN_DONE);
    }

    FRAM_Save();
}

/* ═══════════════════════════════════════════════════════════
 *  Getter
 * ═══════════════════════════════════════════════════════════*/

uint8_t FRAM_GetSlaveID(void)       { return g_fram.slave_id; }
int16_t FRAM_GetFactoryZeroRaw(void){ return g_fram.fzero_raw; }
int16_t FRAM_GetFactorySpanRaw(void){ return g_fram.fspan_raw; }
uint16_t FRAM_GetFactorySpanPpmX10(void){ return g_fram.fspan_ppm_x10; }
int16_t FRAM_GetZeroRaw(void)       { return g_fram.zero_raw; }
int16_t FRAM_GetSpanRaw(void)       { return g_fram.span_raw; }
uint16_t FRAM_GetSpanPpmX10(void)   { return g_fram.span_ppm_x10; }
uint8_t FRAM_GetCalFlags(void)      { return g_fram.cal_flags; }
uint16_t FRAM_GetTIACN(void)        { return g_fram.tiacn_val; }

const FRAM_Data_t* FRAM_GetData(void)
{
    return (const FRAM_Data_t *)&g_fram;
}

/* ─── Setter ─────────────────────────────────────────────── */

uint8_t FRAM_SetSlaveID(uint8_t id)
{
    if (id < FRAM_SLAVE_ID_MIN || id > FRAM_SLAVE_ID_MAX) {
        return FRAM_ERR_INVALID;
    }
    g_fram.slave_id = id;
    return FRAM_Save();
}

void FRAM_SetCalFlags(uint8_t flags)
{
    g_fram.cal_flags = flags;
    FRAM_Save();
}

void FRAM_SetTIACN(uint16_t val)
{
    g_fram.tiacn_val = val;
    FRAM_Save();
}
