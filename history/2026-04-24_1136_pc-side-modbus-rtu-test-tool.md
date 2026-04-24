# 2026-04-24 11:36 KST - PC-side Modbus RTU test tool

## Summary
- Added a simple PowerShell PC test program for Modbus RTU read testing
- Supports `FC03` and `FC04`
- Builds request frame, validates CRC, prints raw registers, and decodes the implemented measurement map
- Does not require Python and can run directly in the current Windows environment
