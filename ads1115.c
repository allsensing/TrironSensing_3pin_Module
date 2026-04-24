/* ============================================================
 * ads1115.c - ADS1115 16-bit ADC driver
 * ============================================================ */

#include <msp430.h>
#include "config.h"
#include "i2c.h"
#include "ads1115.h"

static unsigned char ADS_Fail(int *raw)
{
    if (raw) {
        *raw = 0;
    }
    I2C_BusReset();
    return ADS_ERR_IO;
}

unsigned char ADS_SetPointer(unsigned char reg)
{
    UCB0I2CSA = ADS_ADDR;
    UCB0IFG &= ~(UCTXIFG0 | UCRXIFG0 | UCNACKIFG);

    if (!I2C_WaitFlag(&UCB0CTLW0, UCTXSTP, 0)) return ADS_Fail(0);
    UCB0CTLW0 |= UCTR | UCTXSTT;

    if (!I2C_WaitFlag(&UCB0IFG, UCTXIFG0, 1)) return ADS_Fail(0);
    UCB0TXBUF = reg;

    if (!I2C_WaitFlag(&UCB0IFG, UCTXIFG0, 1)) return ADS_Fail(0);
    UCB0CTLW0 |= UCTXSTP;

    if (!I2C_WaitFlag(&UCB0CTLW0, UCTXSTP, 0)) return ADS_Fail(0);
    return ADS_OK;
}

unsigned char ADS_StartSingleShot(unsigned char mux, unsigned char pga)
{
    unsigned int cfg = (unsigned int)0x8000u
                     | (unsigned int)(mux << 12)
                     | (unsigned int)(pga << 9)
                     | (unsigned int)(1u << 8)
                     | (unsigned int)(4u << 5)
                     | (unsigned int)(3u << 0);

    UCB0I2CSA = ADS_ADDR;
    UCB0IFG &= ~(UCTXIFG0 | UCRXIFG0 | UCNACKIFG);

    if (!I2C_WaitFlag(&UCB0CTLW0, UCTXSTP, 0)) return ADS_Fail(0);
    UCB0CTLW0 |= UCTR | UCTXSTT;

    if (!I2C_WaitFlag(&UCB0IFG, UCTXIFG0, 1)) return ADS_Fail(0);
    UCB0TXBUF = ADS_REG_CFG;

    if (!I2C_WaitFlag(&UCB0IFG, UCTXIFG0, 1)) return ADS_Fail(0);
    UCB0TXBUF = (unsigned char)((cfg >> 8) & 0xFFu);

    if (!I2C_WaitFlag(&UCB0IFG, UCTXIFG0, 1)) return ADS_Fail(0);
    UCB0TXBUF = (unsigned char)(cfg & 0xFFu);

    if (!I2C_WaitFlag(&UCB0IFG, UCTXIFG0, 1)) return ADS_Fail(0);
    UCB0CTLW0 |= UCTXSTP;
    if (!I2C_WaitFlag(&UCB0CTLW0, UCTXSTP, 0)) return ADS_Fail(0);

    return ADS_OK;
}

unsigned char ADS_ReadConversionRaw(int *raw)
{
    int result = 0;

    if (!raw) {
        return ADS_ERR_IO;
    }

    if (ADS_SetPointer(ADS_REG_CONV) != ADS_OK) {
        return ADS_Fail(raw);
    }

    UCB0IFG &= ~(UCTXIFG0 | UCRXIFG0 | UCNACKIFG);
    UCB0CTLW0 &= ~UCTR;
    UCB0CTLW0 |= UCTXSTT;

    if (!I2C_WaitFlag(&UCB0CTLW0, UCTXSTT, 0)) return ADS_Fail(raw);
    if (!I2C_WaitFlag(&UCB0IFG, UCRXIFG0, 1)) return ADS_Fail(raw);
    result = (int)((unsigned int)UCB0RXBUF << 8);

    UCB0CTLW0 |= UCTXSTP;
    if (!I2C_WaitFlag(&UCB0IFG, UCRXIFG0, 1)) return ADS_Fail(raw);
    result |= (int)UCB0RXBUF;

    if (!I2C_WaitFlag(&UCB0CTLW0, UCTXSTP, 0)) return ADS_Fail(raw);

    UCB0IFG &= ~(UCTXIFG0 | UCRXIFG0 | UCNACKIFG);
    *raw = result;
    return ADS_OK;
}

void ADS_Init(void)
{
    UCB0I2CSA = ADS_ADDR;
}

int ADS_ReadCh(unsigned char mux)
{
    int raw = 0;
    (void)ADS_ReadChRaw(mux, &raw);
    return raw;
}

unsigned char ADS_ReadChRaw(unsigned char mux, int *raw)
{
    return ADS_ReadChPGA_Raw(mux, ADS_PGA_USE, raw);
}

int ADS_ReadChPGA(unsigned char mux, unsigned char pga)
{
    int raw = 0;
    (void)ADS_ReadChPGA_Raw(mux, pga, &raw);
    return raw;
}

unsigned char ADS_ReadChPGA_Raw(unsigned char mux, unsigned char pga, int *raw)
{
    if (!raw) {
        return ADS_ERR_IO;
    }

    if (ADS_StartSingleShot(mux, pga) != ADS_OK) {
        return ADS_Fail(raw);
    }

    __delay_cycles(400000);
    return ADS_ReadConversionRaw(raw);
}
