/* =============================================================
 *  fram.c  —  MSP430FR2155 FRAM 영구 저장 드라이버
 *
 *  [MSP430FR2155 FRAM 쓰기 방법]
 *  1. SYSCFG0 레지스터에서 DFWP(Data FRAM Write Protect) 해제
 *  2. 일반 메모리 쓰기처럼 포인터 직접 접근
 *  3. 쓰기 완료 후 DFWP 재설정 (보호 복원)
 *  ※ __noinit 또는 #pragma NOINIT 로 초기화 방지 가능하나
 *    여기서는 직접 주소 접근 방식 사용
 *
 *  Rev History
 *  -----------
 *  REV 0.2  2026-04-24  Initial
 * =============================================================*/

#include "fram.h"
#include <string.h>

/* ─── FRAM 물리 주소 포인터 ─────────────────────────────── */
/* volatile: 컴파일러 최적화로 인한 접근 생략 방지           */
#define FRAM_PTR    ((volatile FRAM_Data_t *)FRAM_BASE_ADDR)

/* ─── RAM 캐시 (부팅 후 작업용 복사본) ─────────────────── */
static FRAM_Data_t g_fram;

/* ═══════════════════════════════════════════════════════════
 *  내부 함수
 * ═══════════════════════════════════════════════════════════*/

/**
 * @brief  FRAM Write Protect 해제
 *         MSP430FR2155: SYSCFG0의 DFWP 비트로 FRAM 쓰기 제어
 */
static void FRAM_WriteEnable(void)
{
    SYSCFG0 = FRWPPW | PFWP;   /* Data FRAM 쓰기 허용, Program FRAM 보호 유지 */
}

/**
 * @brief  FRAM Write Protect 복원
 */
static void FRAM_WriteDisable(void)
{
    SYSCFG0 = FRWPPW | DFWP | PFWP;   /* Data + Program FRAM 모두 보호 */
}

/**
 * @brief  기본값으로 RAM 캐시 초기화 (FRAM 미저장)
 */
static void FRAM_SetDefaults(void)
{
    g_fram.magic        = FRAM_MAGIC;
    g_fram.slave_id     = FRAM_SLAVE_ID_DEFAULT;
    g_fram.reserved0    = 0x00u;
    g_fram.zero_raw     = 0;
    g_fram.span_raw     = 0;
    g_fram.span_ppm_x10 = FRAM_SPAN_PPM_DEFAULT;
    g_fram.cal_flags    = 0x00u;
    g_fram.reserved1    = 0x00u;
    g_fram.tiacn_val    = 0x000Cu;   /* 기본값: TIACN=0x0C (7kΩ, 10Ω) */
    g_fram.crc16        = 0x0000u;   /* Save() 호출 시 계산             */
}

/* ═══════════════════════════════════════════════════════════
 *  CRC16/ARC (Modbus RTU 표준)
 *  초기값 0xFFFF, 다항식 0x8005, 입력/출력 반전
 * ═══════════════════════════════════════════════════════════*/
uint16_t FRAM_CRC16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i, j;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (j = 0; j < 8u; j++) {
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

/**
 * @brief  부팅 시 FRAM 자동 복원
 */
uint8_t FRAM_Load(void)
{
    uint16_t calc_crc;
    uint16_t stored_crc;

    /* ── FRAM → RAM 캐시 복사 ── */
    memcpy(&g_fram, (const void *)FRAM_PTR, sizeof(FRAM_Data_t));

    /* ── Magic Number 확인 ── */
    if (g_fram.magic != FRAM_MAGIC) {
        /* 최초 부팅 또는 FRAM 미초기화 */
        FRAM_SetDefaults();
        FRAM_Save();
        return FRAM_ERR_INVALID;
    }

    /* ── CRC 검증 (crc16 필드 자신 제외하고 계산) ── */
    calc_crc   = FRAM_CRC16((const uint8_t *)&g_fram,
                             sizeof(FRAM_Data_t) - sizeof(uint16_t));
    stored_crc = g_fram.crc16;

    if (calc_crc != stored_crc) {
        /* CRC 불일치 → 기본값으로 초기화 */
        FRAM_SetDefaults();
        FRAM_Save();
        return FRAM_ERR_CRC;
    }

    /* ── 유효성 범위 검사 ── */
    if (g_fram.slave_id < FRAM_SLAVE_ID_MIN ||
        g_fram.slave_id > FRAM_SLAVE_ID_MAX) {
        g_fram.slave_id = FRAM_SLAVE_ID_DEFAULT;
        FRAM_Save();
    }

    return FRAM_OK;
}

/**
 * @brief  RAM 캐시 → FRAM 저장
 */
uint8_t FRAM_Save(void)
{
    volatile FRAM_Data_t *p = FRAM_PTR;

    /* CRC 계산 (crc16 필드 제외) */
    g_fram.crc16 = FRAM_CRC16((const uint8_t *)&g_fram,
                               sizeof(FRAM_Data_t) - sizeof(uint16_t));

    /* FRAM 쓰기 허용 */
    FRAM_WriteEnable();

    /* 16bit 단위로 FRAM에 직접 기록 */
    p->magic        = g_fram.magic;
    p->slave_id     = g_fram.slave_id;
    p->reserved0    = g_fram.reserved0;
    p->zero_raw     = g_fram.zero_raw;
    p->span_raw     = g_fram.span_raw;
    p->span_ppm_x10 = g_fram.span_ppm_x10;
    p->cal_flags    = g_fram.cal_flags;
    p->reserved1    = g_fram.reserved1;
    p->tiacn_val    = g_fram.tiacn_val;
    p->crc16        = g_fram.crc16;

    /* FRAM 쓰기 보호 복원 */
    FRAM_WriteDisable();

    return FRAM_OK;
}

/**
 * @brief  기본값으로 초기화 후 FRAM 저장
 */
uint8_t FRAM_Reset(void)
{
    FRAM_SetDefaults();
    return FRAM_Save();
}

/* ─── Getter / Setter ────────────────────────────────────── */

uint8_t FRAM_GetSlaveID(void)
{
    return g_fram.slave_id;
}

uint8_t FRAM_SetSlaveID(uint8_t id)
{
    if (id < FRAM_SLAVE_ID_MIN || id > FRAM_SLAVE_ID_MAX) {
        return FRAM_ERR_INVALID;
    }
    g_fram.slave_id = id;
    return FRAM_Save();
}

int16_t FRAM_GetZeroRaw(void)
{
    return g_fram.zero_raw;
}

void FRAM_SetZeroRaw(int16_t raw)
{
    g_fram.zero_raw = raw;
    g_fram.cal_flags |= FRAM_CAL_ZERO_DONE;
    FRAM_Save();
}

int16_t FRAM_GetSpanRaw(void)
{
    return g_fram.span_raw;
}

void FRAM_SetSpanRaw(int16_t raw)
{
    g_fram.span_raw = raw;
    g_fram.cal_flags |= FRAM_CAL_SPAN_DONE;
    FRAM_Save();
}

uint16_t FRAM_GetSpanPpmX10(void)
{
    return g_fram.span_ppm_x10;
}

void FRAM_SetSpanPpmX10(uint16_t ppm_x10)
{
    g_fram.span_ppm_x10 = ppm_x10;
    FRAM_Save();
}

uint8_t FRAM_GetCalFlags(void)
{
    return g_fram.cal_flags;
}

void FRAM_SetCalFlags(uint8_t flags)
{
    g_fram.cal_flags = flags;
    FRAM_Save();
}

uint16_t FRAM_GetTIACN(void)
{
    return g_fram.tiacn_val;
}

void FRAM_SetTIACN(uint16_t val)
{
    g_fram.tiacn_val = val;
    FRAM_Save();
}

const FRAM_Data_t* FRAM_GetData(void)
{
    return (const FRAM_Data_t *)&g_fram;
}
