/******************************************************************************
 * @file    ds18b20.c
 * @brief   DS18B20 单总线驱动 + 非阻塞采样状态机。
 *
 *  ============================ 1-Wire 时序说明 ============================
 *  DS18B20 没有时钟线，全靠"拉低多久"来表达 0 和 1。数据手册的关键窗口：
 *
 *    操作          主机拉低时间      对时隙总长的要求
 *    ------------------------------------------------------
 *    复位脉冲      ≥480us            无上限
 *    存在脉冲      从机拉低 60~240us  主机应在释放后 60~240us 内采样
 *    写 1          1 ~ 15us           时隙总长 60~120us
 *    写 0          60 ~ 120us         时隙总长 60~120us
 *    读时隙        1 ~ 15us           主机必须在拉低后 15us 内采样
 *
 *  这里有两个"硬上限"：写 1 的拉低不能超过 15us，读的采样点不能晚于 15us。
 *  所以不能用循环延时（一次循环开销就十几微秒），必须用 _nop_() 精确计数：
 *      Keil C51 的一条 _nop_() = 1 个机器周期 = 12/FOSC 秒
 *      11.0592MHz → 1.085us      12MHz → 1.000us
 *
 *  ★ 为什么位级操作要关中断？
 *    Timer0 每 200us 中断一次，服务程序约 35us。如果它正好插在"写 0"的
 *    70us 低电平中间，低电平就被拉长到 105us 甚至 140us，超出 120us 上限，
 *    从机就会把 0 误判成 1（或者干脆复位）。
 *    所以在每个位的操作期间用 EA=0 保护约 80us，做完立刻恢复。
 *    关中断的总时间约 6ms/次采样（分摊在 72 个小窗口里），
 *    对毫秒计数和 PWM 完全没有可见影响。
 *  =========================================================================
 ******************************************************************************/

#include "ds18b20.h"
#include "delay.h"
#include "timer.h"
#include <intrins.h>

/* ---------------- 精确短延时宏（每个 _nop_ = 1 个机器周期） ---------------- */
#define NOP2()   do { _nop_(); _nop_(); } while (0)
#define NOP4()   do { NOP2(); NOP2(); } while (0)
#define NOP8()   do { NOP4(); NOP4(); } while (0)
#define NOP16()  do { NOP8(); NOP8(); } while (0)
#define NOP32()  do { NOP16(); NOP16(); } while (0)
#define NOP64()  do { NOP32(); NOP32(); } while (0)

/* ---------------- DS18B20 命令字 ---------------- */
#define DS_CMD_SKIP_ROM     0xCC
#define DS_CMD_CONVERT_T    0x44
#define DS_CMD_READ_SCR     0xBE

/* ---------------- 模块状态 ---------------- */
static s16 g_temp       = 0;
static u8  g_status     = DS_NOINIT;
static u8  g_fail       = 0;

#define PH_IDLE     0       /* 空闲，等采样周期 */
#define PH_CONVERT  1       /* 已发起转换，等 800ms */
#define PH_READ     2       /* 该读结果了 */

static u8  g_phase      = PH_IDLE;
static u16 g_phase_ms   = 0;
static u16 g_last_ms    = 0;
static u8  g_force      = 0;

/* ==========================================================================
 * 1-Wire 底层：位级操作
 * ========================================================================== */

/* 复位脉冲 + 检测存在脉冲。返回 1 表示有器件应答。 */
static u8 ow_reset(void)
{
    u8 present;

    DQ = 0;
    delay_10us(50);             /* 拉低约 650us，远大于要求的 480us */

    EA = 0;                     /* ---- 临界区开始（约 70us） ---- */
    DQ = 1;                     /* 释放总线 */
    NOP64();                    /* 等约 69us 后采样存在脉冲 */
    present = DQ;               /* 0 = 从机把总线拉低了 = 存在 */
    EA = 1;                     /* ---- 临界区结束 ---- */

    delay_10us(35);             /* 等存在脉冲彻底走完（最长 240us） */

    return (present == 0) ? 1 : 0;
}

/* 写一个位 */
static void ow_write_bit(u8 b)
{
    EA = 0;
    if (b) {
        DQ = 0;
        NOP2();                 /* 拉低约 3us（要求 1~15us） */
        DQ = 1;
        NOP64();                /* 补足时隙到约 84us（要求 60~120us） */
    } else {
        DQ = 0;
        NOP64();                /* 拉低约 70us（要求 60~120us） */
        DQ = 1;
        NOP8();                 /* 恢复时间 */
    }
    EA = 1;
}

/* 读一个位 */
static u8 ow_read_bit(void)
{
    u8 b;

    EA = 0;
    DQ = 0;
    NOP2();                     /* 拉低约 3us 启动读时隙 */
    DQ = 1;                     /* 释放，交给从机驱动 */
    NOP4();                     /* 再等约 4us */
    b = DQ;                     /* 采样点 = 拉低后约 8.7us（要求 <15us） */
    NOP64();                    /* 等时隙结束 */
    EA = 1;

    return (b != 0) ? 1 : 0;
}

/* 写一个字节（低位先出，这是 1-Wire 的规定） */
static void ow_write_byte(u8 dat)
{
    u8 i;
    for (i = 0; i < 8; i++) {
        ow_write_bit((u8)(dat & 0x01));
        dat >>= 1;
    }
}

/* 读一个字节 */
static u8 ow_read_byte(void)
{
    u8 i;
    u8 dat = 0;
    for (i = 0; i < 8; i++) {
        dat >>= 1;
        if (ow_read_bit()) {
            dat |= 0x80;
        }
    }
    return dat;
}

/* ==========================================================================
 * DS18B20 专有：暂存器读取（含 CRC 校验）与温度换算
 * ========================================================================== */

/* 读 9 字节暂存器并换算成 0.1℃ 定点数。
 *
 * ★ 为什么把 CRC 循环直接内联在这里，而不是抽成一个 ds_crc8(buf, len) 函数？
 *   Keil C51 里指针是分内存空间的：`u8 code *p` 只能指向 ROM，
 *   `u8 xdata *p` 只能指向外部 RAM。而 buf 是 data 空间的局部变量，
 *   写成 `u8 code *` 会直接编译报错（类型不兼容）。
 *   要通用就得用 3 字节的"泛型指针"，又慢又占空间。
 *   这个循环一共才 12 行，内联反而更简单、更快、零歧义。 */
static u8 ds_read_scratchpad(void)
{
    u8  buf[9];
    u8  i, j;
    u8  crc = 0;
    u8  b, mix;
    u16 raw;

    if (!ow_reset()) {
        return DS_NOPRESENT;
    }

    ow_write_byte(DS_CMD_SKIP_ROM);
    ow_write_byte(DS_CMD_READ_SCR);
    for (i = 0; i < 9; i++) {
        buf[i] = ow_read_byte();
    }

    /* ---- Dallas 单总线 CRC-8，多项式 X^8+X^5+X^4+1 ---- */
    for (i = 0; i < 8; i++) {
        b = buf[i];
        for (j = 0; j < 8; j++) {
            mix = (u8)((crc ^ b) & 0x01);
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            b >>= 1;
        }
    }
    if (crc != buf[8]) {
        return DS_BADCRC;
    }

    raw = ((u16)buf[1] << 8) | (u16)buf[0];     /* 低字节在前 */

    /* raw 是"1/16 ℃"的有符号定点数：1200 = 75.0℃，-176 = -11.0℃ */
    if (raw & 0x8000) {
        u16 mag = (u16)(~raw) + 1;              /* 取绝对值再算，避免负数除法的舍入方向出错 */
        g_temp = (s16)(0 - (s16)((mag * 10U + 8U) / 16U));
    } else {
        g_temp = (s16)((raw * 10U + 8U) / 16U); /* +8 是做四舍五入 */
    }

    return DS_OK;
}

/* ==========================================================================
 * 对外接口
 * ========================================================================== */

void ds_init(void)
{
    g_temp     = 0;
    g_status   = DS_NOINIT;
    g_fail     = 0;
    g_phase    = PH_IDLE;
    g_phase_ms = 0;
    g_last_ms  = 0;
    g_force    = 1;             /* 上电后立刻采一次 */
}

void ds_force_sample(void)
{
    g_force = 1;
}

void ds_task(void)
{
    u16 now = get_ms16();

    switch (g_phase) {

    case PH_IDLE:
        if (g_force) {
            g_force = 0;
            g_last_ms = now;
        }
        if ((u16)(now - g_last_ms) >= DS_SAMPLE_MS) {
            g_last_ms = now;

            if (ow_reset()) {
                ow_write_byte(DS_CMD_SKIP_ROM);
                ow_write_byte(DS_CMD_CONVERT_T);    /* 发起转换后立刻返回 */
                g_phase    = PH_CONVERT;
                g_phase_ms = now;
            } else {
                g_status = DS_NOPRESENT;
                if (g_fail < 255) {
                    g_fail++;
                }
            }
        }
        break;

    case PH_CONVERT:
        /* 转换在 DS18B20 芯片内部进行（最长 750ms），总线上是空闲的，
           主循环可以照常跑状态机、刷显示、收串口。 */
        if ((u16)(now - g_phase_ms) >= DS_CONV_MS) {
            g_phase = PH_READ;
        }
        break;

    case PH_READ:
        {
            u8 r = ds_read_scratchpad();
            if (r == DS_OK) {
                g_status = DS_OK;
                g_fail   = 0;
            } else {
                g_status = r;
                if (g_fail < 255) {
                    g_fail++;
                }
            }
            g_phase = PH_IDLE;
        }
        break;

    default:
        g_phase = PH_IDLE;
        break;
    }
}

s16 ds_get_temp(void)        { return g_temp; }
u8  ds_get_status(void)      { return g_status; }
u8  ds_get_fail_count(void)  { return g_fail; }
