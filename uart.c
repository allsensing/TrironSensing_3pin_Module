/* ============================================================
 * uart.c - UART driver
 * UCA1 / P4.3(TXD) / P4.2(RXD) / 9600bps / 8MHz
 * ============================================================ */

#include <msp430.h>
#include <stdint.h>
#include "modbus_rtu.h"
#include "uart.h"

#define UART_RX_BUF_SIZE  64u

static volatile uint8_t g_uart_rx_overflow = 0u;
static volatile unsigned int g_uart_last_rx_tick = 0u;
static volatile uint8_t g_uart_rx_activity = 0u;
static volatile uint8_t g_uart_rx_errors = 0u;
static volatile uint8_t g_uart_isr_byte_count = 0u;

void UART_Init(void)
{
    UCA1CTLW0  = UCSWRST | UCSSEL__SMCLK | UCRXEIE;
    UCA1BR0    = 52;
    UCA1BR1    = 0;
    UCA1MCTLW  = 0x4900 | UCOS16 | UCBRF_1;
    UCA1IFG    = 0;
    UCA1CTLW0 &= ~UCSWRST;  /* Bug4 fix: TI 권장 순서대로 UCSWRST 해제 후 IE 설정 */
    UCA1IE     = UCRXIE;
}

void UART_SendChar(char c)
{
    while (!(UCA1IFG & UCTXIFG));
    UCA1TXBUF = (uint8_t)c;
}

void UART_SendBuf(const uint8_t *data, unsigned int len)
{
    unsigned int i;

    for (i = 0u; i < len; i++) {
        UART_SendChar((char)data[i]);
    }
}

void UART_SendStr(const char *s)
{
    while (*s) UART_SendChar(*s++);
}

void UART_SendUInt(unsigned int v)
{
    char buf[6];
    unsigned char i = 0;

    if (v == 0u) {
        UART_SendChar('0');
        return;
    }

    while (v) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }

    while (i) UART_SendChar(buf[--i]);
}

void UART_SendInt(int v)
{
    if (v < 0) {
        UART_SendChar('-');
        v = -v;
    }
    UART_SendUInt((unsigned int)v);
}

void UART_SendHex8(unsigned char v)
{
    const char h[] = "0123456789ABCDEF";

    UART_SendChar(h[(v >> 4) & 0x0Fu]);
    UART_SendChar(h[v & 0x0Fu]);
}

void UART_SendFloat(float v, unsigned char dec)
{
    unsigned char d;

    if (v < 0.0f) {
        UART_SendChar('-');
        v = -v;
    }

    UART_SendUInt((unsigned int)(long)v);
    if (dec == 0u) return;

    UART_SendChar('.');
    for (d = 0u; d < dec; d++) {
        v = (v - (float)(long)v) * 10.0f;
        UART_SendChar((char)('0' + (int)v));
    }
}

unsigned char UART_ReadByte(uint8_t *data)
{
    if (!data) return 0u;
    return 0u;
}

unsigned char UART_PollByte(uint8_t *data, uint16_t *status)
{
    uint16_t uart_status;

    if (!data) return 0u;
    if (!(UCA1IFG & UCRXIFG)) return 0u;

    uart_status = UCA1STATW;
    *data = UCA1RXBUF;
    if (status) {
        *status = uart_status;
    }

    return 1u;
}

unsigned char UART_RxOverflowed(void)
{
    return g_uart_rx_overflow;
}

void UART_ClearRxOverflow(void)
{
    g_uart_rx_overflow = 0u;
}

unsigned int UART_GetLastRxTick(void)
{
    return g_uart_last_rx_tick;
}

uint8_t UART_GetAndClearRxActivity(void)
{
    uint8_t activity;

    __disable_interrupt();
    activity = g_uart_rx_activity;
    g_uart_rx_activity = 0u;
    __enable_interrupt();

    return activity;
}

uint8_t UART_GetAndClearRxErrors(void)
{
    uint8_t errors;

    __disable_interrupt();
    errors = g_uart_rx_errors;
    g_uart_rx_errors = 0u;
    __enable_interrupt();

    return errors;
}

uint8_t UART_GetAndClearIsrByteCount(void)
{
    uint8_t count;

    __disable_interrupt();
    count = g_uart_isr_byte_count;
    g_uart_isr_byte_count = 0u;
    __enable_interrupt();

    return count;
}

#pragma vector=EUSCI_A1_VECTOR
__interrupt void EUSCI_A1_ISR(void)
{
    switch (__even_in_range(UCA1IV, 0x08u)) {
    case 0x00u:
        break;
    case UCIV__UCRXIFG:
    {
        uint16_t uart_statw = UCA1STATW; /* Bug1 fix: RXBUF 읽기 전에 STATW 저장 */
        uint8_t data = UCA1RXBUF;        /* RXBUF 읽으면 STATW 에러 플래그 클리어 */

        g_uart_last_rx_tick = TB0R;
        if (uart_statw & (UCOE | UCFE | UCPAR | UCBRK)) {
            g_uart_rx_errors |= (uint8_t)(uart_statw & (UCOE | UCFE | UCPAR | UCBRK));
        }
        if (g_uart_rx_activity < 0xFFu) {
            g_uart_rx_activity++;
        }
        if (g_uart_isr_byte_count < 0xFFu) {
            g_uart_isr_byte_count++;
        }

        ModbusRTU_OnRxByte(data, g_uart_last_rx_tick);
        break;
    }
    default:
        break;
    }
}
