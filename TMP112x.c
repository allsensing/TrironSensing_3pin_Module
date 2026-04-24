/* =============================================================
 * TMP112x.c - TMP112B temperature sensor driver
 * ============================================================= */

#include "tmp112x.h"
#include "i2c.h"

#define I2C_TIMEOUT     (50000L)
#define DELAY_15MS      (120000UL)

static TMP112_t g_tmp112 = {0.0f, 0u};

static uint8_t TMP112_Fail(uint8_t code)
{
    UCB0IFG &= ~(UCTXIFG0 | UCRXIFG0 | UCNACKIFG);
    I2C_BusReset();
    return code;
}

static uint8_t TMP112_WriteReg(uint8_t reg, uint8_t b1, uint8_t b2)
{
    volatile long t;

    t = I2C_TIMEOUT;
    while (UCB0STAT & UCBBUSY) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }

    UCB0I2CSA = TMP112_I2C_ADDR;
    UCB0CTL1 |= UCTR;
    UCB0CTL1 |= UCTXSTT;

    t = I2C_TIMEOUT;
    while (UCB0CTL1 & UCTXSTT) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }
    if (UCB0IFG & UCNACKIFG) {
        UCB0CTL1 |= UCTXSTP;
        UCB0IFG &= ~UCNACKIFG;
        return TMP112_Fail(TMP112_ERR_NACK);
    }

    t = I2C_TIMEOUT;
    while (!(UCB0IFG & UCTXIFG0)) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }
    UCB0TXBUF = reg;

    t = I2C_TIMEOUT;
    while (!(UCB0IFG & UCTXIFG0)) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }
    UCB0TXBUF = b1;

    t = I2C_TIMEOUT;
    while (!(UCB0IFG & UCTXIFG0)) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }
    UCB0TXBUF = b2;

    t = I2C_TIMEOUT;
    while (!(UCB0IFG & UCTXIFG0)) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }
    UCB0CTL1 |= UCTXSTP;

    t = I2C_TIMEOUT;
    while (UCB0CTL1 & UCTXSTP) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }

    return TMP112_OK;
}

static uint8_t TMP112_ReadReg(uint8_t reg, uint8_t *b1, uint8_t *b2)
{
    volatile long t;

    t = I2C_TIMEOUT;
    while (UCB0STAT & UCBBUSY) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }

    UCB0I2CSA = TMP112_I2C_ADDR;
    UCB0CTL1 |= UCTR;
    UCB0CTL1 |= UCTXSTT;

    t = I2C_TIMEOUT;
    while (UCB0CTL1 & UCTXSTT) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }
    if (UCB0IFG & UCNACKIFG) {
        UCB0CTL1 |= UCTXSTP;
        UCB0IFG &= ~UCNACKIFG;
        return TMP112_Fail(TMP112_ERR_NACK);
    }

    t = I2C_TIMEOUT;
    while (!(UCB0IFG & UCTXIFG0)) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }
    UCB0TXBUF = reg;

    t = I2C_TIMEOUT;
    while (!(UCB0IFG & UCTXIFG0)) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }
    UCB0CTL1 |= UCTXSTP;

    t = I2C_TIMEOUT;
    while (UCB0CTL1 & UCTXSTP) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }

    UCB0I2CSA = TMP112_I2C_ADDR;
    UCB0CTL1 &= ~UCTR;
    UCB0CTL1 |= UCTXSTT;

    t = I2C_TIMEOUT;
    while (UCB0CTL1 & UCTXSTT) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }
    if (UCB0IFG & UCNACKIFG) {
        UCB0CTL1 |= UCTXSTP;
        UCB0IFG &= ~UCNACKIFG;
        return TMP112_Fail(TMP112_ERR_NACK);
    }

    t = I2C_TIMEOUT;
    while (!(UCB0IFG & UCRXIFG0)) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }
    *b1 = UCB0RXBUF;

    UCB0CTL1 |= UCTXSTP;

    t = I2C_TIMEOUT;
    while (!(UCB0IFG & UCRXIFG0)) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }
    *b2 = UCB0RXBUF;

    t = I2C_TIMEOUT;
    while (UCB0CTL1 & UCTXSTP) {
        if (--t <= 0) return TMP112_Fail(TMP112_ERR_TIMEOUT);
    }

    return TMP112_OK;
}

static float TMP112_RawToTemp(uint8_t msb, uint8_t lsb)
{
    int16_t raw = (int16_t)(((uint16_t)msb << 8) | (uint16_t)lsb);
    raw >>= 4;
    return (float)raw * TMP112_LSB_DEG_C;
}

uint8_t TMP112_Init(void)
{
    uint8_t ret, rb1, rb2;
    uint8_t exp1, exp2;

    ret = TMP112_WriteConfig(TMP112_CFG1_DEFAULT, TMP112_CFG2_DEFAULT);
    if (ret != TMP112_OK) return ret;

    ret = TMP112_ReadConfig(&rb1, &rb2);
    if (ret != TMP112_OK) return ret;

    exp1 = (TMP112_CFG1_DEFAULT | TMP112_CFG1_R1 | TMP112_CFG1_R0) & 0x7Fu;
    exp2 = TMP112_CFG2_DEFAULT & 0xDFu;

    if (((rb1 & 0x7Fu) != exp1) || ((rb2 & 0xDFu) != exp2)) {
        return TMP112_ERR_NACK;
    }

    g_tmp112.initialized = 1u;
    return TMP112_OK;
}

uint8_t TMP112_ReadTemp(float *temp_c)
{
    uint8_t ret, msb, lsb;

    if (!temp_c) return TMP112_ERR_NACK;

    ret = TMP112_ReadReg(TMP112_REG_TEMP, &msb, &lsb);
    if (ret != TMP112_OK) return ret;

    g_tmp112.temp_c = TMP112_RawToTemp(msb, lsb);
    *temp_c = g_tmp112.temp_c;
    return TMP112_OK;
}

uint8_t TMP112_ReadTempOneShot(float *temp_c)
{
    uint8_t ret, msb, lsb;

    if (!temp_c) return TMP112_ERR_NACK;

    ret = TMP112_WriteConfig(
        TMP112_CFG1_OS | TMP112_CFG1_R1 | TMP112_CFG1_R0 |
        TMP112_CFG1_F0 | TMP112_CFG1_SD,
        TMP112_CFG2_DEFAULT);
    if (ret != TMP112_OK) return ret;

    __delay_cycles(DELAY_15MS);

    ret = TMP112_ReadReg(TMP112_REG_TEMP, &msb, &lsb);
    if (ret != TMP112_OK) return ret;

    g_tmp112.temp_c = TMP112_RawToTemp(msb, lsb);
    *temp_c = g_tmp112.temp_c;
    return TMP112_OK;
}

uint8_t TMP112_WriteConfig(uint8_t byte1, uint8_t byte2)
{
    return TMP112_WriteReg(TMP112_REG_CONFIG, byte1, byte2);
}

uint8_t TMP112_ReadConfig(uint8_t *byte1, uint8_t *byte2)
{
    return TMP112_ReadReg(TMP112_REG_CONFIG, byte1, byte2);
}

uint8_t TMP112_SetShutdown(uint8_t enable)
{
    uint8_t ret, b1, b2;

    ret = TMP112_ReadConfig(&b1, &b2);
    if (ret != TMP112_OK) return ret;

    if (enable) b1 |= TMP112_CFG1_SD;
    else        b1 &= (uint8_t)~TMP112_CFG1_SD;

    return TMP112_WriteConfig(b1, b2);
}

void TMP112_I2CBusReset(void)
{
    I2C_BusReset();
}
