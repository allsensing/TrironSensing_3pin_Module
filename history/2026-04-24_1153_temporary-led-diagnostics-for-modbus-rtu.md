# 2026-04-24 11:53 KST - Temporary LED diagnostics for Modbus RTU

## Summary
- Added temporary Modbus diagnostic events and LED blink patterns
- 1 blink: read request frame detected
- 2 blinks: CRC passed
- 3 blinks: normal response transmit started
- 4 blinks: exception response transmit started
- Removed the old periodic LED toggle so the LED can be used only for Modbus diagnosis
