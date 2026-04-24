# 2026-04-24 11:44 KST - Modbus read request immediate handling

## Summary
- Updated the Modbus RTU slave to process `FC03` and `FC04` read requests as soon as 8 request bytes are received
- Kept the inter-frame gap fallback logic for incomplete or unsupported frames
- Goal is to avoid timeout caused by depending only on gap-based frame end detection on UART
