/* =============================================================
 *  fram.h  —  MSP430FR2155 FRAM 영구 저장 드라이버
 *  Target MCU : MSP430FR2155RSM
 *
 *  [MSP430FR2155 FRAM 메모리 맵]
 *  0xC000 ~ 0xFF7F : 코드/데이터 FRAM (일반 사용)
 *  0x1800 ~ 0x19FF : Information Memory FRAM (설정 저장 권장)
 *
 *  [저장 영역 선택]
 *  Information Memory (0x1980) 사용
 *  → 코드 업데이트 시에도 데이터 보존 가능
 *
 *  [FRAM 레이아웃]
 *  Offset  Size  항목
 *  0x00     2    Magic Number (0xA55A: 초기화 완료 표시)
 *  0x02     1    Slave ID (1~64)
 *  0x03     1    예약
 *  0x04     2    Factory Zero RAW (ADS1115 LSB)
 *  0x06     2    Factory Span RAW (ADS1115 LSB)
 *  0x08     2    Factory Span 가스 농도 (ppm × 10)
 *  0x0A     2    User Zero RAW
 *  0x0C     2    User Span RAW
 *  0x0E     2    User Span 가스 농도 (ppm × 10)
 *  0x10     1    교정 완료 플래그
 *               bit0=FZERO, bit1=FSPAN, bit2=ZERO, bit3=SPAN
 *  0x11     1    예약
 *  0x12     2    TIA Gain 설정 (LMP91000 TIACN 레지스터값)
 *  0x14     2    CRC16 (Magic~0x13 전체)
 *  총 22 bytes
 *
 *  [교정 플래그]
 *  FZERO(bit0) + FSPAN(bit1) : 공장 교정 (FZERO/FSPAN 명령)
 *  ZERO(bit2)  + SPAN(bit3)  : 사용자 교정 (ZERO/SPAN 명령)
 *  RECAL 실행 시 ZERO/SPAN ← FZERO/FSPAN 복원
 *
 *  Rev History
 *  -----------
 *  REV 0.3  2026-04-26  Factory/User Cal 2-tier 구조로 확장
 *  REV 0.2  2026-04-24  Initial
 * =============================================================*/

#ifndef FRAM_H_
#define FRAM_H_

#include <msp430.h>
#include <stdint.h>

/* ─── FRAM 저장 기준 주소 ────────────────────────────────── */
#define FRAM_BASE_ADDR          0x1980u

/* ─── Magic Number ───────────────────────────────────────── */
#define FRAM_MAGIC              0xA55Au

/* ─── Slave ID 범위 ──────────────────────────────────────── */
#define FRAM_SLAVE_ID_MIN       1u
#define FRAM_SLAVE_ID_MAX       64u
#define FRAM_SLAVE_ID_DEFAULT   1u

/* ─── 교정 플래그 비트 ───────────────────────────────────── */
#define FRAM_CAL_FZERO_DONE     (1u << 0)   /* Factory Zero 완료 */
#define FRAM_CAL_FSPAN_DONE     (1u << 1)   /* Factory Span 완료 */
#define FRAM_CAL_ZERO_DONE      (1u << 2)   /* User Zero 완료    */
#define FRAM_CAL_SPAN_DONE      (1u << 3)   /* User Span 완료    */

/* ─── Span 기본값 ────────────────────────────────────────── */
#define FRAM_SPAN_PPM_DEFAULT   5000u       /* 500.0 ppm (×10)  */

/* ─── 오류 코드 ───────────────────────────────────────────── */
#define FRAM_OK                 (0u)
#define FRAM_ERR_CRC            (1u)
#define FRAM_ERR_INVALID        (2u)

/* ─── FRAM 저장 구조체 (22 bytes) ────────────────────────── */
typedef struct {
    uint16_t magic;             /* 0x00: 0xA55A = 초기화 완료      */
    uint8_t  slave_id;          /* 0x02: Modbus Slave ID 1~64      */
    uint8_t  reserved0;         /* 0x03: 예약                      */
    int16_t  fzero_raw;         /* 0x04: Factory Zero ADC raw      */
    int16_t  fspan_raw;         /* 0x06: Factory Span ADC raw      */
    uint16_t fspan_ppm_x10;     /* 0x08: Factory Span 농도 ×10    */
    int16_t  zero_raw;          /* 0x0A: User Zero ADC raw         */
    int16_t  span_raw;          /* 0x0C: User Span ADC raw         */
    uint16_t span_ppm_x10;      /* 0x0E: User Span 농도 ×10       */
    uint8_t  cal_flags;         /* 0x10: 교정 완료 플래그          */
    uint8_t  reserved1;         /* 0x11: 예약                      */
    uint16_t tiacn_val;         /* 0x12: LMP91000 TIACN 설정       */
    uint16_t crc16;             /* 0x14: CRC16 검증값              */
} __attribute__((packed)) FRAM_Data_t;

/* ─── 공개 API ───────────────────────────────────────────── */

uint8_t  FRAM_Load(void);
uint8_t  FRAM_Save(void);
uint8_t  FRAM_Reset(void);

/* ─── Slave ID ────────────────────────────────────────────── */
uint8_t  FRAM_GetSlaveID(void);
uint8_t  FRAM_SetSlaveID(uint8_t id);

/* ─── 교정 API ───────────────────────────────────────────── */

/**
 * @brief  Factory Zero 교정 (FZERO 명령)
 *         fzero_raw 저장 + zero_raw 동기화
 *         FZERO_DONE + ZERO_DONE 플래그 설정
 */
void FRAM_SetFactoryZero(int16_t raw);

/**
 * @brief  Factory Span 교정 (FSPAN:<ppm> 명령)
 *         fspan_raw/ppm 저장 + span_raw/ppm 동기화
 *         FSPAN_DONE + SPAN_DONE 플래그 설정
 */
void FRAM_SetFactorySpan(int16_t raw, uint16_t ppm_x10);

/**
 * @brief  User Zero 교정 (ZERO 명령)
 *         zero_raw 갱신, ZERO_DONE 플래그 설정
 *         Factory cal은 변경하지 않음
 */
void FRAM_SetUserZero(int16_t raw);

/**
 * @brief  User Span 교정 (SPAN:<ppm> 명령)
 *         span_raw/ppm 갱신, SPAN_DONE 플래그 설정
 *         Factory cal은 변경하지 않음
 */
void FRAM_SetUserSpan(int16_t raw, uint16_t ppm_x10);

/**
 * @brief  Factory cal → User cal 복원 (RECAL 명령)
 *         zero_raw = fzero_raw, span_raw = fspan_raw
 *         ZERO/SPAN 플래그를 FZERO/FSPAN 상태에 맞춰 갱신
 */
void FRAM_RecalFromFactory(void);

/* ─── Getter ─────────────────────────────────────────────── */
int16_t  FRAM_GetFactoryZeroRaw(void);
int16_t  FRAM_GetFactorySpanRaw(void);
uint16_t FRAM_GetFactorySpanPpmX10(void);
int16_t  FRAM_GetZeroRaw(void);
int16_t  FRAM_GetSpanRaw(void);
uint16_t FRAM_GetSpanPpmX10(void);
uint8_t  FRAM_GetCalFlags(void);
uint16_t FRAM_GetTIACN(void);

/* ─── 하위 호환 Setter (내부용) ──────────────────────────── */
void     FRAM_SetCalFlags(uint8_t flags);
void     FRAM_SetTIACN(uint16_t val);

/**
 * @brief  현재 RAM 캐시 데이터 포인터 반환 (읽기 전용)
 */
const FRAM_Data_t* FRAM_GetData(void);

/**
 * @brief  CRC16/ARC 계산 (Modbus 표준)
 */
uint16_t FRAM_CRC16(const uint8_t *data, uint16_t len);

#endif /* FRAM_H_ */
