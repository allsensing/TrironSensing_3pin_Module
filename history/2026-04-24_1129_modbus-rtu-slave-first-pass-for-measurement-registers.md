# 2026-04-24 11:29 KST - Modbus RTU slave first pass for measurement registers

## Summary
- Added UART RX interrupt buffering so Modbus requests are not lost during blocking sensor reads
- Added Modbus RTU slave handling for `FC03` and `FC04`
- Mapped read-only registers `0x0000` to `0x0007` to current measurement values
- Switched main loop from continuous debug print mode to measurement update plus Modbus polling mode
- Reused FRAM slave ID as Modbus slave address

## Register Map Implemented
- `0x0000` / `0x0001`: `CO_PPM` float high and low word
- `0x0002`: `VOUT_RAW` signed 16-bit
- `0x0003`: `VREF_RAW` signed 16-bit
- `0x0004` / `0x0005`: `TEMP_C` float high and low word
- `0x0006` / `0x0007`: `VS_mV` float high and low word

## Notes
- `CO_PPM` is calculated from FRAM zero/span calibration when both calibration flags are valid
- When calibration is not complete, `CO_PPM` returns `0.0`
- Debug UART text output was removed from the runtime path because it conflicts with Modbus RTU on the same port
