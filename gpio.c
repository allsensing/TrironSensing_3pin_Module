/* ============================================================
 * gpio.c - GPIO 초기화
 * ============================================================ */

#include <msp430.h>
#include "config.h"
#include "gpio.h"

/* ============================================================
 *  GPIO_Init
 *
 *  미사용 핀: 풀다운 출력으로 설정 (전류 누설 방지)
 *  I2C   : P1.2(SDA), P1.3(SCL) - 복합 기능, 풀업 해제
 *  UART  : P4.2(RXD), P4.3(TXD) - 복합 기능
 *  LMP MENB : P3.1 = HIGH 초기값 (비활성)
 *  ADS CS   : P3.2 = LOW  초기값 (비활성)
 * ============================================================ */
void GPIO_Init(void)
{
    /* 미사용 핀 전체 풀다운 */
    P1DIR = 0xFF; P2DIR = 0xFF; P3DIR = 0xFF; P4DIR = 0xFF;
    P1REN = 0xFF; P2REN = 0xFF; P3REN = 0xFF; P4REN = 0xFF;
    P1OUT = 0x00; P2OUT = 0x00; P3OUT = 0x00; P4OUT = 0x00;

    /* I2C: P1.2(SDA), P1.3(SCL) */
    P1REN  &= ~(BIT2 | BIT3);  /* 풀업/풀다운 해제 (외부 풀업 사용) */
    P1SEL0 |=  (BIT2 | BIT3);  /* UCB0 복합 기능 선택 */
    P1SEL1 &= ~(BIT2 | BIT3);

    /* LED: P2.0 출력, 풀저항 해제 */
    LED_DIR |=  LED_PIN;
    P2REN   &= ~LED_PIN;
    LED_OFF();

    /* UART: P4.2(RXD), P4.3(TXD) */
    P4SEL0 |=  (BIT2 | BIT3);  /* UCA1 복합 기능 선택 */
    P4REN  &= ~(BIT1 | BIT2 | BIT3);
    P4DIR  &= ~BIT2;
    P4DIR  |=  (BIT1 | BIT3);
    P4SEL1 &= ~(BIT2 | BIT3);

    /* U3 analog switch select: LOW = UART path, HIGH = I2C path */
    UART_I2C_CS_SEL0 &= ~UART_I2C_CS_PIN;
    UART_I2C_CS_SEL1 &= ~UART_I2C_CS_PIN;
    UART_I2C_CS_DIR  |=  UART_I2C_CS_PIN;
    UART_PATH_SEL();

    /* CS 핀 출력 설정, 풀저항 해제 */
    P3DIR  |=  (BIT1 | BIT2);
    P3REN  &= ~(BIT1 | BIT2);

    /* 초기 상태: 두 디바이스 모두 비활성 */
    P3OUT  |=  BIT1;    /* LMP MENB = HIGH → 비활성 */
    P3OUT  &= ~BIT2;    /* ADS CS   = LOW  → 비활성 */
}
