/* ============================================================
 * lmp91000.c - LMP91000 AFE 드라이버
 * ============================================================ */

#include <msp430.h>
#include "config.h"
#include "i2c.h"
#include "uart.h"
#include "lmp91000.h"

#define LMP_DEBUG_LOG 0u

#if LMP_DEBUG_LOG
#define LMP_LOG_STR(s)   UART_SendStr(s)
#define LMP_LOG_HEX8(v)  UART_SendHex8(v)
#else
#define LMP_LOG_STR(s)   ((void)0)
#define LMP_LOG_HEX8(v)  ((void)0)
#endif

/* ============================================================
 *  LMP_Write - 1바이트 레지스터 쓰기
 *  반환: 1=성공, 0=실패
 *
 *  ★ TIACN/REFCN 쓰기 전 반드시 Unlock(LOCK=0x00) 필요
 *  ★ MODECN은 Lock 상태에서도 쓰기 가능
 * ============================================================ */
unsigned char LMP_Write(unsigned char reg, unsigned char data)
{
    UCB0I2CSA = LMP_ADDR;
    UCB0IFG  &= ~(UCTXIFG0 | UCNACKIFG);

    if (!I2C_WaitFlag(&UCB0CTLW0, UCTXSTP,  0)) { I2C_BusReset(); return 0u; }
    UCB0CTLW0 |= UCTR | UCTXSTT;

    if (!I2C_WaitFlag(&UCB0IFG, UCTXIFG0, 1)) { I2C_BusReset(); return 0u; }
    if (UCB0IFG & UCNACKIFG) { UCB0IFG &= ~UCNACKIFG; I2C_BusReset(); return 0u; }
    UCB0TXBUF = reg;

    if (!I2C_WaitFlag(&UCB0IFG, UCTXIFG0, 1)) { I2C_BusReset(); return 0u; }
    if (UCB0IFG & UCNACKIFG) { UCB0IFG &= ~UCNACKIFG; I2C_BusReset(); return 0u; }
    UCB0TXBUF = data;

    if (!I2C_WaitFlag(&UCB0IFG, UCTXIFG0, 1)) { I2C_BusReset(); return 0u; }
    UCB0CTLW0 |= UCTXSTP;
    if (!I2C_WaitFlag(&UCB0CTLW0, UCTXSTP, 0)) { I2C_BusReset(); return 0u; }

    if (UCB0IFG & UCNACKIFG) { UCB0IFG &= ~UCNACKIFG; return 0u; }
    return 1u;
}

/* ============================================================
 *  LMP_Read - 1바이트 레지스터 읽기
 *  반환: 1=성공, 0=실패
 * ============================================================ */
unsigned char LMP_Read(unsigned char reg, unsigned char *data)
{
    UCB0I2CSA = LMP_ADDR;
    UCB0IFG  &= ~(UCTXIFG0 | UCNACKIFG);

    /* Write: 레지스터 주소 전송 */
    if (!I2C_WaitFlag(&UCB0CTLW0, UCTXSTP,  0)) { I2C_BusReset(); return 0u; }
    UCB0CTLW0 |= UCTR | UCTXSTT;

    if (!I2C_WaitFlag(&UCB0IFG, UCTXIFG0, 1)) { I2C_BusReset(); return 0u; }
    if (UCB0IFG & UCNACKIFG) { UCB0IFG &= ~UCNACKIFG; I2C_BusReset(); return 0u; }
    UCB0TXBUF = reg;

    if (!I2C_WaitFlag(&UCB0IFG, UCTXIFG0, 1)) { I2C_BusReset(); return 0u; }
    UCB0CTLW0 |= UCTXSTP;
    if (!I2C_WaitFlag(&UCB0CTLW0, UCTXSTP, 0)) { I2C_BusReset(); return 0u; }
    __delay_cycles(800);

    /* Read: 1바이트 수신 */
    UCB0IFG   &= ~UCNACKIFG;
    UCB0CTLW0 &= ~UCTR;
    UCB0CTLW0 |=  UCTXSTT;

    if (!I2C_WaitFlag(&UCB0CTLW0, UCTXSTT,  0)) { I2C_BusReset(); return 0u; }
    if (UCB0IFG & UCNACKIFG) { UCB0IFG &= ~UCNACKIFG; I2C_BusReset(); return 0u; }

    UCB0CTLW0 |= UCTXSTP;
    if (!I2C_WaitFlag(&UCB0IFG, UCRXIFG0,   1)) { I2C_BusReset(); return 0u; }
    *data = UCB0RXBUF;
    if (!I2C_WaitFlag(&UCB0CTLW0, UCTXSTP,  0)) { I2C_BusReset(); return 0u; }

    UCB0IFG &= ~(UCTXIFG0 | UCRXIFG0 | UCNACKIFG);
    return 1u;
}

/* ============================================================
 *  LMP_Init
 *  STATUS 폴링 → Unlock → TIACN/REFCN/MODECN 설정 → Lock
 *
 *  ★ STATUS=0x00(not ready) 경고 후 계속 진행
 *    (deep sleep 상태에서도 설정 가능한 경우 있음)
 * ============================================================ */
unsigned char LMP_Init(void)
{
    unsigned char status = 0u, rb = 0u, retry;

    /* STATUS 폴링: 최대 200ms (5ms × 40회) */
    for (retry = 0u; retry < 40u; retry++) {
        __delay_cycles(40000);
        if (LMP_Read(LMP_REG_STATUS, &status) && (status & 0x01u)) break;
    }
    LMP_LOG_STR("    STATUS=");
    LMP_LOG_HEX8(status);
    LMP_LOG_STR(status & 0x01u ? " Ready\r\n" : " Warn: not ready, continue\r\n");

    /* Unlock */
    if (!LMP_Write(LMP_REG_LOCK, 0x00u)) {
        LMP_LOG_STR("    [ERR] Unlock\r\n");
        return LMP_ERR; /* Bug6 fix: 실패는 LMP_ERR(1) 반환 */
    }
    __delay_cycles(8000);

    /* TIACN / REFCN — Bug7 fix: 쓰기 실패 시 에러로 반환 */
    if (!LMP_Write(LMP_REG_TIACN, LMP_TIACN_VAL)) {
        LMP_LOG_STR("    [ERR] TIACN\r\n");
        LMP_Write(LMP_REG_LOCK, 0x01u); /* 잠금 복원 후 종료 */
        return LMP_ERR;
    }
    if (!LMP_Write(LMP_REG_REFCN, LMP_REFCN_VAL)) {
        LMP_LOG_STR("    [ERR] REFCN\r\n");
        LMP_Write(LMP_REG_LOCK, 0x01u);
        return LMP_ERR;
    }

    /* MODECN: Amperometric - 최대 3회 재시도 */
    rb = 0u;
    for (retry = 0u; retry < 3u; retry++) {
        LMP_Write(LMP_REG_MODECN, LMP_MODECN_VAL);
        __delay_cycles(8000);
        LMP_Read(LMP_REG_MODECN, &rb);
        if ((rb & 0x07u) == LMP_MODECN_VAL) break;
    }

    /* Lock */
    LMP_Write(LMP_REG_LOCK, 0x01u);
    __delay_cycles(8000);

    /* Bug6 fix: 성공=LMP_OK(0), 실패=LMP_ERR(1) — FRAM/TMP112와 동일 규약 */
    return ((rb & 0x07u) == LMP_MODECN_VAL) ? LMP_OK : LMP_ERR;
}

/* ============================================================
 *  LMP_Verify - 레지스터 readback 검증 (UART 출력 포함)
 *  반환: 1=전체 PASS, 0=일부 FAIL
 *
 *  ★ readback 주의사항
 *    TIACN: 0x0C (write값 그대로)
 *    REFCN: 0xB0 (write값 그대로, 단 일부 보드에서 0x90 관측 가능)
 * ============================================================ */
unsigned char LMP_Verify(void)
{
    unsigned char val = 0u, pass = 1u;

    LMP_Read(LMP_REG_TIACN, &val);
    LMP_LOG_STR("    TIACN  "); LMP_LOG_HEX8(val);
    LMP_LOG_STR(" exp ");       LMP_LOG_HEX8(LMP_TIACN_VAL);
    if (val == LMP_TIACN_VAL) LMP_LOG_STR(" OK\r\n");
    else { LMP_LOG_STR(" FAIL\r\n"); pass = 0u; }

    LMP_Read(LMP_REG_REFCN, &val);
    LMP_LOG_STR("    REFCN  "); LMP_LOG_HEX8(val);
    LMP_LOG_STR(" exp ");       LMP_LOG_HEX8(LMP_REFCN_VAL);
    if (val == LMP_REFCN_VAL) LMP_LOG_STR(" OK\r\n");
    else { LMP_LOG_STR(" FAIL\r\n"); pass = 0u; }

    LMP_Read(LMP_REG_MODECN, &val);
    LMP_LOG_STR("    MODECN "); LMP_LOG_HEX8(val);
    LMP_LOG_STR(" exp ");       LMP_LOG_HEX8(LMP_MODECN_VAL);
    if ((val & 0x07u) == LMP_MODECN_VAL) LMP_LOG_STR(" OK\r\n");
    else { LMP_LOG_STR(" FAIL\r\n"); pass = 0u; }

    return pass;
}

/* ============================================================
 *  LMP_SetMode - OP_MODE만 변경 (TIACN/REFCN 유지)
 *  MODECN은 Lock 상태에서도 쓰기 가능
 *  최대 3회 재시도
 * ============================================================ */
unsigned char LMP_SetMode(unsigned char mode)
{
    unsigned char rb = 0u, retry;
    for (retry = 0u; retry < 3u; retry++) {
        LMP_Write(LMP_REG_MODECN, mode);
        __delay_cycles(8000);
        LMP_Read(LMP_REG_MODECN, &rb);
        if ((rb & 0x07u) == (mode & 0x07u)) return 1u;
    }
    return 0u;
}

/* ============================================================
 *  LMP_CalcTemp - 온도 계산
 *  vs     : 현재 VS 전압 (VOUT - VZERO) [V]
 *  vs_ref : 25°C 기준 VS 전압 (캘리브레이션값) [V]
 *  반환   : 온도 [°C]
 *
 *  공식: T = 25 + (VS - VS_ref) / (-11.76mV/°C)
 * ============================================================ */
float LMP_CalcTemp(float vs, float vs_ref)
{
    return LMP_TEMP_REF_C + (vs - vs_ref) / LMP_TEMP_SENS_V_PER_C;
}
