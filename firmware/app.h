/******************************************************************************
 * @file    app.h
 * @brief   应用层：温控状态机、数码管显示、串口协议。
 ******************************************************************************/

#ifndef __APP_H__
#define __APP_H__

#include "config.h"

/** @brief 上电初始化（在 timer/uart/motor/smg/key 初始化之后调用）。 */
void app_init(void);

/** @brief 主循环任务，while(1) 里不停调用。内部按时间片调度，不会阻塞。 */
void app_task(void);

/** @brief 当前状态（ST_xxx）。 */
u8 app_get_state(void);

/** @brief 当前状态名，返回 code 空间的字符串常量。 */
char code *app_state_name(void);

/** @brief 发送开机横幅。 */
void app_banner(void);

#endif /* __APP_H__ */
