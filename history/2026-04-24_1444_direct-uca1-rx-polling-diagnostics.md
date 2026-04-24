# 2026-04-24 14:44 KST - Direct UCA1 RX polling diagnostics

## Summary
- Switched the temporary receive diagnostic from GPIO probe mode back to the UCA1 UART peripheral
- Added direct polling of `UCA1 RXIFG` to check whether the hardware UART sees incoming bytes without relying on the RX interrupt path
- Enabled `UCRXEIE` so erroneous characters still reach software for diagnosis
- Added `[UART-RX]` logging with raw byte value, `STATW`, and immediate echo
