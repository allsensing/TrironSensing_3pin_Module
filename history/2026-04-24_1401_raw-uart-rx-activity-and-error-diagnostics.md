# 2026-04-24 14:01 KST - Raw UART RX activity and error diagnostics

## Summary
- Added raw UART RX activity diagnostics independent of Modbus frame parsing
- LED now gives short blinks when `UCA1` receives any bytes at all
- Added UART error capture for overrun, framing, parity, or break conditions
- LED now gives a separate long 2-blink pattern when UART receive errors are detected
