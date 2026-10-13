/******************************************************************************
 * @file    motor.h
 * @brief   5V 直流小电机驱动：定时器中断里做的软件 PWM。
 ******************************************************************************/

#ifndef __MOTOR_H__
#define __MOTOR_H__

#include "config.h"

/* 四档占空比的定义在 config.h 里（唯一事实来源）：
     MOTOR_DUTY_STOP / MOTOR_DUTY_LOW / MOTOR_DUTY_MID / MOTOR_DUTY_HIGH
   改档位配比请改 config.h，不要在别处另写一份。 */

void motor_init(void);

/** @brief 设置占空比（0~100 的百分数），超过 100 会被限幅。 */
void motor_set_duty(u8 percent);

/** @brief 当前占空比百分数。 */
u8   motor_get_duty(void);

/**
 * @brief PWM 计数与输出翻转。**只能由定时器中断调用**，每 PWM_ISR_US 一次。
 * @note  不要在主循环里调它，否则电机转速会乱掉。
 */
void motor_pwm_tick(void);

#endif /* __MOTOR_H__ */
