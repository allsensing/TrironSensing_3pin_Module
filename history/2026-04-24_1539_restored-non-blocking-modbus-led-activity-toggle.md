# 2026-04-24 15:39 KST - Restored non-blocking Modbus LED activity toggle

## Summary
- Re-enabled LED feedback for Modbus diagnostic events without adding delay loops
- The LED now toggles state on a captured Modbus diagnostic event instead of using blocking blink timing
- This helps verify whether request processing reaches the main loop while keeping communication latency low
