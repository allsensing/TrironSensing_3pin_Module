#ifndef I2C_H
#define I2C_H

/* ============================================================
 * i2c.h - I2C 버스 공통 드라이버
 * UCB0 / P1.2(SDA) / P1.3(SCL) / 100kHz / 폴링 모드
 * ============================================================ */

void          I2C_Init(void);
void          I2C_BusReset(void);
unsigned char I2C_WaitFlag(volatile unsigned int *reg,
                            unsigned int flag, unsigned char set);

#endif /* I2C_H */
