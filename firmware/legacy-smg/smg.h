/******************************************************************************
 * @file    smg.h
 * @brief   8 位共阳数码管动态扫描显示（74HC138 位选）。
 ******************************************************************************/

#ifndef __SMG_H__
#define __SMG_H__

#include "config.h"

/* 常用段码（共阳数码管：位为 0 表示该段点亮） */
#define SEG_BLANK       0xFF    /* 全灭 */
#define SEG_MINUS       0xBF    /* '-' 只有中间一横 */
#define SEG_CH_A        0x88    /* 'A' */
#define SEG_CH_H        0x89    /* 'H' */
#define SEG_CH_E        0x86    /* 'E' */
#define SEG_CH_F        0x8E    /* 'F' */
#define SEG_CH_L        0xC7    /* 'L' */
#define SEG_CH_P        0x8C    /* 'P' */
#define SEG_CH_d        0xA1    /* 'd' */
#define SEG_CH_o        0xA3    /* 'o' */
#define SEG_CH_t        0x87    /* 't' */
#define SEG_CH_r        0xAF    /* 'r' */

void smg_init(void);

/** @brief 扫描一位数码管。**只能由定时器中断调用**，每 2ms 一次。 */
void smg_scan_tick(void);

/** @brief 全部熄灭。 */
void smg_clear(void);

/** @brief 直接写某一位的段码。 */
void smg_set_seg(u8 idx, u8 seg);

/** @brief 显示数字 0~9，dot 非 0 时点亮该位的小数点。 */
void smg_set_digit(u8 idx, u8 num, u8 dot);

/** @brief 显示一个小数点（把某个数字位的小数点点亮）。 */
void smg_show_blank(u8 idx);

/** @brief 取数字 0~9 的段码。 */
u8   smg_seg_of(u8 num);

#endif /* __SMG_H__ */
