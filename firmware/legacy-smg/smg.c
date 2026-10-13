/******************************************************************************
 * @file    smg.c
 * @brief   数码管动态扫描实现。
 *
 *  硬件结构（普中开发板）：
 *      P0.0~P0.7  ──经 74HC245 缓冲──▶  数码管的 a b c d e f g dp（共阳，低电平点亮）
 *      P2.4 P2.3 P2.2 ──▶ 74HC138 的 C B A ──▶ 8 路位选，低电平选中的那一位亮
 *
 *  ★ 动态扫描的关键是"消隐"：
 *    如果直接改位选再送新段码，在改位选的那一瞬间，旧段码还挂在 P0 上，
 *    于是新选中的那一位会短暂显示出上一位的内容 —— 这就是"重影"。
 *    正确顺序是：**先送 0xFF 把段全部熄灭，再改位选，最后送新段码。**
 *
 *  ★ 刷新率：
 *    每 2ms 扫一位，8 位一轮 = 16ms = 62.5Hz，远高于人眼闪烁感知阈值（约 50Hz）。
 ******************************************************************************/

#include "smg.h"

/* 0~9 的共阳段码：0xC0 = 1100 0000 → a~f 亮、g 灭 = "0" */
static u8 code SEG_DIGIT[10] = {
    0xC0, 0xF9, 0xA4, 0xB0, 0x99,
    0x92, 0x82, 0xF8, 0x80, 0x90
};

static u8 g_disp[SMG_DIGITS];       /* 段码缓冲，主循环写、中断读 */
static u8 g_scan = 0;               /* 当前扫描到第几位 */

/* 设置 74HC138 的 A/B/C，决定点亮第 idx 位 */
static void smg_select(u8 idx)
{
    u8 a, b, c;

#if SMG_BIT_REVERSE
    /* 有些板子把 74HC138 的 C 接在 P2.2 上，位序是反的 */
    a = (u8)((idx >> 2) & 0x01);
    b = (u8)((idx >> 1) & 0x01);
    c = (u8)(idx & 0x01);
#else
    a = (u8)(idx & 0x01);
    b = (u8)((idx >> 1) & 0x01);
    c = (u8)((idx >> 2) & 0x01);
#endif

    if (a) { SMG_A = 1; } else { SMG_A = 0; }
    if (b) { SMG_B = 1; } else { SMG_B = 0; }
    if (c) { SMG_C = 1; } else { SMG_C = 0; }
}

void smg_init(void)
{
    u8 i;
    for (i = 0; i < SMG_DIGITS; i++) {
        g_disp[i] = SEG_BLANK;
    }
    g_scan = 0;
    SMG_SEG_PORT = SEG_BLANK;
}

/* 由 Timer0 中断每 2ms 调用一次 */
void smg_scan_tick(void)
{
    SMG_SEG_PORT = SEG_BLANK;       /* ① 先消隐，防重影 */
    smg_select(g_scan);             /* ② 再改位选 */
    SMG_SEG_PORT = g_disp[g_scan];  /* ③ 最后送新段码 */

    g_scan++;
    if (g_scan >= SMG_DIGITS) {
        g_scan = 0;
    }
}

void smg_clear(void)
{
    u8 i;
    for (i = 0; i < SMG_DIGITS; i++) {
        g_disp[i] = SEG_BLANK;
    }
}

void smg_set_seg(u8 idx, u8 seg)
{
    if (idx < SMG_DIGITS) {
        g_disp[idx] = seg;
    }
}

u8 smg_seg_of(u8 num)
{
    return SEG_DIGIT[num % 10];
}

void smg_set_digit(u8 idx, u8 num, u8 dot)
{
    u8 s = SEG_DIGIT[num % 10];

    if (dot) {
        s = (u8)(s & 0x7F);         /* 共阳：清掉 dp 位就点亮小数点 */
    }
    smg_set_seg(idx, s);
}

void smg_show_blank(u8 idx)
{
    smg_set_seg(idx, SEG_BLANK);
}
