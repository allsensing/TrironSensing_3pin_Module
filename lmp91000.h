#ifndef LMP91000_H
#define LMP91000_H

/* ============================================================
 * lmp91000.h - LMP91000 AFE 드라이버
 * I2C 주소: 0x48 (고정) / MENB: P3.1 (Active-LOW)
 *
 * 레지스터 설정 (CO 3전극 센서 기준)
 *   TIACN  0x0C : TIA_GAIN=7kΩ / RLOAD=10Ω
 *   REFCN  0xB0 : Ext VREF / ZERO=50% / BIAS=0%+
 *   MODECN 0x03 : 3-electrode Amperometric
 *
 * 온도 측정
 *   MODECN 0x07 : Temperature measurement (TIA ON)
 *   감도: -1.68µA/°C × 7kΩ = -11.76mV/°C
 * ============================================================ */

/* ---- 레지스터 주소 ---- */
#define LMP_REG_STATUS  0x00u
#define LMP_REG_LOCK    0x01u
#define LMP_REG_TIACN   0x10u
#define LMP_REG_REFCN   0x11u
#define LMP_REG_MODECN  0x12u

/* ---- 레지스터 설정값 ---- */
#define LMP_TIACN_VAL   0x0Cu  /* TIA_GAIN=7kΩ bits[4:2]=011 / RLOAD=10Ω bits[1:0]=00 */
#define LMP_REFCN_VAL   0xB0u  /* Ext VREF [4]=1 / ZERO=50% [6:5]=01 / BIAS=0% [3:0]=0 / Positive [7]=1 */
#define LMP_MODECN_VAL  0x03u  /* 3-electrode Amperometric [2:0]=011 */

/* ---- TIA GAIN 인덱스 (GAIN:<n> 커맨드용) ----
 *  TIACN bits[4:2] = TIA_GAIN, bits[1:0] = RLOAD(유지)
 *  인덱스:  1     2     3     4     5     6      7
 *  저항:  2.75k  3.5k   7k   14k   35k  120k  350k  (kΩ)
 * ------------------------------------------------- */
#define LMP_GAIN_IDX_MIN    1u
#define LMP_GAIN_IDX_MAX    7u
#define LMP_TIACN_GAIN_SHIFT  2u              /* TIA_GAIN 비트 위치 */
#define LMP_TIACN_RLOAD_MASK  0x03u           /* RLOAD 비트 마스크 (유지용) */

/* ---- 반환 코드 (FRAM_OK=0, TMP112_OK=0과 동일 규약) ---- */
#define LMP_OK   0u   /* 성공 */
#define LMP_ERR  1u   /* 실패 */

/* ---- 동작 모드 ---- */
#define LMP_MODE_DEEP_SLEEP  0x00u  /* 000: Deep sleep        */
#define LMP_MODE_2LEAD       0x01u  /* 001: 2-lead galvanic   */
#define LMP_MODE_STANDBY     0x02u  /* 010: Standby           */
#define LMP_MODE_AMPERO      0x03u  /* 011: 3-electrode amperometric */
#define LMP_MODE_TEMP_TIA    0x06u  /* 110: Temp (TIA ON)     */
#define LMP_MODE_TEMP        0x07u  /* 111: Temp measurement  */

/* ---- 온도 변환 파라미터 ---- */
#define LMP_TEMP_SENS_V_PER_C  (-0.01176f)  /* -11.76mV/°C (7kΩ 기준) */
#define LMP_TEMP_REF_C         25.0f

/* ---- 함수 선언 ---- */
unsigned char LMP_Write(unsigned char reg, unsigned char data);
unsigned char LMP_Read (unsigned char reg, unsigned char *data);

unsigned char LMP_Init(void);           /* 가스 측정용 Amperometric 초기화 */
unsigned char LMP_Verify(void);         /* 레지스터 readback 검증 */
unsigned char LMP_SetMode(unsigned char mode); /* OP_MODE 변경 */
unsigned char LMP_SetTIACN(unsigned char tiacn_val); /* Unlock→TIACN쓰기→Lock+검증 */

float         LMP_CalcTemp(float vs, float vs_ref);  /* 온도 계산 */

#endif /* LMP91000_H */
