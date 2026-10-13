/******************************************************************************
 * @file    uart.c
 * @brief   串口驱动实现。
 *
 *  ★ 为什么收和发都做成中断驱动，而不是简单的"发的时候死等 TI"？
 *    如果发送用 `SBUF = c; while(!TI);` 这种阻塞写法，每发一个字节就要占用
 *    约 1ms（9600 波特率下）。一帧 52 字节 = 54ms，这 54ms 里如果串口中断
 *    被关掉（阻塞发送的常见写法），对方发来的字节就会因为 SBUF 来不及读而
 *    溢出丢失 —— 自动化测试脚本"发命令→立刻读响应"时必丢数据。
 *    改成中断驱动后，发送完全在后台进行，主循环一个字节都不用等。
 *
 *  ★ 内存分配：
 *    STC89C52 只有 128 字节可直接寻址的 data 空间，非常紧张。
 *    所以 32 字节的接收缓冲和 64 字节的发送缓冲都显式放到 xdata
 *    （片内扩展 RAM，共 256 字节），只把两个索引变量留在 data 里。
 ******************************************************************************/

#include "uart.h"

static u8 xdata s_rx_buf[UART_RX_SIZE];
static u8       s_rx_head = 0;          /* 只被中断写 */
static u8       s_rx_tail = 0;          /* 只被主循环写 */

static u8 xdata s_tx_buf[UART_TX_SIZE];
static u8       s_tx_len = 0;
static u8       s_tx_idx = 0;
static u8       s_tx_busy = 0;

void uart_init(void)
{
    SCON = 0x50;                /* 方式 1（8 位 UART），REN=1 允许接收 */

    /* 用 #if 而不是 if：UART_SMOD 是编译期常量，
       写成 if 的话另一个分支永远不可达，C51 会报 WARNING C294 unreachable code。 */
#if UART_SMOD
    PCON |= 0x80;               /* SMOD = 1，波特率加倍 */
#else
    PCON &= 0x7F;               /* SMOD = 0 */
#endif

    TMOD &= 0x0F;
    TMOD |= 0x20;               /* Timer1：方式 2，8 位自动重装，做波特率发生器 */
    TH1  = UART_TH1;
    TL1  = UART_TH1;
    TR1  = 1;                   /* 启动 Timer1 */

    ET1 = 0;                    /* Timer1 只做波特率，不要开它的中断 */
    ES  = 1;                    /* 允许串口中断 */

    s_rx_head = 0;
    s_rx_tail = 0;
    s_tx_len  = 0;
    s_tx_idx  = 0;
    s_tx_busy = 0;
}

/* 串口中断，向量号 4：RI 收字节，TI 推进发送 */
void uart_isr(void) interrupt 4
{
    u8 next;
    u8 b;

    if (RI) {
        RI = 0;
        b = SBUF;
        next = (u8)((s_rx_head + 1) % UART_RX_SIZE);
        if (next != s_rx_tail) {            /* 满了就丢弃，绝不阻塞中断 */
            s_rx_buf[s_rx_head] = b;
            s_rx_head = next;
        }
    }

    if (TI) {
        TI = 0;
        if (s_tx_idx < s_tx_len) {
            SBUF = s_tx_buf[s_tx_idx];
            s_tx_idx++;
        } else {
            s_tx_busy = 0;                  /* 一帧发完 */
        }
    }
}

/* ==========================================================================
 * 发送
 * ========================================================================== */
void uart_tx_reset(void)
{
    s_tx_len = 0;
}

void uart_tx_ch(char c)
{
    if (s_tx_len < UART_TX_SIZE) {
        s_tx_buf[s_tx_len] = (u8)c;
        s_tx_len++;
    }
}

void uart_tx_str(char code *s)
{
    while (*s != '\0') {
        uart_tx_ch((char)(*s));
        s++;
    }
}

void uart_tx_u16(u16 v)
{
    u8  tmp[5];
    u8  n = 0;

    if (v == 0) {
        uart_tx_ch('0');
        return;
    }
    while ((v > 0) && (n < 5)) {
        tmp[n] = (u8)('0' + (u8)(v % 10));
        v /= 10;
        n++;
    }
    while (n > 0) {
        n--;
        uart_tx_ch((char)tmp[n]);
    }
}

void uart_tx_s16(s16 v)
{
    u16 m;

    if (v < 0) {
        uart_tx_ch('-');
        m = (u16)(0 - v);
    } else {
        m = (u16)v;
    }
    uart_tx_u16(m);
}

void uart_tx_flush(void)
{
    if (s_tx_len == 0) {
        return;
    }

    while (s_tx_busy) {
        /* 等上一帧发完。此时 TI 已被中断清零，SBUF 是空闲的 */
    }

    s_tx_idx  = 0;
    s_tx_busy = 1;
    TI = 0;
    SBUF = s_tx_buf[s_tx_idx];      /* 先手动发第一个字节，剩下的靠中断 */
    s_tx_idx++;
}

u8 uart_tx_busy(void)
{
    return s_tx_busy;
}

/* ==========================================================================
 * 接收
 * ========================================================================== */
u8 uart_has_char(void)
{
    return (s_rx_head != s_rx_tail) ? 1 : 0;
}

u8 uart_getc(void)
{
    u8 b = 0;

    if (s_rx_head != s_rx_tail) {
        b = s_rx_buf[s_rx_tail];
        s_rx_tail = (u8)((s_rx_tail + 1) % UART_RX_SIZE);
    }
    return b;
}
