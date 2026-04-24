/* ============================================================
 * i2c.c - I2C 버스 공통 드라이버
 * UCB0 / P1.2(SDA) / P1.3(SCL) / 100kHz / 폴링 모드
 * ISR 미사용 (UCB0IE = 0)
 * ============================================================ */

#include <msp430.h>
#include "config.h"
#include "i2c.h"

/* ============================================================
 *  I2C_Init
 *  100kHz = 8MHz / 80
 * ============================================================ */
void I2C_Init(void)
{
    UCB0CTLW0  = UCSWRST;
    UCB0CTLW0 |= UCMODE_3 | UCMST | UCSYNC | UCSSEL__SMCLK;
    UCB0CTLW1  = UCASTP_0;     /* 자동 STOP 비활성 (수동 폴링) */
    UCB0BRW    = 80;            /* 8MHz / 80 = 100kHz */
    UCB0TBCNT  = 0;
    UCB0CTLW0 &= ~UCSWRST;
    UCB0IE     = 0;             /* ISR 미사용 */
}

/* ============================================================
 *  I2C_BusReset
 *  SDA가 LOW로 묶인 경우 SCL 9클럭으로 강제 해제
 *  수동 STOP 조건 후 UCB0 재초기화
 * ============================================================ */
void I2C_BusReset(void)
{
    unsigned char i;

    UCB0CTLW0 |= UCSWRST;

    /* P1.2/P1.3을 GPIO 출력으로 전환 */
    P1SEL0 &= ~(BIT2 | BIT3);
    P1DIR  |=  (BIT2 | BIT3);
    P1OUT  |=  (BIT2 | BIT3);  /* SDA=HIGH, SCL=HIGH */
    __delay_cycles(800);

    /* SCL 9클럭 토글 (SDA 해제 대기) */
    for (i = 0u; i < 9u; i++) {
        P1OUT &= ~BIT3; __delay_cycles(400);    /* SCL LOW  */
        P1OUT |=  BIT3; __delay_cycles(400);    /* SCL HIGH */
        if (P1IN & BIT2) break;                 /* SDA HIGH 확인 → 해제됨 */
    }

    /* 수동 STOP 조건: SDA LOW→HIGH (SCL HIGH 유지) */
    P1OUT &= ~BIT2; __delay_cycles(400);
    P1OUT |=  BIT3; __delay_cycles(400);
    P1OUT |=  BIT2; __delay_cycles(800);

    /* UCB0 복합 기능 복원 */
    P1SEL0 |=  (BIT2 | BIT3);
    P1DIR  &= ~(BIT2 | BIT3);

    I2C_Init();
}

/* ============================================================
 *  I2C_WaitFlag
 *  reg  : 감시할 레지스터 포인터 (UCB0CTLW0 또는 UCB0IFG)
 *  flag : 감시할 비트 마스크
 *  set  : 1=비트가 SET될 때까지 대기, 0=CLEAR될 때까지 대기
 *  반환 : 1=성공, 0=타임아웃
 *
 *  ★ long 타입 사용으로 unsigned int 언더플로우(무한루프) 방지
 * ============================================================ */
unsigned char I2C_WaitFlag(volatile unsigned int *reg,
                            unsigned int flag, unsigned char set)
{
    long t = I2C_TO;
    if (set) { while (!(*reg & flag) && --t); }
    else     { while (  (*reg & flag) && --t); }
    return (t > 0L) ? 1u : 0u;
}
