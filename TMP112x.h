/* =============================================================
 *  TMP112x.h  —  TMP112B High-Accuracy Digital Temperature Sensor
 *  Target MCU : MSP430FR2155RSM
 *
 *  [하드웨어 연결 - REV0.2 PCB]
 *  I2C Bus    : UCB0  (P1.2=SDA / P1.3=SCL)  100kHz
 *               LMP91000(0x48), ADS1115(0x49), TMP112B(0x4A) 공유 버스
 *  I2C Addr   : 0x4A  (ADD0=SDA) ← 데이터시트 Table 7-4 확인
 *  ALERT      : GND 연결 → ALERT 기능 미사용
 *  VDD        : +3.0VDC
 *  Pullup     : R4=4.7kΩ, R5=4.7kΩ
 *  Bypass Cap : C5=0.1μF
 *
 *  Part       : TMP112BIDRLR (SOT563-6)
 *  Accuracy   : ±0.5°C (0°C ~ 65°C @ 1.8V supply)
 *  Resolution : 12-bit Normal Mode, 0.0625°C / LSB
 *
 *  Rev History
 *  -----------
 *  REV 0.2  2026-04-24  ADD0=SDA → 0x4A (데이터시트 확정)
 *                        I2C Scan 오탐(timeout) 원인 확인 → 무시
 * =============================================================*/

#ifndef TMP112X_H_
#define TMP112X_H_

#include <msp430.h>
#include <stdint.h>

/* ─── I2C 주소 ───────────────────────────────────────────── */
#define TMP112_I2C_ADDR         0x4Au   /* ADD0=SDA → 0x4A */

/* ─── 내부 레지스터 포인터 주소 ─────────────────────────── */
#define TMP112_REG_TEMP         0x00u
#define TMP112_REG_CONFIG       0x01u
#define TMP112_REG_TLOW         0x02u
#define TMP112_REG_THIGH        0x03u

/* ─── Configuration Byte1 비트 정의 ─────────────────────── */
#define TMP112_CFG1_OS          (1u << 7)
#define TMP112_CFG1_R1          (1u << 6)   /* Read-Only=1 */
#define TMP112_CFG1_R0          (1u << 5)   /* Read-Only=1 */
#define TMP112_CFG1_F1          (1u << 4)
#define TMP112_CFG1_F0          (1u << 3)
#define TMP112_CFG1_POL         (1u << 2)
#define TMP112_CFG1_TM          (1u << 1)
#define TMP112_CFG1_SD          (1u << 0)

/* ─── Configuration Byte2 비트 정의 ─────────────────────── */
#define TMP112_CFG2_CR1         (1u << 7)
#define TMP112_CFG2_CR0         (1u << 6)
#define TMP112_CFG2_AL          (1u << 5)   /* Read-Only */
#define TMP112_CFG2_EM          (1u << 4)

/* ─── Conversion Rate ────────────────────────────────────── */
#define TMP112_CR_4HZ           0x80u       /* 4Hz default */

/* ─── 초기화 설정값 ──────────────────────────────────────── */
#define TMP112_CFG1_DEFAULT     (TMP112_CFG1_R1 | TMP112_CFG1_R0 | TMP112_CFG1_F0)
#define TMP112_CFG2_DEFAULT     (TMP112_CR_4HZ)

/* ─── 온도 변환 상수 ─────────────────────────────────────── */
#define TMP112_LSB_DEG_C        0.0625f

/* ─── 오류 코드 ───────────────────────────────────────────── */
#define TMP112_OK               (0u)
#define TMP112_ERR_NACK         (1u)
#define TMP112_ERR_TIMEOUT      (2u)

/* ─── 데이터 구조체 ──────────────────────────────────────── */
typedef struct {
    float   temp_c;
    uint8_t initialized;
} TMP112_t;

/* ─── 공개 API ───────────────────────────────────────────── */
uint8_t TMP112_Init(void);
uint8_t TMP112_ReadTemp(float *temp_c);
uint8_t TMP112_ReadTempOneShot(float *temp_c);
uint8_t TMP112_WriteConfig(uint8_t byte1, uint8_t byte2);
uint8_t TMP112_ReadConfig(uint8_t *byte1, uint8_t *byte2);
uint8_t TMP112_SetShutdown(uint8_t enable);
void    TMP112_I2CBusReset(void);

#endif /* TMP112X_H_ */
