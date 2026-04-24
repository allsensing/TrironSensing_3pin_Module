# 2026-04-24 14:36 KST - P4.2 raw GPIO receive probe

## Summary
- Added a temporary raw GPIO probe mode on `P4.2` to separate pad-level input detection from the UCA1 UART peripheral
- Prints `[RX-PROBE]` over UART TX and blinks the LED whenever a falling edge is detected on `P4.2`
- Uses the existing boot UART TX path while temporarily disabling hardware UART RX on `P4.2`
- Goal is to confirm whether the external PC signal reaches the MCU pad electrically
