/******************************************************************************
 * @file    app_min.c
 * @brief   ★ 4KB 精简版应用层 —— 给只有 4KB Flash 的芯片（如 STC89C51RC）用。
 *
 *  ======================== 和完整版 app.c 的关系 ========================
 *  对外提供的接口**完全相同**（app_init / app_banner / app_task），
 *  所以 main.c、timer.c 一行都不用改 —— 只是链接的时候把 app.o 换成 app_min.o。
 *
 *  控制算法**完全一致**：
 *      四档阶梯回差、报警锁存、故障保护，
 *      所有门限仍然只从 config.h 读（LV_ENTER_* / LV_EXIT_* / ALARM_*），
 *      不另立一套参数，所以 tools/sim_controller.py 的等价仿真结论依然适用。
 *
 *  ======================== 为了塞进 4KB 砍掉了什么 ========================
 *      ✗ 串口通信（驱动 387 + 命令行解析 1044 + 每秒上报帧 224）≈ 1650 字节
 *      ✗ SIM 模拟温度注入                                     ≈ 100 字节
 *      ✗ 手动模式 MODE_MANUAL                                 ≈  80 字节
 *      ✗ 字符串名表（sensor_str / app_state_name）             ≈ 150 字节
 *
 *  ======================== 保留的核心功能（作业要求一个不少）===============
 *      ✓ DS18B20 持续测温                    ← 输入条件 1（环境在变）
 *      ✓ 按键 K3/K4 调节目标温度              ← 输入条件 2（人在操作）
 *      ✓ 四档阶梯回差 → 自动选择不同转速       ← "根据输入变化自动执行不同动作"
 *      ✓ 软件 PWM 驱动 5V 电机分档调速
 *      ✓ LCD1602 实时显示温度/目标/状态/占空比
 *      ✓ 双色 LED + 蜂鸣器声光提示
 *      ✓ 超温报警（锁存 ≥5 秒，强制全速）
 *      ✓ 传感器故障保护（电机强制停 + 长鸣）
 *
 *  ★ 按键操作方式（精简版特有）
 *      K3(SET) 短按 = 目标温度 −1.0℃，长按 = −5.0℃
 *      K4(UP)  短按 = 目标温度 +1.0℃，长按 = +5.0℃
 *    现场演示想快速把目标温度从 35℃ 压到 20℃：连按 3 次 K3 长按即可。
 ******************************************************************************/

#include "config.h"
#include "app.h"
#include "timer.h"
#include "motor.h"
#include "lcd1602.h"
#include "key.h"
#include "indicator.h"
#include "ds18b20.h"

/* ---- 查表：档位 → 占空比 ----
   用 code 空间的常量表，比一串 if-else 省 Flash（C51 会生成 MOVC 取表指令）。 */
static u8 code DUTY_TAB[4] = {
    MOTOR_DUTY_STOP, MOTOR_DUTY_LOW, MOTOR_DUTY_MID, MOTOR_DUTY_HIGH
};

/* ---- 查表：自动档位 → 对外状态名 ---- */
static u8 code STATE_TAB[4] = { ST_STOP, ST_LOW, ST_MID, ST_HIGH };

/* ---- 状态 ---- */
static u16 g_tset;              /* 目标温度 ×10 */
static s16 g_temp;              /* 实测温度 ×10 */
static u8  g_level;             /* 自动档位 LV_xxx */
static u8  g_duty;              /* 当前占空比百分数 */
static u8  g_state;             /* ST_xxx */
static u8  g_fault;
static u8  g_alarm;
static u16 g_alarm_start;

/* ---- 软件定时器与闪烁相位 ---- */
static u16 g_last_ctrl;
static u16 g_last_disp;
static u8  g_blink;             /* 300ms 翻转一次，用于整屏闪烁和红灯闪 */
static u8  g_blink_cnt;
static u8  g_buz_phase;         /* 100ms 翻转一次，报警时急促鸣叫 */
static u8  g_beep_ticks;        /* 状态跳变时短鸣的剩余节拍数 */

/* ==========================================================================
 * 目标温度限幅（K3/K4 都走这里，避免写两份）
 * ========================================================================== */
static void tset_add(s16 d)
{
    s16 v = (s16)((s16)g_tset + d);

    if (v < TSET_MIN) {
        v = TSET_MIN;
    } else if (v > TSET_MAX) {
        v = TSET_MAX;
    }
    g_tset = (u16)v;
}

/* ==========================================================================
 * 核心控制：与 app.c 的 control_update() 算法完全一致
 *   第 1 步 取温度 → 第 2 步 判故障 → 第 3 步 报警锁存
 *   第 4 步 阶梯回差定档 → 第 5 步 合成状态与占空比 → 第 6 步 输出
 * ========================================================================== */
static void control_update(void)
{
    u16 now = get_ms16();
    u8  cond;
    u8  prev;
    s16 t;

    prev = g_state;

    /* ---- 第 1、2 步：温度与故障 ---- */
    g_temp = ds_get_temp();
    if (ds_get_fail_count() >= DS_FAIL_LIMIT) {
        g_fault = 1;
    } else {
        g_fault = 0;
    }

    /* ---- 第 3 步：报警锁存（带最短保持时间） ---- */
    cond = 0;
    if (g_fault) {
        cond = 1;                                   /* 故障也算报警条件 */
    }
    if (g_temp >= (s16)(g_tset + ALARM_OFFSET)) {
        cond = 1;                                   /* 超温 */
    }

    if (!g_alarm) {
        if (cond) {
            g_alarm = 1;
            g_alarm_start = now;
        }
    } else {
        if ((!cond) &&
            (g_temp <= (s16)(g_tset + ALARM_EXIT)) &&
            ((u16)(now - g_alarm_start) >= ALARM_HOLD_MS)) {
            g_alarm = 0;
        }
    }

    /* ---- 第 4 步：阶梯回差决定档位 ----
       每个档位都有独立的「进档门限」和「退档门限」，中间留死区。
       这就是把临界抖动时的切换次数从 100 次降到 1 次的关键。 */
    t = g_temp;
    switch (g_level) {
    case LV_STOP:
        if (t >= (s16)(g_tset + LV_ENTER_LOW)) {
            g_level = LV_LOW;
        }
        break;
    case LV_LOW:
        if (t >= (s16)(g_tset + LV_ENTER_MID)) {
            g_level = LV_MID;
        } else if (t <= (s16)(g_tset + LV_EXIT_LOW)) {
            g_level = LV_STOP;
        }
        break;
    case LV_MID:
        if (t >= (s16)(g_tset + LV_ENTER_HIGH)) {
            g_level = LV_HIGH;
        } else if (t <= (s16)(g_tset + LV_EXIT_MID)) {
            g_level = LV_LOW;
        }
        break;
    default:                                        /* LV_HIGH */
        if (t <= (s16)(g_tset + LV_EXIT_HIGH)) {
            g_level = LV_MID;
        }
        break;
    }

    /* ---- 第 5 步：合成状态与占空比（安全优先） ---- */
    if (g_fault) {
        g_state = ST_FAULT;                         /* 传感器不可信 → 电机停 */
        g_duty  = MOTOR_DUTY_STOP;
    } else if (g_alarm) {
        g_state = ST_ALARM;                         /* 报警 → 强制全速散热 */
        g_duty  = MOTOR_DUTY_HIGH;
    } else {
        g_state = STATE_TAB[g_level];
        g_duty  = DUTY_TAB[g_level];
    }

    motor_set_duty(g_duty);

    /* ---- 第 6 步：状态跳变 → 短鸣一声 ---- */
    if (g_state != prev) {
        g_beep_ticks = 3;                           /* 3 个节拍 = 300ms 短鸣 */
    }
}

/* ==========================================================================
 * 按键
 * ========================================================================== */
static void handle_keys(void)
{
    u8 e;

    e = key_get_event(KEY_SET_ID);
    if (e == KEY_EVT_SHORT) {
        tset_add(-10);
    } else if (e == KEY_EVT_LONG) {
        tset_add(-50);
    }

    e = key_get_event(KEY_UP_ID);
    if (e == KEY_EVT_SHORT) {
        tset_add(10);
    } else if (e == KEY_EVT_LONG) {
        tset_add(50);
    }
}

/* ==========================================================================
 * 显示
 * ========================================================================== */
static char code *state_short(void)
{
    switch (g_state) {
    case ST_STOP:  return "STOP ";
    case ST_LOW:   return "LOW  ";
    case ST_MID:   return "MID  ";
    case ST_HIGH:  return "HIGH ";
    case ST_ALARM: return "ALARM";
    default:       return "FAULT";
    }
}

static void render(void)
{
    /* 报警时整屏闪空，现场一眼就能看到 */
    if (g_alarm && g_blink) {
        lcd_goto(0, 0);
        lcd_str("                ");
        lcd_goto(1, 0);
        lcd_str("                ");
        return;
    }

    /* 第 1 行：实测温度 + 目标温度 */
    lcd_goto(0, 0);
    if (g_state == ST_FAULT) {
        lcd_str("T=  Err ");
    } else {
        lcd_str("T=");
        lcd_temp1(g_temp);                  /* 固定 5 列，如 " 35.5" */
        lcd_str("C");
    }
    lcd_str(" SET=");
    lcd_ufixed((u16)(g_tset / 10U), 2U);
    lcd_pad(16U);

    /* 第 2 行：状态 + 占空比 */
    lcd_goto(1, 0);
    lcd_str(state_short());
    lcd_str(" SPD=");
    lcd_ufixed((u16)g_duty, 3U);
    lcd_str("%");
    lcd_pad(16U);
}

/* ==========================================================================
 * 声光指示
 * ========================================================================== */
static void led_task(void)
{
    if (g_state == ST_FAULT) {
        led_green(0);
        led_red(1);                     /* 故障：红灯常亮 */
    } else if (g_alarm) {
        led_green(0);
        led_red(g_blink);               /* 报警：红灯 ~2Hz 闪 */
    } else {
        led_red(0);
        led_green((u8)(g_duty > 0U));   /* 正常：电机在转就亮绿灯 */
    }
}

static void buzzer_task(void)
{
    if (g_state == ST_FAULT) {
        beep(1);                        /* 故障：长鸣 */
        return;
    }
    if (g_alarm) {
        beep(g_buz_phase);              /* 报警：急促鸣叫 */
        return;
    }
    if (g_beep_ticks) {
        g_beep_ticks--;
        beep(1);                        /* 状态跳变的短鸣 */
        return;
    }
    beep(0);
}

/* ==========================================================================
 * 对外接口（与 app.c 同名，供 main.c 调用）
 * ========================================================================== */
void app_banner(void)
{
    lcd_clear();
    lcd_goto(0, 0);
    lcd_str("TEMP-FAN  (LITE)");
    lcd_goto(1, 0);
    lcd_str("K3:-1C  K4:+1C  ");
}

void app_init(void)
{
    g_tset        = TSET_DEF;
    g_temp        = 0;
    g_level       = LV_STOP;
    g_duty        = MOTOR_DUTY_STOP;
    g_state       = ST_INIT;
    g_fault       = 0;
    g_alarm       = 0;
    g_alarm_start = 0;
    g_blink       = 0;
    g_blink_cnt   = 0;
    g_buz_phase   = 0;
    g_beep_ticks  = 0;
    g_last_ctrl   = get_ms16();
    g_last_disp   = g_last_ctrl;

    motor_set_duty(MOTOR_DUTY_STOP);
    ds_force_sample();
}

void app_task(void)
{
    u16 now;

    ds_task();                          /* DS18B20 状态机，必须一直调 */

    now = get_ms16();
    if ((u16)(now - g_last_ctrl) < TICK_MS) {
        return;                         /* 还没到 100ms 的判定节拍 */
    }
    g_last_ctrl = now;

    /* ---- 以下每 100ms 执行一次 ---- */

    /* 闪烁相位：每 3 个节拍翻转一次 = 300ms */
    if (g_blink_cnt >= 2U) {
        g_blink_cnt = 0;
        g_blink = (u8)(!g_blink);
    } else {
        g_blink_cnt++;
    }
    g_buz_phase = (u8)(!g_buz_phase);   /* 每 100ms 翻转 → 报警时 5Hz 急促鸣叫 */

    handle_keys();
    control_update();
    led_task();
    buzzer_task();

    /* ---- 显示每 200ms 刷一次 ---- */
    if ((u16)(now - g_last_disp) >= OLED_MS) {
        g_last_disp = now;
        render();
    }
}
