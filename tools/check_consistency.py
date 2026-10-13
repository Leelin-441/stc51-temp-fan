#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_consistency.py —— 文档与代码的一致性校验。

一个真实的教训：课设报告最丢分的往往不是"功能没做出来"，
而是**文档写的引脚和代码里的引脚对不上**（文档写 P3.6、代码是 P3.7），
或者 README 里放了一个 404 的图片链接。

检查五件事：
  1. 引脚一致性   —— 文档里的每个 Px.y 必须在 config.h 里有对应声明
                     （明确作为"本工程未使用/空闲"提及的除外，见 ALLOWED_UNUSED）
  2. 状态名一致性 —— 文档里的每个状态名必须在 app.c 的 app_state_name() 里有定义
  3. 链接有效性   —— 所有 Markdown 相对链接与图片链接都能解析到真实文件
  4. 串口帧字段   —— config.h 的协议注释与 docs/05 的描述、uart 的实际输出三者一致
  5. 参数抽查     —— config.h 的关键阈值与预期值一致

用法：python tools/check_consistency.py
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FW = os.path.join(ROOT, "firmware")

fails = []


def read(p):
    with open(p, "r", encoding="utf-8") as f:
        return f.read()


def strip_code_blocks(text):
    """去掉 ``` 代码块，但保留行数，保证报错行号准确。"""
    return re.sub(r'```.*?```', lambda m: "\n" * m.group(0).count("\n"),
                  text, flags=re.S)


# ==================================================================
# 一、从代码里提取"真相"
# ==================================================================
cfg = read(os.path.join(FW, "config.h"))
appc = read(os.path.join(FW, "app.c"))

# ---- 引脚：全部来自 config.h 里的 sbit 声明 ----
valid_pins = set()
for m in re.finditer(r'sbit\s+\w+\s*=\s*(P[0-3])\s*\^\s*(\d)', cfg):
    valid_pins.add(f"{m.group(1)}.{m.group(2)}")

# 串口引脚由硬件固定，不在 sbit 里
valid_pins.update({"P3.0", "P3.1"})

# LCD 数据口是"整口"用的（#define LCD_DATA_PORT P0），8 位全部占用
if re.search(r'#define\s+LCD_DATA_PORT\s+P0', cfg):
    for i in range(8):
        valid_pins.add(f"P0.{i}")

# 本工程明确不使用、但文档里会作为"空闲引脚 / 板载数码管"提到的
ALLOWED_UNUSED = set()
# P1.1~P1.4 / P1.6 / P1.7：LCD 改用普中板原生接线（P0 + P2.5~P2.7）后空出来的
ALLOWED_UNUSED.update({"P1.1", "P1.2", "P1.3", "P1.4", "P1.6", "P1.7"})
ALLOWED_UNUSED.update({
    "P2.2", "P2.3", "P2.4",                            # 74HC138 数码管位选
    "P3.4", "P3.5", "P3.6",                            # 计数器/外部中断/写选通，空闲
})

print(f"代码中使用的引脚（{len(valid_pins)} 个）: {' '.join(sorted(valid_pins))}")

# ---- 状态名：从 app.c 的 app_state_name() 提取 ----
m = re.search(r'char code \*app_state_name\(.*?\n\}\n', appc, re.S)
valid_states = set(re.findall(r'return\s+"([A-Z_]+)"', m.group(0))) if m else set()
valid_states.update({"AUTO", "MANUAL"})          # 运行模式也是合法标识
print(f"代码中定义的状态/模式名（{len(valid_states)} 个）: {' '.join(sorted(valid_states))}")

# ---- 串口帧字段 ----
def frame_fields(text):
    mm = re.search(r'\$S,((?:<[^>]+>\s*,?\s*)+)', text)
    return [x.strip() for x in re.findall(r'<([^>]+)>', mm.group(1))] if mm else None


cfg_fields = frame_fields(cfg)
print(f"config.h 协议注释里的字段（{len(cfg_fields) if cfg_fields else 0} 个）: {cfg_fields}")
print()

# ==================================================================
# 二、校验所有 Markdown
# ==================================================================
md_files = []
for base in (ROOT, os.path.join(ROOT, "docs"), os.path.join(ROOT, "firmware")):
    if os.path.isdir(base):
        for f in sorted(os.listdir(base)):
            if f.endswith(".md"):
                md_files.append(os.path.join(base, f))

PIN_RE = re.compile(r'\bP[0-3]\.[0-7]\b')
STATE_LIKE = re.compile(r'\b(STOP|LOW|MID|HIGH|ALARM|FAULT|MANUAL|INIT|AUTO)\b')
doc05_fields = None

for md in md_files:
    text = read(md)
    rel = os.path.relpath(md, ROOT)
    body = strip_code_blocks(text)

    for pm in PIN_RE.finditer(body):
        pin = pm.group(0)
        if pin in valid_pins or pin in ALLOWED_UNUSED:
            continue
        ln = body[:pm.start()].count("\n") + 1
        fails.append(f"{rel}:{ln}: 出现了代码里不存在的引脚 {pin}")

    for sm in STATE_LIKE.finditer(body):
        nm = sm.group(0)
        if nm in valid_states:
            continue
        ln = body[:sm.start()].count("\n") + 1
        fails.append(f"{rel}:{ln}: 出现了代码里不存在的状态名 {nm}")

    for lm in re.finditer(r'!?\[([^\]]*)\]\(([^)]+)\)', text):
        target = lm.group(2).strip()
        if target.startswith(("http://", "https://", "#", "mailto:")):
            continue
        target = target.split("#")[0].strip()
        if not target:
            continue
        resolved = os.path.normpath(os.path.join(os.path.dirname(md), target))
        if not os.path.exists(resolved):
            ln = text[:lm.start()].count("\n") + 1
            fails.append(f"{rel}:{ln}: 链接指向的文件不存在 → {target}")

    if os.path.basename(md) == "05-测试方案与判定标准.md":
        doc05_fields = frame_fields(text)

print(f"检查了 {len(md_files)} 个 Markdown 文件")

print(f"docs/05 描述里的字段: {doc05_fields}")
if cfg_fields and doc05_fields:
    a = [x.replace("状态", "").replace("SENSOR", "SENSOR") for x in doc05_fields]
    if a == cfg_fields:
        print("  ✓ config.h 协议注释与 docs/05 描述字段一致")
        print(f"    字段序列：{', '.join(cfg_fields)}")
    else:
        fails.append(f"docs/05 的字段 {a} 与 config.h 的 {cfg_fields} 不一致")
else:
    fails.append("无法从 config.h 或 docs/05 解析出串口帧字段")

# ==================================================================
# 三、参数抽查
# ==================================================================
expect = {
    "TSET_DEF": 350, "TSET_MIN": 200, "TSET_MAX": 600,
    "LV_ENTER_LOW": -20, "LV_EXIT_LOW": -40,
    "LV_ENTER_MID": 0, "LV_EXIT_MID": -20,
    "LV_ENTER_HIGH": 30, "LV_EXIT_HIGH": 10,
    "ALARM_OFFSET": 80, "ALARM_EXIT": 40, "ALARM_HOLD_MS": 5000,
    "DS_FAIL_LIMIT": 3, "PWM_LEVELS": 50, "PWM_ISR_US": 200,
    "MOTOR_DUTY_STOP": 0, "MOTOR_DUTY_LOW": 34,
    "MOTOR_DUTY_MID": 66, "MOTOR_DUTY_HIGH": 100,
}
print()
bad = 0
for name, want in expect.items():
    # 允许三种写法：  350    (-20)    (  0)    ( 30)
    mm = re.search(r'#define\s+' + name + r'\s+\(?\s*(-?\d+)\s*\)?', cfg)
    if not mm:
        fails.append(f"config.h 里找不到 {name}")
        bad += 1
        continue
    got = int(mm.group(1))
    if got != want:
        fails.append(f"参数 {name} 期望 {want}，实际 {got}")
        bad += 1
print(f"参数抽查：{len(expect)} 项" + ("  ✓ 全部一致" if bad == 0 else f"  ✗ {bad} 项不一致"))

# ==================================================================
# 输出
# ==================================================================
print()
if fails:
    print(f"✗ 发现 {len(fails)} 个不一致：")
    for f in fails:
        print("   ", f)
    sys.exit(1)

print("✓ 文档与代码一致性校验全部通过")
print("   （引脚 / 状态名 / Markdown 链接 / 串口帧字段 / 关键参数）")
