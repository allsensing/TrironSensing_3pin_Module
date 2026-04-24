# 2026-04-24 14:19 KST - Temporary UART echo diagnostic mode

## Summary
- Added a temporary UART echo mode for direct PC to MCU to PC path testing
- Echoes received bytes back on the same `UCA1` port after startup
- Pauses Modbus RTU handling while the echo diagnostic is enabled
- Keeps the raw RX LED diagnostics active during the echo test
