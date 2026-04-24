# 2026-04-24 11:10 KST - Extra UART startup settle time

## Summary
- Added extra startup delay after `UART_Init()`
- Increased leading blank lines before the startup banner
- Goal is to reduce stray first-character corruption right after reset or terminal attach
