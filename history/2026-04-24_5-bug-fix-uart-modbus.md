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
