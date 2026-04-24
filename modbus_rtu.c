#include <stdint.h>
#include "fram.h"
#include "modbus_rtu.h"
#include "uart.h"

#define MODBUS_MAX_FRAME_LEN     64u
#define MODBUS_REG_COUNT         8u
#define MODBUS_MAX_READ_COUNT    8u
#define MODBUS_RTU_GAP_TICKS     15u  /* Bug3 fix: 9600bps 3.5char=14.95틱 → 15틱으로 사양에 맞춤 (기존 18틱은 너무 커 프레임 경계 미감지 위험) */
#define MODBUS_READ_REQ_LEN      8u

#define MODBUS_DIAG_RX_FRAME     1u
#define MODBUS_DIAG_CRC_OK       2u
#define MODBUS_DIAG_TX_RESP      3u
#define MODBUS_DIAG_TX_EXC       4u

static uint8_t g_modbus_frame[MODBUS_MAX_FRAME_LEN];
static volatile uint8_t g_modbus_diag_event = 0u;
static volatile uint8_t g_modbus_isr_frame[MODBUS_READ_REQ_LEN];
static volatile uint8_t g_modbus_isr_len = 0u;
static volatile unsigned int g_modbus_isr_last_tick = 0u;
static volatile uint8_t g_modbus_frame_ready = 0u;

static void ProcessFrame(uint8_t frame_len, uint8_t slave_id,
                         const ModbusRegsData_t *data);

static unsigned int TicksElapsed16(unsigned int now, unsigned int then)
{
    return (unsigned int)(now - then);
}

static void SetDiagEvent(uint8_t event_code)
{
    g_modbus_diag_event = event_code;
}

static uint16_t FloatWord(float value, unsigned char high_word)
{
    union {
        float f;
        uint32_t u32;
    } cvt;

    cvt.f = value;
    if (high_word) {
        return (uint16_t)(cvt.u32 >> 16);
    }
    return (uint16_t)(cvt.u32 & 0xFFFFu);
}

static unsigned char BuildRegisters(const ModbusRegsData_t *data,
                                    uint16_t start_addr,
                                    uint16_t reg_count,
                                    uint16_t *regs)
{
    uint16_t i;

    if (!data || !regs) return 0u;
    if ((start_addr + reg_count) > MODBUS_REG_COUNT) return 0u;

    for (i = 0u; i < reg_count; i++) {
        switch ((uint16_t)(start_addr + i)) {
        case 0u:
            regs[i] = FloatWord(data->co_ppm, 1u);
            break;
        case 1u:
            regs[i] = FloatWord(data->co_ppm, 0u);
            break;
        case 2u:
            regs[i] = (uint16_t)data->vout_raw;
            break;
        case 3u:
            regs[i] = (uint16_t)data->vref_raw;
            break;
        case 4u:
            regs[i] = FloatWord(data->temp_c, 1u);
            break;
        case 5u:
            regs[i] = FloatWord(data->temp_c, 0u);
            break;
        case 6u:
            regs[i] = FloatWord(data->vs_mV, 1u);
            break;
        case 7u:
            regs[i] = FloatWord(data->vs_mV, 0u);
            break;
        default:
            return 0u;
        }
    }

    return 1u;
}

static void SendException(uint8_t slave_id, uint8_t function_code, uint8_t exception_code)
{
    uint8_t resp[5];
    uint16_t crc;

    resp[0] = slave_id;
    resp[1] = (uint8_t)(function_code | 0x80u);
    resp[2] = exception_code;
    crc = FRAM_CRC16(resp, 3u);
    resp[3] = (uint8_t)(crc & 0xFFu);
    resp[4] = (uint8_t)(crc >> 8);
    SetDiagEvent(MODBUS_DIAG_TX_EXC);
    UART_SendBuf(resp, 5u);
}

static void HandleReadRegisters(const uint8_t *frame, uint8_t slave_id,
                                const ModbusRegsData_t *data)
{
    uint16_t start_addr;
    uint16_t reg_count;
    uint16_t regs[MODBUS_MAX_READ_COUNT];
    uint8_t resp[3u + (MODBUS_MAX_READ_COUNT * 2u) + 2u];
    uint8_t byte_count;
    uint8_t i;
    uint16_t crc;

    start_addr = (uint16_t)(((uint16_t)frame[2] << 8) | frame[3]);
    reg_count  = (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]);

    if ((reg_count == 0u) || (reg_count > MODBUS_MAX_READ_COUNT)) {
        SendException(slave_id, frame[1], 0x03u);
        return;
    }

    if (!BuildRegisters(data, start_addr, reg_count, regs)) {
        SendException(slave_id, frame[1], 0x02u);
        return;
    }

    byte_count = (uint8_t)(reg_count * 2u);
    resp[0] = slave_id;
    resp[1] = frame[1];
    resp[2] = byte_count;

    for (i = 0u; i < reg_count; i++) {
        resp[3u + (2u * i)] = (uint8_t)(regs[i] >> 8);
        resp[4u + (2u * i)] = (uint8_t)(regs[i] & 0xFFu);
    }

    crc = FRAM_CRC16(resp, (uint16_t)(3u + byte_count));
    resp[3u + byte_count] = (uint8_t)(crc & 0xFFu);
    resp[4u + byte_count] = (uint8_t)(crc >> 8);
    SetDiagEvent(MODBUS_DIAG_TX_RESP);
    UART_SendBuf(resp, (unsigned int)(5u + byte_count));
}

static void ProcessFrame(uint8_t frame_len, uint8_t slave_id,
                         const ModbusRegsData_t *data)
{
    uint16_t crc_calc;
    uint16_t crc_recv;

    if (frame_len < 4u) return;
    if ((g_modbus_frame[0] != slave_id) || (slave_id == 0u)) return;

    crc_calc = FRAM_CRC16(g_modbus_frame, (uint16_t)(frame_len - 2u));
    crc_recv = (uint16_t)(((uint16_t)g_modbus_frame[frame_len - 1u] << 8)
               | g_modbus_frame[frame_len - 2u]);
    if (crc_calc != crc_recv) return;

    SetDiagEvent(MODBUS_DIAG_CRC_OK);

    switch (g_modbus_frame[1]) {
    case 0x03u:
    case 0x04u:
        if (frame_len != 8u) {
            SendException(slave_id, g_modbus_frame[1], 0x03u);
            return;
        }
        HandleReadRegisters(g_modbus_frame, slave_id, data);
        break;
    default:
        SendException(slave_id, g_modbus_frame[1], 0x01u);
        break;
    }
}

void ModbusRTU_Poll(unsigned int now_ticks, uint8_t slave_id,
                    const ModbusRegsData_t *data)
{
    uint8_t i;
    uint8_t frame_ready;

    (void)now_ticks;

    if (UART_RxOverflowed()) {
        __disable_interrupt();
        g_modbus_isr_len = 0u;
        g_modbus_frame_ready = 0u;
        __enable_interrupt();
        UART_ClearRxOverflow();
    }

    __disable_interrupt();
    frame_ready = g_modbus_frame_ready;
    if (frame_ready != 0u) {
        for (i = 0u; i < MODBUS_READ_REQ_LEN; i++) {
            g_modbus_frame[i] = g_modbus_isr_frame[i];
        }
        g_modbus_frame_ready = 0u;
    }
    __enable_interrupt();

    if (frame_ready != 0u) {
        SetDiagEvent(MODBUS_DIAG_RX_FRAME);
        ProcessFrame(MODBUS_READ_REQ_LEN, slave_id, data);
    }
}

void ModbusRTU_OnRxByte(uint8_t byte_in, unsigned int rx_tick)
{
    if (TicksElapsed16(rx_tick, g_modbus_isr_last_tick) >= MODBUS_RTU_GAP_TICKS) {
        g_modbus_isr_len = 0u;
    }
    g_modbus_isr_last_tick = rx_tick;

    if (g_modbus_frame_ready != 0u) {
        return;
    }

    if (g_modbus_isr_len >= MODBUS_READ_REQ_LEN) {
        g_modbus_isr_len = 0u;
    }

    g_modbus_isr_frame[g_modbus_isr_len++] = byte_in;

    if (g_modbus_isr_len == MODBUS_READ_REQ_LEN) {
        g_modbus_frame_ready = 1u;
        g_modbus_isr_len = 0u;
    }
}

uint8_t ModbusRTU_GetAndClearDiagEvent(void)
{
    uint8_t event_code = g_modbus_diag_event;
    g_modbus_diag_event = 0u;
    return event_code;
}
