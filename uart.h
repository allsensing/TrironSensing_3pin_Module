/* ============================================================
 * uart.h - UART driver
 * UCA1 / P4.3(TXD) / P4.2(RXD) / 9600bps / 8MHz
 * ============================================================ */

#ifndef UART_H
#define UART_H

#include <stdint.h>

/* ─── 커맨드 버퍼 크기 ───────────────────────────────────── */
/* ASCII 명령 최대 길이: "FSPAN:1000" = 10자, 여유 포함 32자  */
#define UART_CMD_MAX_LEN    33u     /* 32 chars + null terminator */

/* ─── 초기화 / 송신 ──────────────────────────────────────── */
void UART_Init(void);
void UART_SendChar(char c);
void UART_SendStr(const char *s);
void UART_SendUInt(unsigned int v);
void UART_SendInt(int v);
void UART_SendHex8(unsigned char v);
void UART_SendFloat(float v, unsigned char dec);
void UART_SendBuf(const uint8_t *data, unsigned int len);

/* ─── 커맨드 수신 (ASCII 라인 기반) ─────────────────────── */

/**
 * @brief  완성된 커맨드 라인을 buf에 복사하고 버퍼를 초기화
 *         ISR에서 '\r' 또는 '\n' 수신 시 라인 완성
 * @return 복사된 바이트 수 (0이면 커맨드 없음)
 */
uint8_t UART_GetCommand(char *buf, uint8_t max_len);

/**
 * @brief  커맨드 버퍼 강제 초기화
 */
void UART_FlushCmdBuf(void);

/* ─── 진단 / 상태 ────────────────────────────────────────── */
unsigned char UART_ReadByte(uint8_t *data);
unsigned char UART_PollByte(uint8_t *data, uint16_t *status);
unsigned char UART_RxOverflowed(void);
void          UART_ClearRxOverflow(void);
unsigned int  UART_GetLastRxTick(void);
uint8_t       UART_GetAndClearRxActivity(void);
uint8_t       UART_GetAndClearRxErrors(void);
uint8_t       UART_GetAndClearIsrByteCount(void);

#endif /* UART_H */
