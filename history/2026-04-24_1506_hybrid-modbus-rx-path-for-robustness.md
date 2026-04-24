# 2026-04-24 15:06 KST - Hybrid Modbus RX path for robustness

## Summary
- Updated Modbus RTU receive handling to prefer the ISR ring buffer and fall back to direct `RXIFG` polling when the buffer is empty
- This keeps interrupt capture during blocking sensor work while also avoiding total receive loss if the ISR path is not servicing bytes as expected
- Preserved the same register map and response framing logic
