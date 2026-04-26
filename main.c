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
#define ADS_CONVERSION_WAIT_TICKS   10u     /* ~10ms            */
#define ADS_MUX_VREF                4u
#define ADS_MUX_VOUT                5u
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

static SensorTask_t g_sensor = { SENSOR_IDLE, 0ul, 0, 0 };
static SensorData_t g_meas   = { -1.0f, 0, 0, 0.0f, 0.0f };
static SystemMode_t g_mode   = MODE_MONITOR;

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
            g_sensor.state = SENSOR_ADS_REF_WAIT;
        } else {
            SensorTask_Reset();
        }
        break;

    case SENSOR_ADS_REF_WAIT:
        if (TickReached(now_ticks, g_sensor.deadline_ticks)) {
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
            g_sensor.state = SENSOR_ADS_OUT_WAIT;
        } else {
            SensorTask_Reset();
        }
        break;

    case SENSOR_ADS_OUT_WAIT:
        if (TickReached(now_ticks, g_sensor.deadline_ticks)) {
            if (ADS_ReadConversionRaw(&g_sensor.rvout) == ADS_OK) {
                vref  = ADS_RawToVolt(g_sensor.rvref);
                vout  = ADS_RawToVolt(g_sensor.rvout);
                vzero = ADS_VZERO(vref);

                g_meas.vref_raw = (int16_t)g_sensor.rvref;
                g_meas.vout_raw = (int16_t)g_sensor.rvout;
                g_meas.vs_mV    = (vout - vzero) * 1000.0f;
                g_meas.co_ppm   = Calc_CO_Ppm(g_meas.vout_raw);

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
        SensorTask_Reset();
        break;

    default:
        SensorTask_Reset();
        break;
    }
}

/* ============================================================
 *  교정용 VOUT ADC 블로킹 읽기
 * ============================================================ */
static int Cal_ReadVoutADC(void)
{
    int raw = 0;
    SensorTask_Reset();
    __delay_cycles(CAL_ADC_SETTLE_CYCLES);
    ADS_SEL();
    ADS_ReadChPGA_Raw(ADS_MUX_VOUT, ADS_PGA_USE, &raw);
    ALL_DESEL();
    return raw;
}

/* ============================================================
 *  Monitor 라인 출력
 *  CO:xx.xppm ADC:xxxxx ZERO:xxxxx SPAN:xxxxx TEMP:xx.xC
 * ============================================================ */
static void PrintMonitorLine(void)
{
    const FRAM_Data_t *f = FRAM_GetData();

    UART_SendStr("CO:");
    if (g_meas.co_ppm < 0.0f) {
        UART_SendStr("----");
    } else {
        UART_SendFloat(g_meas.co_ppm, 1u);
    }
    UART_SendStr("ppm ");

    UART_SendStr("ADC:");
    UART_SendInt(g_meas.vout_raw);
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
 *  교정 상태 / 메뉴 출력
 * ============================================================ */
static void PrintCalStatus(void)
{
    const FRAM_Data_t *f = FRAM_GetData();

    UART_SendStr("[CAL STATUS]\r\n");

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

    /* FZERO: Factory Zero 교정 */
    if (strcmp(cmd, "FZERO") == 0) {
        UART_SendStr("[FZERO] Reading ADC...\r\n");
        adc_raw = Cal_ReadVoutADC();
        FRAM_SetFactoryZero((int16_t)adc_raw);
        g_meas.vout_raw = (int16_t)adc_raw;
        g_meas.co_ppm   = Calc_CO_Ppm(g_meas.vout_raw);
        UART_SendStr("[FZERO OK] ADC=");
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
        UART_SendStr("[FSPAN] Reading ADC...\r\n");
        adc_raw = Cal_ReadVoutADC();
        FRAM_SetFactorySpan((int16_t)adc_raw, (uint16_t)(ppm * 10u));
        g_meas.co_ppm = Calc_CO_Ppm(g_meas.vout_raw);
        UART_SendStr("[FSPAN OK] ADC=");
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
        UART_SendStr("[ZERO] Reading ADC...\r\n");
        adc_raw = Cal_ReadVoutADC();
        FRAM_SetUserZero((int16_t)adc_raw);
        g_meas.co_ppm = Calc_CO_Ppm(g_meas.vout_raw);
        UART_SendStr("[ZERO OK] ADC=");
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
        UART_SendStr("[SPAN] Reading ADC...\r\n");
        adc_raw = Cal_ReadVoutADC();
        FRAM_SetUserSpan((int16_t)adc_raw, (uint16_t)(ppm * 10u));
        g_meas.co_ppm = Calc_CO_Ppm(g_meas.vout_raw);
        UART_SendStr("[SPAN OK] ADC=");
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
