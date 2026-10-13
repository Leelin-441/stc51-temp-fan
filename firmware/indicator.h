/******************************************************************************
 * @file    indicator.h
 * @brief   双色 LED 与蜂鸣器。
 ******************************************************************************/

#ifndef __INDICATOR_H__
#define __INDICATOR_H__

#include "config.h"

void ind_init(void);

void led_green(u8 on);
void led_red(u8 on);
void led_all_off(void);

void beep(u8 on);

#endif /* __INDICATOR_H__ */
