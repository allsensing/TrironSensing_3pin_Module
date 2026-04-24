#ifndef ADS1115_H
#define ADS1115_H

/* ============================================================
 * ads1115.h - ADS1115 16-bit ADC driver
 * I2C address: 0x49 / CS: P3.2 (Active-HIGH)
 * ============================================================ */

/* Register addresses */
#define ADS_REG_CONV    0x00u
#define ADS_REG_CFG     0x01u
#define ADS_REG_LOTH    0x02u
#define ADS_REG_HITH    0x03u

/* PGA settings */
#define ADS_PGA0        0u
#define ADS_PGA1        1u
#define ADS_PGA2        2u
#define ADS_PGA3        3u
#define ADS_PGA4        4u
#define ADS_PGA5        5u

/* Project defaults */
#define ADS_PGA_USE     ADS_PGA2
#define ADS_LSB_UV      62.5f
#define ADS_LSB_V       (ADS_LSB_UV / 1000000.0f)
#define ADS_VZERO(vref) ((vref) * 0.5f)

/* Return codes */
#define ADS_OK          0u
#define ADS_ERR_IO      1u

void ADS_Init(void);
int  ADS_ReadCh(unsigned char mux);
int  ADS_ReadChPGA(unsigned char mux, unsigned char pga);
unsigned char ADS_ReadChRaw(unsigned char mux, int *raw);
unsigned char ADS_ReadChPGA_Raw(unsigned char mux, unsigned char pga, int *raw);
unsigned char ADS_StartSingleShot(unsigned char mux, unsigned char pga);
unsigned char ADS_SetPointer(unsigned char reg);
unsigned char ADS_ReadConversionRaw(int *raw);

static inline float ADS_RawToVolt(int raw)
{
    return (float)raw * ADS_LSB_V;
}

#endif /* ADS1115_H */
