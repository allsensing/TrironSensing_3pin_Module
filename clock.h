#ifndef CLOCK_H
#define CLOCK_H

/* ============================================================
 * clock.h - 클럭 초기화
 * 8MHz MCLK / DCOCLKDIV / Software FLL Trim
 * ============================================================ */

void Clock_Init(void);
void Software_Trim(void);

#endif /* CLOCK_H */
