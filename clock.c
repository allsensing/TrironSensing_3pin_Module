/* ============================================================
 * clock.c - 클럭 초기화
 * 8MHz MCLK / DCOCLKDIV / REFOCLK 기준 / Software FLL Trim
 * ============================================================ */

#include <msp430.h>
#include "config.h"
#include "clock.h"

/* ============================================================
 *  Clock_Init
 *  MCLK = 8MHz (DCOCLKDIV), ACLK = REFOCLK
 * ============================================================ */
void Clock_Init(void)
{
    __bis_SR_register(SCG0);
    CSCTL3 |= SELREF__REFOCLK;
    CSCTL1  = DCOFTRIMEN_1 | DCOFTRIM0 | DCOFTRIM1 | DCORSEL_3;
    CSCTL2  = FLLD_0 + 243;
    __delay_cycles(3);
    __bic_SR_register(SCG0);
    Software_Trim();
    CSCTL4  = SELMS__DCOCLKDIV | SELA__REFOCLK;
}

/* ============================================================
 *  Software_Trim
 *  DCO 주파수를 정밀하게 8MHz로 트리밍
 * ============================================================ */
void Software_Trim(void)
{
    unsigned int oldTap  = 0xffff;
    unsigned int newTap  = 0xffff;
    unsigned int newDelta = 0xffff;
    unsigned int bestDelta = 0xffff;
    unsigned int c0 = 0, c1 = 0, c1r = 0, c0r = 0, trim = 3;
    unsigned char done = 0;

    do {
        CSCTL0 = 0x100;
        do { CSCTL7 &= ~DCOFFG; } while (CSCTL7 & DCOFFG);
        __delay_cycles((unsigned int)3000 * MCLK_FREQ_MHZ);
        while ((CSCTL7 & (FLLUNLOCK0 | FLLUNLOCK1)) && !(CSCTL7 & DCOFFG));

        c0r = CSCTL0; c1r = CSCTL1;
        oldTap = newTap;
        newTap = c0r & 0x01ff;
        trim   = (c1r & 0x0070) >> 4;

        if (newTap < 256) {
            newDelta = 256 - newTap;
            if (oldTap != 0xffff && oldTap >= 256) done = 1;
            else { trim--; CSCTL1 = (c1r & ~DCOFTRIM) | (trim << 4); }
        } else {
            newDelta = newTap - 256;
            if (oldTap < 256) done = 1;
            else { trim++; CSCTL1 = (c1r & ~DCOFTRIM) | (trim << 4); }
        }
        if (newDelta < bestDelta) { c0 = c0r; c1 = c1r; bestDelta = newDelta; }
    } while (!done);

    CSCTL0 = c0; CSCTL1 = c1;
    while (CSCTL7 & (FLLUNLOCK0 | FLLUNLOCK1));
}
