# 2026-04-26 - GAIN 커맨드 추가 (TIA 이득 시리얼 설정 + FRAM 저장)

## 추가 기능
`S` 진입 후 교정 모드에서 `GAIN:<1~7>` 커맨드로 LMP91000 TIA 이득 변경 가능

| 인덱스 | 저항값 | TIACN[4:2] |
|--------|--------|-----------|
| 1 | 2.75 kΩ | 001 |
| 2 | 3.5 kΩ  | 010 |
| 3 | **7 kΩ** (기본) | 011 |
| 4 | 14 kΩ  | 100 |
| 5 | 35 kΩ  | 101 |
| 6 | 120 kΩ | 110 |
| 7 | 350 kΩ | 111 |

## 동작
- RLOAD 비트(bits[1:0]) 유지, GAIN 비트(bits[4:2])만 교체
- LMP91000 Unlock → TIACN 쓰기 → Lock → readback 검증 (최대 3회 재시도)
- 성공 시 `FRAM_SetTIACN()` 으로 FRAM 저장 (재부팅 후 `LMP_Init`에서 복원됨)
- GAIN 변경 후 ZERO/SPAN 재교정 권고 경고 출력

## 수정 파일
- `lmp91000.h` — GAIN 상수(LMP_GAIN_IDX_MIN/MAX, LMP_TIACN_GAIN_SHIFT/RLOAD_MASK) 추가, `LMP_SetTIACN()` 선언
- `lmp91000.c` — `LMP_SetTIACN()` 구현
- `main.c` — `GainIdxToStr()`, `TIACNToGainIdx()` 헬퍼, `PrintCalStatus()`에 GAIN 표시, `PrintCalMenu()`에 GAIN 항목, `ProcessCommand()`에 `GAIN:<n>` 처리 추가

---

# 2026-04-26 - Bug8: 부팅 후 첫 출력까지 ~15초 지연 수정

## 증상
- PrintStartupBanner 표시 후 약 15초간 침묵
- 이후 1초 간격 모니터 출력은 정상 동작

## 원인 분석
Timer B0 CONTINUOUS 모드에서 `TB0CCR0 = TB0R + 4 = 4` 로 고정된 상태에서
`__enable_interrupt()` 호출 전까지 모든 초기화(~300ms)가 인터럽트 없이 진행됨.

이 시간 동안 TB0R은 계속 카운트하여 CCR0(=4)를 이미 지나친 상태.
다음 CCR0 매치는 TB0R이 65535 → 0 → 4 로 한 바퀴 돌아와야 발생:

```
남은 카운트 ≈ (65535 - 1200 + 4) = 64339
대기 시간  = 64339 / 4096Hz ≈ 15.7초
```

첫 ISR 이후로는 `TB0CCR0 += 4` 정상 동작 → 1초 주기 유지.

## 수정 내용 (`main.c` REV 0.4)
`__enable_interrupt()` 직전에 CCR0를 현재 TB0R 기준으로 재설정:
```c
TB0CCTL0 &= ~CCIFG;
TB0CCR0   = (unsigned int)(TB0R + SCHED_TICK_COUNTS);
__enable_interrupt();
```

## 추가 수정
- `SENSOR_TMP_READ`에서 첫 측정 완료 즉시 `g_print_due=1` 세팅 (g_first_done 플래그)
  → 배너 표시 후 ~25ms 이내 첫 모니터 라인 출력 가능

## 수정 파일
- `main.c`

---

# 2026-04-26 - ADS1115 간헐적 이상값(0x7FEC) 수정 — OS bit 폴링

## 증상
- 정상값 ~16215 중에서 ~3-5초 간격으로 32748(0x7FEC)이 출력
- 0x7FEC = ADS1115 풀스케일(0x7FFF)보다 19카운트 낮은 값
- 교정 후 CO 농도 계산에 직접 영향을 주는 심각한 버그

## 원인 분석
- ADS1115 128SPS 단일변환 시간: **7.81 ms**
- 기존 `ADS_CONVERSION_WAIT_TICKS = 10` → 9.77 ms 대기
- `StartSingleShot()` I2C 전송 시간 ~1–2 ms 소요
- 실제 변환 완료 전에 읽기가 시작되는 타이밍 레이스 발생
- ADS1115는 단일변환 미완료 시 이전 변환 결과(또는 풀스케일)를 반환함

## 수정 내용
### `ads1115.c` / `ads1115.h` (REV 0.2)
- `ADS_IsConversionReady()` 추가
  - Config 레지스터 bit15(OS bit) 폴링
  - OS=1이면 변환 완료, OS=0이면 변환 진행 중

### `main.c` (REV 0.3)
- `ADS_CONVERSION_TIMEOUT_TICKS = 30u` 상수 추가 (~29 ms 하드 타임아웃)
- `SensorTask_t`에 `timeout_ticks` 필드 추가
- `SENSOR_ADS_REF_WAIT` / `SENSOR_ADS_OUT_WAIT` 로직 변경:
  1. 최소 10틱 대기 (기존과 동일)
  2. `ADS_IsConversionReady()` 확인
  3. OS=0이면 2틱 후 재확인 (최대 29 ms까지 반복)
  4. 29 ms 초과 시 `SensorTask_Reset()` (타임아웃 안전망)

## 결과
- ADS1115가 변환 완료를 신호할 때만 결과를 읽음
- 타이밍 여유와 무관하게 이상값 원천 차단

## 수정 파일
- `ads1115.h`
- `ads1115.c`
- `main.c`

---

# 2026-04-24 10:50 KST - ADS1115/TMP112 I2C 오류 처리 및 실제 시간 로깅 반영

## 요약
- ADS1115 읽기 경로를 상태 기반으로 보강하여 I2C 실패가 raw `0`으로 숨겨지지 않도록 수정
- TMP112 오류 시 공통 `I2C_BusReset()` 경로를 타도록 보완
- 모니터 시간 라벨을 `t_act(s)`로 변경하고 실제 경과 시간을 출력하도록 수정
- TMP112 설정 readback 실패가 `PASS`로 기록되지 않도록 수정

## 수정 파일
- `ads1115.h`
- `ads1115.c`
- `TMP112x.c`
- `main.c`

## 검증 포인트
- ADC 통신 실패 시 `ADC-ERR(x/y)`가 출력되는지 확인
- TMP112 NACK 또는 timeout 이후 공통 버스 복구가 정상 동작하는지 확인
- UART 시간 출력이 고정 1초 증가가 아니라 실제 루프 주기를 따르는지 확인

# 2026-04-24 11:07 KST - 실기 동작 확인 및 UART 배너 동기화 보완

## 요약
- UART 로그 기준으로 실제 하드웨어 동작이 정상임을 확인
- TMP112 초기화 및 readback 정상 동작 확인
- `t_act(s)`가 현재 구성 기준 약 2.28초 주기로 실제 루프 시간을 반영하는지 확인
- 리셋/시작 직후 첫 글자 깨짐을 줄이기 위해 시작 배너 앞에 CRLF를 추가

## 관찰 로그
- `FRAM restore OK`
- `LMP91000` 초기화 및 검증 통과
- `TMP112B` 설정 readback 통과
- 모니터 루프에서 `VREF`, `VOUT`, `VS`, 온도 값이 안정적으로 출력됨

# 2026-04-24 11:10 KST - UART 시작 안정화 시간 추가

## 요약
- `UART_Init()` 이후 추가 시작 지연 삽입
- 시작 배너 앞의 공백 줄 수를 늘림
- 목표는 리셋 직후 또는 터미널 attach 직후 발생하는 첫 글자 깨짐 감소

# 2026-04-24 11:29 KST - 측정 레지스터용 Modbus RTU 슬레이브 1차 구현

## 요약
- 센서 블로킹 읽기 중에도 Modbus 요청이 유실되지 않도록 UART RX 인터럽트 버퍼링 추가
- `FC03`, `FC04` 처리 가능한 Modbus RTU 슬레이브 추가
- 읽기 전용 레지스터 `0x0000` ~ `0x0007`에 현재 측정값 매핑
- 메인 루프를 연속 디버그 출력 방식에서 측정값 갱신 + Modbus polling 방식으로 변경
- FRAM의 slave ID를 Modbus slave address로 재사용

## 구현 레지스터 맵
- `0x0000` / `0x0001`: `CO_PPM` float 상위/하위 워드
- `0x0002`: `VOUT_RAW` signed 16-bit
- `0x0003`: `VREF_RAW` signed 16-bit
- `0x0004` / `0x0005`: `TEMP_C` float 상위/하위 워드
- `0x0006` / `0x0007`: `VS_mV` float 상위/하위 워드

## 비고
- `CO_PPM`은 FRAM의 zero/span 보정값이 모두 유효할 때만 계산
- 보정이 완료되지 않은 경우 `CO_PPM`은 `0.0` 반환
- 동일 포트에서 Modbus RTU를 사용하므로 런타임 디버그 UART 텍스트 출력은 제거

# 2026-04-24 11:36 KST - PC용 Modbus RTU 테스트 도구 추가

## 요약
- PowerShell 기반의 간단한 PC 측 Modbus RTU 읽기 테스트 프로그램 추가
- `FC03`, `FC04` 지원
- 요청 프레임 생성, CRC 검증, raw 레지스터 출력, 구현된 측정 맵 디코딩 기능 포함
- Python 없이 현재 Windows 환경에서 바로 실행 가능

# 2026-04-24 11:36 KST - 별도 UI 테스트 폴더 구성

## 요약
- PC 측 UI 도구용 별도 `pc_modbus_ui` 폴더 추가
- PowerShell WinForms 기반 간단한 Modbus RTU 테스트 앱 추가
- COM 포트 선택, 1회 읽기, watch 모드, raw frame 로그, 측정값 디코드 표시 기능 포함

# 2026-04-24 11:44 KST - Modbus 읽기 요청 즉시 처리 반영

## 요약
- `FC03`, `FC04` 읽기 요청은 8바이트 수신 즉시 처리하도록 Modbus RTU 슬레이브 수정
- 미완성 프레임이나 미지원 프레임에 대해서는 inter-frame gap 기반 fallback 로직 유지
- UART에서 gap만으로 프레임 끝을 판단할 때 발생하던 timeout을 줄이는 것이 목적

# 2026-04-24 11:53 KST - Modbus RTU 임시 LED 진단 추가

## 요약
- 임시 Modbus 진단 이벤트 및 LED 점멸 패턴 추가
- 1회 점멸: 읽기 요청 프레임 감지
- 2회 점멸: CRC 통과
- 3회 점멸: 정상 응답 송신 시작
- 4회 점멸: 예외 응답 송신 시작
- 기존 주기 LED 토글은 제거하여 LED를 Modbus 진단 전용으로 사용

# 2026-04-24 13:47 KST - U3 UART/I2C 아날로그 스위치 명시 제어

## 요약
- `U3 IN1`, `IN2`가 MCU `P4.1`의 `UART-I2C-CS`에 함께 연결됨을 확인
- 기본 `P4OUT = 0x00` 상태에 의존하지 않고 `P4.1` GPIO를 명시 제어하도록 수정
- `GPIO_Init()` 중 선택선을 LOW로 강제하여 보드가 항상 UART 경로로 시작되도록 보완
- 이후 UART/I2C 경로 전환에 사용할 수 있는 재사용 매크로 추가

# 2026-04-24 14:01 KST - Raw UART RX 활동 및 에러 진단 추가

## 요약
- Modbus 프레임 파싱과 무관하게 UART RX 활동을 직접 확인하는 진단 추가
- `UCA1`가 어떤 바이트든 수신하면 LED가 짧게 점멸하도록 변경
- overrun, framing, parity, break 에러를 포착하는 UART 에러 진단 추가
- UART 수신 에러 검출 시 LED가 별도의 긴 2회 점멸 패턴을 출력하도록 변경

# 2026-04-24 14:10 KST - 리셋 진단용 시작 배너 복원

## 요약
- 리셋 후 1회 출력되는 UART 시작 메시지 복원
- `FRAM`, `LMP91000`, `TMP112` 초기화 결과를 간단히 요약하는 boot summary 추가
- 현재 `Slave ID`와 `U3`가 UART 경로로 고정되었는지 시작 시점에 출력
- 부팅 후 런타임 동안은 UART를 조용히 유지하여 Modbus RTU와 동일 포트를 계속 사용 가능하도록 구성

# 2026-04-24 14:19 KST - 임시 UART 에코 진단 모드 추가

## 요약
- PC -> MCU -> PC 경로를 직접 확인하기 위한 임시 UART 에코 모드 추가
- 시작 이후 `UCA1` 포트로 들어온 바이트를 그대로 되돌려 송신
- 에코 진단이 활성화된 동안 Modbus RTU 처리는 일시 중지
- 에코 테스트 중에도 raw RX LED 진단은 계속 유지

# 2026-04-24 14:28 KST - UCA1 RX 핀 설정 명시 정리

## 요약
- 포트 전체 기본값에 의존하지 않고 `P4.2/P4.3` UART 핀 설정을 명확히 정리
- U3 선택선과 UCA1 핀인 `P4.1/P4.2/P4.3`의 pull 저항을 명시적으로 비활성화
- UART 주변기능 활성화 전에 `P4.2`는 입력, `P4.1/P4.3`은 출력으로 강제 설정
- `P4SEL0`, `P4SEL1`, `P4DIR`, `P4REN`, `P4OUT`를 시작 시 출력하여 핀 상태를 직접 확인 가능하도록 구성

# 2026-04-24 14:36 KST - P4.2 raw GPIO 수신 프로브 추가

## 요약
- `P4.2` 패드 단에서 신호가 실제 들어오는지 확인하기 위해 임시 raw GPIO probe 모드 추가
- `P4.2`에서 falling edge가 검출되면 UART TX로 `[RX-PROBE]`를 출력하고 LED를 점멸
- 이 모드에서는 `P4.2` 하드웨어 UART RX를 잠시 비활성화하고 기존 boot UART TX만 사용
- 목적은 외부 PC 신호가 MCU 패드까지 전기적으로 도달하는지 확인하는 것

# 2026-04-24 14:44 KST - UCA1 RX 직접 polling 진단 추가

## 요약
- 임시 수신 진단을 GPIO probe 모드에서 다시 UCA1 하드웨어 UART 기반으로 전환
- RX 인터럽트 경로에 의존하지 않고 `UCA1 RXIFG` 직접 polling으로 UART가 실제 바이트를 보는지 확인하도록 구성
- 에러 문자도 소프트웨어까지 올라오도록 `UCRXEIE` 활성화
- raw 바이트 값, `STATW`, 즉시 에코를 포함한 `[UART-RX]` 로그 추가

# 2026-04-24 14:52 KST - 검증된 polling RX 경로로 Modbus RTU 복귀

## 요약
- 임시 에코/프로브 진단을 런타임 경로에서 제거
- 검증된 `UCA1 RXIFG` direct polling 경로로 Modbus RTU 수신 처리 전환
- polling과 ISR 버퍼링이 서로 수신 바이트를 경쟁하지 않도록 UART RX 인터럽트 비활성화
- 시작 메시지는 `Modbus: Ready` 상태로 복원

# 2026-04-24 15:00 KST - Modbus 버퍼링을 위한 UART RX 인터럽트 복원

## 요약
- UART 하드웨어 경로가 정상임을 확인한 뒤 `UCA1` RX 인터럽트를 다시 활성화
- Modbus RTU 수신 경로를 인터럽트 기반 ring buffer 방식으로 복귀
- 메인 루프가 센서/I2C 작업으로 바쁠 때 발생하는 요청 유실을 줄이는 것이 목적
- 검증된 UART 핀 설정 및 시작 summary는 그대로 유지

# 2026-04-24 15:06 KST - 강건성을 위한 Hybrid Modbus RX 경로

## 요약
- Modbus RTU 수신 경로를 ISR ring buffer 우선, 버퍼 비어 있을 때 direct `RXIFG` polling fallback 방식으로 수정
- 센서 블로킹 작업 중에는 인터럽트 캡처를 유지하고, ISR 경로가 기대대로 바이트를 처리하지 못하는 상황에서도 완전한 수신 유실을 방지하도록 구성
- 레지스터 맵과 응답 프레이밍 로직은 그대로 유지

# 2026-04-24 15:14 KST - ISR 주도 Modbus 요청 프레임 조립

## 요약
- Modbus RTU 요청 프레임 조립을 UART RX 인터럽트 경로로 이동
- ISR에서 고정 8바이트 `FC03/FC04` 요청을 수집하고, 완성된 프레임만 메인 루프로 전달
- 메인 루프는 더 이상 바이트 단위로 요청을 조립하지 않고, 완성된 요청 프레임만 처리
- 주기 polling 환경에서 더 안정적인 인터럽트 기반 구조에 맞추기 위한 변경

# 2026-04-24 15:22 KST - Modbus RX 경로의 잔여 FIFO 충돌 제거

## 요약
- 활성 Modbus RX 경로에서 이전 UART 소프트웨어 FIFO 저장 로직 제거
- UART ISR은 더 이상 사용하지 않는 버퍼를 채우지 않고 Modbus 요청 조립기로만 바이트를 전달
- 정상 Modbus 런타임 중 응답 지연을 줄이기 위해 요청별 LED 점멸 진단 비활성화
- UART 에러 표시는 가시성 확보를 위해 유지

# 2026-04-24 15:31 KST - 센서 갱신보다 Modbus 우선 처리

## 요약
- 최근 UART 트래픽이 감지되면 센서 갱신을 지연시켜 통신 충돌을 줄이도록 조정
- 통신 블로킹 가능성을 낮추기 위해 측정 갱신 주기를 0.5초에서 1.0초로 증가
- 새 센서 업데이트를 시작하기 전 UART quiet-time guard 추가
- Modbus 응답은 최신 캐시 측정값을 기반으로 유지

# 2026-04-24 15:39 KST - 비차단 Modbus LED 활동 토글 복원

## 요약
- 지연 루프 없이 Modbus 진단 이벤트에 대한 LED 피드백을 다시 활성화
- 블로킹 blink 타이밍 대신 포착된 Modbus 진단 이벤트에서 LED 상태만 토글하도록 변경
- 통신 지연은 최소화하면서 요청 처리가 메인 루프까지 도달하는지 확인하는 것이 목적

# 2026-04-24 15:46 KST - UART ISR 바이트 카운터 가시성 추가

## 요약
- `EUSCI_A1_ISR`가 실제 요청 바이트를 수신하는지 확인하기 위해 ISR 바이트 카운터 추가
- 메인 루프는 이전 반복 이후 UART RX ISR 바이트가 하나라도 포착되면 LED를 토글하도록 수정
- 이를 통해 "ISR이 아예 안 들어오는 경우"와 "ISR은 들어오지만 Modbus 프레임 처리가 실패하는 경우"를 구분 가능하도록 구성

# 2026-04-24 - UART 인터럽트 기반 Modbus 인식 불량 수정

## 요약
- Modbus RTU 통신 인식 불량의 주요 원인이 된 5가지 버그를 수정

## Bug 1: UCA1STATW 에러 플래그 읽기 순서 수정 (`uart.c` ISR)
- 원인: `UCA1RXBUF`를 먼저 읽으면 `UCA1STATW`의 `UCOE/UCFE/UCPAR/UCBRK` 에러 플래그가 자동으로 클리어됨
- 증상: 프레이밍 에러나 오버런이 발생해도 `g_uart_rx_errors`에 남지 않아 통신 이상 진단이 어려움
- 수정: `UCA1RXBUF`를 읽기 전에 `uint16_t uart_statw = UCA1STATW`를 먼저 저장한 뒤 에러 체크에 사용

## Bug 2: UART 최근 수신 시각 체크 순서 경합 수정 (`main.c`)
- 원인: `now_ticks = TB0R`를 먼저 읽고 그 다음 `UART_GetLastRxTick()`를 읽는 사이 UART ISR이 들어오면 `rx_tick > now_ticks` 상황이 생겨 elapsed tick 계산이 비정상적으로 커질 수 있음
- 증상: UART quiet-time guard가 거짓으로 만족되어 통신 직후에도 I2C 작업이 시작되고, 그 결과 응답 지연 또는 프레임 유실이 발생
- 수정: `g_last_uart_rx_tick_snapshot`을 먼저 읽고, 그 다음 `now_ticks = TB0R`를 읽도록 순서 변경

## Bug 3: `MODBUS_RTU_GAP_TICKS` 값 과대 설정 수정 (`modbus_rtu.c`)
- 원인: `MODBUS_RTU_GAP_TICKS = 18`은 9600bps 기준 Modbus RTU 최소 inter-frame gap인 3.5문자 시간보다 큼
- 증상: PC가 규격 최소 수준의 gap으로 연속 전송할 경우 프레임 경계를 늦게 인식하거나 바이트가 이어붙는 현상 발생
- 수정: `MODBUS_RTU_GAP_TICKS`를 18에서 15로 조정하여 3.5문자 시간 기준에 맞춤

## Bug 4: `UCA1IE` 초기화 순서 수정 (`uart.c`)
- 원인: `UCA1IE = UCRXIE`를 `UCSWRST` 해제 전에 설정하여 TI 권장 초기화 순서를 따르지 않음
- 수정: `UCA1IFG = 0` 정리 후 `UCSWRST` 해제, 그 다음 `UCA1IE = UCRXIE` 순서로 변경

## Bug 5: 센서 갱신 주기 계산 및 rollover 처리 수정 (`main.c`)
- 원인: TB0가 1024Hz tick으로 동작하는데도 ms 기반 `% 1000` 방식으로 샘플 주기를 계산하여 rollover 시점에 예기치 않은 트리거가 발생할 수 있었음
- 수정: `SENSOR_SAMPLE_PERIOD_MS` 대신 `SENSOR_SAMPLE_PERIOD_TICKS = 1024`를 사용하도록 변경하여 tick 기반으로 일관되게 처리

## 수정 파일
- `uart.c` (Bug 1, Bug 4)
- `main.c` (Bug 2, Bug 5)
- `modbus_rtu.c` (Bug 3)

# 2026-04-24 - LMP91000 반환값 규약 및 오류 처리 수정

## 요약
- `LMP91000 Init: ERR(1)`이 출력되던 원인을 수정하고, 실제 설정 실패 시 즉시 오류를 반환하도록 보강

## Bug 6: `LMP_Init()` 반환값 규약 정리 (`lmp91000.h`, `lmp91000.c`)
- 원인: `LMP_Init()`이 1=성공, 0=실패 형태로 동작했지만 초기화 결과 출력부는 0=OK, 그 외=ERR 규약으로 해석하고 있었음
- 증상: 실제 초기화 성공인데 `ERR(1)`로 표시되거나, 반대로 실패를 정상으로 오인할 수 있었음
- 수정: `lmp91000.h`에 `LMP_OK=0`, `LMP_ERR=1` 정의를 추가하고 `LMP_Init()`도 동일 규약을 따르도록 변경

## Bug 7: TIACN/REFCN 쓰기 실패 시 즉시 오류 반환 (`lmp91000.c`)
- 원인: Unlock 실패는 즉시 반환했지만 TIACN/REFCN 쓰기 실패는 로그만 남기고 계속 진행할 수 있었음
- 증상: 설정이 완전히 적용되지 않은 상태에서도 측정 루틴이 이어질 수 있었음
- 수정: TIACN/REFCN 쓰기 실패 시 lock 복구 후 `LMP_ERR`를 즉시 반환하도록 변경

## 수정 파일
- `lmp91000.h` (`LMP_OK=0`, `LMP_ERR=1` 정의 추가)
- `lmp91000.c` (반환값 규약 수정, TIACN/REFCN 실패 시 조기 반환)

# 2026-04-26 - ASCII 시리얼 교정 모드 1차 버전

## 개요
Modbus RTU를 홀딩하고 ASCII 시리얼 통신 기반의 가스 교정 기능을 1차 버전으로 구현

## 동작 방식

### Monitor 모드 (기본)
- 부팅 후 1초 주기로 측정값 출력
- 출력 형식: `CO:xx.xppm ADC:xxxxx ZERO:xxxxx SPAN:xxxxx TEMP:xx.xC`
- 교정 미완료 시 CO 자리에 `----` 표시

### Calibrate 모드 (S 명령 진입)
- `S` 입력: Monitor 중지, 교정 커맨드 대기
- `C` 입력: Monitor 모드 복귀

### 교정 커맨드
| 커맨드 | 설명 | 조건 |
|---|---|---|
| `FZERO` | Factory Zero 교정 — 현재 ADC → fzero/zero 저장 | 없음 |
| `FSPAN:<ppm>` | Factory Span 교정 — 현재 ADC + 농도 저장 | FZERO 완료 후 |
| `ZERO` | User Zero 교정 | FZERO 완료 후 |
| `SPAN:<ppm>` | User Span 교정 | FSPAN 완료 후 |
| `RECAL` | User cal을 Factory cal로 복원 | FZERO+FSPAN 완료 후 |

### 농도 계산식
```
CO(ppm) = (VOUT_ADC - ZERO_ADC) / (SPAN_ADC - ZERO_ADC) × SPAN_PPM
```
- 결과값 클램프: 0 ~ 10000 ppm
- ZERO_ADC == SPAN_ADC 예외 처리 (0 반환)
- 교정 미완료 시 -1.0f 반환 → 출력은 `----`

## FRAM 구조 확장 (REV 0.3)

### 변경 전 (16 bytes)
```
zero_raw / span_raw / span_ppm_x10 / cal_flags(bit0=ZERO, bit1=SPAN)
```

### 변경 후 (22 bytes)
```
fzero_raw / fspan_raw / fspan_ppm_x10   ← Factory cal
zero_raw  / span_raw  / span_ppm_x10    ← User cal
cal_flags: bit0=FZERO / bit1=FSPAN / bit2=ZERO / bit3=SPAN
```

- FZERO 실행: fzero_raw + zero_raw 동시 저장 (초기에는 Factory = User)
- RECAL 실행: zero/span ← fzero/fspan 복원

## Modbus RTU 처리
- `modbus_rtu.c / modbus_rtu.h` 코드 보존
- `uart.c` ISR에서 `ModbusRTU_OnRxByte()` 호출 비활성화 (주석 처리)
- `main.c`에서 `ModbusRTU_Poll()` 호출 제거

## 수정 파일
- `fram.h` (구조체 확장, 신규 API 선언)
- `fram.c` (Factory/User cal setter, RECAL 구현)
- `uart.h` (UART_GetCommand / UART_FlushCmdBuf 추가)
- `uart.c` (ISR에 ASCII 커맨드 라인 버퍼 추가, Modbus 호출 비활성화)
- `main.c` (ASCII 시리얼 모드 완전 재작성)
