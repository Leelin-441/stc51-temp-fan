#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_diagrams.py —— 生成 4 张作品展示图（PNG）：
    docs/images/fig1-system-block.png      系统框图
    docs/images/fig2-level-hysteresis.png  四档阶梯回差（本设计的核心）
    docs/images/fig3-wiring.png            接线拓扑
    docs/images/fig4-motor-driver.png      电机驱动电路（两种方案）

为什么用 Pillow 手绘而不是 mermaid / graphviz？
    这两样本机都没装，而 Pillow 是现成的；手绘还能精确控制排版，
    中文字体直接指定 simhei，不会出现"图里中文变方框"。
"""

import math
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "docs", "images")
FONT_PATH = r"C:\Windows\Fonts\simhei.ttf"
_fc = {}


def F(size):
    if size not in _fc:
        _fc[size] = ImageFont.truetype(FONT_PATH, size)
    return _fc[size]


BG = (255, 255, 255)
INK = (26, 31, 38)
MUTED = (108, 118, 130)
LINE = (70, 82, 98)

C_IN = (232, 244, 255); C_IN_E = (52, 120, 200)
C_MCU = (245, 247, 250); C_MCU_E = (40, 48, 60)
C_STEP = (238, 242, 248)
C_OUT = (232, 250, 238); C_OUT_E = (34, 150, 90)
C_ALARM = (255, 235, 235); C_ALARM_E = (200, 45, 45)
C_MODE = (255, 248, 228); C_MODE_E = (200, 150, 20)
C_WARN = (255, 245, 232); C_WARN_E = (200, 120, 40)
C_RED = (214, 48, 48)
C_BLUE = (52, 120, 200)
C_GREEN = (34, 150, 90)

# 四档的配色（从冷到热）
LV_COLORS = {
    "STOP":  ((226, 240, 255), (60, 110, 180)),
    "LOW":   ((220, 248, 226), (40, 150, 90)),
    "MID":   ((255, 246, 214), (200, 150, 20)),
    "HIGH":  ((255, 226, 214), (210, 100, 40)),
    "ALARM": ((255, 220, 220), (200, 45, 45)),
}


# ---------------------------------------------------------------- 基础绘图
def dc(d, cx, cy, lines, fonts, colors=None, spacing=6):
    if isinstance(lines, str):
        lines = [lines]
    if not isinstance(fonts, list):
        fonts = [fonts] * len(lines)
    if colors is None:
        colors = [INK] * len(lines)
    hs = []
    for s, f in zip(lines, fonts):
        b = d.textbbox((0, 0), s, font=f)
        hs.append(b[3] - b[1])
    total = sum(hs) + spacing * (len(lines) - 1)
    y = cy - total / 2
    for s, f, c, h in zip(lines, fonts, colors, hs):
        b = d.textbbox((0, 0), s, font=f)
        d.text((cx - (b[2] - b[0]) / 2 - b[0], y - b[1]), s, font=f, fill=c)
        y += h + spacing


def box(d, x, y, w, h, lines, fonts, fill, outline, colors=None, radius=10, width=2):
    d.rounded_rectangle([x, y, x + w, y + h], radius=radius,
                        fill=fill, outline=outline, width=width)
    dc(d, x + w / 2, y + h / 2, lines, fonts, colors)


def arrow(d, x1, y1, x2, y2, color=LINE, width=3, head=13, dashed=False):
    ang = math.atan2(y2 - y1, x2 - x1)
    bx, by = x2 - head * 0.9 * math.cos(ang), y2 - head * 0.9 * math.sin(ang)
    if dashed:
        total = math.hypot(bx - x1, by - y1)
        if total > 0:
            t = 0.0
            while t < total:
                t2 = min(t + 12, total)
                d.line([x1 + (bx - x1) * t / total, y1 + (by - y1) * t / total,
                        x1 + (bx - x1) * t2 / total, y1 + (by - y1) * t2 / total],
                       fill=color, width=width)
                t += 20
    else:
        d.line([x1, y1, bx, by], fill=color, width=width)
    a1, a2 = ang + math.radians(155), ang - math.radians(155)
    d.polygon([(x2, y2),
               (bx + head * 0.5 * math.cos(a1), by + head * 0.5 * math.sin(a1)),
               (bx + head * 0.5 * math.cos(a2), by + head * 0.5 * math.sin(a2))],
              fill=color)


def title(d, W, main, sub=None):
    dc(d, W / 2, 46, [main], [F(38)], [INK])
    if sub:
        dc(d, W / 2, 88, [sub], [F(20)], [MUTED])


def chip(d, x, y, w, h, lines, fill=(250, 251, 253), outline=(160, 172, 188),
         colors=None, size=16, radius=8):
    box(d, x, y, w, h, lines, [F(size)] * len(lines), fill, outline,
        colors or [INK] * len(lines), radius=radius, width=2)


# ==================================================================
# 图 1：系统框图
# ==================================================================
def fig1():
    W, H = 1760, 900
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    title(d, W, "图 1  DS18B20 智能温控散热系统 · 系统框图",
          "传感 → 判定 → 执行 的本地闭环；PWM 与显示跑在定时器中断里，主循环只做软实时")

    for cx, s in ((200, "输 入 层"), (820, "主 控 层（STC89C52RC）"), (1535, "输 出 层")):
        dc(d, cx, 130, [s], [F(24)], [(30, 64, 124)])

    ins = [
        ("DS18B20 温度传感器", "单总线，-55 ~ +125℃", "DQ → P3.7  + 4.7k 上拉"),
        ("按键 K3 / K4", "切换模式 / 调目标温度", "P3.2 / P3.3"),
        ("串口注入模拟温度", "SIM T=42.5（用于可重复测试）", "RXD P3.0 / TXD P3.1"),
    ]
    ys = [200, 340, 480]
    for (name, desc, pin), y in zip(ins, ys):
        box(d, 40, y, 320, 100, [name, desc, pin], [F(21), F(17), F(16)],
            C_IN, C_IN_E, [INK, MUTED, C_IN_E])

    mx, my, mw, mh = 400, 160, 840, 620
    d.rounded_rectangle([mx, my, mx + mw, my + mh], radius=18,
                        fill=C_MCU, outline=C_MCU_E, width=3)

    left_steps = [
        ("①  采集", "DS18B20 1-Wire 位级时序 + CRC 校验\n非阻塞采样：发起转换 → 等 800ms → 读结果", 195),
        ("②  判定", "四档阶梯回差（迟滞）· 报警锁存 5s · 传感器故障保护", 330),
        ("③  执行", "档位 → 占空比 → 软件 PWM（0 / 34 / 66 / 100%）", 465),
    ]
    step_mid = []
    for name, desc, sy in left_steps:
        box(d, mx + 40, sy, mw - 80, 105, [name, desc], [F(23), F(17)],
            C_STEP, (150, 162, 178), [INK, MUTED])
        step_mid.append((mx + mw / 2, sy + 52))

    # 中断框（右侧，跨三行）—— 高度留够，别让下面的"主循环"文字压在边框上
    ix, iy, iw, ih = mx + 40, 600, mw - 80, 136
    d.rounded_rectangle([ix, iy, ix + iw, iy + ih], radius=12,
                        fill=(255, 250, 235), outline=C_MODE_E, width=3)
    dc(d, ix + iw / 2, iy + 36, ["定时器 0 中断（每 200us）——“硬实时”部分"],
       [F(21)],
       [C_MODE_E])
    dc(d, ix + iw / 2, iy + 76,
       ["PWM 计数翻转电机 IO（100Hz）   ·   LCD 一次写入自保持，中断里不再扫描"],
       [F(17)], [INK])
    dc(d, ix + iw / 2, iy + 110,
       ["毫秒计数   ·   按键采样消抖（20ms）"],
       [F(17)], [INK])

    # 主循环说明
    dc(d, mx + mw / 2, my + mh - 20,
       ["主循环：DS18B20 采样状态机 · 每 100ms 判定 · 每 200ms 刷显示 · 每 1s 上报串口"],
       [F(17)], [MUTED])

    outs = [
        ("5V 直流小电机", "经 ULN2003 低边驱动", "P1.0 → IN1"),
        ("LCD1602 字符液晶", "两行 16 字：温度 / 目标 / 模式 / 状态 / 占空比",
         "P0.0~P0.7 数据 · P2.5 RW · P2.6 RS · P2.7 E"),
        ("红绿 LED", "绿=运行 红=报警/故障闪烁", "P2.0 / P2.1"),
        ("蜂鸣器", "跳变短鸣 · 报警急促 · 故障长鸣", "P1.5"),
        ("串口上报", "$S,ms,T,SET,LEVEL,DUTY,ST,MODE,BUZ,SIM,SENSOR", "TXD P3.1"),
    ]
    ys2 = [180, 315, 450, 565, 685]
    for (name, desc, pin), y in zip(outs, ys2):
        box(d, 1290, y, 450, 115, [name, desc, pin], [F(20), F(14), F(14)],
            C_OUT, C_OUT_E, [INK, MUTED, C_OUT_E])

    # 连线
    for y in ys:
        arrow(d, 366, y + 50, mx + 36, step_mid[0][1] if y < 300 else
              (step_mid[1][1] if y < 440 else iy + 75), C_IN_E, 3)
    for y in ys2:
        src = step_mid[0][1] if y < 300 else (step_mid[2][1] if y < 560 else iy + 75)
        arrow(d, mx + mw - 6, src, 1284, y + 56, C_OUT_E, 3)
    for i in range(len(step_mid) - 1):
        arrow(d, mx + mw / 2, left_steps[i][2] + 105, mx + mw / 2,
              left_steps[i + 1][2] - 2, (140, 152, 168), 3)

    os.makedirs(OUT, exist_ok=True)
    p = os.path.join(OUT, "fig1-system-block.png")
    img.save(p)
    return p


# ==================================================================
# 图 2：四档阶梯回差
# ==================================================================
def fig2():
    W, H = 1680, 1070
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    title(d, W, "图 2  四档阶梯回差（迟滞）——本设计的核心",
          "进档与退档用不同阈值，中间留 2℃ 死区；实测把临界抖动时的档位切换从 100 次降到 1 次")

    # ---- 温度轴映射：T-Tset 从 -60 到 +100（单位 0.1℃）----
    X0, X1 = 150, 1600
    LO, HI = -60, 100

    def X(v):
        return X0 + (v - LO) / (HI - LO) * (X1 - X0)

    axis_y = 430
    band_h = 96

    thr = [
        ("退停机", -40, "Tset-4.0℃", C_BLUE),
        ("进低速", -20, "Tset-2.0℃", C_GREEN),
        ("进中速", 0, "Tset", C_MODE_E),
        ("进全速", 30, "Tset+3.0℃", C_WARN_E),
        ("解除", 40, "Tset+4.0℃", C_ALARM_E),
        ("报警", 80, "Tset+8.0℃", C_ALARM_E),
    ]

    bands = [
        ("STOP  停机", -60, -20),
        ("LOW  低速 34%", -20, 0),
        ("MID  中速 66%", 0, 30),
        ("HIGH  全速 100%", 30, 100),
    ]
    for name, a, b in bands:
        ca, cb = LV_COLORS[name.split()[0]]
        xa, xb = X(a), X(b)
        d.rounded_rectangle([xa, axis_y - band_h / 2, xb - 6, axis_y + band_h / 2],
                            radius=8, fill=ca, outline=cb, width=3)
        dc(d, (xa + xb) / 2, axis_y, [name], [F(20)], [cb])

    xa, xb = X(80), X(100)
    d.rounded_rectangle([xa, axis_y + band_h / 2 + 14, xb, axis_y + band_h / 2 + 84],
                        radius=8, fill=LV_COLORS["ALARM"][0],
                        outline=LV_COLORS["ALARM"][1], width=3)
    dc(d, (xa + xb) / 2, axis_y + band_h / 2 + 49, ["ALARM  报警"],
       [F(20)], [LV_COLORS["ALARM"][1]])

    for label, v, expr, color in thr:
        x = X(v)
        ytop = axis_y - band_h / 2 - 90
        ybot = axis_y + band_h / 2 + 96 if v == 80 else axis_y + band_h / 2 + 8
        d.line([x, ytop, x, ybot], fill=color, width=2)
        dc(d, x, ytop - 40, [expr], [F(16)], [color])
        dc(d, x, ytop - 20, [label], [F(15)], [MUTED])

    # ---- 温度轴刻度 ----
    d.line([X0 - 40, axis_y + band_h + 110, X1 + 20, axis_y + band_h + 110],
           fill=LINE, width=3)
    v = -60
    while v <= 100:
        x = X(v)
        d.line([x, axis_y + band_h + 110, x, axis_y + band_h + 122], fill=LINE, width=2)
        dc(d, x, axis_y + band_h + 144, [f"{v / 10:+.1f}" if v != 0 else "Tset"],
           [F(15)], [MUTED])
        v += 20
    dc(d, (X0 + X1) / 2, axis_y + band_h + 178,
       ["温度相对于目标温度的偏差（℃）"], [F(18)], [INK])

    # ---- 回差示意（LOW ↔ MID 这一对）----
    y_enter = axis_y - band_h / 2 - 200
    arrow(d, X(-20), y_enter, X(0), y_enter, C_BLUE, 3)
    dc(d, (X(-20) + X(0)) / 2, y_enter - 26,
       ["进档：T ≥ Tset 就升到中速"], [F(17)], [C_BLUE])

    y_exit = axis_y + band_h + 240
    arrow(d, X(0), y_exit, X(-20), y_exit, (200, 90, 60), 3)
    dc(d, (X(-20) + X(0)) / 2, y_exit + 28,
       ["退档：要等 T ≤ Tset-2.0℃ 才降回低速"], [F(17)], [(200, 90, 60)])

    d.rectangle([X(-20), axis_y - band_h / 2 - 6, X(0), axis_y + band_h / 2 + 6],
                outline=(200, 45, 45), width=3)
    dc(d, (X(-20) + X(0)) / 2, axis_y - band_h / 2 - 28,
       ["2℃ 死区"], [F(17)], [(200, 45, 45)])

    # ---- 底部：状态转移 ----
    by = 920
    d.rounded_rectangle([60, by - 50, 1620, by + 115], radius=14,
                        fill=(248, 250, 252), outline=(160, 172, 188), width=2)
    dc(d, 840, by - 26, ["档位状态转移（进档阈值 / 退档阈值）"], [F(20)], [INK])

    nodes = [("STOP", 120), ("LOW", 420), ("MID", 760), ("HIGH", 1160)]
    for name, x in nodes:
        ca, cb = LV_COLORS[name]
        box(d, x, by + 6, 180, 60, [name], [F(22)], ca, cb, [cb], radius=8, width=2)

    pairs = [
        (120, 420, "Tset-2.0", "Tset-4.0"),
        (420, 760, "Tset", "Tset-2.0"),
        (760, 1160, "Tset+3.0", "Tset+1.0"),
    ]
    for x1, x2, up, dn in pairs:
        arrow(d, x1 + 184, by + 24, x2 - 6, by + 24, C_BLUE, 3)
        arrow(d, x2 - 6, by + 50, x1 + 184, by + 50, (200, 90, 60), 3)
        dc(d, (x1 + 184 + x2) / 2, by + 6, [f"T≥{up}"], [F(15)], [C_BLUE])
        dc(d, (x1 + 184 + x2) / 2, by + 70, [f"T≤{dn}"], [F(15)], [(200, 90, 60)])

    chip(d, 1400, by + 6, 210, 60, ["T≥Tset+8.0", "→ ALARM 报警"],
         colors=[C_ALARM_E, C_ALARM_E], size=15)

    os.makedirs(OUT, exist_ok=True)
    p = os.path.join(OUT, "fig2-level-hysteresis.png")
    img.save(p)
    return p


# ==================================================================
# 图 3：接线拓扑
# ==================================================================
def fig3():
    W, H = 1900, 1380
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    title(d, W, "图 3  模块接线拓扑与引脚对应（普中/清翔开发板）",
          "板载外设只需插上 DS18B20 并给 DQ 加 4.7k 上拉；电机必须经 ULN2003 驱动，不能直连 IO")

    bx, by, bw, bh = 620, 190, 480, 780
    d.rounded_rectangle([bx, by, bx + bw, by + bh], radius=18,
                        fill=(236, 241, 248), outline=C_MCU_E, width=3)
    dc(d, bx + bw / 2, by + 44, ["STC89C52RC"], [F(30)], [INK])
    dc(d, bx + bw / 2, by + 82, ["开发板 · 11.0592MHz · 5V 供电"],
       [F(17)], [MUTED])

    left_pins = [
        ("P3.7", "DS18B20 DQ（4.7k 上拉到 +5V）", 160),
        ("P3.2", "按键 K3  SET", 250),
        ("P3.3", "按键 K4  UP", 320),
        ("P3.0", "串口 RXD ← USB-TTL TXD", 430),
        ("P3.1", "串口 TXD → USB-TTL RXD", 495),
        ("P1.5", "蜂鸣器 BEEP", 600),
        ("P1.0", "电机 PWM → ULN2003 IN1", 700),
    ]
    right_pins = [
        ("P0.0~P0.7", "LCD1602 数据", 160),
        ("P2.6/P2.5/P2.7", "LCD1602 RS / RW / E", 235),
        ("P2.0", "LED 绿", 330),
        ("P2.1", "LED 红", 380),
    ]

    # 标签框留 150px：'P2.6/P2.5/P2.7' 这类长名在 F(18) 下也不会顶到框线
    PIN_TAG_W = 150
    for name, desc, dy in left_pins:
        yy = by + dy
        d.rectangle([bx - 6 - PIN_TAG_W, yy - 17, bx - 6, yy + 17], fill=(255, 255, 255),
                    outline=(150, 162, 178), width=2)
        dc(d, bx - 6 - PIN_TAG_W / 2, yy, [name], [F(18)], [INK])
        d.text((bx + 14, yy - 10), desc, font=F(16), fill=MUTED)

    for name, desc, dy in right_pins:
        yy = by + dy
        d.rectangle([bx + bw + 6, yy - 17, bx + bw + 6 + PIN_TAG_W, yy + 17],
                    fill=(255, 255, 255), outline=(150, 162, 178), width=2)
        dc(d, bx + bw + 6 + PIN_TAG_W / 2, yy, [name], [F(18)], [INK])
        # 说明文字右对齐到开发板框内边，留 10px 不贴框线
        b = d.textbbox((0, 0), desc, font=F(15))
        d.text((bx + bw - 10 - (b[2] - b[0]), yy - 9), desc, font=F(15), fill=MUTED)

    mods_left = [
        (50, by + 100, 320, 130, "DS18B20 温度传感器",
         "VDD → +5V   GND → GND\nDQ  → P3.7   并接 4.7kΩ 到 +5V", C_IN, C_IN_E),
        (50, by + 250, 320, 160, "按键 K3 / K4", "低电平有效\n一端接 P3.2 / P3.3\n另一端接 GND",
         C_IN, C_IN_E),
        (50, by + 440, 320, 150, "USB 转 TTL (CH340)",
         "TXD → P3.0\nRXD → P3.1\nGND → GND    不接 VCC", C_IN, C_IN_E),
        (50, by + 620, 320, 150, "蜂鸣器（板载）", "BEEP → P1.5\n低电平响",
         C_IN, C_IN_E),
    ]
    for x, y, w, h, nm, ds, fa, fe in mods_left:
        box(d, x, y, w, h, [nm, ds], [F(20), F(16)], fa, fe, [INK, MUTED])

    mods_right = [
        (1530, by + 85, 320, 205, "LCD1602 字符液晶（8 位模式）",
         "D0~D7   → P0.0~P0.7\n"
         "RS      → P2.6\n"
         "RW      → P2.5\n"
         "E       → P2.7\n"
         "V0      → 板上电位器 RJ1 中间脚\n"
         "A / K   → +5V / GND",
         C_OUT, C_OUT_E),
        (1530, by + 310, 320, 130, "双色 LED（板载）", "绿 → P2.0\n红 → P2.1\n低电平点亮",
         C_OUT, C_OUT_E),
        (1530, by + 470, 320, 200, "ULN2003 驱动 + 5V 电机",
         "51 P1.0 → IN1\nOUT1 → 电机负极\n电机正极 → +5V\nGND 共地",
         C_WARN, C_WARN_E),
    ]
    for x, y, w, h, nm, ds, fa, fe in mods_right:
        box(d, x, y, w, h, [nm, ds], [F(20), F(15)], fa, fe, [INK, MUTED])

    # 电机与负载示意
    box(d, 1530, by + 720, 320, 90, ["5V 直流小电机", "稳态 0.15~0.25A，启动可达 1A"],
        [F(19), F(15)], (255, 245, 232), (200, 120, 40), [INK, MUTED])
    arrow(d, 1690, by + 675, 1690, by + 716, (200, 120, 40), 3)

    # 提示块：LCD 与板载数码管共用 P0（整幅宽，压在底部规则面板上方）
    d.rounded_rectangle([50, 985, 1850, 1080], radius=12,
                        fill=(238, 242, 248), outline=(160, 172, 188), width=2)
    dc(d, 950, 1032,
       ["提示：LCD 与板载数码管共用 P0 口（板子硬件如此），写 LCD 时数码管会跟着乱显示；",
        "拔掉数码管模块的电源跳线帽即可，不影响 LCD"],
       [F(18), F(18)], [MUTED, MUTED], spacing=10)

    # 底部提示：四条硬性规则
    d.rounded_rectangle([50, 1100, 1850, 1350], radius=14,
                        fill=(255, 250, 235), outline=C_MODE_E, width=2)
    dc(d, 950, 1225,
       ["① 电机绝对不能直接接 51 的 IO 口（IO 只能出约 20mA，电机会烧芯片）",
        "② 电机的启动电流是稳态的 3~5 倍，务必给 5V 电源并一个 470uF 电解电容",
        "③ 电机两端必须反并联一只 1N4007 续流二极管，否则关断瞬间的反向电动势会打坏驱动管",
        "④ LCD 的 D0~D7 走 P0 整口写；RS/RW/E 走 P2.5~P2.7 用 sbit 逐位写，不会碰到 P2.0/P2.1 的 LED"],
       [F(19)] * 4, [INK, INK, (180, 90, 20), C_BLUE], spacing=40)

    os.makedirs(OUT, exist_ok=True)
    p = os.path.join(OUT, "fig3-wiring.png")
    img.save(p)
    return p


# ==================================================================
# 图 4：电机驱动电路
# ==================================================================
def fig4():
    W, H = 1700, 1180
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    title(d, W, "图 4  5V 直流小电机的两种驱动电路",
          "51 的 IO 口只能输出约 20mA，绝不能直连电机；下面两种方案任选其一")

    def panel(x0, y0, w, h, accent, name, sub):
        d.rounded_rectangle([x0, y0, x0 + w, y0 + h], radius=16,
                            fill=(252, 253, 255), outline=accent, width=3)
        dc(d, x0 + w / 2, y0 + 36, [name], [F(25)], [accent])
        dc(d, x0 + w / 2, y0 + 72, [sub], [F(17)], [MUTED])
        # 电路区用浅底框起来，与文字彻底隔离，避免压盖
        d.rounded_rectangle([x0 + 25, y0 + 100, x0 + w - 25, y0 + 610],
                            radius=12, fill=(248, 250, 252), outline=(206, 214, 226),
                            width=2)

    def notes(x0, lines):
        y = 800
        for s, c, sz in lines:
            d.text((x0, y), s, font=F(sz), fill=c)
            y += 42

    # ================= 方案 A：ULN2003 =================
    ax0, ay0, aw, ah = 60, 150, 780, 960
    panel(ax0, ay0, aw, ah, C_OUT_E, "方案 A：用板载 ULN2003（推荐）",
          "普中/清翔开发板上的步进电机驱动芯片就是它，零成本")
    sx, sy = ax0 + 60, ay0 + 150          # (120, 300)

    d.line([sx + 60, sy, sx + 560, sy], fill=C_ALARM_E, width=4)
    dc(d, sx + 25, sy - 26, ["+5V"], [F(20)], [C_ALARM_E])

    d.line([sx + 200, sy, sx + 200, sy + 30], fill=LINE, width=3)
    box(d, sx + 140, sy + 30, 120, 85, ["5V 电机"], [F(20)], C_WARN, C_WARN_E, [C_WARN_E])
    d.line([sx + 200, sy + 115, sx + 200, sy + 210], fill=LINE, width=3)

    d.line([sx + 470, sy, sx + 470, sy + 210], fill=C_ALARM_E, width=3)
    d.polygon([(sx + 470, sy + 90), (sx + 450, sy + 126), (sx + 490, sy + 126)],
              fill=C_ALARM_E)
    d.line([sx + 450, sy + 86, sx + 490, sy + 86], fill=C_ALARM_E, width=3)
    dc(d, sx + 550, sy + 96, ["1N4007"], [F(17)], [C_ALARM_E])
    dc(d, sx + 550, sy + 124, ["续流"], [F(15)], [C_ALARM_E])

    d.line([sx + 200, sy + 210, sx + 470, sy + 210], fill=LINE, width=3)
    d.line([sx + 335, sy + 210, sx + 335, sy + 252], fill=LINE, width=3)

    box(d, sx + 130, sy + 255, 410, 110,
        ["ULN2003   7 路达林顿驱动阵列"], [F(21)], C_OUT, C_OUT_E, [C_OUT_E])
    dc(d, sx + 350, sy + 238, ["OUT1 ← 电机负极"], [F(15)], [C_OUT_E])
    box(d, sx - 20, sy + 275, 110, 60, ["P1.0"], [F(20)], C_MCU, C_MCU_E, [INK])
    arrow(d, sx + 94, sy + 305, sx + 124, sy + 305, C_BLUE, 3)
    dc(d, sx + 108, sy + 268, ["IN1"], [F(15)], [C_BLUE])
    dc(d, sx + 335, sy + 392, ["GND → 与单片机共地      COM → +5V"], [F(16)], [MUTED])

    notes(ax0 + 45, [
        ("优点：零成本、输入阻抗高，8051 的高电平可以直接驱动", C_OUT_E, 18),
        ("接线：P1.0 → IN1     电机正极 → +5V     电机负极 → OUT1", INK, 17),
        ("      COM → +5V      GND → 与单片机共地", INK, 17),
        ("注意：ULN2003 是反相驱动，负载必须接在 +5V 与 OUT 之间；", MUTED, 17),
        ("      把电机接在 OUT 与 GND 之间是永远不会转的", MUTED, 17),
        ("板载 ULN2003 的 IN1~IN4 一般对应 P1.0~P1.3", MUTED, 16),
    ])

    # ================= 方案 B：三极管 =================
    bx0, by0, bw, bh = 860, 150, 780, 960
    panel(bx0, by0, bw, bh, C_WARN_E, "方案 B：S8050 三极管自己搭（约 0.5 元）",
          "元件：S8050 ×1、1N4007 ×1、1kΩ ×1、10kΩ ×1")
    sx, sy = bx0 + 60, by0 + 150          # (920, 300)

    d.line([sx + 80, sy + 20, sx + 540, sy + 20], fill=C_ALARM_E, width=4)
    dc(d, sx + 40, sy - 6, ["+5V"], [F(20)], [C_ALARM_E])

    d.line([sx + 200, sy + 20, sx + 200, sy + 50], fill=LINE, width=3)
    box(d, sx + 140, sy + 50, 120, 85, ["5V 电机"], [F(20)], C_WARN, C_WARN_E, [C_WARN_E])
    d.line([sx + 200, sy + 135, sx + 200, sy + 240], fill=LINE, width=3)

    d.line([sx + 470, sy + 20, sx + 470, sy + 240], fill=C_ALARM_E, width=3)
    d.polygon([(sx + 470, sy + 110), (sx + 450, sy + 146), (sx + 490, sy + 146)],
              fill=C_ALARM_E)
    d.line([sx + 450, sy + 106, sx + 490, sy + 106], fill=C_ALARM_E, width=3)
    dc(d, sx + 550, sy + 116, ["1N4007"], [F(17)], [C_ALARM_E])
    dc(d, sx + 550, sy + 144, ["续流"], [F(15)], [C_ALARM_E])

    d.line([sx + 200, sy + 240, sx + 470, sy + 240], fill=LINE, width=3)

    # 三极管符号
    d.line([sx + 380, sy + 240, sx + 380, sy + 280], fill=LINE, width=3)   # C 引线
    d.line([sx + 330, sy + 290, sx + 330, sy + 370], fill=INK, width=5)    # 基极板
    d.line([sx + 330, sy + 310, sx + 380, sy + 280], fill=INK, width=3)    # C 臂
    d.line([sx + 330, sy + 350, sx + 380, sy + 380], fill=INK, width=3)    # E 臂
    d.line([sx + 380, sy + 380, sx + 380, sy + 430], fill=INK, width=3)
    d.polygon([(sx + 368, sy + 372), (sx + 386, sy + 358), (sx + 380, sy + 378)],
              fill=INK)
    dc(d, sx + 398, sy + 268, ["C"], [F(17)], [INK])
    dc(d, sx + 398, sy + 392, ["E"], [F(17)], [INK])
    dc(d, sx + 305, sy + 322, ["B"], [F(17)], [INK])
    dc(d, sx + 560, sy + 320, ["S8050"], [F(18)], [C_WARN_E])
    dc(d, sx + 560, sy + 348, ["(NPN)"], [F(16)], [C_WARN_E])

    # 基极：P1.0 → 1k → B
    d.line([sx + 330, sy + 330, sx + 250, sy + 330], fill=C_BLUE, width=3)
    d.rectangle([sx + 200, sy + 312, sx + 250, sy + 348], fill=(255, 255, 255),
                outline=C_BLUE, width=3)
    dc(d, sx + 225, sy + 330, ["1k"], [F(17)], [C_BLUE])
    d.line([sx + 200, sy + 330, sx + 150, sy + 330], fill=C_BLUE, width=3)
    box(d, sx + 20, sy + 300, 110, 60, ["P1.0"], [F(20)], C_MCU, C_MCU_E, [INK])
    arrow(d, sx + 134, sy + 330, sx + 146, sy + 330, C_BLUE, 3)
    dc(d, sx + 60, sy + 278, ["PWM"], [F(15)], [C_BLUE])

    # 基极下拉 10k
    d.line([sx + 250, sy + 330, sx + 250, sy + 430], fill=(140, 150, 165), width=2)
    d.rectangle([sx + 232, sy + 370, sx + 268, sy + 410], fill=(255, 255, 255),
                outline=(140, 150, 165), width=2)
    dc(d, sx + 250, sy + 390, ["10k"], [F(14)], [MUTED])

    # GND 轨
    d.line([sx + 150, sy + 430, sx + 500, sy + 430], fill=INK, width=4)
    dc(d, sx + 110, sy + 430, ["GND"], [F(19)], [INK])

    notes(bx0 + 45, [
        ("优点：成本最低，一只 S8050 就能驱动 0.5A 以内的电机", C_WARN_E, 18),
        ("元件：S8050 ×1    1N4007 ×1    1kΩ ×1    10kΩ ×1", INK, 17),
        ("接线：P1.0 →1kΩ→ 基极 B      发射极 E → GND", INK, 17),
        ("      集电极 C → 电机负极     电机正极 → +5V", INK, 17),
        ("要点：基极到 GND 加 10kΩ 下拉，防止单片机复位期间引脚悬空", MUTED, 17),
        ("      导致电机乱转；电机两端必须反并联 1N4007 续流", MUTED, 17),
    ])

    os.makedirs(OUT, exist_ok=True)
    p = os.path.join(OUT, "fig4-motor-driver.png")
    img.save(p)
    return p


if __name__ == "__main__":
    for fn in (fig1, fig2, fig3, fig4):
        p = fn()
        print("已生成", os.path.relpath(p, ROOT))
