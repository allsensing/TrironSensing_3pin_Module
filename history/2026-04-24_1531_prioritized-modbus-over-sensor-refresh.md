# 2026-04-24 15:31 KST - Prioritized Modbus over sensor refresh

## Summary
- Reduced communication collisions by deferring sensor refresh when UART traffic was seen recently
- Increased the measurement refresh interval from 0.5 s to 1.0 s to lower the chance of blocking communication
- Added a UART quiet-time guard before starting a new sensor update cycle
- Kept Modbus responses based on the latest cached measurement values
