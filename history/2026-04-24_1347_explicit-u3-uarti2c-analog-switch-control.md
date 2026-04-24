# 2026-04-24 13:47 KST - Explicit U3 UART/I2C analog switch control

## Summary
- Verified that `U3 IN1` and `IN2` are both tied to `UART-I2C-CS` on MCU `P4.1`
- Added explicit `P4.1` GPIO control instead of relying on the default `P4OUT = 0x00` state
- Forced the analog switch select line LOW during `GPIO_Init()` so the board starts in the UART path deterministically
- Added reusable macros for later `UART` and `I2C` path switching
