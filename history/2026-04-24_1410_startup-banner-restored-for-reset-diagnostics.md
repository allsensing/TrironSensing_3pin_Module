# 2026-04-24 14:10 KST - Startup banner restored for reset diagnostics

## Summary
- Restored one-time UART startup messages after reset
- Added a short boot summary for `FRAM`, `LMP91000`, and `TMP112` initialization
- Printed the active `Slave ID` and confirmed the `U3` path is forced to UART on startup
- Kept runtime UART quiet after boot so Modbus RTU can still use the same port
