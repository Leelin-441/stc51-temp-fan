/******************************************************************************
 * @file    selftest.h
 * @brief   引脚自检程序：逐个演示每个外设，帮你在 30 秒内确认接线对不对。
 ******************************************************************************/

#ifndef __SELFTEST_H__
#define __SELFTEST_H__

#include "config.h"

/**
 * @brief 运行自检流程。**这个函数不会返回**，它自己接管主循环。
 * @note  只有在 config.h 里把 SELFTEST_MODE 置 1 时才会被编译进去。
 *        自检完把 SELFTEST_MODE 改回 0 重新烧录，才是正式运行。
 */
void selftest_run(void);

#endif /* __SELFTEST_H__ */
