#ifndef CONFIG_H
#define CONFIG_H

/* ============================================================
 * config.h - 프로젝트 전역 설정
 * MSP430FR2155 / CO 가스 센서 시스템
 * ============================================================ */

/* ---- 클럭 ---- */
#define MCLK_FREQ_MHZ       8u

/* ---- LED 제어 매크로 ---- */
#define LED_DIR     P2DIR
#define LED_OUT     P2OUT
#define LED_PIN     BIT0        /* P2.0 */

#define LED_ON()    (LED_OUT |=  LED_PIN)
#define LED_OFF()   (LED_OUT &= ~LED_PIN)
#define LED_TOGGLE()(LED_OUT ^=  LED_PIN)


/* ---- I2C 타임아웃 (long: unsigned int 언더플로우 방지) ---- */
#define UART_I2C_CS_DIR     P4DIR
#define UART_I2C_CS_OUT     P4OUT
#define UART_I2C_CS_SEL0    P4SEL0
#define UART_I2C_CS_SEL1    P4SEL1
#define UART_I2C_CS_PIN     BIT1

#define UART_PATH_SEL()     (UART_I2C_CS_OUT &= ~UART_I2C_CS_PIN)
#define I2C_PATH_SEL()      (UART_I2C_CS_OUT |=  UART_I2C_CS_PIN)

#define I2C_TO              30000L

/* ---- I2C 디바이스 주소 ---- */
#define LMP_ADDR            0x48u   /* LMP91000  (고정) */
#define ADS_ADDR            0x49u   /* ADS1115   (ADDR→VDD) */

/* ============================================================
 *  CS / MENB 핀 제어
 *
 *  LMP91000 MENB (P3.1) : Active-LOW  → LOW=활성, HIGH=비활성
 *  ADS1115  CS   (P3.2) : Active-HIGH → HIGH=활성, LOW=비활성
 *
 *  LMP_SEL  : LMP ON  / ADS OFF
 *  ADS_SEL  : LMP OFF / ADS ON
 *  ALL_DESEL: LMP OFF / ADS OFF  (기본/안전 상태)
 * ============================================================ */
#define LMP_SEL()   do { P3OUT &= ~BIT1; P3OUT &= ~BIT2; \
                         __delay_cycles(8000); } while(0)
#define ADS_SEL()   do { P3OUT |=  BIT1; P3OUT |=  BIT2; \
                         __delay_cycles(8000); } while(0)
#define ALL_DESEL() do { P3OUT |=  BIT1; P3OUT &= ~BIT2; } while(0)

#endif /* CONFIG_H */
