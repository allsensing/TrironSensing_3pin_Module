# 2026-04-24 15:46 KST - Added UART ISR byte-count visibility

## Summary
- Added an ISR byte counter to confirm whether `EUSCI_A1_ISR` is actually receiving request bytes
- The main loop now toggles the LED when any UART RX ISR bytes were captured since the previous iteration
- This separates “ISR not firing” from “ISR fired but Modbus frame handling failed”
