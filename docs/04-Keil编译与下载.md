# Keil C51 编译与下载

> 本文档带你从零把工程编译出来并烧进单片机。
> **更快的办法见 §0.5：本工程提供了一个一键编译脚本，双击就出 hex。**

---

## 0. 先搞清楚：你需要的是 Keil **C51**，不是 MDK-ARM

| | Keil C51 | Keil MDK-ARM |
|---|---|---|
| 用来编译 | **8051 / STC89C52** ✅ | STM32 / ARM Cortex-M |
| 安装包名字 | `C51V9xx.exe` | `MDK5xx.exe` |
| 编译器 | C51 Compiler | ARMCC / AC6 |

**两个可以装到同一个目录**，uVision5 的 IDE 界面是一样的，只是编译器不同。
如果你只装了 MDK-ARM，打开 51 工程会提示找不到 C51 编译器。

**下载**：Keil 官网 → Downloads → **C51**。
也可以直接从你开发板资料包里找（普中/清翔的资料光盘里通常都带）。

### 怎么确认你装的是 C51 版

看这个文件在不在：

```
<Keil安装目录>\C51\BIN\C51.EXE
```

例如 `D:\Keil_v5\C51\BIN\C51.EXE`。在的话就没问题。

也可以在命令行里直接问编译器版本：

```bat
"D:\Keil_v5\C51\BIN\C51.EXE"
```
（不给参数会报 `NON-NULL ARGUMENT EXPECTED`，这是正常的，关键是它会打印出
`C51 COMPILER V9.60.7.0 - SN: xxxxx-xxxxxx` 这一行。）

### 关于 2KB 限制

Keil C51 的**未注册评估版**只能编译 2KB 以内的代码，会报：

```
*** FATAL ERROR L250: CODE SIZE LIMIT IN RESTRICTED VERSION EXCEEDED
```

判断自己是不是评估版：打开 `<Keil安装目录>\TOOLS.INI`，看 `[C51]` 段里有没有
`LIC0=` 这一行。**有就是已授权，不用管这个限制。**

本工程实测编译结果（`SELFTEST_MODE = 0`）：

```
Program Size: data=84.0  xdata=175  code=6362
```

`code = 6362` 字节（正式固件）/ 5111 字节（自检固件），只要你的版本不是 2KB 评估版就没问题。
万一真的是评估版，三种解决办法：

| 方案 | 说明 | 推荐度 |
|---|---|---|
| ① 用正版 / 学校授权 | 很多学校实验室有正版授权，开发板资料包里也常附带注册方式 | ★★★ |
| ② **改用 SDCC**（完全免费、无任何限制） | 见 §7 | ★★ |
| ③ 裁剪代码 | 正式固件 6362 字节已经是最精简的了；自检固件只有单独编译（不含 app.c）时才是 5111 字节 | ★ |

> 本工程的代码是**完全标准的 C51 C 代码**，与是否注册无关。

---

## 0.5 最快的一条路：一键编译脚本

本工程在 `tools/` 下提供了**两个**批处理脚本，**双击就编译出 hex**，不用打开 IDE：

| 脚本 | 编出什么 | 链接哪些模块 | 实测结果 |
|---|---|---|---|
| `tools\build.bat` | **完整版** `hex\TempFan.hex` | 11 个 .c（含 `app.c`、`uart.c`） | `data=84  xdata=175  code=6362` |
| `tools\build_selftest.bat` | **自检版** `hex\TempFan_Selftest.hex` | 10 个 .c（**不含 `app.c`**） | `data=95  xdata=96   code=5111` |
| `tools\build_minimal.bat` | **4KB 精简版** `hex\TempFan_4K.hex` | 9 个 .c（**不含 `app.c`/`uart.c`/`selftest.c`**） | `data=86  xdata=0    code=3502` |

> **为什么自检要单独编一份？**
> 自检版的主循环只跑硬件体检，根本不进应用层。如果两套代码编在一起，
> 自检固件会达到 **8628 字节 —— 超过 STC89C52 的 8192 字节 Flash 上限，烧不进去**。
> 拆开之后只有 5111 字节。
>
> 附带的好处：自检模式是通过**命令行 `DEFINE(SELFTEST_MODE=1)`** 传进去的
> （`config.h` 里的 `#define` 被 `#ifndef` 包住了），
> **你不需要手动改宏，也就不会出现"改了忘改回来"**。

它会依次做三件事：编译 → 链接 → 生成 hex，
并把结果同时复制一份到 `hex\` 目录。

看到这样的输出就是成功了：

```
============== [1/3] Compiling ==============
C51 COMPILATION COMPLETE.  0 WARNING(S),  0 ERROR(S)   ×11
============== [2/3] Linking ==============
Program Size: data=84.0 xdata=175 code=6362
LINK/LOCATE RUN COMPLETE.  7 WARNING(S),  0 ERROR(S)
============== [3/3] Making HEX ==============
GENERATING INTEL HEX FILE: Objects\TempFan.hex
================== BUILD OK ==================
```

**如果 Keil 不在 `D:\Keil_v5`**：用记事本打开 `tools\build.bat`，
改第 12 行的 `set KEIL=D:\Keil_v5\C51\BIN` 即可。

> `hex\` 目录里已经放了两个**预编译好的 hex**，赶时间的话连编译都可以跳过：
> - `hex\TempFan.hex` —— 正式固件（`SELFTEST_MODE = 0`）
> - `hex\TempFan_Selftest.hex` —— 自检固件（`SELFTEST_MODE = 1`）

### 关于那些警告

编译时会出现这几类提示，都**不是错误**，不影响使用：

| 提示 | 含义 | 要不要管 |
|---|---|---|
| `WARNING L16: UNCALLED SEGMENT` | 有几个辅助函数（`get_ms`、`motor_get_duty`、`key_is_down` 等）暂时没被调用，链接器跳过了它们的覆盖分析 | 不用管。它们是为方便你扩展而留的 API |
| `WARNING L14: INCOMPATIBLE MEMORY MODEL` | 混用了不同的内存模型 | **不该出现**。本工程全部用 Small 模型，如果看到它说明 build.bat 被改过 |
| `WARNING C294: unreachable code` | 写了编译期常量判断的 `if` | **不该出现**。已改成 `#if` |
| `WARNING C206: missing function-prototype` | 调用了函数但没包含它的头文件 | **不该出现**，出现了要补 `#include` |

---

## 0.6 实测内存占用（这是本工程最需要注意的约束）

STC89C52RC 的存储资源非常紧张，链接结果必须满足：

| 空间 | 容量 | 本工程实测 | 余量 | 说明 |
|---|---|---|---|---|
| `data` | **128 字节** | **93** | 35 | 可直接寻址的内部 RAM，**最紧张的就是它** |
| `xdata` | 256 字节 | 175 | 81 | 片内扩展 RAM（MOVX 间接访问） |
| `code` | **8192 字节** | **6362**（正式）/ **5111**（自检） | 1830 / 3081 | 程序 Flash |

**开发时踩到的坑（这个只有真编译器能发现）**：所有变量一开始都放在 `data` 里，
链接时报：

```
*** ERROR L107: ADDRESS SPACE OVERFLOW
    SPACE:   DATA
    SEGMENT: ?DT?KEY
Program Size: data=165.0 xdata=96 code=5254
```

`data` 用到了 **165 字节，超了 37 字节**。

**解决办法**：把 `app.c` 里的状态变量（约 80 字节）显式标上 `xdata`。

```c
static u16 xdata g_tset;        /* 目标温度 */
static char xdata s_line[LINE_MAX];  /* 命令行缓冲，40 字节 */
```

**为什么不用 `LARGE` 内存模型偷懒？** 两个原因：

1. `LARGE` 会改变函数调用约定，与中断模块混链时链接器报
   `L14 INCOMPATIBLE MEMORY MODEL`；
2. **中断服务程序里的变量必须待在 `data` 里才够快** ——
   `MOVX` 是 2 个机器周期，200µs 一次的中断经不起这种拖慢。

所以只有 `app.c`（纯主循环、没有速度要求）的变量改成 `xdata`，
`timer.c` / `lcd1602.c` / `key.c` / `motor.c` / `uart.c` 全部保持 Small 模型留在 `data`。

**你自己改代码时如果加了新的全局变量**，编译后一定要看
`Program Size: data=...` 这个数字，**超过 128 就会链接失败**。
解决办法同样是给变量加 `xdata`。

---

## 0.7 只有 4KB Flash 怎么办？用 4KB 精简版

有些板子用的是 **STC89C51RC（4KB）**，或者芯片 Flash 被 EEPROM 占用了一部分，
这时完整版（6362 字节）和自检版（5111 字节）都会报「**文件太大**」。

本工程单独做了一份 **4KB 精简版**：

```
tools\build_minimal.bat     →     hex\TempFan_4K.hex
Program Size: data=86.0  xdata=0  code=3502        ← 4096 上限还剩 594 字节
```

### 它是怎么做到减半的？

先把每个模块占多少 Flash 量出来（统计 BL51 的 `.M51` 段表）：

| 模块 | 完整版占用 | 主要构成 |
|---|---|---|
| **APP**（应用层） | **3475** | `control_update` 608、`handle_line` 326、`parse_num` 289、`log_task` 224、`find_key` 221、`app_task` 257 … |
| DS18B20 | 758 | 单总线位级时序，省不了 |
| LCD1602 | 628 | |
| KEY | 405 | |
| **UART** | **387** | 串口驱动 |
| 其它 | 700 | 启动代码、库、电机、延时、指示 |

**串口那一整套**（UART 驱动 387 + 命令行解析 ≈1044 + 上报帧 `log_task` 224）
**合计约 1650 字节**，是最大的可砍项。

所以精简版砍掉了：

| 砍掉的 | 省下 | 代价 |
|---|---|---|
| 串口通信（全部） | ~1650 | ❌ 不能用 `tools/serial_test.ps1` 做自动化测试 |
| SIM 模拟温度注入 | ~100 | ❌ 不能注入假温度，测试可复现性下降 |
| 手动模式 MODE_MANUAL | ~80 | ❌ 不能按键直接指定档位 |
| 传感器状态字符串名 | ~150 | ❌ 串口日志里没有了（本来也没串口） |
| 应用层换成紧凑版 `app_min.c` | ~1000 | 见下 |

### 关键：控制逻辑一行没改

精简版换用了 `firmware/app_min.c`，但**回差算法与完整版一字不差**，
而且所有门限**仍然只从 `config.h` 读**，没有另立一套参数：

```c
if (t >= (s16)(g_tset + LV_ENTER_LOW))  g_level = LV_LOW;    /* 和 app.c 完全一样 */
...
if (t <= (s16)(g_tset + LV_EXIT_LOW))   g_level = LV_STOP;
```

`tools/lint_c51.py` 里加了一条自动检查，**保证 `app_min.c` 引用了全部 14 个门限宏**：

```
app_min.c 复用全部 14 个门限宏 ✓
```

这就是「精简版控制逻辑与完整版一致」的静态证据 ——
所以 `tools/sim_controller.py` 那 270 次等价仿真的结论**依然适用于 4KB 版**。

### 操作方法上的唯一区别

4KB 版没有串口，只能靠按键：

| 按键 | 短按 | 长按（>1.5 秒） |
|---|---|---|
| **K3 (SET)** | 目标温度 −1.0℃ | 目标温度 **−5.0℃** |
| **K4 (UP)** | 目标温度 +1.0℃ | 目标温度 **+5.0℃** |

> 现场演示想快速把目标温度从 35℃ 压到 20℃：**连按 3 次 K3 长按**即可。

### 该烧哪一个

| 你的芯片 | 烧哪个 |
|---|---|
| STC89C52RC（8KB）及以上 —— **绝大多数普中/清翔板** | 先 `TempFan_Selftest.hex` 体检，再 `TempFan.hex` |
| STC89C51RC（4KB） | `TempFan_4K.hex` |
| 不确定 | 先试 8KB 那两个；报「文件太大」就换 `TempFan_4K.hex` |

> **建议**：条件允许的话还是花 **3~5 元换一颗 STC89C52RC**（DIP-40，插座的话直接拔插）。
> 8KB 能跑完整版，**串口自动化测试那 14 条用例是可复现性的重要证据**，答辩时用得上。

---

## 1. 新建工程

1. 打开 **Keil uVision5**
2. 菜单 **Project → New uVision Project...**
3. 选一个**不含中文和空格**的路径，工程名填 `TempFan`，保存
4. 弹出器件选择框：
   - 如果你的 Keil 装了 **STC 器件包**：搜 `STC89C52RC` 选中
   - 如果没装：选 **Atmel → AT89C52** 也可以（内核与寄存器完全兼容）
   - 如果提示 "Copy Standard 8051 Startup Code?" → 点 **否**
     （我们自己不依赖启动代码，Keil 会自动生成 `STARTUP.A51`，保留默认即可）
5. 弹出 "Add Files to Group" 对话框时可以先取消，下一步再统一添加

---

## 2. 添加源文件

把本仓库 `firmware/` 目录下的 **11 个 .c 文件**全部加进工程：

```
main.c          主程序
app.c           状态机 + 显示 + 串口协议
timer.c         定时器 0 中断：PWM + 毫秒计时 + 按键采样
motor.c         软件 PWM
lcd1602.c       LCD1602 驱动（4 位模式）
key.c           按键消抖
uart.c          串口收发
ds18b20.c       DS18B20 单总线驱动
indicator.c     LED 与蜂鸣器
delay.c         延时
selftest.c      引脚自检（只在 build_selftest.bat 里参与编译）
```

**操作方法**：在左侧 Project 窗口右键 `Source Group 1` → **Add Existing Files to Group...**
→ 文件类型选 `C Source file (*.c)` → 一次性全选 11 个 .c 文件 → Add → Close。

> **头文件（.h）不需要手动添加**，只要在 §3 里把 `firmware` 目录加进 Include Paths 就行。
>
> **注意**：`config.h` 里的 `#if SELFTEST_MODE` 在 `selftest.c` 最外层，
> 当 `SELFTEST_MODE = 0` 时这个文件编译出来是空的，这是正常的，不会报错。

---

## 3. 编译设置（关键，别跳过）

点工具栏的**魔术棒**图标（或 `Project → Options for Target 'Target 1'`）。

### 3.1 Target 标签页

| 项目 | 设置 | 为什么 |
|---|---|---|
| **Memory Model** | **Small: variables in DATA** | 本项目把大缓冲显式标了 `xdata`，其余变量放 data 最快 |
| **Code Rom Size** | **Large: 64K program** | 默认值。8KB 芯片也能用（链接器按实际大小分配） |
| **Use On-chip ROM (0x0-0x1FFF)** | ✅ 勾选 | 强制把代码限制在片内 8KB 内，超出会明确报错，而不是生成一个烧不进去的 hex |
| Operating system | None | |
| **Use MicroLIB** | 不用管（那是 ARM 的选项） | |

> **为什么 Code Rom Size 不用 "Small: program 2K or less"？**
> 因为 2KB 装不下本项目。Small 模式只在代码 <2KB 时才有意义（调用指令更短）。
> 如果注册后想省空间，可以试 "Compact: 2K functions, 64K program"。

### 3.2 Output 标签页

| 项目 | 设置 |
|---|---|
| **Create HEX File** | ✅ **必须勾选**，否则 STC-ISP 没有文件可烧 |
| Create Executable | ✅ 默认勾选 |

### 3.3 C51 标签页

| 项目 | 设置 |
|---|---|
| Code Optimization | Level 8（默认） |
| **Preprocessor Symbols → Define** | 不用填 |
| **Include Paths** | 点右边的 `...`，把 **`firmware` 目录**加进去 |

> ⚠ **Include Paths 这一步一定不能漏**，否则会报
> `can't open source input file "config.h"`。

> **C89 语法说明**：Keil C51 的编译器是**按 C89 实现的**，
> 变量声明必须放在块的**最前面**，不能像 C99 那样在语句后面声明。
> 本工程已经全部按 C89 写好（用 `tools/lint_c51.py` 自动检查过），
> 编译时**不会**出现 `declaration may not appear after executable statement` 这类错误。
>
> 如果你自己改代码时遇到了这个报错，把那个变量声明移到所在 `{` 的后面即可。

### 3.4 Debug 标签页

选 **Use: STC-ISP / 或 None**（51 不支持 ST-Link 那种在线调试，
我们靠串口打印来观察运行状态）。如果要单步调试，需要 STC 的仿真器。

---

## 4. 编译

按 **F7**（或工具栏的 Build 图标）。

成功的输出长这样：

```
Build target 'Target 1'
compiling main.c...
compiling app.c...
...
linking...
Program Size: data=xx.x xdata=xx code=xxxx
".\Objects\TempFan" - 0 Error(s), 2 Warning(s).
creating hex file...
```

关键是最后一行要有 **`0 Error(s)`** 和 **`creating hex file`**。

编译产物在工程目录的 **`Objects\TempFan.hex`**，这就是要烧进单片机的文件。

> **`Program Size` 三个数字的含义**：
> - `data` = 可直接寻址的内部 RAM（STC89C52 只有 128 字节，很紧张）
> - `xdata` = 片内扩展 RAM（共 256 字节）
> - `code` = Flash 程序空间（STC89C52 有 8KB = 8192 字节）
>
> 如果 `data` 超过 128，Keil 会报 `L107: ADDRESS SPACE OVERFLOW`。
> 本项目已经把串口收发缓冲（96 字节）显式放到了 `xdata`，就是为了给 `data` 腾地方。

---

## 5. 烧录（STC-ISP）

51 单片机**不能**用 ST-Link 下载，要用 **STC-ISP**（STC 官方的下载软件）。

1. 下载并打开 **STC-ISP**（STC 官网 `www.stcai.com` → 工具软件）
2. **单片机型号**：选 `STC89C52RC`
3. **串口号**：选开发板对应的 COM 口
   （插上 USB 线后在设备管理器里能看到，通常是 `USB-SERIAL CH340`）
4. **打开程序文件**：选 `Objects\TempFan.hex`
5. 波特率保持默认（一般 4800 或 9600，这是**下载用的波特率**，
   和程序里跑通信的 9600 是两回事）
6. 点 **"下载/编程"** 按钮
7. **给开发板断电，再重新上电**（这叫"冷启动"）

> **为什么要冷启动？** STC 单片机内部有一段引导程序（ISP bootloader），
> 只在**上电的一瞬间**检测串口有没有下载请求。所以必须"先点下载、再上电"。
> 普中/清翔的开发板有一键下载电路，通常点一下下载按钮再按一下复位键就行。

**下载成功的提示**：

```
正在检测目标单片机 ...
单片机型号 : STC89C52RC
...
编程成功 !
```

---

## 6. 上电验证

烧录完成、复位之后，按顺序检查：

**第一步：先用自检模式体检测接线**

把 `firmware/config.h` 里的 `SELFTEST_MODE` 从 `0` 改成 `1`，
重新编译下载。程序会依次演示：

```
[1] LCD1602: two lines of text, then digits 1..6 fill the first line
[2] LED    : green / red alternating
[3] BEEP   : 3 short beeps
[4] MOTOR  : duty 0 -> 34 -> 66 -> 100 -> 0 (%)
[5] DS18B20: sampling ... status = OK, temp = 253 (x0.1 C)
[6] KEY    : press K3(P3.2) and K4(P3.3) within 20s
```

逐项对照，哪一项不对就按提示排查（自检程序会在串口打印排查建议）。

**体检完把 `SELFTEST_MODE` 改回 `0`，重新编译下载。**

**第二步：正式运行检查**

- [ ] 串口助手（**9600 8N1**）收到开机横幅 `# STC51-TempFan v1.0 ready, baud=9600, tset=350`
- [ ] 之后每 1 秒收到一帧 `$S,...`
- [ ] 液晶第一行显示 `T= 25.3C  SET=35`，第二行显示 `AUTO STOP   P=  0`
- [ ] 用手握住 DS18B20 的感温头 10 秒，液晶上的温度上升
- [ ] 按 K4 把目标温度一路按到 20℃，液晶状态从 `STOP` 跳到 `HIGH`、占空比跳到 `100`，**电机明显转起来**
- [ ] 再按 K4 按到 60℃，电机停转
- [ ] 串口发 `SIM T=45.0` → 液晶显示 `ALARM` 且整屏闪烁，蜂鸣器急促鸣叫，电机全速，红灯 2Hz 闪
- [ ] 串口发 `SIM OFF` → 回到真实传感器
- [ ] 拔掉 DS18B20 的数据线，等约 3 秒 → 液晶显示 `T=  Err` 和 `FAULT`，电机停转，蜂鸣器长鸣
- [ ] 串口发 `RESET` → 参数恢复默认

---

## 7. 备选方案：用 SDCC 编译（完全免费、无 2KB 限制）

如果不想处理 Keil 的授权问题，可以用 **SDCC**（开源、免费、无限制）。

1. 到 `sdcc.sourceforge.net` 下载 Windows 版，解压并把 `bin` 目录加进 PATH
2. 在 `firmware/` 目录下建一个 `build.bat`：

```bat
@echo off
set SRC=main.c app.c timer.c motor.c smg.c key.c uart.c ds18b20.c indicator.c delay.c selftest.c
sdcc -mmcs51 --model-small --iram-size 256 --xram-size 256 ^
     --code-size 8192 --stack-auto ^
     -o TempFan.ihx %SRC%
packihx TempFan.ihx > TempFan.hex
echo done: TempFan.hex
```

3. 双击运行，生成的 `TempFan.hex` 用 STC-ISP 烧录

> **注意**：SDCC 与 Keil C51 在 `sbit` 语法上是兼容的，
> 但 SDCC 不支持 `interrupt N` 的写法，需要改成
> `void timer0_isr(void) __interrupt(1) __using(1)`。
> 如果要用 SDCC，中断函数需要手工改一下（共 2 处：Timer0 和串口）。

---

## 8. 常见编译 / 下载报错对照表

| 报错 / 现象 | 原因 | 解决 |
|---|---|---|
| `*** FATAL ERROR L250: CODE SIZE LIMIT IN RESTRICTED VERSION EXCEEDED` | Keil C51 未注册（2KB 限制） | 见 §0 的三种方案 |
| `can't open source input file "config.h"` | Include Path 没加 | §3.3 加上 `firmware` 目录 |
| `declaration may not appear after executable statement in block` | 用了 C99 的块内声明 | 把该变量声明移到块首（本项目已全部符合 C89） |
| `L107: ADDRESS SPACE OVERFLOW`（data 溢出） | 变量太多，超过 128 字节 | 把大数组加 `xdata` 关键字（本项目已处理） |
| `L104: MULTIPLE PUBLIC DEFINITIONS` | 同一个函数/变量定义了两次 | 检查是不是把 `main.c` 里的函数也写进了别的文件 |
| `undefined symbol` | 某个 .c 文件没加进工程 | 回头检查 §2 的 11 个文件是否都加了 |
| `error C202: 'XXX': undefined identifier` | 拼写错误，或缺少 `#include "config.h"` | 检查拼写和头文件 |
| `error C231: 'XXX': redefinition` | 变量名重复 | 重命名 |
| `warning C206: 'xxx': missing function-prototype` | 函数没有前置声明 | 在文件顶部加函数原型声明 |
| 编译通过，但下载后没反应 | hex 没更新 / 没冷启动 | 确认 `Create HEX File` 已勾选；重新编译；点下载后给板子断电重上电 |
| STC-ISP 提示"正在检测目标单片机..."一直不动 | 没冷启动 / COM 口选错 / 串口被占用 | 关掉串口助手；确认 COM 口；点下载后立刻给板子断电重上电 |
| STC-ISP 提示"握手失败" | 同上 | 同上；也检查 USB 线是不是只能充电不能传数据的那种 |
| 烧录成功但程序跑不起来 | 晶振频率选错 | STC-ISP 里确认"输入时钟频率"选的是 **11.0592MHz** |
| 串口输出乱码 | 程序里波特率与串口助手不一致 | 11.0592MHz 板用 **9600**；12MHz 板用 **4800** |

---

## 9. 工程文件清单

```
firmware/
├─ config.h        ★ 唯一事实来源：引脚、参数、协议
├─ main.c          主程序
├─ app.h / app.c   状态机 + 液晶显示 + 串口协议
├─ timer.h / timer.c   定时器 0 中断（PWM + 扫描 + 毫秒 + 按键）
├─ motor.h / motor.c   软件 PWM
├─ lcd1602.h / lcd1602.c  LCD1602 驱动（4 位模式）
├─ key.h / key.c       按键消抖
├─ uart.h / uart.c     串口收发
├─ ds18b20.h / ds18b20.c   DS18B20 单总线驱动
├─ indicator.h / indicator.c   LED 与蜂鸣器
├─ delay.h / delay.c   延时
└─ selftest.h / selftest.c 引脚自检（SELFTEST_MODE 控制）
```

**编译顺序无关**（Keil 会自己处理依赖），但**头文件必须都能找到**。
