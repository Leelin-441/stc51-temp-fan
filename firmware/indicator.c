/******************************************************************************
 * @file    indicator.c
 * @brief   LED 与蜂鸣器输出。有效电平统一在这里处理。
 *
 *  ★ 换板子时如果 LED 是"高电平点亮"或者蜂鸣器是"高电平响"，
 *    只改 config.h 里的 LED_ON / BEEP_ON 两个宏即可，本文件不用动。
 ******************************************************************************/

#include "indicator.h"

void ind_init(void)
{
    led_all_off();
    beep(0);
}

void led_green(u8 on)
{
    LED_G = on ? LED_ON : LED_OFF;
}

void led_red(u8 on)
{
    LED_R = on ? LED_ON : LED_OFF;
}

void led_all_off(void)
{
    LED_G = LED_OFF;
    LED_R = LED_OFF;
}

void beep(u8 on)
{
    BEEP = on ? BEEP_ON : BEEP_OFF;
}
