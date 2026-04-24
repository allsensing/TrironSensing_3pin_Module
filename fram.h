/* =============================================================
 *  fram.h  —  MSP430FR2155 FRAM 영구 저장 드라이버
 *  Target MCU : MSP430FR2155RSM
 *
 *  [MSP430FR2155 FRAM 메모리 맵]
 *  0xC000 ~ 0xFF7F : 코드/데이터 FRAM (일반 사용)
 *  0x1800 ~ 0x19FF : Information Memory FRAM (설정 저장 권장)
 *
 *  [저장 영역 선택]
 *  Information Memory (0x1800) 사용
 *  → 코드 업데이트 시에도 데이터 보존 가능
 *  → 512 bytes, 본 용도에 충분
 *
 *  [FRAM 레이아웃]
 *  Offset  Size  항목
 *  0x00     2    Magic Number (0xA55A: 초기화 완료 표시)
 *  0x02     1    Slave ID (1~64)
 *  0x03     1    예약
 *  0x04     2    Zero 교정 RAW (ADS1115 LSB)
 *  0x06     2    Span 교정 RAW (ADS1115 LSB)
 *  0x08     2    Span 교정 가스 농도 (ppm × 10, e.g. 250 = 25.0ppm)
 *  0x0A     1    교정 완료 플래그 (bit0=Zero완료, bit1=Span완료)
 *  0x0B     1    예약
 *  0x0C     2    TIA Gain 설정 (LMP91000 TIACN 레지스터값)
 *  0x0E     2    CRC16 (Magic~0x0D 전체)
 *  총 16 bytes
 *
 *  [CRC16]
 *  알고리즘: CRC-16/ARC (Modbus RTU 표준과 동일)
 *  범위: Magic Number ~ 설정값 끝 (CRC 자신 제외)
 *
 *  Rev History
 *  -----------
 *  REV 0.2  2026-04-24  Initial
 * =============================================================*/

#ifndef FRAM_H_
#define FRAM_H_

#include <msp430.h>
#include <stdint.h>

/* ─── FRAM 저장 기준 주소 ────────────────────────────────── */
/* Information Memory Segment A (0x1980~0x19FF) 사용         */
/* Segment B (0x1900~0x197F) 는 예비로 보존                  */
#define FRAM_BASE_ADDR          0x1980u

/* ─── Magic Number (초기화 완료 표시) ───────────────────── */
#define FRAM_MAGIC              0xA55Au

/* ─── Slave ID 범위 ──────────────────────────────────────── */
#define FRAM_SLAVE_ID_MIN       1u
#define FRAM_SLAVE_ID_MAX       64u
#define FRAM_SLAVE_ID_DEFAULT   1u

/* ─── 교정 플래그 비트 ───────────────────────────────────── */
#define FRAM_CAL_ZERO_DONE      (1u << 0)   /* Zero 교정 완료 */
#define FRAM_CAL_SPAN_DONE      (1u << 1)   /* Span 교정 완료 */

/* ─── Span 교정 가스 농도 기본값 ────────────────────────── */
#define FRAM_SPAN_PPM_DEFAULT   500u        /* 50.0 ppm (×10) */

/* ─── 오류 코드 ───────────────────────────────────────────── */
#define FRAM_OK                 (0u)
#define FRAM_ERR_CRC            (1u)   /* CRC 불일치 → 기본값 적용 */
#define FRAM_ERR_INVALID        (2u)   /* Magic 없음 → 미초기화    */

/* ─── FRAM 저장 구조체 ───────────────────────────────────── */
/*  __attribute__((aligned(2))) : FRAM 16bit 접근 정렬 보장   */
typedef struct {
    uint16_t magic;             /* 0x00: 0xA55A = 초기화 완료  */
    uint8_t  slave_id;          /* 0x02: Modbus Slave ID 1~64  */
    uint8_t  reserved0;         /* 0x03: 예약                  */
    int16_t  zero_raw;          /* 0x04: Zero 교정 ADC raw값   */
    int16_t  span_raw;          /* 0x06: Span 교정 ADC raw값   */
    uint16_t span_ppm_x10;      /* 0x08: Span 교정 가스농도×10 */
    uint8_t  cal_flags;         /* 0x0A: 교정 완료 플래그      */
    uint8_t  reserved1;         /* 0x0B: 예약                  */
    uint16_t tiacn_val;         /* 0x0C: LMP91000 TIACN 설정   */
    uint16_t crc16;             /* 0x0E: CRC16 검증값          */
} __attribute__((packed)) FRAM_Data_t;

/* ─── 공개 API ───────────────────────────────────────────── */

/**
 * @brief  부팅 시 FRAM 데이터 로드
 *         Magic + CRC 검증 → 유효하면 복원, 아니면 기본값 적용 후 저장
 * @return FRAM_OK        : 정상 복원
 *         FRAM_ERR_CRC   : CRC 불일치 → 기본값으로 초기화됨
 *         FRAM_ERR_INVALID: 최초 부팅 → 기본값으로 초기화됨
 */
uint8_t FRAM_Load(void);

/**
 * @brief  현재 RAM 설정값을 FRAM에 저장
 *         CRC 자동 계산 후 기록
 * @return FRAM_OK
 */
uint8_t FRAM_Save(void);

/**
 * @brief  FRAM 데이터를 기본값으로 초기화 후 저장
 */
uint8_t FRAM_Reset(void);

/* ─── Getter / Setter ────────────────────────────────────── */
uint8_t  FRAM_GetSlaveID(void);
uint8_t  FRAM_SetSlaveID(uint8_t id);     /* 범위 검증 포함 */

int16_t  FRAM_GetZeroRaw(void);
void     FRAM_SetZeroRaw(int16_t raw);

int16_t  FRAM_GetSpanRaw(void);
void     FRAM_SetSpanRaw(int16_t raw);

uint16_t FRAM_GetSpanPpmX10(void);
void     FRAM_SetSpanPpmX10(uint16_t ppm_x10);

uint8_t  FRAM_GetCalFlags(void);
void     FRAM_SetCalFlags(uint8_t flags);

uint16_t FRAM_GetTIACN(void);
void     FRAM_SetTIACN(uint16_t val);

/**
 * @brief  현재 RAM 캐시 데이터 포인터 반환 (읽기 전용)
 */
const FRAM_Data_t* FRAM_GetData(void);

/**
 * @brief  CRC16/ARC 계산 (Modbus 표준)
 * @param  data  계산할 데이터 포인터
 * @param  len   바이트 수
 * @return CRC16 값
 */
uint16_t FRAM_CRC16(const uint8_t *data, uint16_t len);

#endif /* FRAM_H_ */
