/******************************************************************************
 * @file    key.h
 * @brief   两个按键的消抖与短按/长按识别（在定时器中断里采样）。
 ******************************************************************************/

#ifndef __KEY_H__
#define __KEY_H__

#include "config.h"

void key_init(void);

/**
 * @brief 按键采样与消抖。**由定时器中断每 20ms 调用一次**。
 * @note  放在中断里是关键设计：主循环偶尔会被别的事情占用，
 *        按键放在中断里就不会漏按、也不会迟钝。
 */
void key_scan_tick(void);

/**
 * @brief 取走一次按键事件（取后清零）。
 * @param  which  0 = SET 键(K3)，1 = UP 键(K4)
 * @return 0 = 无事件，1 = 短按，2 = 长按
 */
u8   key_get_event(u8 which);

/** @brief 对应按键当前是否处于按下状态。 */
u8   key_is_down(u8 which);

#define KEY_SET_ID      0
#define KEY_UP_ID       1

#define KEY_EVT_NONE    0
#define KEY_EVT_SHORT   1
#define KEY_EVT_LONG    2

#endif /* __KEY_H__ */
