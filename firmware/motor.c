/******************************************************************************
 * @file    motor.c
 * @brief   软件 PWM 实现。
 *
 *  ★ 为什么不用硬件 PWM？
 *    8051（STC89C52）的定时器**没有 PWM 模式** —— 它只能定时中断，
 *    不像 STM32 有专门的 PWM 输出通道。所以只有两条路：
 *      (a) 用 PCA 模块 —— 但 STC89C52 没有 PCA，要换成 STC12/STC15 才有；
 *      (b) 用定时器中断 + 软件计数，这就是本文件的做法。
 *
 *  原理：
 *    定时器 0 每 200us 中断一次，中断里做：
 *        pwm_cnt 在 0 ~ PWM_LEVELS-1(49) 之间循环
 *        pwm_cnt < duty_cnt  →  电机通电
 *        否则               →  电机断电
 *    于是 PWM 周期 = 50 × 200us = 10ms = 100Hz。
 *
 *  ★ 为什么选 100Hz？
 *    频率太高 → 中断太频繁，8051 扛不住（CPU 全花在进中断上）；
 *    频率太低 → 电机会有可闻的"嗡嗡"声和轻微抖动。
 *    100Hz 是这个晶振下的甜点：中断占用约 18% CPU，电机转速肉眼可见地平滑。
 ******************************************************************************/

#include "motor.h"

static u8 g_duty_pct = 0;       /* 对外汇报用的百分数 */
static u8 g_duty_cnt = 0;       /* 内部计数阈值 0~PWM_LEVELS */
static u8 g_pwm_cnt  = 0;

void motor_init(void)
{
    MOTOR = MOTOR_OFF;
    g_duty_pct = 0;
    g_duty_cnt = 0;
    g_pwm_cnt  = 0;
}

void motor_set_duty(u8 percent)
{
    u16 c;

    if (percent > 100) {
        percent = 100;
    }
    g_duty_pct = percent;

    /* 百分数 → PWM 级数：34% → 34×50/100 = 17 级 */
    c = ((u16)percent * (u16)PWM_LEVELS) / 100U;
    if (c > (u16)PWM_LEVELS) {
        c = (u16)PWM_LEVELS;
    }
    g_duty_cnt = (u8)c;

    /* 关断时立刻拉低，避免残留的高电平让电机"爬行" */
    if (g_duty_cnt == 0) {
        MOTOR = MOTOR_OFF;
    }
}

u8 motor_get_duty(void)
{
    return g_duty_pct;
}

/* 由 Timer0 中断每 200us 调用一次 */
void motor_pwm_tick(void)
{
    if (g_pwm_cnt < g_duty_cnt) {
        MOTOR = MOTOR_ON;
    } else {
        MOTOR = MOTOR_OFF;
    }

    g_pwm_cnt++;
    if (g_pwm_cnt >= PWM_LEVELS) {
        g_pwm_cnt = 0;
    }
}
