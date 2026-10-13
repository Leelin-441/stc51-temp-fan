/******************************************************************************
 * @file    key.c
 * @brief   两个按键的消抖与短按/长按识别。
 *
 *  ★ 为什么把按键扫描放在定时器中断里（每 20ms）而不是主循环里？
 *    因为主循环偶尔会很忙（比如自检程序、或者未来加入更耗时的任务），
 *    按键放在主循环里就会漏按。放进中断后，无论主循环在干什么，
 *    按键都能以稳定的 20ms 节奏被采样。
 *
 *  ★ 为什么事件用"计数器"而不是"标志位"？
 *    标志位只能记住"有一次按键"，如果用户连按 3 次而主循环还没来得及处理，
 *    后 2 次就丢了。计数器（上限 200）能把这些事件排好队，一个都不丢。
 ******************************************************************************/

#include "key.h"

#define KEY_COUNT       2
#define KEY_REPEAT_MS   200     /* 长按后每 200ms 自动重复一次（用于快速调温度） */
#define EVENT_MAX       200

static u8  s_stable[KEY_COUNT];     /* 消抖后的电平：0=按下，1=松开 */
static u8  s_cnt[KEY_COUNT];        /* 消抖计数 */
static u8  s_pressed[KEY_COUNT];
static u8  s_long_fired[KEY_COUNT];
static u16 s_hold_ms[KEY_COUNT];
static u16 s_repeat_ms[KEY_COUNT];
static u8  s_evt_short[KEY_COUNT];
static u8  s_evt_long[KEY_COUNT];

/* 读按键的原始电平：返回 0 表示按下 */
static u8 key_raw(u8 which)
{
    if (which == KEY_SET_ID) {
        return (KEY_SET == KEY_PRESSED) ? 0 : 1;
    }
    return (KEY_UP == KEY_PRESSED) ? 0 : 1;
}

void key_init(void)
{
    u8 i;
    for (i = 0; i < KEY_COUNT; i++) {
        s_stable[i]     = 1;
        s_cnt[i]        = 0;
        s_pressed[i]    = 0;
        s_long_fired[i] = 0;
        s_hold_ms[i]    = 0;
        s_repeat_ms[i]  = 0;
        s_evt_short[i]  = 0;
        s_evt_long[i]   = 0;
    }
}

/* 由 Timer0 中断每 20ms 调用一次 */
void key_scan_tick(void)
{
    u8 k;
    u8 raw;

    for (k = 0; k < KEY_COUNT; k++) {

        raw = key_raw(k);

        if (raw != s_stable[k]) {
            s_cnt[k]++;
            if (s_cnt[k] >= 2) {                /* 连续 2 次(40ms)同向 → 认定稳定 */
                s_cnt[k]    = 0;
                s_stable[k] = raw;

                if (raw == 0) {                 /* 刚被按下 */
                    s_pressed[k]    = 1;
                    s_hold_ms[k]    = 0;
                    s_repeat_ms[k]  = 0;
                    s_long_fired[k] = 0;
                } else {                        /* 刚被松开 */
                    if (s_pressed[k] && !s_long_fired[k]) {
                        if (s_evt_short[k] < EVENT_MAX) {
                            s_evt_short[k]++;   /* 没触发过长按 → 这是一次短按 */
                        }
                    }
                    s_pressed[k] = 0;
                }
            }
        } else {
            s_cnt[k] = 0;
        }

        if (s_pressed[k]) {
            s_hold_ms[k] += KEY_SCAN_MS;

            if (!s_long_fired[k] && s_hold_ms[k] >= KEY_LONG_MS) {
                s_long_fired[k] = 1;
                s_repeat_ms[k]  = 0;
                if (s_evt_long[k] < EVENT_MAX) {
                    s_evt_long[k]++;
                }
            }

            /* 只有 UP 键支持"按住连发"，用来快速把目标温度拉到底。
               SET 键如果也连发，模式会被疯狂来回切换。 */
            if (s_long_fired[k] && (k == KEY_UP_ID)) {
                s_repeat_ms[k] += KEY_SCAN_MS;
                if (s_repeat_ms[k] >= KEY_REPEAT_MS) {
                    s_repeat_ms[k] = 0;
                    if (s_evt_short[k] < EVENT_MAX) {
                        s_evt_short[k]++;
                    }
                }
            }
        }
    }
}

u8 key_get_event(u8 which)
{
    u8 v = KEY_EVT_NONE;

    if (which >= KEY_COUNT) {
        return KEY_EVT_NONE;
    }

    ET0 = 0;                                /* 计数器是"读-改-写"，短暂关中断防止丢事件 */
    if (s_evt_short[which] > 0) {
        s_evt_short[which]--;
        v = KEY_EVT_SHORT;
    } else if (s_evt_long[which] > 0) {
        s_evt_long[which]--;
        v = KEY_EVT_LONG;
    }
    ET0 = 1;

    return v;
}

u8 key_is_down(u8 which)
{
    if (which >= KEY_COUNT) {
        return 0;
    }
    return s_pressed[which];
}
