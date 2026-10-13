/******************************************************************************
 * @file    lcd1602.c
 * @brief   LCD1602 驱动实现（**8 位数据模式**，只写不读）。
 *
 *  ==================== 引脚（见 config.h，跟着普中 PZ-51 板走）====================
 *      LCD1602 引脚        接到
 *      --------------------------------------------------
 *      VSS (1)            GND
 *      VDD (2)            +5V
 *      V0  (3)            10kΩ 电位器中间脚（调对比度）
 *      RS  (4)            P2.6
 *      RW  (5)            P2.5     由软件固定拉低（只写不读）
 *      E   (6)            P2.7
 *      D0~D7 (7~14)       P0.0 ~ P0.7
 *      A   (15)           +5V
 *      K   (16)           GND
 *  这块板子的 LCD 是插在**专用插座**上的，接线是出厂印在板子上的，不能改，
 *  所以驱动必须适配它 —— 我们自己挑引脚是行不通的。
 *
 *  ============================ 为什么用 8 位模式 ============================
 *  普通接法下 4 位模式能省 4 根 IO，但这里的 P0 本来就是整整 8 根线，
 *  省不出东西，而 8 位模式每传一个字节只需要一次 E 脉冲（4 位模式要两次），
 *  代码更短、刷新更快。
 *
 *  ★ 为什么不读忙标志？
 *    RW 虽然接到了 P2.5 可以拉高来读，但读忙要做"先把 P0 设成输入、
 *    再读、再改回输出"的三步操作，代码反而更长。
 *    这里改用固定延时：普通命令/数据 40µs（留一倍余量给 80µs），
 *    清屏 1.52ms（等 2ms）。
 *
 *  ★ 注意 P0 还挂着板载数码管的段选
 *    普中板上 P0 同时接到数码管的 74HC245。写 LCD 时数码管会跟着乱显示，
 *    这是正常的 —— 把数码管模块的电源跳线帽拔掉即可，不影响 LCD。
 * =========================================================================
 ******************************************************************************/

#include "lcd1602.h"
#include "delay.h"

/* LCD_DATA_PORT / LCD_RS / LCD_RW / LCD_EN 都在 config.h 里声明 */

static u8 data s_col = 0;           /* 当前列，0~15 */

/* 使能脉冲：E 拉高→拉低，让 LCD 在下降沿锁存数据。
   手册要求 E 的高电平至少 450ns；11.0592MHz 下一条指令就 1.085µs，
   这里再显式延时 10µs，余量非常充足。 */
static void lcd_pulse(void)
{
    LCD_EN = 1;
    delay_10us(1);
    LCD_EN = 0;
    delay_10us(1);
}

/* 写一个字节：is_cmd=1 发命令，is_cmd=0 发数据 */
static void lcd_write(u8 dat, u8 is_cmd)
{
    LCD_RS = is_cmd ? 0 : 1;        /* RS=0 命令，RS=1 数据 */
    LCD_RW = 0;                     /* RW=0 写模式 */
    LCD_DATA_PORT = dat;            /* 8 位一次性送出（P0 整口写） */
    lcd_pulse();
    delay_10us(8);                  /* 等命令执行完（普通命令 40µs，留一倍余量） */
}

void lcd_cmd(u8 cmd)
{
    lcd_write(cmd, 1);
}

void lcd_char(char c)
{
    lcd_write((u8)c, 0);
    if (s_col < LCD_LINE_LEN) {
        s_col++;
    }
}

void lcd_str(char code *s)
{
    while (*s != '\0') {
        lcd_char((char)(*s));
        s++;
    }
}

void lcd_goto(u8 row, u8 col)
{
    u8 addr;

    if (col >= LCD_LINE_LEN) {
        col = LCD_LINE_LEN - 1;
    }
    /* 第一行 DDRAM 起始 0x00，第二行起始 0x40 */
    addr = (u8)((row ? 0x40 : 0x00) + col);
    lcd_cmd((u8)(0x80 | addr));
    s_col = col;
}

void lcd_clear(void)
{
    lcd_cmd(0x01);
    delay_ms(2);                    /* 清屏要 1.52ms，必须等够 */
    s_col = 0;
}

u8 lcd_col(void)
{
    return s_col;
}

void lcd_pad(u8 to_col)
{
    while (s_col < to_col && s_col < LCD_LINE_LEN) {
        lcd_char(' ');
    }
}

void lcd_init(void)
{
    /* 板载数码管的位选（74HC138 的 A/B/C）固定拉高，让它别乱闪。
       注意 P0 现在是 LCD 的数据口，不能再像以前那样写 0xFF 关数码管了，
       所以这里只能管住位选，其余靠拔跳线帽。 */
    P2 |= SMG_OFF_MASK;             /* P2.2 / P2.3 / P2.4 置 1 */

    LCD_RS = 0;
    LCD_RW = 0;                     /* 固定写模式 */
    LCD_EN = 0;

    delay_ms(20);                   /* 上电后 LCD 内部要稳定 >15ms */

    /* ---- 手册规定的初始化序列：连发三次 0x38 确保进入 8 位模式 ----
       上电时 HD44780 可能处于 4 位模式也可能处于 8 位模式，状态不确定，
       所以要在 8 位模式下多发几次同一条命令，无论它原来是什么模式都能同步上。 */
    lcd_write(0x38, 1);             /* 8 位数据，2 行显示，5x8 点阵 */
    delay_ms(5);
    lcd_write(0x38, 1);
    delay_ms(1);
    lcd_write(0x38, 1);
    delay_ms(1);

    /* ---- 正式配置 ---- */
    lcd_cmd(0x0C);                  /* 开显示，关光标，关闪烁 */
    lcd_cmd(0x06);                  /* 写入后光标自动右移，画面不滚动 */
    lcd_cmd(0x01);                  /* 清屏 */
    delay_ms(2);

    s_col = 0;
}

/* 输出定点温度（单位 0.1℃），固定占 5 列，右对齐：
     355  → " 35.5"
     -55  → " -5.5"
     1055 → "105.5"
     5    → "  0.5"          */
void lcd_temp1(s16 t_x10)
{
    char buf[7];
    u8   n = 0;
    u8   neg = 0;
    u8   i;
    u16  u;

    if (t_x10 < 0) {
        neg = 1;
        u = (u16)(0 - t_x10);
    } else {
        u = (u16)t_x10;
    }

    /* 从最低位开始往 buf 里塞，最后反序输出 */
    buf[n] = (char)('0' + (u8)(u % 10U));   /* 十分位 */
    n++;
    u /= 10U;
    buf[n] = '.';
    n++;
    do {
        buf[n] = (char)('0' + (u8)(u % 10U));
        n++;
        u /= 10U;
    } while ((u > 0U) && (n < 7U));

    if (neg && (n < 7U)) {
        buf[n] = '-';
        n++;
    }

    /* 前导空格补齐到 5 列 */
    for (i = (u8)(5U); i > n; i--) {
        lcd_char(' ');
    }
    /* 反序输出 */
    for (i = n; i > 0U; i--) {
        lcd_char(buf[i - 1U]);
    }
}

/* 输出无符号数，固定占 width 列，右对齐前补空格 */
void lcd_ufixed(u16 v, u8 width)
{
    char buf[6];
    u8   n = 0;
    u8   i;

    do {
        buf[n] = (char)('0' + (u8)(v % 10U));
        n++;
        v /= 10U;
    } while ((v > 0U) && (n < 6U));

    if (n < width) {
        for (i = (u8)(width - n); i > 0U; i--) {
            lcd_char(' ');
        }
    }
    for (i = n; i > 0U; i--) {
        lcd_char(buf[i - 1U]);
    }
}
