/******************************************************************************
 * @file    uart.h
 * @brief   串口通信（9600 8N1）：中断收 + 中断发 + 命令行解析支持。
 ******************************************************************************/

#ifndef __UART_H__
#define __UART_H__

#include "config.h"

/* ---- 波特率参数（由晶振自动选择） ----
   11.0592MHz + SMOD=0：TH1=253 → 9600 baud，误差 0.00%
   12MHz      + SMOD=1：TH1=243 → 4800 baud，误差 0.00%
   ★ 12MHz 下如果硬要跑 9600，误差会高达 +8.5%，串口一定乱码，
     所以这里自动降级到 4800。换晶振时串口助手也要跟着改。 */
#if (FOSC_HZ == 11059200UL)
  #define UART_BAUD     9600
  #define UART_SMOD     0
  #define UART_TH1      253
#else
  #define UART_BAUD     4800
  #define UART_SMOD     1
  #define UART_TH1      243
#endif

void uart_init(void);

/* ---------- 发送：先往缓冲区里拼，最后 flush 一次发出去 ---------- */

/** @brief 清空发送缓冲，准备拼一帧新内容。 */
void uart_tx_reset(void);

void uart_tx_ch(char c);
void uart_tx_str(char code *s);         /* 字符串常量在 code 空间，必须用 code 指针 */
void uart_tx_u16(u16 v);
void uart_tx_s16(s16 v);

/** @brief 把缓冲里的内容发出去（中断驱动，函数立刻返回，不阻塞主循环）。 */
void uart_tx_flush(void);

/** @brief 上一次发送是否还没完成。 */
u8   uart_tx_busy(void);

/* ---------- 接收：环形缓冲 ---------- */

/** @brief 环形缓冲里有没有待处理的字节。 */
u8   uart_has_char(void);

/** @brief 取一个字节（调用前必须先用 uart_has_char 确认）。 */
u8   uart_getc(void);

#endif /* __UART_H__ */
