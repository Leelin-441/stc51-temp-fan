/******************************************************************************
 * @file    app.c
 * @brief   应用层：温控状态机 + 数码管显示 + 串口协议。
 *
 *  ============================ 设计要点 ============================
 *  1) 四档阶梯回差（迟滞）
 *     如果四档共用一个阈值，温度只要在临界点抖 0.1℃，档位就会来回跳，
 *     电机跟着忽快忽慢，既吵又伤电机。这里给每一档都配了"进档阈值"和
 *     "退档阈值"，中间留 2℃ 的死区：
 *         停机 ──(T≥Tset-2.0)──▶ 低速 ──(T≥Tset)──────▶ 中速 ──(T≥Tset+3.0)──▶ 全速
 *         停机 ◀──(T≤Tset-4.0)── 低速 ◀──(T≤Tset-2.0)── 中速 ◀──(T≤Tset+1.0)── 全速
 *
 *  2) 档位是"一步一步"爬的，不是直接跳
 *     温度突然超很多时，档位也是每 100ms 升一级（停机→低速→中速→全速，约 300ms）。
 *     这相当于一个天然的软启动，电机不会一下子全速冲击。
 *
 *  3) 安全优先：传感器故障时电机必须停
 *     DS18B20 读不到时，绝不能拿"最后一次的温度"继续瞎转 ——
 *     万一是电机自己把传感器烤坏了，继续转就是火上浇油。
 *     所以 FAULT 状态强制 STOP + 蜂鸣器长鸣告警。
 *
 *  4) 判定层与执行层解耦
 *     状态机只产出一个"档位"，由 duty_of_level() 翻译成占空比，
 *     再由 motor_set_duty() 落到硬件。换电机、换成继电器开关，
 *     都只改 duty_of_level() 这一个函数。
 *  ==================================================================
 ******************************************************************************/

#include "app.h"
#include "timer.h"
#include "motor.h"
#include "lcd1602.h"
#include "key.h"
#include "uart.h"
#include "ds18b20.h"
#include "indicator.h"

/* ==========================================================================
 * 全局状态
 * --------------------------------------------------------------------------
 * ★ 为什么这些变量全部加了 xdata ？
 *   STC89C52 只有 **128 字节**可直接寻址的 data 空间（0x00~0x7F），
 *   光本文件的状态变量就有 80 多字节，再加上其它模块的静态变量，
 *   链接时会直接报：
 *       *** ERROR L107: ADDRESS SPACE OVERFLOW
 *           SPACE:   DATA
 *   实测不加 xdata 时 data 用量是 165 字节，整整超了 37 字节。
 *   把这些变量挪到 xdata（片内扩展 RAM，用 MOVX 间接访问）之后，
 *   data 只留给中断服务程序，栈也有充足空间。
 *
 * ★ 为什么不用 LARGE 内存模型偷懒？
 *   两个原因：
 *     1) LARGE 会改变函数调用约定，与中断模块（timer/smg/key/motor/uart）
 *        混链时链接器会报 L14 INCOMPATIBLE MEMORY MODEL；
 *     2) 中断服务程序里的变量必须待在 data 里才够快 ——
 *        MOVX 是 2 个机器周期，200us 一次的中断经不起这种拖慢。
 *   所以只把这个"主循环专用"的文件里的变量改成 xdata。
 *
 * ★ 性能影响：
 *   这些变量只在主循环里访问（判定周期 100ms、显示 200ms、上报 1s），
 *   MOVX 多出来的那点时间完全可以忽略。
 * ========================================================================== */
static u16 xdata g_tset;            /* 目标温度 ×10（350 = 35.0℃） */
static u8  xdata g_mode;            /* MODE_AUTO / MODE_MANUAL */
static u8  xdata g_level;           /* 自动模式下的档位 */
static u8  xdata g_manual_level;    /* 手动模式下的档位 */
static u8  xdata g_level_eff;       /* 对外汇报的有效档位 */
static u8  xdata g_duty;            /* 当前占空比百分数 */

static s16 xdata g_temp;            /* 当前温度 ×10 */
static u8  xdata g_sensor_status;   /* DS_OK / DS_NOPRESENT / ... */
static u8  xdata g_fault;           /* 传感器故障标志 */
static u8  xdata g_state;           /* ST_xxx */

static u8  xdata g_sim_enable;      /* 仿真模式 */
static s16 xdata g_sim_temp;

static u8  xdata g_alarm;
static u16 xdata g_alarm_start;

static u8  xdata g_buz_on;

/* 软件定时器（全部用"时间差比较"，天然处理 u16 回绕） */
static u16 xdata g_last_ctrl;
static u16 xdata g_last_disp;
static u16 xdata g_last_log;
static u16 xdata g_led_ms;
static u16 xdata g_led_fast_ms;
static u16 xdata g_blink_ms;
static u16 xdata g_buz_ms;
static u16 xdata g_beep_until;

static u8  xdata g_led_phase;
static u8  xdata g_led_fast;
static u8  g_blink;                 /* 这个留在 data 里，够用 */
static u8  xdata g_buz_phase;
static u8  xdata g_force_log;

/* 命令行缓冲区：40 字节，是单个最大的一块，必须放 xdata。
   注意：既然它不在 data 空间，下面所有指向它的指针都要写成 char xdata *，
   否则 C51 会报 C259 pointer: different mspace，运行时会取错地址。 */
static char xdata s_line[LINE_MAX];
static u8   data  s_line_len;

/* ==========================================================================
 * 小工具（不引入 string.h，省 Flash）
 * ========================================================================== */
static u8 code_len(char code *s)
{
    u8 n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

/* s 是否以 prefix 开头 */
static u8 starts_with(char xdata *s, char code *prefix)
{
    u8 i = 0;
    while (prefix[i] != '\0') {
        if (s[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

/* 把形如 "42.5" / "-3" / "30" 的文本解析成 ×10 定点数 */
static u8 parse_num(char xdata *p, s16 *out)
{
    u8  neg = 0;
    u8  any = 0;
    u8  fd  = 0;
    u16 ip  = 0;
    u16 fp  = 0;

    if (*p == '-') {
        neg = 1;
        p++;
    }
    while ((*p >= '0') && (*p <= '9')) {
        ip = (u16)(ip * 10U + (u8)(*p - '0'));
        p++;
        any = 1;
    }
    if (*p == '.') {
        p++;
        while ((*p >= '0') && (*p <= '9') && (fd < 2)) {
            fp = (u16)(fp * 10U + (u8)(*p - '0'));
            p++;
            fd++;
            any = 1;
        }
    }
    if (!any) {
        return 0;
    }

    /* 统一成"一位小数" */
    if (fd == 0) {
        fp = 0;
    } else if (fd == 1) {
        fp = (u16)(fp * 10U);
    } else {
        fp = (u16)((fp + 5U) / 10U);
    }

    ip = (u16)(ip * 10U + fp);
    *out = neg ? (s16)(0 - (s16)ip) : (s16)ip;
    return 1;
}

/**
 * @brief 在命令行里找形如  KEY=value 或 KEY value 的项。
 * @note  要求 KEY 前面必须是行首或空格。
 *        否则 "SET T=30" 里 "SET" 中的那个 T 会被误当成键。
 */
static u8 find_key(char xdata *s, char code *key, s16 *out)
{
    char xdata *p = s;
    u8  klen = code_len(key);
    u8  i;

    while (*p != '\0') {
        if ((p == s) || (*(p - 1) == ' ')) {
            i = 0;
            while ((i < klen) && (p[i] == key[i])) {
                i++;
            }
            if (i == klen) {
                p += klen;
                while (*p == ' ') {
                    p++;
                }
                if (*p == '=') {
                    p++;
                    while (*p == ' ') {
                        p++;
                    }
                }
                return parse_num(p, out);
            }
        }
        p++;
    }
    return 0;
}

/* 行里是否含有某个独立单词（忽略大小写） */
static u8 has_word(char xdata *s, char code *word)
{
    char xdata *p = s;
    u8  wlen = code_len(word);
    u8  i;
    u8  a, b;

    while (*p != '\0') {
        if ((p == s) || (*(p - 1) == ' ')) {
            i = 0;
            while (i < wlen) {
                a = (u8)p[i];
                b = (u8)word[i];
                if ((a >= 'a') && (a <= 'z')) {
                    a = (u8)(a - 32);
                }
                if ((b >= 'a') && (b <= 'z')) {
                    b = (u8)(b - 32);
                }
                if ((a != b) || (a == 0)) {
                    break;
                }
                i++;
            }
            if (i == wlen) {
                return 1;
            }
        }
        p++;
    }
    return 0;
}

/* ==========================================================================
 * 档位 → 占空比（判定层与执行层之间的唯一接口）
 * ========================================================================== */
static u8 duty_of_level(u8 lv)
{
    switch (lv) {
    case LV_LOW:  return MOTOR_DUTY_LOW;
    case LV_MID:  return MOTOR_DUTY_MID;
    case LV_HIGH: return MOTOR_DUTY_HIGH;
    default:      return MOTOR_DUTY_STOP;
    }
}

static char code *sensor_str(void)
{
    switch (g_sensor_status) {
    case DS_OK:        return "OK";
    case DS_NOPRESENT: return "NOPRESENT";
    case DS_BADCRC:    return "BADCRC";
    default:           return "NOINIT";
    }
}

char code *app_state_name(void)
{
    switch (g_state) {
    case ST_STOP:   return "STOP";
    case ST_LOW:    return "LOW";
    case ST_MID:    return "MID";
    case ST_HIGH:   return "HIGH";
    case ST_ALARM:  return "ALARM";
    case ST_FAULT:  return "FAULT";
    case ST_MANUAL: return "MANUAL";
    default:        return "INIT";
    }
}

u8 app_get_state(void)
{
    return g_state;
}

/* ==========================================================================
 * 参数设置
 * ========================================================================== */
static void set_tset(s16 v)
{
    if (v < TSET_MIN) {
        v = TSET_MIN;
    }
    if (v > TSET_MAX) {
        v = TSET_MAX;
    }
    g_tset = (u16)v;
}

static void beep_short(void)
{
    g_beep_until = (u16)(get_ms16() + BEEP_TICK_MS);
}

/* ==========================================================================
 * 开始拼一帧新的串口输出
 * --------------------------------------------------------------------------
 * ★ 为什么不能直接调 uart_tx_reset() ？
 *   串口发送是由串口中断逐字节推进的：中断里判断
 *       if (s_tx_idx < s_tx_len) { SBUF = s_tx_buf[s_tx_idx]; s_tx_idx++; }
 *   如果上一帧还只发了一半，就调用 uart_tx_reset() 把 s_tx_len 清成 0，
 *   中断立刻认为"发完了"而停下 —— 串口上就出现**半截帧接一整帧**。
 *
 *   实测现象（真机上抓到的）：
 *       $S,$S,26681,271,350,0,0,STOP,AUTO,1,0,OK
 *          ↑ 前一帧的残骸
 *
 *   所以每次开始拼新帧前，必须先等上一帧发完。
 *   一帧最长约 52 字符，9600 波特率下需要约 54ms，这里给 100ms 上限，
 *   等不到就放弃（宁可丢一帧，也绝不把主循环卡死）。
 * ========================================================================== */
static void tx_begin(void)
{
    u16 t0 = get_ms16();

    while (uart_tx_busy()) {
        if ((u16)(get_ms16() - t0) > 100U) {
            break;
        }
    }
    uart_tx_reset();
}

/* ==========================================================================
 * 核心：状态判定
 * ========================================================================== */
static void control_update(void)
{
    u16 now = get_ms16();
    s16 enter_low, exit_low, enter_mid, exit_mid, enter_high, exit_high;
    u8  cond;
    u8  prev_state;

    prev_state = g_state;

    /* ---------- 第 1 步：数据源（真实传感器 or 注入的模拟值） ---------- */
    if (g_sim_enable) {
        g_temp = g_sim_temp;
        g_sensor_status = DS_OK;
    } else {
        g_temp = ds_get_temp();
        g_sensor_status = ds_get_status();
    }

    /* ---------- 第 2 步：传感器故障判定 ---------- */
    g_fault = 0;
    if (!g_sim_enable) {
        if (ds_get_fail_count() >= DS_FAIL_LIMIT) {
            g_fault = 1;
        }
    }

    /* ---------- 第 3 步：报警锁存（带最短保持时间） ---------- */
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

    /* ---------- 第 4 步：阶梯回差决定档位 ---------- */
    if (g_mode == MODE_AUTO) {
        enter_low  = (s16)(g_tset + LV_ENTER_LOW);
        exit_low   = (s16)(g_tset + LV_EXIT_LOW);
        enter_mid  = (s16)(g_tset + LV_ENTER_MID);
        exit_mid   = (s16)(g_tset + LV_EXIT_MID);
        enter_high = (s16)(g_tset + LV_ENTER_HIGH);
        exit_high  = (s16)(g_tset + LV_EXIT_HIGH);

        switch (g_level) {
        case LV_STOP:
            if (g_temp >= enter_low) {
                g_level = LV_LOW;
            }
            break;
        case LV_LOW:
            if (g_temp >= enter_mid) {
                g_level = LV_MID;
            } else if (g_temp <= exit_low) {
                g_level = LV_STOP;
            }
            break;
        case LV_MID:
            if (g_temp >= enter_high) {
                g_level = LV_HIGH;
            } else if (g_temp <= exit_mid) {
                g_level = LV_LOW;
            }
            break;
        case LV_HIGH:
            if (g_temp <= exit_high) {
                g_level = LV_MID;
            }
            break;
        default:
            g_level = LV_STOP;
            break;
        }
    }

    /* ---------- 第 5 步：合成状态与占空比 ---------- */
    if (g_fault) {
        /* 安全优先：传感器不可信，电机一律停转 */
        g_state     = ST_FAULT;
        g_level_eff = LV_STOP;
        g_duty      = MOTOR_DUTY_STOP;
    } else if (g_alarm) {
        /* 报警：强制全速散热 */
        g_state     = ST_ALARM;
        g_level_eff = LV_HIGH;
        g_duty      = MOTOR_DUTY_HIGH;
    } else if (g_mode == MODE_MANUAL) {
        g_state     = ST_MANUAL;
        g_level_eff = g_manual_level;
        g_duty      = duty_of_level(g_manual_level);
    } else {
        g_level_eff = g_level;
        g_duty      = duty_of_level(g_level);
        switch (g_level) {
        case LV_LOW:  g_state = ST_LOW;  break;
        case LV_MID:  g_state = ST_MID;  break;
        case LV_HIGH: g_state = ST_HIGH; break;
        default:      g_state = ST_STOP; break;
        }
    }

    motor_set_duty(g_duty);

    /* ---------- 第 6 步：状态跳变 → 短鸣一声 + 立刻补发一帧日志 ---------- */
    if (g_state != prev_state) {
        beep_short();
        g_force_log = 1;
    }
}

/* ==========================================================================
 * 串口命令处理
 * ========================================================================== */
static void handle_line(void)
{
    char xdata *p = s_line;
    s16 v;

    while (*p == ' ') {
        p++;
    }
    if (*p == '\0') {
        return;
    }

    /* ---- SIM ---- */
    if (starts_with(p, "SIM")) {
        if (has_word(p, "OFF")) {
            g_sim_enable = 0;
        } else {
            if (find_key(p, "T", &v)) {
                g_sim_temp = v;
            }
            g_sim_enable = 1;
        }
        g_force_log = 1;
        return;
    }

    /* ---- MODE ---- */
    if (starts_with(p, "MODE")) {
        if (has_word(p, "MANUAL")) {
            g_mode = MODE_MANUAL;
        } else if (has_word(p, "AUTO")) {
            g_mode = MODE_AUTO;
        }
        g_force_log = 1;
        return;
    }

    /* ---- SET T=30.0 ---- */
    if (starts_with(p, "SET")) {
        if (find_key(p, "T", &v)) {
            set_tset(v);
        }
        g_force_log = 1;
        return;
    }

    /* ---- LEVEL 2 ---- */
    if (starts_with(p, "LEVEL")) {
        if (find_key(p, "LEVEL", &v)) {
            if (v < 0) {
                v = 0;
            }
            if (v > LV_HIGH) {
                v = LV_HIGH;
            }
            g_manual_level = (u8)v;
        }
        g_force_log = 1;
        return;
    }

    /* ---- STATUS ---- */
    if (starts_with(p, "STATUS")) {
        g_force_log = 1;
        return;
    }

    /* ---- RESET ---- */
    if (starts_with(p, "RESET")) {
        set_tset(TSET_DEF);
        g_mode         = MODE_AUTO;
        g_level        = LV_STOP;
        g_manual_level = LV_STOP;
        g_alarm        = 0;
        g_sim_enable   = 0;
        ds_force_sample();
        g_force_log    = 1;
        return;
    }

    /* 未知命令：把收到的内容原样回显，方便排查上位机发错。
       这里直接按下标遍历 s_line（xdata），不做指针传递 ——
       因为 s_line 在 xdata 空间，而 uart_tx_str() 要的是 code 空间的常量，
       传错会报 C259 并且运行时会取错地址、串口输出乱码。 */
    {
        u8 i;
        tx_begin();
        uart_tx_str("#ERR ");
        for (i = 0; i < s_line_len; i++) {
            uart_tx_ch((char)s_line[i]);
        }
        uart_tx_str("\r\n");
        uart_tx_flush();
    }
}

static void serial_task(void)
{
    u8 b;

    while (uart_has_char()) {
        b = uart_getc();

        if (b == (u8)'\n') {
            s_line[s_line_len] = '\0';
            handle_line();
            s_line_len = 0;
        } else if (b != (u8)'\r') {
            if (s_line_len < (u8)(LINE_MAX - 1)) {
                s_line[s_line_len] = (char)b;
                s_line_len++;
            } else {
                s_line_len = 0;         /* 行太长，丢弃重来 */
            }
        }
    }
}

/* ==========================================================================
 * 按键
 * ========================================================================== */
static void handle_keys(void)
{
    u8 e;

    /* ---- K3 (SET)：短按切换 自动/手动，长按恢复默认 ---- */
    e = key_get_event(KEY_SET_ID);
    if (e == KEY_EVT_SHORT) {
        if (g_mode == MODE_AUTO) {
            g_mode = MODE_MANUAL;
        } else {
            g_mode = MODE_AUTO;
        }
        beep_short();
        g_force_log = 1;
    } else if (e == KEY_EVT_LONG) {
        set_tset(TSET_DEF);
        g_mode         = MODE_AUTO;
        g_manual_level = LV_STOP;
        g_alarm        = 0;
        beep_short();
        g_force_log = 1;
    }

    /* ---- K4 (UP)：自动模式下调目标温度；手动模式下切档 ----
       长按会由中断里的"连发"逻辑每 200ms 产生一次短按事件，
       所以按住不放就是连续 +1，不需要在这里额外处理长按。 */
    e = key_get_event(KEY_UP_ID);
    if (e == KEY_EVT_SHORT) {
        if (g_mode == MODE_MANUAL) {
            if (g_manual_level < LV_HIGH) {
                g_manual_level++;
            } else {
                g_manual_level = LV_STOP;
            }
        } else {
            set_tset((s16)(g_tset + TSET_STEP));
        }
        beep_short();
        g_force_log = 1;
    }
}

/* ==========================================================================
 * LCD1602 显示
 *
 *   两行 × 16 字符的布局：
 *       0123456789ABCDEF
 *   行0  T= 35.5C  SET=35      ← 实测温度 + 目标温度
 *   行1  AUTO MID    P= 66     ← 运行模式 + 状态 + PWM 占空比
 *
 *   示例：
 *       T= 35.5C  SET=35        实测 35.5℃，目标 35℃
 *       AUTO HIGH   P=100       自动模式，全速档，占空比 100%
 *       AUTO ALARM  P=100       超温报警
 *       AUTO FAULT  P=  0       传感器故障，电机强制停
 *       MAN  HAND   P= 66       手动模式，档位 2
 *
 *   ★ 刷新时**绝不调用 lcd_clear()** —— 清屏会让整屏闪一下，
 *     看起来像在"抽搐"。正确做法是每次只把光标定位回行首，
 *     然后用新内容覆盖旧内容（等长覆盖，不会留下残影）。
 * ========================================================================== */
static char code *state_short(void)
{
    /* 状态名固定 5 个字符，方便对齐 */
    switch (g_state) {
    case ST_STOP:   return "STOP ";
    case ST_LOW:    return "LOW  ";
    case ST_MID:    return "MID  ";
    case ST_HIGH:   return "HIGH ";
    case ST_ALARM:  return "ALARM";
    case ST_FAULT:  return "FAULT";
    case ST_MANUAL: return "HAND ";     /* Hand = 手动 */
    default:        return "INIT ";
    }
}

static void render_display(void)
{
    /* 报警时整屏闪烁：把两行都填成空格，形成"空—满"交替的视觉效果 */
    if (g_alarm && g_blink) {
        lcd_goto(0, 0);
        lcd_str("                ");
        lcd_goto(1, 0);
        lcd_str("                ");
        return;
    }

    /* ---------------- 第 1 行：实测温度 + 目标温度 ---------------- */
    lcd_goto(0, 0);

    if (g_state == ST_FAULT) {
        /* 传感器读不到时不显示一个假温度，直接说明情况 */
        lcd_str("T=  Err ");
    } else {
        lcd_str("T=");
        lcd_temp1(g_temp);              /* 固定占 5 列，如 " 35.5" */
        lcd_str("C");
    }
    lcd_str("  SET=");
    lcd_ufixed((u16)(g_tset / 10U), 2U); /* 目标温度整数部分 20~60 */
    lcd_pad(16U);                        /* 补空格到第 16 列，清掉旧内容 */

    /* ---------------- 第 2 行：模式 + 状态 + 占空比 ---------------- */
    lcd_goto(1, 0);

    if (g_mode == MODE_MANUAL) {
        lcd_str("MAN ");
    } else {
        lcd_str("AUTO");
    }
    lcd_str(" ");
    lcd_str(state_short());             /* 固定 5 列 */
    lcd_str("  P=");
    lcd_ufixed((u16)g_duty, 3U);        /* 占空比百分数，右对齐 3 列 */
    lcd_pad(16U);
}

/* ==========================================================================
 * 指示灯
 *   绿灯：电机在转（或手动模式下闪烁表示"已接管"）
 *   红灯：报警 / 故障，闪烁
 * ========================================================================== */
static void led_task(void)
{
    switch (g_state) {
    case ST_STOP:
        led_green(0);
        led_red(0);
        break;

    case ST_LOW:
    case ST_MID:
    case ST_HIGH:
        led_green(1);
        led_red(0);
        break;

    case ST_ALARM:                      /* 红灯 2Hz 慢闪 */
        led_green(0);
        led_red(g_led_phase);
        break;

    case ST_FAULT:                      /* 红灯 5Hz 快闪，和报警区分开 */
        led_green(0);
        led_red(g_led_fast);
        break;

    case ST_MANUAL:                     /* 绿灯闪烁 = 手动接管 */
        led_green(g_led_phase);
        led_red(0);
        break;

    default:
        led_green(0);
        led_red(0);
        break;
    }
}

/* ==========================================================================
 * 蜂鸣器
 *   报警：150ms 通断（急促）
 *   故障：500ms 通断（长鸣，听起来更像"出事了"）
 *   状态跳变：短鸣 60ms
 * ========================================================================== */
static void buzzer_task(void)
{
    u16 now = get_ms16();
    u16 period;
    u8  want;

    period = 0;
    if (g_state == ST_ALARM) {
        period = ALARM_ON_MS;
    } else if (g_state == ST_FAULT) {
        period = 500U;
    }

    if (period > 0) {
        if ((u16)(now - g_buz_ms) >= period) {
            g_buz_ms = now;
            g_buz_phase = (u8)(!g_buz_phase);
        }
        want = g_buz_phase;
    } else if ((s16)(g_beep_until - now) > 0) {
        want = 1;
        g_buz_phase = 0;
    } else {
        want = 0;
        g_buz_phase = 0;
    }

    g_buz_on = want;
    beep(want);
}

/* ==========================================================================
 * 串口上报
 *   $S,<ms>,<T>,<SET>,<LEVEL>,<DUTY>,<ST>,<MODE>,<BUZ>,<SIM>,<SENSOR>
 * ========================================================================== */
static void log_task(void)
{
    u16 now = get_ms16();

    if (!g_force_log && ((u16)(now - g_last_log) < LOG_MS)) {
        return;
    }
    g_force_log = 0;
    g_last_log = now;

    tx_begin();
    uart_tx_str("$S,");
    uart_tx_u16(now);
    uart_tx_ch(',');
    uart_tx_s16(g_temp);
    uart_tx_ch(',');
    uart_tx_s16((s16)g_tset);
    uart_tx_ch(',');
    uart_tx_u16((u16)g_level_eff);
    uart_tx_ch(',');
    uart_tx_u16((u16)g_duty);
    uart_tx_ch(',');
    uart_tx_str(app_state_name());
    uart_tx_ch(',');
    if (g_mode == MODE_MANUAL) {
        uart_tx_str("MANUAL");
    } else {
        uart_tx_str("AUTO");
    }
    uart_tx_ch(',');
    uart_tx_u16((u16)g_buz_on);
    uart_tx_ch(',');
    uart_tx_u16((u16)g_sim_enable);
    uart_tx_ch(',');
    uart_tx_str(sensor_str());
    uart_tx_str("\r\n");
    uart_tx_flush();
}

/* ==========================================================================
 * 对外接口
 * ========================================================================== */
void app_banner(void)
{
    tx_begin();
    uart_tx_str("# STC51-TempFan v1.0 ready, baud=");
    uart_tx_u16((u16)UART_BAUD);
    uart_tx_str(", tset=");
    uart_tx_s16((s16)g_tset);
    uart_tx_str("\r\n");
    uart_tx_flush();
}

void app_init(void)
{
    g_tset          = TSET_DEF;
    g_mode          = MODE_AUTO;
    g_level         = LV_STOP;
    g_manual_level  = LV_STOP;
    g_level_eff     = LV_STOP;
    g_duty          = MOTOR_DUTY_STOP;

    g_temp          = 0;
    g_sensor_status = DS_NOINIT;
    g_fault         = 0;
    g_state         = ST_INIT;

    g_sim_enable    = 0;
    g_sim_temp      = TSET_DEF;

    g_alarm         = 0;
    g_alarm_start   = 0;
    g_buz_on        = 0;

    g_last_ctrl     = 0;
    g_last_disp     = 0;
    g_last_log      = 0;
    g_led_ms        = 0;
    g_led_fast_ms   = 0;
    g_blink_ms      = 0;
    g_buz_ms        = 0;
    g_beep_until    = 0;

    g_led_phase     = 0;
    g_led_fast      = 0;
    g_blink         = 0;
    g_buz_phase     = 0;
    g_force_log     = 1;

    s_line_len      = 0;
}

void app_task(void)
{
    u16 now;

    /* ---- 1. 采集（非阻塞） ---- */
    ds_task();

    /* ---- 2. 输入 ---- */
    handle_keys();
    serial_task();

    now = get_ms16();

    /* ---- 3. 判定 + 执行（每 100ms） ---- */
    if ((u16)(now - g_last_ctrl) >= TICK_MS) {
        g_last_ctrl = now;
        control_update();
    }

    /* ---- 4. 软件定时器：闪烁与呼吸 ---- */
    if ((u16)(now - g_led_ms) >= 250U) {
        g_led_ms = now;
        g_led_phase = (u8)(!g_led_phase);
    }
    if ((u16)(now - g_led_fast_ms) >= 100U) {
        g_led_fast_ms = now;
        g_led_fast = (u8)(!g_led_fast);
    }
    if ((u16)(now - g_blink_ms) >= 300U) {
        g_blink_ms = now;
        g_blink = (u8)(!g_blink);
    }

    /* ---- 5. 人机交互 ---- */
    led_task();
    buzzer_task();

    if ((u16)(now - g_last_disp) >= OLED_MS) {
        g_last_disp = now;
        render_display();
    }

    log_task();
}
