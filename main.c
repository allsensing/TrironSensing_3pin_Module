/* ============================================================
 * main.c - CO sensor module with Modbus RTU slave
 * MSP430FR2155 / LMP91000 + ADS1115 + TMP112B + FRAM
 * ============================================================ */

// 테스트 깃허브 연동

#include <msp430.h>
#include "ads1115.h"
#include "clock.h"
#include "config.h"
#include "fram.h"
#include "gpio.h"
#include "i2c.h"
#include "lmp91000.h"
#include "modbus_rtu.h"
#include "tmp112x.h"
#include "uart.h"

#define SENSOR_UPDATE_INTERVAL_TICKS  4096u
#define MONITOR_TIMER_HZ              4096u
#define UART_ECHO_DIAG_MODE           0u
#define UART_RX_GPIO_PROBE_MODE       0u
#define UART_RX_RAW_POLL_MODE         0u
#define MODBUS_LED_DIAG_ENABLE        1u
#define MODBUS_RX_GUARD_TICKS         1024u
#define SCHED_TICK_COUNTS             4u
#define SENSOR_SAMPLE_PERIOD_TICKS    1024u  /* Bug5 fix: 실제 타이머 1024Hz에 맞춤 (정확히 1초, 롤오버도 1024 배수로 안전) */
#define ADS_CONVERSION_WAIT_MS        10u

typedef enum {
    SENSOR_TASK_IDLE = 0,
    SENSOR_TASK_ADS_REF_START,
    SENSOR_TASK_ADS_REF_WAIT,
    SENSOR_TASK_ADS_OUT_START,
    SENSOR_TASK_ADS_OUT_WAIT,
    SENSOR_TASK_TMP_READ
} SensorTaskState_t;

typedef struct {
    SensorTaskState_t state;
    unsigned long     deadline_ms;
    int               rvref;
    int               rvout;
} SensorTaskContext_t;

static volatile unsigned long g_system_tick_ms = 0ul;
static volatile uint16_t g_tick_divider = 0u;
static volatile uint8_t g_sensor_sample_due = 1u;
static volatile unsigned int g_last_uart_rx_tick_snapshot = 0u;
static SensorTaskContext_t g_sensor_task = { SENSOR_TASK_IDLE, 0ul, 0, 0 };

static void PrintInitResult(const char *label, uint8_t status)
{
    UART_SendStr("    ");
    UART_SendStr(label);
    UART_SendStr(": ");
    if (status == 0u) {
        UART_SendStr("OK\r\n");
    } else {
        UART_SendStr("ERR(");
        UART_SendUInt(status);
        UART_SendStr(")\r\n");
    }
}

static void PrintHexByteLine(const char *label, uint8_t value)
{
    UART_SendStr("    ");
    UART_SendStr(label);
    UART_SendStr(": 0x");
    UART_SendHex8(value);
    UART_SendStr("\r\n");
}

static void PrintStartupBanner(uint8_t fram_status, uint8_t lmp_status, uint8_t tmp_status)
{
    UART_SendStr("\r\n\r\n");
    UART_SendStr("=== CO Sensor System ===\r\n");
    UART_SendStr("MSP430FR2155 UART Modbus RTU Slave\r\n");
    UART_SendStr("========================\r\n");
    UART_SendStr("[BOOT] Startup summary\r\n");
    PrintInitResult("FRAM Load", fram_status);
    UART_SendStr("    Slave ID: ");
    UART_SendUInt(FRAM_GetSlaveID());
    UART_SendStr("\r\n");
    PrintInitResult("LMP91000 Init", lmp_status);
    PrintInitResult("TMP112 Init", tmp_status);
    UART_SendStr("    U3 Path: UART (P4.1 LOW)\r\n");
    PrintHexByteLine("P4SEL0", (uint8_t)P4SEL0);
    PrintHexByteLine("P4SEL1", (uint8_t)P4SEL1);
    PrintHexByteLine("P4DIR", (uint8_t)P4DIR);
    PrintHexByteLine("P4REN", (uint8_t)P4REN);
    PrintHexByteLine("P4OUT", (uint8_t)P4OUT);
    UART_SendStr("    Modbus: Ready\r\n\r\n");
}

static unsigned long System_GetTickMs(void)
{
    unsigned long tick_ms;

    __disable_interrupt();
    tick_ms = g_system_tick_ms;
    __enable_interrupt();

    return tick_ms;
}

static unsigned char TickMsReached(unsigned long now_ms, unsigned long deadline_ms)
{
    return ((long)(now_ms - deadline_ms) >= 0L) ? 1u : 0u;
}

static unsigned int TicksElapsed16(unsigned int now, unsigned int then)
{
    return (unsigned int)(now - then);
}

static float ClampFloat(float value, float min_value, float max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static float Calc_CO_Ppm(int16_t vout_raw)
{
    const FRAM_Data_t *fram = FRAM_GetData();
    float span_ppm;
    float ratio;
    int16_t zero_raw;
    int16_t span_raw;

    if ((fram->cal_flags & (FRAM_CAL_ZERO_DONE | FRAM_CAL_SPAN_DONE))
        != (FRAM_CAL_ZERO_DONE | FRAM_CAL_SPAN_DONE)) {
        return 0.0f;
    }

    zero_raw = fram->zero_raw;
    span_raw = fram->span_raw;
    if (span_raw == zero_raw) {
        return 0.0f;
    }

    span_ppm = (float)fram->span_ppm_x10 / 10.0f;
    ratio = (float)(vout_raw - zero_raw) / (float)(span_raw - zero_raw);
    return ClampFloat(ratio * span_ppm, 0.0f, 10000.0f);
}

static void UpdateCalculatedValues(ModbusRegsData_t *data, int rvref, int rvout)
{
    float vref;
    float vout;
    float vzero;
    vref = ADS_RawToVolt(rvref);
    vout = ADS_RawToVolt(rvout);
    vzero = ADS_VZERO(vref);

    data->vref_raw = (int16_t)rvref;
    data->vout_raw = (int16_t)rvout;
    data->vs_mV = (vout - vzero) * 1000.0f;
    data->co_ppm = Calc_CO_Ppm((int16_t)rvout);
}

static void SensorTask_Reset(void)
{
    ALL_DESEL();
    g_sensor_task.state = SENSOR_TASK_IDLE;
    g_sensor_task.deadline_ms = 0ul;
}

static void SensorTask_Run(ModbusRegsData_t *data, unsigned long now_ms)
{
    float temp_c;

    switch (g_sensor_task.state) {
    case SENSOR_TASK_IDLE:
        if (g_sensor_sample_due != 0u) {
            g_sensor_sample_due = 0u;
            g_sensor_task.state = SENSOR_TASK_ADS_REF_START;
        }
        break;

    case SENSOR_TASK_ADS_REF_START:
        ADS_SEL();
        if (ADS_StartSingleShot(4u, ADS_PGA_USE) == ADS_OK) {
            g_sensor_task.deadline_ms = now_ms + ADS_CONVERSION_WAIT_MS;
            g_sensor_task.state = SENSOR_TASK_ADS_REF_WAIT;
        } else {
            SensorTask_Reset();
        }
        break;

    case SENSOR_TASK_ADS_REF_WAIT:
        if (TickMsReached(now_ms, g_sensor_task.deadline_ms)) {
            if (ADS_ReadConversionRaw(&g_sensor_task.rvref) == ADS_OK) {
                g_sensor_task.state = SENSOR_TASK_ADS_OUT_START;
            } else {
                SensorTask_Reset();
            }
        }
        break;

    case SENSOR_TASK_ADS_OUT_START:
        if (ADS_StartSingleShot(5u, ADS_PGA_USE) == ADS_OK) {
            g_sensor_task.deadline_ms = now_ms + ADS_CONVERSION_WAIT_MS;
            g_sensor_task.state = SENSOR_TASK_ADS_OUT_WAIT;
        } else {
            SensorTask_Reset();
        }
        break;

    case SENSOR_TASK_ADS_OUT_WAIT:
        if (TickMsReached(now_ms, g_sensor_task.deadline_ms)) {
            if (ADS_ReadConversionRaw(&g_sensor_task.rvout) == ADS_OK) {
                UpdateCalculatedValues(data, g_sensor_task.rvref, g_sensor_task.rvout);
                ALL_DESEL();
                g_sensor_task.state = SENSOR_TASK_TMP_READ;
            } else {
                SensorTask_Reset();
            }
        }
        break;

    case SENSOR_TASK_TMP_READ:
        if (TMP112_ReadTemp(&temp_c) == TMP112_OK) {
            data->temp_c = temp_c;
        }
        SensorTask_Reset();
        break;

    default:
        SensorTask_Reset();
        break;
    }
}

static void BlinkUartError(uint8_t error_flags)
{
    if (error_flags == 0u) {
        return;
    }

    LED_ON();
    __delay_cycles(500000);
    LED_OFF();
    __delay_cycles(200000);
    LED_ON();
    __delay_cycles(500000);
    LED_OFF();
    __delay_cycles(500000);
}

static void HandleModbusDiagEvent(uint8_t event_code)
{
    if (event_code == 0u) {
        return;
    }

    LED_TOGGLE();
}

int main(void)
{
    ModbusRegsData_t regs = {0.0f, 0, 0, 0.0f, 0.0f};
    uint8_t fram_status;
    uint8_t lmp_status;
    uint8_t tmp_status;

    WDTCTL = WDTPW | WDTHOLD;

    GPIO_Init();
    PM5CTL0 &= ~LOCKLPM5;
    Clock_Init();

    TB0CTL = TBSSEL__ACLK | ID__8 | MC__CONTINUOUS | TBCLR;
    TB0CCR0 = (unsigned int)(TB0R + SCHED_TICK_COUNTS);
    TB0CCTL0 = CCIE;

    UART_Init();
    __delay_cycles(160000);
    I2C_Init();
    fram_status = FRAM_Load();

    LMP_SEL();
    lmp_status = LMP_Init();
    ALL_DESEL();
    I2C_BusReset();

    ALL_DESEL();
    __delay_cycles(400000);
    tmp_status = TMP112_Init();

    PrintStartupBanner(fram_status, lmp_status, tmp_status);

    __enable_interrupt();

    while (1) {
        unsigned int now_ticks;
        unsigned long now_ms = System_GetTickMs();
        uint8_t diag_event;
        uint8_t isr_byte_count;
        uint8_t uart_rx_errors;

        /* Bug2 fix: rx_tick 스냅샷을 now_ticks보다 먼저 읽어야 함
         * 반대 순서면 ISR이 사이에 끼어 rx_tick > now_ticks 언더플로우 발생,
         * 가드 조건이 거짓 양성이 되어 센서 작업이 Modbus 수신 중에 실행될 수 있음 */
        g_last_uart_rx_tick_snapshot = UART_GetLastRxTick();
        now_ticks = TB0R;

        ModbusRTU_Poll(now_ticks, FRAM_GetSlaveID(), &regs);
        isr_byte_count = UART_GetAndClearIsrByteCount();
        uart_rx_errors = UART_GetAndClearRxErrors();
        if (isr_byte_count != 0u) {
            LED_TOGGLE();
        }
        if (uart_rx_errors != 0u) {
            BlinkUartError(uart_rx_errors);
        }
        diag_event = ModbusRTU_GetAndClearDiagEvent();
        if (diag_event != 0u) {
            HandleModbusDiagEvent(diag_event);
        }

        if (TicksElapsed16(now_ticks, g_last_uart_rx_tick_snapshot) >= MODBUS_RX_GUARD_TICKS) {
            SensorTask_Run(&regs, now_ms);
        }
    }
}

#pragma vector=TIMER0_B0_VECTOR
__interrupt void TIMER0_B0_ISR(void)
{
    TB0CCR0 = (unsigned int)(TB0CCR0 + SCHED_TICK_COUNTS);

    g_tick_divider++;
    g_system_tick_ms++;

    if ((g_tick_divider % SENSOR_SAMPLE_PERIOD_TICKS) == 0u) {
        g_sensor_sample_due = 1u;
    }
}
