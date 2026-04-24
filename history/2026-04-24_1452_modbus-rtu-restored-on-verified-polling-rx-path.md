# 2026-04-24 14:52 KST - Modbus RTU restored on verified polling RX path

## Summary
- Removed the temporary echo and probe diagnostics from the runtime path
- Switched Modbus RTU receive handling to the verified direct `UCA1 RXIFG` polling path
- Disabled UART RX interrupts so polling and ISR buffering do not compete for received bytes
- Restored normal startup message with `Modbus: Ready`
