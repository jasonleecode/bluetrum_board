/* Real HAL declarations with host-backed GPIO/timer/PWM MMIO. */
#ifndef TEST_PWM_BOARD_H
#define TEST_PWM_BOARD_H
#include "../gpio_board/board.h"
extern volatile unsigned long pwm_timers[3][8], pwm_regs[6];
#undef TIM3_BASE
#undef TIM4_BASE
#undef TIM5_BASE
#undef PWMCON
#define TIM3_BASE pwm_timers[0]
#define TIM4_BASE pwm_timers[1]
#define TIM5_BASE pwm_timers[2]
#define PWMCON pwm_regs[0]
#endif
