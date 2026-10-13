/******************************************************************************
 * @file    selftest.c
 * @brief   引脚自检程序。
 *
 *  ★ 为什么要做这个？
 *    51 开发板各家版本对引脚的分配并不完全一致（比如 74HC138 的 A/B/C
 *    接到 P2.2/P2.3/P2.4 还是反过来的，蜂鸣器是 P1.5 还是 P2.5）。
 *    与其猜，不如烧一个自检程序，按固定节奏依次点亮/驱动每一个外设，
 *    你对照着看一遍，30 秒就能确认接线，发现不对再改 config.h。
 *
 *  ★ 怎么用？
 *    1. 把 config.h 里的 SELFTEST_MODE 改成 1
 *    2. 编译下载
 *    3. 打开串口助手（9600 8N1，注意 12MHz 板子是 4800）看提示
 *    4. 逐项确认，记录哪一项不对
 *    5. 确认完把 SELFTEST_MODE 改回 0，重新编译下载
 ******************************************************************************/

/* ★ 这个 #include 必须在 #if 之前！
   预处理器遇到未定义的标识符会当作 0。如果把 #if SELFTEST_MODE 写在
   #include "config.h" 前面，SELFTEST_MODE 恒等于 0，整个文件被编译成空 ——
   也就是"把 SELFTEST_MODE 改成 1 就能进自检模式"这个功能会彻底失效，
   而且编译器只给一个 WARNING C322 unknown identifier，非常容易被忽略。
   这个坑是靠真实编译才发现的（本机装了 Keil C51 之后）。 */
#include "config.h"

#if SELFTEST_MODE

#include "selftest.h"
#include "timer.h"
#include "lcd1602.h"
#include "motor.h"
#include "key.h"
#include "uart.h"
#include "indicator.h"
#include "ds18b20.h"
#include "delay.h"      /* ★ st_msg() 里用了 delay_ms()，漏掉这个头文件会报
                           C267 'delay_ms': requires ANSI-style prototype。
                           这个错只有把 SELFTEST_MODE 打开才会暴露出来 ——
                           关着的时候整个文件被 #if 编译成空，什么都看不出来。 */

static void st_msg(char code *s)
{
    uart_tx_reset();
    uart_tx_str(s);
    uart_tx_flush();
    delay_ms(60);               /* 等发送完再继续，自检时不在乎效率 */
}

/* 步骤 1：LCD1602 自检 —— 先显示固定内容，再逐格填数字验证 16 列都正常 */
static void st_lcd(void)
{
    u8 i;

    st_msg("\r\n[1] LCD1602: should show two lines of text now\r\n");
    st_msg("            line1: 'LCD1602  OK    '\r\n");
    st_msg("            line2: '1234567890123456'\r\n");

    lcd_clear();
    lcd_goto(0, 0);
    lcd_str("LCD1602  OK     ");
    lcd_goto(1, 0);
    lcd_str("1234567890123456");

    delay_ms(1500);

    /* 逐格显示，方便看出是哪一列没亮 / 哪根数据线有问题 */
    lcd_clear();
    lcd_goto(0, 0);
    for (i = 0; i < 16U; i++) {
        lcd_char((char)('0' + (char)((i + 1U) % 10U)));
        delay_ms(120);
    }

    delay_ms(800);
    lcd_clear();
    lcd_goto(0, 0);
    lcd_str("LCD1602 SELF OK ");
    lcd_goto(1, 0);
    lcd_str("LOOK AT SCREEN  ");

    st_msg("            -> if blank: adjust the 10k contrast pot on V0\r\n");
    st_msg("            -> if all black blocks: contrast too high\r\n");
    st_msg("            -> if garbled: check D4~D7 -> P1.1~P1.4 wiring\r\n");
    delay_ms(1200);
}

/* 步骤 2：红绿 LED 交替 */
static void st_led(void)
{
    u8 i;
    u16 t0;

    st_msg("[2] LED    : green / red alternating\r\n");
    for (i = 0; i < 6; i++) {
        led_green((u8)(i % 2 == 0));
        led_red((u8)(i % 2 != 0));
        t0 = get_ms16();
        while ((u16)(get_ms16() - t0) < 300U) {
        }
    }
    led_all_off();
}

/* 步骤 3：蜂鸣器鸣叫 */
static void st_beep(void)
{
    u8 i;
    u16 t0;

    st_msg("[3] BEEP   : 3 short beeps\r\n");
    for (i = 0; i < 3; i++) {
        beep(1);
        t0 = get_ms16();
        while ((u16)(get_ms16() - t0) < 200U) {
        }
        beep(0);
        t0 = get_ms16();
        while ((u16)(get_ms16() - t0) < 200U) {
        }
    }
    st_msg("            -> if it stays on, check BEEP_ON polarity\r\n");
    st_msg("            -> if it is silent, check the P1.5 jumper on your board\r\n");
}

/* 步骤 4：电机四档调速 */
static void st_motor(void)
{
    u8 lv;
    u16 t0;

    st_msg("[4] MOTOR  : duty 0 -> 34 -> 66 -> 100 -> 0 (%)\r\n");
    for (lv = 0; lv < 5; lv++) {
        u8 d;
        switch (lv) {
        case 0: d = MOTOR_DUTY_STOP; break;
        case 1: d = MOTOR_DUTY_LOW;  break;
        case 2: d = MOTOR_DUTY_MID;  break;
        case 3: d = MOTOR_DUTY_HIGH; break;
        default: d = MOTOR_DUTY_STOP; break;
        }
        motor_set_duty(d);

        uart_tx_reset();
        uart_tx_str("            duty = ");
        uart_tx_u16((u16)d);
        uart_tx_str(" %\r\n");
        uart_tx_flush();

        t0 = get_ms16();
        while ((u16)(get_ms16() - t0) < 1500U) {
        }
    }
    st_msg("            -> if it does not run at all, check ULN2003 wiring\r\n");
    st_msg("            -> if the speed does not change, check MOTOR pin\r\n");
}

/* 步骤 5：读一次 DS18B20 */
static void st_ds18b20(void)
{
    u16 t0;

    st_msg("[5] DS18B20: sampling, please wait about 1.5s ...\r\n");

    ds_force_sample();
    t0 = get_ms16();
    while ((u16)(get_ms16() - t0) < 1500U) {
        ds_task();
    }

    uart_tx_reset();
    uart_tx_str("            status = ");
    switch (ds_get_status()) {
    case DS_OK:        uart_tx_str("OK");        break;
    case DS_NOPRESENT: uart_tx_str("NOPRESENT"); break;
    case DS_BADCRC:    uart_tx_str("BADCRC");    break;
    default:           uart_tx_str("NOINIT");    break;
    }
    uart_tx_str(",  temp = ");
    uart_tx_s16(ds_get_temp());
    uart_tx_str(" (x0.1 C)\r\n");
    uart_tx_flush();

    if (ds_get_status() == DS_NOPRESENT) {
        st_msg("            -> check 4.7k pull-up on DQ, and DQ -> P3.7\r\n");
    } else if (ds_get_temp() == 850) {
        st_msg("            -> 85.0C is the power-on default: conversion not done\r\n");
    }
}

/* 步骤 6：按键 */
static void st_key(void)
{
    u16 t0;
    u8  e;

    st_msg("[6] KEY    : press K3(P3.2) and K4(P3.3) within 20s\r\n");

    t0 = get_ms16();
    while ((u16)(get_ms16() - t0) < 20000U) {
        e = key_get_event(KEY_SET_ID);
        if (e == KEY_EVT_SHORT) {
            st_msg("            K3 short\r\n");
        } else if (e == KEY_EVT_LONG) {
            st_msg("            K3 long\r\n");
        }

        e = key_get_event(KEY_UP_ID);
        if (e == KEY_EVT_SHORT) {
            st_msg("            K4 short\r\n");
        } else if (e == KEY_EVT_LONG) {
            st_msg("            K4 long\r\n");
        }
    }
}

void selftest_run(void)
{
    st_msg("\r\n================ SELFTEST MODE ================\r\n");
    st_msg("SELFTEST_MODE = 1 in config.h\r\n");
    st_msg("Remember to set it back to 0 and re-flash!\r\n");

    st_lcd();
    st_led();
    st_beep();
    st_motor();
    st_ds18b20();
    st_key();

    st_msg("\r\n================ SELFTEST DONE ================\r\n");
    st_msg("Now set SELFTEST_MODE = 0 and re-flash the firmware.\r\n");

    /* 自检完成后进入"呼吸灯"状态，方便确认单片机还在运行 */
    while (1) {
        u16 t0 = get_ms16();
        led_green(1);
        while ((u16)(get_ms16() - t0) < 500U) {
        }
        led_green(0);
        t0 = get_ms16();
        while ((u16)(get_ms16() - t0) < 500U) {
        }
    }
}

#endif /* SELFTEST_MODE */
