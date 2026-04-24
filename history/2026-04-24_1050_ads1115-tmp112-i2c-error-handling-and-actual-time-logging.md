# 2026-04-24 10:50 KST - ADS1115 TMP112 I2C error handling and actual time logging

## Summary
- Added ADS1115 status-based read path so I2C failures are not hidden as raw `0`
- Added TMP112 bus recovery through shared `I2C_BusReset()`
- Changed monitor time label to `t_act(s)` and print actual elapsed time
- Prevented TMP112 config readback failures from being logged as `PASS`

## Files Updated
- `ads1115.h`
- `ads1115.c`
- `TMP112x.c`
- `main.c`

## Validation Notes
- Check for `ADC-ERR(x/y)` on ADC communication failures
- Check shared bus recovery after TMP112 NACK or timeout
- Check that UART time output follows actual loop period instead of fixed 1 second steps
