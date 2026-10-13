/******************************************************************************
 * @file    main.c
 * @brief   DS18B20 智能温控散热系统 —— 主程序。
 *
 *  平台：STC89C52RC（8051 内核，11.0592MHz）
 *  开发板：普中 / 清翔 系列单片机开发板
 *  编译器：Keil C51（uVision5）  ★ 本工程全部使用 C89 语法
 *
 *  硬件清单：
 *    STC89C52RC 开发板（板载 8 位数码管、独立按键、蜂鸣器、LED、DS18B20 座、
 *    74HC245、74HC138、ULN2003）
 *    DS18B20 ×1
 *    4.7kΩ 电阻 ×1（DQ 上拉，多数开发板已焊好，没有就要自己加）
 *    5V 直流小电机 ×1
 *
 *  主循环结构：
 *    定时器中断（200us）负责"硬实时"的三件事：PWM、数码管扫描、按键采样；
 *    主循环负责"软实时"的事：DS18B20 采样状态机、控制判定、显示刷新、串口。
 *    这样即使主循环某一步慢了一点，电机转速和显示也不会抖动。
 ******************************************************************************/

#include "config.h"
#include "timer.h"
#include "uart.h"
#include "motor.h"
#include "lcd1602.h"
#include "key.h"
#include "indicator.h"
#include "ds18b20.h"
#include "app.h"
#include "selftest.h"

void main(void)
{
    /* ---------- 关总中断做初始化 ----------
       否则初始化到一半，定时器中断可能就进来了，读到的是"半成品"状态。 */
    EA = 0;

    motor_init();               /* 电机先停，保证上电安全 */
    lcd_init();                 /* 顺带把板载数码管关掉（P0 拉高） */
    ind_init();                 /* 灯灭、蜂鸣器静音 */
    key_init();
    ds_init();
    uart_init();

    /* ---------- 启动系统心跳 ----------
       定时器 0 一开，PWM、毫秒计数、按键扫描就全部跑起来了。
       （改用液晶后不再需要数码管扫描，中断里少了 2ms 那一路时间片） */
    EA = 1;
    timer0_init();

#if SELFTEST_MODE
    /* ================= 自检模式 =================
       只做硬件体检：LCD / LED / 蜂鸣器 / 电机四档 / DS18B20 / 按键。
       这个版本**不链接 app.c**（见 tools\build_selftest.bat），
       省下 2KB 多 Flash 才塞得进 8KB。
       selftest_run() 不会返回。 */
    selftest_run();
#else
    /* ================= 正式模式 ================= */
    app_init();
    app_banner();

    while (1) {
        app_task();
    }
#endif
}
