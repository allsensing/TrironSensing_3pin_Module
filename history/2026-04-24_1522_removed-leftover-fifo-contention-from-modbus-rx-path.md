# 2026-04-24 15:22 KST - Removed leftover FIFO contention from Modbus RX path

## Summary
- Removed the old UART software FIFO storage from the active Modbus RX path
- The UART ISR now forwards bytes only to the Modbus request assembler instead of filling an unused buffer
- Disabled per-request LED blink diagnostics during normal Modbus runtime to avoid unnecessary response latency
- Kept UART error indication active for fault visibility
