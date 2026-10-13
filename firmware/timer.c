/******************************************************************************
 * @file    timer.c
 * @brief   定时器 0 中断服务：整个系统的心跳。
 *
 *  ★ 为什么要用一个中断干四件事？
 *    8051 只有 2 个定时器（Timer0/Timer1），而 Timer1 已经被串口波特率占用
 *    （工作方式 2 做波特率发生器）。所以 PWM、数码管扫描、毫秒计数、按键扫描
 *    必须共用 Timer0 这一个中断源。
 *    好处是：这四件事都不会被主循环里的阻塞代码影响 ——
 *    数码管不会因为主循环忙而闪烁，按键也不会因为主循环忙而失灵。
 *
 *  时间分配（中断周期 200us）：
 *    每 1 次中断（200us）：PWM 计数并翻转电机 IO
 *    每 5 次中断（1ms）  ：毫秒计数 +1
 *    每 10 次中断（2ms） ：扫描一位数码管（8 位 → 16ms 一轮 → 62.5Hz）
 *    每 100 次中断（20ms）：按键采样与消抖
 ******************************************************************************/

#include "timer.h"
#include "motor.h"
#include "key.h"

static volatile u32 g_ms = 0;

static u8 s_tick_1ms  = 0;
static u8 s_tick_20ms = 0;

void timer0_init(void)
{
    TMOD &= 0xF0;
    TMOD |= 0x02;               /* Timer0：方式 2，8 位自动重装 */

    TH0 = TIMER0_RELOAD;
    TL0 = TIMER0_RELOAD;

    ET0 = 1;                    /* 允许 Timer0 中断 */
    TR0 = 1;                    /* 启动计数（EA 在 main 里统一开） */
}

/* Timer0 中断，向量号 1 */
void timer0_isr(void) interrupt 1
{
    /* 方式 2 自动重装，这里不需要重新写 TH0 */

    motor_pwm_tick();           /* 软件 PWM：每 200us 一次 */

    s_tick_1ms++;
    if (s_tick_1ms >= 5) {
        s_tick_1ms = 0;
        g_ms++;
    }

    /* ★ 这里原本每 2ms 要扫一位数码管（s_tick_2ms / smg_scan_tick）。
       改用 LCD1602 之后，液晶自带控制芯片、一次写入就自己保持显示，
       不需要 CPU 反复扫描，所以这个 2ms 时间片整个去掉了 ——
       中断服务程序变短，CPU 占用从约 18% 降到约 13%。 */

    s_tick_20ms++;
    if (s_tick_20ms >= 100) {
        s_tick_20ms = 0;
        key_scan_tick();        /* 按键采样与消抖 */
    }
}

u32 get_ms(void)
{
    u32 v;
    ET0 = 0;                    /* 32 位读取不是原子的，短暂关一下中断 */
    v = g_ms;
    ET0 = 1;
    return v;
}

u16 get_ms16(void)
{
    u16 v;
    ET0 = 0;
    v = (u16)g_ms;
    ET0 = 1;
    return v;
}
