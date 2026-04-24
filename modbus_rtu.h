#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stdint.h>

typedef struct {
    float   co_ppm;
    int16_t vout_raw;
    int16_t vref_raw;
    float   temp_c;
    float   vs_mV;
} ModbusRegsData_t;

void ModbusRTU_Poll(unsigned int now_ticks, uint8_t slave_id,
                    const ModbusRegsData_t *data);
void ModbusRTU_OnRxByte(uint8_t byte_in, unsigned int rx_tick);
uint8_t ModbusRTU_GetAndClearDiagEvent(void);

#endif /* MODBUS_RTU_H */
