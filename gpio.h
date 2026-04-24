#ifndef GPIO_H
#define GPIO_H

/* ============================================================
 * gpio.h - GPIO 초기화
 * I2C : P1.2(SDA) / P1.3(SCL)
 * UART: P4.2(RXD) / P4.3(TXD)
 * LMP MENB : P3.1 (Active-LOW)
 * ADS CS   : P3.2 (Active-HIGH)
 * ============================================================ */

void GPIO_Init(void);

#endif /* GPIO_H */
