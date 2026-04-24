# 2026-04-24 14:28 KST - Explicit UCA1 RX pin configuration cleanup

## Summary
- Tightened `P4.2/P4.3` UART pin setup instead of relying on whole-port defaults
- Disabled `P4.1/P4.2/P4.3` pull resistors explicitly for the U3 select and UCA1 pins
- Forced `P4.2` to input and `P4.1/P4.3` to output before enabling the UART peripheral function
- Added startup printout of `P4SEL0`, `P4SEL1`, `P4DIR`, `P4REN`, and `P4OUT` for direct UART pin-state verification
