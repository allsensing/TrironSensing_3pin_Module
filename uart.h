#ifndef UART_H
#define UART_H

#include <stdint.h>

void UART_Init(void);
void UART_SendChar(char c);
void UART_SendStr(const char *s);
void UART_SendUInt(unsigned int v);
void UART_SendInt(int v);
void UART_SendHex8(unsigned char v);
void UART_SendFloat(float v, unsigned char dec);
void UART_SendBuf(const uint8_t *data, unsigned int len);

unsigned char UART_ReadByte(uint8_t *data);
unsigned char UART_PollByte(uint8_t *data, uint16_t *status);
unsigned char UART_RxOverflowed(void);
void UART_ClearRxOverflow(void);
unsigned int UART_GetLastRxTick(void);
uint8_t UART_GetAndClearRxActivity(void);
uint8_t UART_GetAndClearRxErrors(void);
uint8_t UART_GetAndClearIsrByteCount(void);

#endif /* UART_H */
