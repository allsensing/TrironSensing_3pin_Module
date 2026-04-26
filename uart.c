/* ============================================================
 * uart.c - UART driver
 * UCA1 / P4.3(TXD) / P4.2(RXD) / 9600bps / 8MHz
 *
 * Rev History
 * -----------
 * REV 0.3  2026-04-26  ASCII 커맨드 라인 버퍼 추가
 *                      Modbus RTU OnRxByte 호출 제거 (hold)
 * REV 0.2  2026-04-24  Bug1(STATW 순서), Bug4(IE 초기화 순서) 수정
 * ============================================================ */

#include <msp430.h>
#include <stdint.h>
#include "uart.h"

/* ─── 커맨드 라인 버퍼 ───────────────────────────────────── */
#define UART_CMD_BUF_SIZE   33u     /* 32 chars + null */

static volatile char    g_cmd_buf[UART_CMD_BUF_SIZE];
static volatile uint8_t g_cmd_len   = 0u;
static volatile uint8_t g_cmd_ready = 0u;

/* ─── 진단용 변수 ────────────────────────────────────────── */
static volatile uint8_t      g_uart_rx_overflow    = 0u;
static volatile unsigned int g_uart_last_rx_tick   = 0u;
static volatile uint8_t      g_uart_rx_activity    = 0u;
static volatile uint8_t      g_uart_rx_errors      = 0u;
static volatile uint8_t      g_uart_isr_byte_count = 0u;

/* ============================================================
 *  초기화
 * ============================================================ */
void UART_Init(void)
{
    UCA1CTLW0  = UCSWRST | UCSSEL__SMCLK | UCRXEIE;
    UCA1BR0    = 52;
    UCA1BR1    = 0;
    UCA1MCTLW  = 0x4900 | UCOS16 | UCBRF_1;
    UCA1IFG    = 0;
    UCA1CTLW0 &= ~UCSWRST;   /* Bug4 fix: UCSWRST 해제 후 IE 설정 */
    UCA1IE     = UCRXIE;
}

/* ============================================================
 *  송신
 * ============================================================ */
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
    unsigned char i = 0u;

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

/* ============================================================
 *  커맨드 수신 (ASCII 라인 기반)
 * ============================================================ */

uint8_t UART_GetCommand(char *buf, uint8_t max_len)
{
    uint8_t i, len;

    if (!buf || max_len == 0u || !g_cmd_ready) {
        return 0u;
    }

    __disable_interrupt();
    len = (g_cmd_len < (uint8_t)(max_len - 1u))
          ? g_cmd_len
          : (uint8_t)(max_len - 1u);

    for (i = 0u; i < len; i++) {
        buf[i] = g_cmd_buf[i];
    }
    buf[len]    = '\0';
    g_cmd_len   = 0u;
    g_cmd_ready = 0u;
    __enable_interrupt();

    return len;
}

void UART_FlushCmdBuf(void)
{
    __disable_interrupt();
    g_cmd_len   = 0u;
    g_cmd_ready = 0u;
    __enable_interrupt();
}

/* ============================================================
 *  직접 폴링 수신 (진단용)
 * ============================================================ */
unsigned char UART_ReadByte(uint8_t *data)
{
    if (!data) return 0u;
    return 0u;  /* stub */
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

/* ============================================================
 *  진단 / 상태
 * ============================================================ */
unsigned char UART_RxOverflowed(void)   { return g_uart_rx_overflow; }
void UART_ClearRxOverflow(void)         { g_uart_rx_overflow = 0u; }
unsigned int UART_GetLastRxTick(void)   { return g_uart_last_rx_tick; }

uint8_t UART_GetAndClearRxActivity(void)
{
    uint8_t v;
    __disable_interrupt();
    v = g_uart_rx_activity;
    g_uart_rx_activity = 0u;
    __enable_interrupt();
    return v;
}

uint8_t UART_GetAndClearRxErrors(void)
{
    uint8_t v;
    __disable_interrupt();
    v = g_uart_rx_errors;
    g_uart_rx_errors = 0u;
    __enable_interrupt();
    return v;
}

uint8_t UART_GetAndClearIsrByteCount(void)
{
    uint8_t v;
    __disable_interrupt();
    v = g_uart_isr_byte_count;
    g_uart_isr_byte_count = 0u;
    __enable_interrupt();
    return v;
}

/* ============================================================
 *  UART RX ISR
 * ============================================================ */
#pragma vector=EUSCI_A1_VECTOR
__interrupt void EUSCI_A1_ISR(void)
{
    switch (__even_in_range(UCA1IV, 0x08u)) {
    case 0x00u:
        break;

    case UCIV__UCRXIFG:
    {
        uint16_t uart_statw = UCA1STATW; /* Bug1 fix: RXBUF 전에 STATW 저장 */
        uint8_t  data       = UCA1RXBUF; /* 읽으면 STATW 에러 플래그 클리어 */

        g_uart_last_rx_tick = TB0R;

        /* ── 에러 감지 ── */
        if (uart_statw & (UCOE | UCFE | UCPAR | UCBRK)) {
            g_uart_rx_errors |= (uint8_t)(uart_statw & (UCOE | UCFE | UCPAR | UCBRK));
        }

        /* ── 진단 카운터 ── */
        if (g_uart_rx_activity < 0xFFu)    { g_uart_rx_activity++; }
        if (g_uart_isr_byte_count < 0xFFu) { g_uart_isr_byte_count++; }

        /* ── ASCII 커맨드 라인 버퍼 ──────────────────────────
         * g_cmd_ready 중에는 새 바이트 무시
         * (main loop의 UART_GetCommand 호출 후 자동 해제)    */
        if (g_cmd_ready == 0u) {
            if (data == (uint8_t)'\r' || data == (uint8_t)'\n') {
                if (g_cmd_len > 0u) {
                    g_cmd_buf[g_cmd_len] = '\0';
                    g_cmd_ready = 1u;
                }
            } else if (data == 0x08u || data == 0x7Fu) {
                /* Backspace / DEL */
                if (g_cmd_len > 0u) { g_cmd_len--; }
            } else if (data >= 0x20u && data < 0x7Fu) {
                if (g_cmd_len < (UART_CMD_BUF_SIZE - 1u)) {
                    g_cmd_buf[g_cmd_len++] = (char)data;
                }
            }
        }

        /* Modbus RTU: 홀딩 (코드 유지, 호출 비활성화) */
        /* ModbusRTU_OnRxByte(data, g_uart_last_rx_tick); */

        break;
    }

    default:
        break;
    }
}
