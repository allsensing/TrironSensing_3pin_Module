# 2026-04-24 15:00 KST - UART RX interrupt restored for Modbus buffering

## Summary
- Re-enabled `UCA1` RX interrupt after confirming the UART hardware path is good
- Switched Modbus RTU receive handling back to the interrupt-driven ring buffer path
- Goal is to avoid intermittent request loss while the main loop is busy with sensor and I2C work
- Kept the verified UART pin configuration and startup summary in place
