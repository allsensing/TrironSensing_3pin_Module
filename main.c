/* ============================================================
 * main.c - CO 센서 모듈 / ASCII 시리얼 모드
 * MSP430FR2155 / LMP91000 + ADS1115 + TMP112B + FRAM
 *
 * [동작 모드]
 *   MONITOR   : 1초 주기로 가스 농도/ADC/온도 출력 (기본)
 *   CALIBRATE : 교정 커맨드 대기 ("S" 입력으로 진입)
 *
 * [커맨드]
 *   S             : Monitor 중지, Calibrate 모드 진입
 *   C             : Monitor 모드 복귀
 *   FZERO         : Factory Zero 교정 (현재 ADC -> fzero/zero 저장)
 *   FSPAN:<ppm>   : Factory Span 교정 (현재 ADC + 농도 -> fspan/span 저장)
 *   ZERO          : User Zero 교정  (FZERO 완료 후 사용 가능)
 *   SPAN:<ppm>    : User Span 교정  (FSPAN 완료 후 사용 가능)
 *   RECAL         : User cal을 Factory cal로 복원
 *
 * [농도 계산식]
 *   CO(ppm) = (VOUT_ADC - ZERO_ADC) / (SPAN_ADC - ZERO_ADC) x SPAN_PPM
 *
 * [Modbus RTU]
 *   코드 보존, 현재 비활성화 (modbus_rtu.c/h 유지)
 *
 * Rev History
 * -----------
 * REV 0.3  2026-04-26  ADS1115 OS bit 폴링으로 간헐적 이상값(0x7FEC) 수정
 * REV 0.2  2026-04-26  ASCII 시리얼 교정 모드 1차 버전
 * REV 0.1  2026-04-24  Modbus RTU 슬레이브 초기 버전
 * ============================================================ */

#include <msp430.h>
#include <string.h>
#include "ads1115.h"
#include "clock.h"
#include "config.h"
#include "fram.h"
#include "gpio.h"
#include "i2c.h"
#include "lmp91000.h"
#include "tmp112x.h"
#include "uart.h"

/* ============================================================
 *  상수
 * ============================================================ */
#define SCHED_TICK_COUNTS           4u
#define SENSOR_SAMPLE_PERIOD_TICKS  1024u   /* 1024Hz 기준 1초 */
#define ADS_CONVERSION_WAIT_TICKS   10u     /* 최소 대기 ~9.77ms (128SPS=7.81ms) */
#define ADS_CONVERSION_TIMEOUT_TICKS 30u    /* 최대 대기 ~29ms  */
#define ADS_MUX_VREF                4u
#define ADS_MUX_VOUT                5u
#define VOUT_AVG_N                  10u   /* 모니터 무빙 에버리지 윈도우 (10초) */
#define CAL_AVG_N                   10u   /* 교정 평균 샘플 수 (10초)           */
#define CAL_ADC_SETTLE_CYCLES       160000u /* ~20ms @8MHz      */
#define SPAN_PPM_MAX                1000u

/* ============================================================
 *  타입
 * ============================================================ */
typedef enum {
    MODE_MONITOR = 0,
    MODE_CALIBRATE
} SystemMode_t;

typedef enum {
    SENSOR_IDLE = 0,
    SENSOR_ADS_REF_START,
    SENSOR_ADS_REF_WAIT,
    SENSOR_ADS_OUT_START,
    SENSOR_ADS_OUT_WAIT,
    SENSOR_TMP_READ
} SensorState_t;

typedef struct {
    SensorState_t  state;
    unsigned long  deadline_ticks;
    unsigned long  timeout_ticks;   /* WAIT 상태 하드 타임아웃 */
    int            rvref;
    int            rvout;
} SensorTask_t;

typedef struct {
    float   co_ppm;     /* < 0 이면 교정 미완료 */
    int16_t vout_raw;
    int16_t vref_raw;
    float   temp_c;
    float   vs_mV;
} SensorData_t;

/* ============================================================
 *  전역 변수
 * ============================================================ */
static volatile unsigned long g_system_ticks = 0ul;
static volatile uint16_t      g_tick_divider = 0u;
static volatile uint8_t       g_sensor_due   = 1u;  /* 부팅 즉시 1회 측정 */
static volatile uint8_t       g_print_due    = 0u;
static          uint8_t       g_first_done   = 0u;  /* 첫 측정 완료 플래그 */

static SensorTask_t g_sensor = { SENSOR_IDLE, 0ul, 0ul, 0, 0 };
static SensorData_t g_meas   = { -1.0f, 0, 0, 0.0f, 0.0f };
static SystemMode_t g_mode   = MODE_MONITOR;

/* ── VOUT 무빙 에버리지 (모니터 모드 전용) ─────────────────── */
static int16_t  g_vout_buf[VOUT_AVG_N];   /* 원형 버퍼            */
static uint8_t  g_vout_buf_idx  = 0u;     /* 다음 쓰기 위치        */
static int32_t  g_vout_sum      = 0L;     /* 합산 (빠른 평균 계산) */
static int16_t  g_vout_avg      = 0;      /* 현재 평균값           */
static uint8_t  g_vout_avg_init = 0u;     /* 첫 샘플 초기화 플래그 */

/* ============================================================
 *  내부 유틸리티
 * ============================================================ */
static unsigned long System_GetTicks(void)
{
    unsigned long t;
    __disable_interrupt();
    t = g_system_ticks;
    __enable_interrupt();
    return t;
}

static unsigned char TickReached(unsigned long now, unsigned long deadline)
{
    return ((long)(now - deadline) >= 0L) ? 1u : 0u;
}

static float ClampFloat(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ============================================================
 *  가스 농도 계산
 *  반환: ppm 값, 교정 미완료 시 -1.0f
 * ============================================================ */
static float Calc_CO_Ppm(int16_t vout_raw)
{
    const FRAM_Data_t *f = FRAM_GetData();
    int16_t zero_raw, span_raw;
    float   span_ppm, ratio;

    if ((f->cal_flags & (FRAM_CAL_ZERO_DONE | FRAM_CAL_SPAN_DONE))
        != (FRAM_CAL_ZERO_DONE | FRAM_CAL_SPAN_DONE)) {
        return -1.0f;
    }

    zero_raw = f->zero_raw;
    span_raw = f->span_raw;
    if (span_raw == zero_raw) { return 0.0f; }

    span_ppm = (float)f->span_ppm_x10 / 10.0f;
    ratio    = (float)(vout_raw - zero_raw) / (float)(span_raw - zero_raw);
    return ClampFloat(ratio * span_ppm, 0.0f, 10000.0f);
}

/* ============================================================
 *  VOUT 무빙 에버리지 업데이트 (모니터 모드 전용)
 *  첫 번째 샘플: 버퍼 전체를 동일 값으로 채워 즉시 유효한 평균 확보
 *  이후: 원형 버퍼 + 합산 차감/추가로 shift 연산만으로 평균 계산
 * ============================================================ */
static void VoutAvg_Update(int16_t new_val)
{
    uint8_t i;

    if (g_vout_avg_init == 0u) {
        for (i = 0u; i < VOUT_AVG_N; i++) {
            g_vout_buf[i] = new_val;
        }
        g_vout_sum      = (int32_t)new_val * (int32_t)VOUT_AVG_N;
        g_vout_buf_idx  = 0u;
        g_vout_avg_init = 1u;
    } else {
        g_vout_sum -= (int32_t)g_vout_buf[g_vout_buf_idx];
        g_vout_buf[g_vout_buf_idx] = new_val;
        g_vout_sum += (int32_t)new_val;
        g_vout_buf_idx = (uint8_t)((g_vout_buf_idx + 1u) % (uint8_t)VOUT_AVG_N);
    }
    g_vout_avg = (int16_t)(g_vout_sum / (int32_t)VOUT_AVG_N);
}

/* ============================================================
 *  센서 State Machine
 * ============================================================ */
static void SensorTask_Reset(void)
{
    ALL_DESEL();
    g_sensor.state          = SENSOR_IDLE;
    g_sensor.deadline_ticks = 0ul;
}

static void SensorTask_Run(unsigned long now_ticks)
{
    float temp_c, vref, vout, vzero;

    switch (g_sensor.state) {

    case SENSOR_IDLE:
        if (g_sensor_due != 0u) {
            g_sensor_due   = 0u;
            g_sensor.state = SENSOR_ADS_REF_START;
        }
        break;

    case SENSOR_ADS_REF_START:
        ADS_SEL();
        if (ADS_StartSingleShot(ADS_MUX_VREF, ADS_PGA_USE) == ADS_OK) {
            g_sensor.deadline_ticks = now_ticks + ADS_CONVERSION_WAIT_TICKS;
            g_sensor.timeout_ticks  = now_ticks + ADS_CONVERSION_TIMEOUT_TICKS;
            g_sensor.state = SENSOR_ADS_REF_WAIT;
        } else {
            SensorTask_Reset();
        }
        break;

    case SENSOR_ADS_REF_WAIT:
        if (TickReached(now_ticks, g_sensor.timeout_ticks)) {
            SensorTask_Reset();   /* 타임아웃: ADS 응답 없음 */
            break;
        }
        if (TickReached(now_ticks, g_sensor.deadline_ticks)) {
            if (!ADS_IsConversionReady()) {
                /* OS=0: 변환 미완료 → 2틱 후 재확인 */
                g_sensor.deadline_ticks = now_ticks + 2u;
                break;
            }
            if (ADS_ReadConversionRaw(&g_sensor.rvref) == ADS_OK) {
                g_sensor.state = SENSOR_ADS_OUT_START;
            } else {
                SensorTask_Reset();
            }
        }
        break;

    case SENSOR_ADS_OUT_START:
        if (ADS_StartSingleShot(ADS_MUX_VOUT, ADS_PGA_USE) == ADS_OK) {
            g_sensor.deadline_ticks = now_ticks + ADS_CONVERSION_WAIT_TICKS;
            g_sensor.timeout_ticks  = now_ticks + ADS_CONVERSION_TIMEOUT_TICKS;
            g_sensor.state = SENSOR_ADS_OUT_WAIT;
        } else {
            SensorTask_Reset();
        }
        break;

    case SENSOR_ADS_OUT_WAIT:
        if (TickReached(now_ticks, g_sensor.timeout_ticks)) {
            SensorTask_Reset();   /* 타임아웃: ADS 응답 없음 */
            break;
        }
        if (TickReached(now_ticks, g_sensor.deadline_ticks)) {
            if (!ADS_IsConversionReady()) {
                /* OS=0: 변환 미완료 → 2틱 후 재확인 */
                g_sensor.deadline_ticks = now_ticks + 2u;
                break;
            }
            if (ADS_ReadConversionRaw(&g_sensor.rvout) == ADS_OK) {
                vref  = ADS_RawToVolt(g_sensor.rvref);
                vout  = ADS_RawToVolt(g_sensor.rvout);
                vzero = ADS_VZERO(vref);

                g_meas.vref_raw = (int16_t)g_sensor.rvref;
                g_meas.vout_raw = (int16_t)g_sensor.rvout;
                g_meas.vs_mV    = (vout - vzero) * 1000.0f;
                g_meas.co_ppm   = Calc_CO_Ppm(g_meas.vout_raw);
                VoutAvg_Update(g_meas.vout_raw); /* 모니터 표시용 평균 업데이트 */

                ALL_DESEL();
                g_sensor.state = SENSOR_TMP_READ;
            } else {
                SensorTask_Reset();
            }
        }
        break;

    case SENSOR_TMP_READ:
        if (TMP112_ReadTemp(&temp_c) == TMP112_OK) {
            g_meas.temp_c = temp_c;
        }
        if (g_first_done == 0u) {
            g_first_done = 1u;
            g_print_due  = 1u;  /* 첫 측정 완료 즉시 출력 (이후는 1초 타이머 사용) */
        }
        SensorTask_Reset();
        break;

    default:
        SensorTask_Reset();
        break;
    }
}

/* ============================================================
 *  교정용 VOUT ADC 8초 평균 읽기
 *  1초 간격으로 CAL_AVG_N(10)회 측정 → 평균 반환
 *  진행 표시: "1 2 3 4 5 6 7 8"
 *
 *  - 센서 스테이트 머신 정지 후 I2C 직접 점유
 *  - 타이머 틱(1024Hz) 기반 1초 대기 → 정확한 간격 보장
 * ============================================================ */
static int Cal_ReadVoutADC_Avg(void)
{
    int32_t       sum     = 0L;
    int           raw     = 0;
    unsigned long t_start;
    uint8_t       i;

    SensorTask_Reset();   /* 센서 스테이트 머신 정지 */

    for (i = 1u; i <= (uint8_t)CAL_AVG_N; i++) {
        /* 1초 대기 (타이머 틱 기준) */
        t_start = System_GetTicks();
        while (!TickReached(System_GetTicks(),
                            t_start + SENSOR_SAMPLE_PERIOD_TICKS)) {
            /* 대기 중 커맨드 버퍼 플러시 (입력 무시) */
        }

        /* ADC 단일 변환 읽기 */
        ADS_SEL();
        ADS_ReadChPGA_Raw(ADS_MUX_VOUT, ADS_PGA_USE, &raw);
        ALL_DESEL();

        sum += (int32_t)raw;

        /* 진행 카운트 표시 */
        UART_SendUInt((unsigned int)i);
        if (i < (uint8_t)CAL_AVG_N) {
            UART_SendStr(" ");
        }
    }
    UART_SendStr("\r\n");

    return (int)(sum / (int32_t)CAL_AVG_N);
}

/* ============================================================
 *  Monitor 라인 출력
 *  CO:xx.xppm ADC:xxxxx ZERO:xxxxx SPAN:xxxxx TEMP:xx.xC
 * ============================================================ */
static void PrintMonitorLine(void)
{
    const FRAM_Data_t *f   = FRAM_GetData();
    float              co  = Calc_CO_Ppm(g_vout_avg); /* 평균값으로 농도 계산 */

    UART_SendStr("CO:");
    if (co < 0.0f) {
        UART_SendStr("----");
    } else {
        UART_SendFloat(co, 1u);
    }
    UART_SendStr("ppm ");

    UART_SendStr("ADC:");
    UART_SendInt(g_vout_avg);   /* 10샘플 이동 평균값 표시 */
    UART_SendStr(" ");

    UART_SendStr("ZERO:");
    if (f->cal_flags & FRAM_CAL_ZERO_DONE) {
        UART_SendInt(f->zero_raw);
    } else {
        UART_SendStr("--");
    }
    UART_SendStr(" ");

    UART_SendStr("SPAN:");
    if (f->cal_flags & FRAM_CAL_SPAN_DONE) {
        UART_SendInt(f->span_raw);
    } else {
        UART_SendStr("--");
    }
    UART_SendStr(" ");

    UART_SendStr("TEMP:");
    UART_SendFloat(g_meas.temp_c, 1u);
    UART_SendStr("C\r\n");
}

/* ============================================================
 *  커맨드 파싱 유틸리티
 * ============================================================ */
static void StrToUpper(char *s)
{
    while (*s) {
        if (*s >= 'a' && *s <= 'z') {
            *s = (char)(*s - 'a' + 'A');
        }
        s++;
    }
}

static unsigned char StrStartsWith(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s++ != *prefix++) { return 0u; }
    }
    return 1u;
}

static unsigned int ParseUInt(const char *s)
{
    unsigned int val = 0u;
    while (*s >= '0' && *s <= '9') {
        val = val * 10u + (unsigned int)(*s - '0');
        s++;
    }
    return val;
}

/* ============================================================
 *  GAIN 헬퍼 — PrintCalStatus/ProcessCommand 보다 먼저 정의
 * ============================================================ */
static const char *GainIdxToStr(unsigned int idx)
{
    switch (idx) {
    case 1u: return "2.75k";
    case 2u: return "3.5k";
    case 3u: return "7k";
    case 4u: return "14k";
    case 5u: return "35k";
    case 6u: return "120k";
    case 7u: return "350k";
    default: return "?";
    }
}

static unsigned int TIACNToGainIdx(unsigned char tiacn)
{
    return (unsigned int)((tiacn >> LMP_TIACN_GAIN_SHIFT) & 0x07u);
}

/* ============================================================
 *  교정 상태 / 메뉴 출력
 * ============================================================ */
static void PrintCalStatus(void)
{
    const FRAM_Data_t *f = FRAM_GetData();
    unsigned int gain_idx;

    UART_SendStr("[CAL STATUS]\r\n");

    gain_idx = TIACNToGainIdx((unsigned char)f->tiacn_val);
    UART_SendStr("  GAIN:  ");
    UART_SendUInt(gain_idx);
    UART_SendStr(" (");
    UART_SendStr(GainIdxToStr(gain_idx));
    UART_SendStr("ohm)  TIACN=0x");
    UART_SendHex8((unsigned char)f->tiacn_val);
    UART_SendStr("\r\n");

    UART_SendStr("  FZERO: ");
    if (f->cal_flags & FRAM_CAL_FZERO_DONE) {
        UART_SendStr("OK  ADC=");
        UART_SendInt(f->fzero_raw);
        UART_SendStr("\r\n");
    } else { UART_SendStr("--\r\n"); }

    UART_SendStr("  FSPAN: ");
    if (f->cal_flags & FRAM_CAL_FSPAN_DONE) {
        UART_SendStr("OK  ADC=");
        UART_SendInt(f->fspan_raw);
        UART_SendStr("  PPM=");
        UART_SendUInt(f->fspan_ppm_x10 / 10u);
        UART_SendStr("\r\n");
    } else { UART_SendStr("--\r\n"); }

    UART_SendStr("  ZERO:  ");
    if (f->cal_flags & FRAM_CAL_ZERO_DONE) {
        UART_SendStr("OK  ADC=");
        UART_SendInt(f->zero_raw);
        UART_SendStr("\r\n");
    } else { UART_SendStr("--\r\n"); }

    UART_SendStr("  SPAN:  ");
    if (f->cal_flags & FRAM_CAL_SPAN_DONE) {
        UART_SendStr("OK  ADC=");
        UART_SendInt(f->span_raw);
        UART_SendStr("  PPM=");
        UART_SendUInt(f->span_ppm_x10 / 10u);
        UART_SendStr("\r\n");
    } else { UART_SendStr("--\r\n"); }
}

static void PrintCalMenu(void)
{
    UART_SendStr("[CAL] Commands:\r\n");
    UART_SendStr("  GAIN:<1~7>    TIA gain (1=2.75k 2=3.5k 3=7k 4=14k 5=35k 6=120k 7=350k ohm)\r\n");
    UART_SendStr("  FZERO         Factory zero calibration\r\n");
    UART_SendStr("  FSPAN:<ppm>   Factory span calibration (1~1000 ppm)\r\n");
    UART_SendStr("  ZERO          User zero calibration\r\n");
    UART_SendStr("  SPAN:<ppm>    User span calibration (1~1000 ppm)\r\n");
    UART_SendStr("  RECAL         Restore user cal from factory\r\n");
    UART_SendStr("  C             Return to monitor mode\r\n");
}

/* ============================================================
 *  커맨드 처리
 * ============================================================ */
static void ProcessCommand(char *cmd)
{
    const FRAM_Data_t *f;
    int adc_raw;
    unsigned int ppm;

    StrToUpper(cmd);

    /* S: Calibrate 모드 진입 */
    if (cmd[0] == 'S' && cmd[1] == '\0') {
        g_mode = MODE_CALIBRATE;
        UART_SendStr("[S] Calibration mode\r\n");
        PrintCalMenu();
        PrintCalStatus();
        return;
    }

    /* C: Monitor 모드 복귀 */
    if (cmd[0] == 'C' && cmd[1] == '\0') {
        g_mode = MODE_MONITOR;
        UART_SendStr("[C] Monitor mode\r\n");
        return;
    }

    /* 이하 교정 커맨드는 CALIBRATE 모드에서만 동작 */
    if (g_mode != MODE_CALIBRATE) {
        return;
    }

    /* GAIN:<n>: TIA 이득 설정 (1=2.75k ~ 7=350k ohm) */
    if (StrStartsWith(cmd, "GAIN:")) {
        unsigned int  gain_idx  = ParseUInt(cmd + 5u);
        unsigned char new_tiacn;

        if (gain_idx < (unsigned int)LMP_GAIN_IDX_MIN ||
            gain_idx > (unsigned int)LMP_GAIN_IDX_MAX) {
            UART_SendStr("[ERR] GAIN range: 1~7\r\n");
            UART_SendStr("  1=2.75k  2=3.5k  3=7k  4=14k  5=35k  6=120k  7=350k (ohm)\r\n");
            return;
        }

        /* RLOAD 비트 유지, GAIN 비트만 교체 */
        new_tiacn = (unsigned char)((gain_idx << LMP_TIACN_GAIN_SHIFT) |
                    ((unsigned char)FRAM_GetTIACN() & LMP_TIACN_RLOAD_MASK));

        LMP_SEL();
        if (LMP_SetTIACN(new_tiacn) == LMP_OK) {
            FRAM_SetTIACN((uint16_t)new_tiacn);
            UART_SendStr("[GAIN OK] ");
            UART_SendUInt(gain_idx);
            UART_SendStr(" (");
            UART_SendStr(GainIdxToStr(gain_idx));
            UART_SendStr("ohm)  TIACN=0x");
            UART_SendHex8(new_tiacn);
            UART_SendStr("\r\n");
            UART_SendStr("[WARN] ZERO/SPAN cal may be invalid. Recalibrate recommended.\r\n");
        } else {
            UART_SendStr("[ERR] GAIN write failed\r\n");
        }
        ALL_DESEL();
        return;
    }

    /* FZERO: Factory Zero 교정 */
    if (strcmp(cmd, "FZERO") == 0) {
        UART_SendStr("[FZERO] 8sec avg: ");
        adc_raw = Cal_ReadVoutADC_Avg();
        FRAM_SetFactoryZero((int16_t)adc_raw);
        g_meas.vout_raw = (int16_t)adc_raw;
        g_meas.co_ppm   = Calc_CO_Ppm(g_meas.vout_raw);
        UART_SendStr("[FZERO OK] AVG=");
        UART_SendInt(adc_raw);
        UART_SendStr("\r\n");
        return;
    }

    /* FSPAN:<ppm>: Factory Span 교정 */
    if (StrStartsWith(cmd, "FSPAN:")) {
        ppm = ParseUInt(cmd + 6u);
        if (ppm == 0u || ppm > SPAN_PPM_MAX) {
            UART_SendStr("[ERR] FSPAN range: 1~1000 ppm\r\n");
            return;
        }
        f = FRAM_GetData();
        if (!(f->cal_flags & FRAM_CAL_FZERO_DONE)) {
            UART_SendStr("[ERR] FZERO must be done first\r\n");
            return;
        }
        UART_SendStr("[FSPAN] 8sec avg: ");
        adc_raw = Cal_ReadVoutADC_Avg();
        FRAM_SetFactorySpan((int16_t)adc_raw, (uint16_t)(ppm * 10u));
        g_meas.co_ppm = Calc_CO_Ppm(g_meas.vout_raw);
        UART_SendStr("[FSPAN OK] AVG=");
        UART_SendInt(adc_raw);
        UART_SendStr("  PPM=");
        UART_SendUInt(ppm);
        UART_SendStr("\r\n");
        return;
    }

    /* ZERO: User Zero 교정 */
    if (strcmp(cmd, "ZERO") == 0) {
        f = FRAM_GetData();
        if (!(f->cal_flags & FRAM_CAL_FZERO_DONE)) {
            UART_SendStr("[ERR] FZERO must be done first\r\n");
            return;
        }
        UART_SendStr("[ZERO] 8sec avg: ");
        adc_raw = Cal_ReadVoutADC_Avg();
        FRAM_SetUserZero((int16_t)adc_raw);
        g_meas.co_ppm = Calc_CO_Ppm(g_meas.vout_raw);
        UART_SendStr("[ZERO OK] AVG=");
        UART_SendInt(adc_raw);
        UART_SendStr("\r\n");
        return;
    }

    /* SPAN:<ppm>: User Span 교정 */
    if (StrStartsWith(cmd, "SPAN:")) {
        ppm = ParseUInt(cmd + 5u);
        if (ppm == 0u || ppm > SPAN_PPM_MAX) {
            UART_SendStr("[ERR] SPAN range: 1~1000 ppm\r\n");
            return;
        }
        f = FRAM_GetData();
        if (!(f->cal_flags & FRAM_CAL_FSPAN_DONE)) {
            UART_SendStr("[ERR] FSPAN must be done first\r\n");
            return;
        }
        UART_SendStr("[SPAN] 8sec avg: ");
        adc_raw = Cal_ReadVoutADC_Avg();
        FRAM_SetUserSpan((int16_t)adc_raw, (uint16_t)(ppm * 10u));
        g_meas.co_ppm = Calc_CO_Ppm(g_meas.vout_raw);
        UART_SendStr("[SPAN OK] AVG=");
        UART_SendInt(adc_raw);
        UART_SendStr("  PPM=");
        UART_SendUInt(ppm);
        UART_SendStr("\r\n");
        return;
    }

    /* RECAL: Factory cal로 복원 */
    if (strcmp(cmd, "RECAL") == 0) {
        f = FRAM_GetData();
        if (!(f->cal_flags & (FRAM_CAL_FZERO_DONE | FRAM_CAL_FSPAN_DONE))) {
            UART_SendStr("[ERR] Factory calibration not completed\r\n");
            return;
        }
        FRAM_RecalFromFactory();
        g_meas.co_ppm = Calc_CO_Ppm(g_meas.vout_raw);
        UART_SendStr("[RECAL OK] User cal restored from factory\r\n");
        PrintCalStatus();
        return;
    }

    /* 알 수 없는 커맨드 */
    if (cmd[0] != '\0') {
        UART_SendStr("[ERR] Unknown: ");
        UART_SendStr(cmd);
        UART_SendStr("\r\n");
    }
}

/* ============================================================
 *  부팅 배너
 * ============================================================ */
static void PrintStartupBanner(uint8_t fram_st, uint8_t lmp_st, uint8_t tmp_st)
{
    UART_SendStr("\r\n\r\n");
    UART_SendStr("================================\r\n");
    UART_SendStr("  CO Sensor System - ASCII Mode \r\n");
    UART_SendStr("  MSP430FR2155 REV0.2           \r\n");
    UART_SendStr("================================\r\n");
    UART_SendStr("[BOOT]\r\n");

    UART_SendStr("  FRAM    : ");
    if (fram_st == FRAM_OK)           { UART_SendStr("OK\r\n"); }
    else if (fram_st == FRAM_ERR_CRC) { UART_SendStr("CRC ERR -> defaults\r\n"); }
    else                              { UART_SendStr("NEW -> defaults\r\n"); }

    UART_SendStr("  LMP91000: ");
    if (lmp_st == LMP_OK) { UART_SendStr("OK\r\n"); }
    else { UART_SendStr("ERR("); UART_SendUInt(lmp_st); UART_SendStr(")\r\n"); }

    UART_SendStr("  TMP112  : ");
    if (tmp_st == 0u) { UART_SendStr("OK\r\n"); }
    else { UART_SendStr("ERR("); UART_SendUInt(tmp_st); UART_SendStr(")\r\n"); }

    UART_SendStr("\r\n");
    PrintCalStatus();
    UART_SendStr("\r\n[READY] Monitor mode. Press S + Enter to calibrate.\r\n\r\n");
}

/* ============================================================
 *  main
 * ============================================================ */
int main(void)
{
    char    cmd[UART_CMD_MAX_LEN];
    uint8_t fram_st, lmp_st, tmp_st;

    WDTCTL = WDTPW | WDTHOLD;

    GPIO_Init();
    PM5CTL0 &= ~LOCKLPM5;
    Clock_Init();

    /* Timer B0: ACLK/8 = 4096Hz, CCR0 매 4틱 = 1024Hz ISR */
    TB0CTL   = TBSSEL__ACLK | ID__8 | MC__CONTINUOUS | TBCLR;
    TB0CCR0  = (unsigned int)(TB0R + SCHED_TICK_COUNTS);
    TB0CCTL0 = CCIE;

    UART_Init();
    __delay_cycles(160000);
    I2C_Init();

    fram_st = FRAM_Load();

    LMP_SEL();
    lmp_st = LMP_Init();
    ALL_DESEL();
    I2C_BusReset();

    ALL_DESEL();
    __delay_cycles(400000);
    tmp_st = TMP112_Init();

    PrintStartupBanner(fram_st, lmp_st, tmp_st);

    /* Bug8 fix: 초기화 중 TB0R이 TB0CCR0(=4)를 이미 지나쳐 있어
     * 다음 매치까지 최대 ~16초 대기하는 문제 수정.
     * __enable_interrupt() 직전에 현재 TB0R 기준으로 CCR0 재설정. */
    TB0CCTL0 &= ~CCIFG;
    TB0CCR0   = (unsigned int)(TB0R + SCHED_TICK_COUNTS);

    __enable_interrupt();

    while (1) {
        unsigned long now_ticks = System_GetTicks();

        /* 1초 출력 트리거 */
        if (g_print_due) {
            g_print_due = 0u;
            if (g_mode == MODE_MONITOR) {
                PrintMonitorLine();
            }
        }

        /* 센서 상태 머신 */
        SensorTask_Run(now_ticks);

        /* 커맨드 처리 */
        if (UART_GetCommand(cmd, (uint8_t)sizeof(cmd))) {
            ProcessCommand(cmd);
        }
    }
}

/* ============================================================
 *  Timer B0 ISR - 1024Hz 스케줄러
 * ============================================================ */
#pragma vector=TIMER0_B0_VECTOR
__interrupt void TIMER0_B0_ISR(void)
{
    TB0CCR0 = (unsigned int)(TB0CCR0 + SCHED_TICK_COUNTS);

    g_tick_divider++;
    g_system_ticks++;

    if ((g_tick_divider % SENSOR_SAMPLE_PERIOD_TICKS) == 0u) {
        g_sensor_due = 1u;
        g_print_due  = 1u;
    }
}
