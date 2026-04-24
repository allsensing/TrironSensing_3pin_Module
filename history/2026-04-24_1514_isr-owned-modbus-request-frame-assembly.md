# 2026-04-24 15:14 KST - ISR-owned Modbus request frame assembly

## Summary
- Moved Modbus RTU request assembly to the UART RX interrupt path
- The ISR now collects fixed 8-byte `FC03/FC04` requests and hands complete frames to the main loop
- The main loop now processes only completed request frames instead of building frames byte by byte
- This better matches the intended interrupt-driven architecture for stable periodic polling
