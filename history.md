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

# 2026-04-24 11:07 KST - Hardware run confirmed and UART banner sync mitigation

## Summary
- Confirmed normal hardware operation from UART log
- Verified TMP112 init and readback are normal
- Verified `t_act(s)` increases by real loop time, about 2.28 s per cycle in current setup
- Added a leading CRLF before the startup banner to reduce occasional first-character corruption at reset/startup

## Observed Log Highlights
- `FRAM restore OK`
- `LMP91000` init and verify passed
- `TMP112B` config readback passed
- Monitor loop reported stable `VREF`, `VOUT`, `VS`, and temperature values

# 2026-04-24 11:10 KST - Extra UART startup settle time

## Summary
- Added extra startup delay after `UART_Init()`
- Increased leading blank lines before the startup banner
- Goal is to reduce stray first-character corruption right after reset or terminal attach

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

# 2026-04-24 11:36 KST - PC-side Modbus RTU test tool

## Summary
- Added a simple PowerShell PC test program for Modbus RTU read testing
- Supports `FC03` and `FC04`
- Builds request frame, validates CRC, prints raw registers, and decodes the implemented measurement map
- Does not require Python and can run directly in the current Windows environment

# 2026-04-24 11:36 KST - Separate UI test folder

## Summary
- Added a separate `pc_modbus_ui` folder for the PC-side UI tool
- Added a simple WinForms Modbus RTU test app in PowerShell
- Included COM port selection, one-shot read, watch mode, raw frame log, and decoded measurement display

# 2026-04-24 11:44 KST - Modbus read request immediate handling

## Summary
- Updated the Modbus RTU slave to process `FC03` and `FC04` read requests as soon as 8 request bytes are received
- Kept the inter-frame gap fallback logic for incomplete or unsupported frames
- Goal is to avoid timeout caused by depending only on gap-based frame end detection on UART

# 2026-04-24 11:53 KST - Temporary LED diagnostics for Modbus RTU

## Summary
- Added temporary Modbus diagnostic events and LED blink patterns
- 1 blink: read request frame detected
- 2 blinks: CRC passed
- 3 blinks: normal response transmit started
- 4 blinks: exception response transmit started
- Removed the old periodic LED toggle so the LED can be used only for Modbus diagnosis

# 2026-04-24 13:47 KST - Explicit U3 UART/I2C analog switch control

## Summary
- Verified that `U3 IN1` and `IN2` are both tied to `UART-I2C-CS` on MCU `P4.1`
- Added explicit `P4.1` GPIO control instead of relying on the default `P4OUT = 0x00` state
- Forced the analog switch select line LOW during `GPIO_Init()` so the board starts in the UART path deterministically
- Added reusable macros for later `UART` and `I2C` path switching

# 2026-04-24 14:01 KST - Raw UART RX activity and error diagnostics

## Summary
- Added raw UART RX activity diagnostics independent of Modbus frame parsing
- LED now gives short blinks when `UCA1` receives any bytes at all
- Added UART error capture for overrun, framing, parity, or break conditions
- LED now gives a separate long 2-blink pattern when UART receive errors are detected

# 2026-04-24 14:10 KST - Startup banner restored for reset diagnostics

## Summary
- Restored one-time UART startup messages after reset
- Added a short boot summary for `FRAM`, `LMP91000`, and `TMP112` initialization
- Printed the active `Slave ID` and confirmed the `U3` path is forced to UART on startup
- Kept runtime UART quiet after boot so Modbus RTU can still use the same port

# 2026-04-24 14:19 KST - Temporary UART echo diagnostic mode

## Summary
- Added a temporary UART echo mode for direct PC to MCU to PC path testing
- Echoes received bytes back on the same `UCA1` port after startup
- Pauses Modbus RTU handling while the echo diagnostic is enabled
- Keeps the raw RX LED diagnostics active during the echo test

# 2026-04-24 14:28 KST - Explicit UCA1 RX pin configuration cleanup

## Summary
- Tightened `P4.2/P4.3` UART pin setup instead of relying on whole-port defaults
- Disabled `P4.1/P4.2/P4.3` pull resistors explicitly for the U3 select and UCA1 pins
- Forced `P4.2` to input and `P4.1/P4.3` to output before enabling the UART peripheral function
- Added startup printout of `P4SEL0`, `P4SEL1`, `P4DIR`, `P4REN`, and `P4OUT` for direct UART pin-state verification

# 2026-04-24 14:36 KST - P4.2 raw GPIO receive probe

## Summary
- Added a temporary raw GPIO probe mode on `P4.2` to separate pad-level input detection from the UCA1 UART peripheral
- Prints `[RX-PROBE]` over UART TX and blinks the LED whenever a falling edge is detected on `P4.2`
- Uses the existing boot UART TX path while temporarily disabling hardware UART RX on `P4.2`
- Goal is to confirm whether the external PC signal reaches the MCU pad electrically

# 2026-04-24 14:44 KST - Direct UCA1 RX polling diagnostics

## Summary
- Switched the temporary receive diagnostic from GPIO probe mode back to the UCA1 UART peripheral
- Added direct polling of `UCA1 RXIFG` to check whether the hardware UART sees incoming bytes without relying on the RX interrupt path
- Enabled `UCRXEIE` so erroneous characters still reach software for diagnosis
- Added `[UART-RX]` logging with raw byte value, `STATW`, and immediate echo

# 2026-04-24 14:52 KST - Modbus RTU restored on verified polling RX path

## Summary
- Removed the temporary echo and probe diagnostics from the runtime path
- Switched Modbus RTU receive handling to the verified direct `UCA1 RXIFG` polling path
- Disabled UART RX interrupts so polling and ISR buffering do not compete for received bytes
- Restored normal startup message with `Modbus: Ready`

# 2026-04-24 15:00 KST - UART RX interrupt restored for Modbus buffering

## Summary
- Re-enabled `UCA1` RX interrupt after confirming the UART hardware path is good
- Switched Modbus RTU receive handling back to the interrupt-driven ring buffer path
- Goal is to avoid intermittent request loss while the main loop is busy with sensor and I2C work
- Kept the verified UART pin configuration and startup summary in place

# 2026-04-24 15:06 KST - Hybrid Modbus RX path for robustness

## Summary
- Updated Modbus RTU receive handling to prefer the ISR ring buffer and fall back to direct `RXIFG` polling when the buffer is empty
- This keeps interrupt capture during blocking sensor work while also avoiding total receive loss if the ISR path is not servicing bytes as expected
- Preserved the same register map and response framing logic

# 2026-04-24 15:14 KST - ISR-owned Modbus request frame assembly

## Summary
- Moved Modbus RTU request assembly to the UART RX interrupt path
- The ISR now collects fixed 8-byte `FC03/FC04` requests and hands complete frames to the main loop
- The main loop now processes only completed request frames instead of building frames byte by byte
- This better matches the intended interrupt-driven architecture for stable periodic polling

# 2026-04-24 15:22 KST - Removed leftover FIFO contention from Modbus RX path

## Summary
- Removed the old UART software FIFO storage from the active Modbus RX path
- The UART ISR now forwards bytes only to the Modbus request assembler instead of filling an unused buffer
- Disabled per-request LED blink diagnostics during normal Modbus runtime to avoid unnecessary response latency
- Kept UART error indication active for fault visibility

# 2026-04-24 15:31 KST - Prioritized Modbus over sensor refresh

## Summary
- Reduced communication collisions by deferring sensor refresh when UART traffic was seen recently
- Increased the measurement refresh interval from 0.5 s to 1.0 s to lower the chance of blocking communication
- Added a UART quiet-time guard before starting a new sensor update cycle
- Kept Modbus responses based on the latest cached measurement values

# 2026-04-24 15:39 KST - Restored non-blocking Modbus LED activity toggle

## Summary
- Re-enabled LED feedback for Modbus diagnostic events without adding delay loops
- The LED now toggles state on a captured Modbus diagnostic event instead of using blocking blink timing
- This helps verify whether request processing reaches the main loop while keeping communication latency low

# 2026-04-24 15:46 KST - Added UART ISR byte-count visibility

## Summary
- Added an ISR byte counter to confirm whether `EUSCI_A1_ISR` is actually receiving request bytes
- The main loop now toggles the LED when any UART RX ISR bytes were captured since the previous iteration
- This separates “ISR not firing” from “ISR fired but Modbus frame handling failed”

# 2026-04-24 - 5-Bug Fix: UART 인터럽트 Modbus 인식 불량 수정

## Summary
Modbus RTU 커맨드 인식 불량 원인이 된 5개 버그를 수정함

## Bug 1: UCA1STATW 에러 플래그 읽기 순서 (uart.c ISR)
- **원인**: `UCA1RXBUF`를 읽으면 `UCA1STATW`의 `UCOE/UCFE/UCPAR/UCBRK` 에러 플래그가 자동 클리어됨
- **증상**: 프레이밍 에러나 오버런이 발생해도 `g_uart_rx_errors`에 전혀 기록되지 않음 → 통신 이상을 진단 불가
- **수정**: `uint16_t uart_statw = UCA1STATW` 를 `UCA1RXBUF` 읽기 **전**에 저장 후 에러 체크에 활용

## Bug 2: 가드 타임 체크 타이밍 경쟁 조건 (main.c)
- **원인**: `now_ticks = TB0R` 캡처 **이후**에 `g_last_uart_rx_tick_snapshot = UART_GetLastRxTick()` 을 읽으면, 그 사이에 UART ISR이 발생할 경우 `rx_tick > now_ticks` 가 되어 `TicksElapsed16` 결과가 언더플로우(~65535)로 폭발
- **증상**: 가드 조건이 거짓 양성이 되어 Modbus 수신 도중에 센서 I2C 작업이 실행될 수 있음 → 응답 지연 또는 프레임 손실
- **수정**: `g_last_uart_rx_tick_snapshot` 캡처를 `now_ticks = TB0R` **앞**으로 이동

## Bug 3: MODBUS_RTU_GAP_TICKS 값 과대 (modbus_rtu.c)
- **원인**: `MODBUS_RTU_GAP_TICKS = 18` (4.39ms) 이 Modbus RTU 사양 최소 인터프레임 갭 3.5문자 = 14.95틱(3.65ms) 보다 큼
- **증상**: PC가 최소 규격의 인터프레임 갭으로 연속 전송 시 프레임 경계를 감지 못해 바이트가 연이어 쌓임
- **수정**: `MODBUS_RTU_GAP_TICKS` 18 → **15** (사양과 동일한 3.5문자 이상)

## Bug 4: UCA1IE 초기화 순서 (uart.c)
- **원인**: `UCA1IE = UCRXIE` 를 `UCSWRST` 해제 **전**에 설정 — TI 권장 시퀀스 위반
- **수정**: `UCA1IFG = 0` → `UCSWRST 해제` → `UCA1IE = UCRXIE` 순서로 변경

## Bug 5: 타이머 틱 주기 오차 및 롤오버 (main.c)
- **원인**: TB0 = ACLK/8 = 4096Hz, SCHED_TICK_COUNTS=4 → 인터럽트 1024Hz. `SENSOR_SAMPLE_PERIOD_MS=1000` 은 실제로 1000/1024 ≈ 0.977초이며, `uint16_t g_tick_divider` 롤오버(65536→0) 시 0%1000=0 이 되어 의도치 않은 센서 트리거 발생
- **수정**: `SENSOR_SAMPLE_PERIOD_MS` → `SENSOR_SAMPLE_PERIOD_TICKS = 1024` 로 변경 (65536 = 64×1024 이므로 롤오버도 안전)

## Files Updated
- `uart.c` (Bug 1, Bug 4)
- `main.c` (Bug 2, Bug 5)
- `modbus_rtu.c` (Bug 3)

# 2026-04-24 - LMP91000 리턴값 규약 역전 수정 (Bug 6, Bug 7)

## 원인 분석
통신 로그에서 `LMP91000 Init: ERR(1)` 표시 → 실제론 **정상 초기화 성공**이었음

## Bug 6: LMP_Init 리턴값 규약 역전 (lmp91000.h / lmp91000.c)
- **원인**: `LMP_Init()`은 1=성공/0=실패 (C boolean 관례) 를 반환하지만, `PrintInitResult()`는 0=OK/非0=ERR (errno 관례) 로 해석 → 성공 시 `ERR(1)`, 실패 시 `OK` 가 출력됨
- **수정**: `lmp91000.h`에 `LMP_OK=0` / `LMP_ERR=1` 추가. `LMP_Init()` 반환을 `LMP_OK`(0=성공) / `LMP_ERR`(1=실패) 로 교정. FRAM(0=OK), TMP112(0=OK)와 동일 규약으로 통일

## Bug 7: TIACN/REFCN 쓰기 실패가 반환값에 미포함 (lmp91000.c)
- **원인**: Unlock 실패는 조기 반환하지만 TIACN/REFCN 쓰기 실패는 로그(`LMP_DEBUG_LOG=0`이면 이것도 없음)만 남기고 계속 진행. 설정 불완전 상태로 측정 진행되는 문제
- **수정**: TIACN/REFCN 쓰기 실패 시 LOCK 복원 후 `LMP_ERR` 반환

## Files Updated
- `lmp91000.h` (Bug 6: LMP_OK=0/LMP_ERR=1 정의 추가)
- `lmp91000.c` (Bug 6: 반환값 수정, Bug 7: TIACN/REFCN 실패 시 조기 반환)
