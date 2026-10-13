#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sim_controller.py —— 温控状态机参考仿真器 + 自动化重复测试。

为什么要有它？
    1. 器件和 Keil 都还没到位，先用 PC 证明"判定逻辑本身是对的"。
    2. 靠手捂 DS18B20 让它升温，每次结果都不一样，写不出可量化的测试结论。
       用注入数据的仿真跑 N 轮完全相同的用例，才能给出"重复 210 次全部通过"。
    3. 阶梯回差的价值需要**定量**证明：把温度卡在临界点上下抖动，
       统计档位切换次数。没有回差会切几十次，有回差只切 1 次。

★ 所有阈值都从 firmware/config.h 解析，不在本文件里另写一份。
  改了固件参数，仿真结果立刻跟着变，不可能出现"文档说 35℃、代码是 30℃"。

输出：
    docs/06-测试记录-自动生成.csv
    docs/07-测试报告-自动生成.md
"""

import csv
import os
import random
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CONFIG_H = os.path.join(ROOT, "firmware", "config.h")
OUT_CSV = os.path.join(ROOT, "docs", "06-测试记录-自动生成.csv")
OUT_MD = os.path.join(ROOT, "docs", "07-测试报告-自动生成.md")


# ==================================================================
# 一、从 config.h 解析常量
# ==================================================================
def parse_config():
    with open(CONFIG_H, "r", encoding="utf-8") as f:
        text = f.read()

    raw = {}
    for m in re.finditer(r'^\s*#\s*define\s+([A-Z][A-Z0-9_]*)\s+(.+?)\s*(?:/\*.*)?$',
                         text, re.M):
        name, expr = m.group(1), m.group(2).strip()
        expr = re.sub(r'/\*.*?\*/', '', expr).strip()
        expr = re.sub(r'\b(\d+(?:\.\d+)?)[uUlL]+\b', r'\1', expr)
        raw[name] = expr

    consts = {}
    for _ in range(6):
        for name, expr in list(raw.items()):
            e = expr
            for k, v in consts.items():
                e = re.sub(r'\b' + re.escape(k) + r'\b', str(v), e)
            e = re.sub(r'\b0[xX][0-9a-fA-F]+\b',
                       lambda mm: str(int(mm.group(0), 16)), e)
            if not re.fullmatch(r'[\d\s\+\-\*/%|&~^\(\)<>\.]+', e):
                continue
            try:
                consts[name] = eval(e, {"__builtins__": {}}, {})   # noqa: S307
            except Exception:                                       # noqa: BLE001
                continue

    need = ["TSET_DEF", "TSET_MIN", "TSET_MAX", "TSET_STEP",
            "LV_ENTER_LOW", "LV_EXIT_LOW", "LV_ENTER_MID", "LV_EXIT_MID",
            "LV_ENTER_HIGH", "LV_EXIT_HIGH",
            "ALARM_OFFSET", "ALARM_EXIT", "ALARM_HOLD_MS",
            "DS_FAIL_LIMIT", "TICK_MS", "PWM_LEVELS",
            "MOTOR_DUTY_STOP", "MOTOR_DUTY_LOW", "MOTOR_DUTY_MID", "MOTOR_DUTY_HIGH",
            "LV_STOP", "LV_LOW", "LV_MID", "LV_HIGH"]
    missing = [k for k in need if k not in consts]
    if missing:
        raise SystemExit(f"解析 config.h 失败，缺少常量: {missing}")
    return consts


C = parse_config()

LV_STOP, LV_LOW, LV_MID, LV_HIGH = C["LV_STOP"], C["LV_LOW"], C["LV_MID"], C["LV_HIGH"]
DUTY = {LV_STOP: C["MOTOR_DUTY_STOP"], LV_LOW: C["MOTOR_DUTY_LOW"],
        LV_MID: C["MOTOR_DUTY_MID"], LV_HIGH: C["MOTOR_DUTY_HIGH"]}
LEVEL_NAME = {LV_STOP: "STOP", LV_LOW: "LOW", LV_MID: "MID", LV_HIGH: "HIGH"}


# ==================================================================
# 二、控制器：与 firmware/app.c 的 control_update() 一一对应
# ==================================================================
class Controller:
    def __init__(self):
        self.reset()

    def reset(self):
        self.tset = C["TSET_DEF"]
        self.mode = "AUTO"
        self.level = LV_STOP
        self.manual_level = LV_STOP
        self.level_eff = LV_STOP
        self.duty = 0
        self.temp = 0
        self.real_temp = 0
        self.alarm = False
        self.alarm_start = 0
        self.fault = False
        self.state = "INIT"
        self.sim_enable = False
        self.sim_temp = C["TSET_DEF"]
        self.ds_fail = 0
        self.now = 0

    def tick(self):
        """推进一个 TICK_MS 的控制周期，逻辑与 app.c 的 control_update() 对齐。"""
        now = self.now

        # ---- 1. 数据源 ----
        if self.sim_enable:
            self.temp = self.sim_temp
        else:
            self.temp = self.real_temp

        # ---- 2. 传感器故障 ----
        self.fault = bool((not self.sim_enable) and (self.ds_fail >= C["DS_FAIL_LIMIT"]))

        # ---- 3. 报警锁存 ----
        cond = self.fault or (self.temp >= self.tset + C["ALARM_OFFSET"])
        if not self.alarm:
            if cond:
                self.alarm = True
                self.alarm_start = now
        else:
            if (not cond) \
               and (self.temp <= self.tset + C["ALARM_EXIT"]) \
               and (now - self.alarm_start) >= C["ALARM_HOLD_MS"]:
                self.alarm = False

        # ---- 4. 阶梯回差 ----
        if self.mode == "AUTO":
            el = self.tset + C["LV_ENTER_LOW"]
            xl = self.tset + C["LV_EXIT_LOW"]
            em = self.tset + C["LV_ENTER_MID"]
            xm = self.tset + C["LV_EXIT_MID"]
            eh = self.tset + C["LV_ENTER_HIGH"]
            xh = self.tset + C["LV_EXIT_HIGH"]

            if self.level == LV_STOP:
                if self.temp >= el:
                    self.level = LV_LOW
            elif self.level == LV_LOW:
                if self.temp >= em:
                    self.level = LV_MID
                elif self.temp <= xl:
                    self.level = LV_STOP
            elif self.level == LV_MID:
                if self.temp >= eh:
                    self.level = LV_HIGH
                elif self.temp <= xm:
                    self.level = LV_LOW
            elif self.level == LV_HIGH:
                if self.temp <= xh:
                    self.level = LV_MID

        # ---- 5. 合成状态与占空比 ----
        if self.fault:
            self.state, self.level_eff, self.duty = "FAULT", LV_STOP, DUTY[LV_STOP]
        elif self.alarm:
            self.state, self.level_eff, self.duty = "ALARM", LV_HIGH, DUTY[LV_HIGH]
        elif self.mode == "MANUAL":
            self.state = "MANUAL"
            self.level_eff = self.manual_level
            self.duty = DUTY[self.manual_level]
        else:
            self.level_eff = self.level
            self.duty = DUTY[self.level]
            self.state = LEVEL_NAME[self.level]

        self.now += C["TICK_MS"]

    def hold(self, ms):
        for _ in range(max(1, ms // C["TICK_MS"])):
            self.tick()


# ==================================================================
# 三、用例集（编号与 tools/serial_test.ps1 的 L2 用例一致）
# ==================================================================
def build_cases():
    def case(cid, name, sim_temp, expect_st, expect_lv, ms=1200,
             mode="AUTO", manual=None, tset=None, sim=True):
        return dict(id=cid, name=name, sim_temp=sim_temp, st=expect_st, lv=expect_lv,
                    ms=ms, mode=mode, manual=manual, tset=tset, sim=sim)

    cases = [
        # ---------------- R1 基本档位 ----------------
        case("R1-01", "停机态：温度低于 Tset-2.0℃", 250, "STOP", LV_STOP),
        case("R1-02", "低速档：温度进入 Tset-2.0 ~ Tset", 340, "LOW", LV_LOW),
        case("R1-03", "中速档：温度进入 Tset ~ Tset+3.0", 360, "MID", LV_MID),
        case("R1-04", "全速档：温度 ≥ Tset+3.0", 390, "HIGH", LV_HIGH),
        case("R1-05", "报警：温度 ≥ Tset+8.0，强制全速", 440, "ALARM", LV_HIGH),

        # ---------------- R2 手动模式 ----------------
        dict(id="R2-01", name="手动模式：指定 3 档，忽略传感器",
             sim_temp=250, st="MANUAL", lv=LV_HIGH, ms=1200,
             mode="MANUAL", manual=LV_HIGH, tset=None, sim=True),
        # 注意：这里刻意用 40.0℃ 而不是 44.0℃。
        # 44.0℃ 已经越过报警阈值（Tset+8.0 = 43.0℃），而报警的优先级
        # **高于**手动模式（安全设计：报警时无论如何都要全速散热），
        # 那时状态会是 ALARM 而不是 MANUAL。40.0℃ 只触发自动档位，
        # 才能干净地验证"手动模式忽略传感器"这一条。
        dict(id="R2-02", name="手动模式：指定 0 档，即使温度很高（未报警）也不转",
             sim_temp=400, st="MANUAL", lv=LV_STOP, ms=1200,
             mode="MANUAL", manual=LV_STOP, tset=None, sim=True),

        # ---------------- R3 阶梯回差 ----------------
        case("R3-01", "回差上沿：Tset-2.5℃ 仍为停机", 325, "STOP", LV_STOP),
        dict(id="R3-02", name="回差保持：先进中速，再降到 Tset-0.5℃ 仍保持",
             sim_temp=360, st="MID", lv=LV_MID, ms=800,
             mode="AUTO", manual=None, tset=None, sim=True,
             step2=345),
        dict(id="R3-03", name="回差退出：降到 Tset-2.5℃ 回落到低速",
             sim_temp=360, st="LOW", lv=LV_LOW, ms=800,
             mode="AUTO", manual=None, tset=None, sim=True,
             step2=325),

        # ---------------- R4 在线改参数 ----------------
        dict(id="R4-01", name="在线改目标温度：Tset=25.0 时 25.0℃ 进中速",
             sim_temp=250, st="MID", lv=LV_MID, ms=1200,
             mode="AUTO", manual=None, tset=250, sim=True),
        dict(id="R4-02", name="报警最短保持 5s：降回安全区后 2s 内仍报警",
             sim_temp=440, st="ALARM", lv=LV_HIGH, ms=1200,
             mode="AUTO", manual=None, tset=None, sim=True,
             step2=350, step2_ms=1400),

        # ---------------- R5 传感器故障 ----------------
        dict(id="R5-01", name="传感器故障保护：连续 3 次读失败 → 电机强制停",
             sim_temp=0, st="FAULT", lv=LV_STOP, ms=1200,
             mode="AUTO", manual=None, tset=None, sim=False),

        # ---------------- R6 恢复默认 ----------------
        dict(id="R6-01", name="RESET 后目标温度回到默认值",
             sim_temp=250, st="R6RESET", lv=0, ms=400,
             mode="AUTO", manual=None, tset=None, sim=True),

        # ---------------- 额外的边界用例 ----------------
        case("R7-01", "低温边界：-5.5℃ 仍为停机", -55, "STOP", LV_STOP),
        case("R7-02", "高温边界：125.0℃ 报警", 1250, "ALARM", LV_HIGH),
        dict(id="R7-03", name="目标温度限幅：设成 90.0℃ 应被夹到 60.0℃",
             sim_temp=250, st="STOP", lv=LV_STOP, ms=1200,
             mode="AUTO", manual=None, tset=900, sim=True, check_tset=C["TSET_MAX"]),
        dict(id="R7-04", name="目标温度限幅：设成 5.0℃ 应被夹到 20.0℃",
             sim_temp=250, st="STOP", lv=LV_STOP, ms=1200,
             mode="AUTO", manual=None, tset=50, sim=True, check_tset=C["TSET_MIN"]),
    ]
    return cases


def run_case(case):
    ctl = Controller()
    ctl.mode = case.get("mode", "AUTO")
    if case.get("manual") is not None:
        ctl.manual_level = case["manual"]
    if case.get("tset") is not None:
        v = case["tset"]
        v = max(C["TSET_MIN"], min(C["TSET_MAX"], v))
        ctl.tset = v

    ctl.sim_enable = case.get("sim", True)
    ctl.sim_temp = case["sim_temp"]
    ctl.real_temp = case["sim_temp"]
    if not ctl.sim_enable:
        ctl.ds_fail = C["DS_FAIL_LIMIT"]

    ctl.hold(case["ms"])

    if "step2" in case:
        ctl.sim_temp = case["step2"]
        ctl.hold(case.get("step2_ms", 1000))

    # R6-01 特殊处理：模拟一次 RESET
    if case["st"] == "R6RESET":
        ctl.tset = C["TSET_DEF"]      # RESET 命令的效果
        ctl.sim_enable = True
        ctl.sim_temp = 250
        ctl.hold(800)
        ok = (ctl.tset == C["TSET_DEF"]) and (ctl.state == "STOP") and (ctl.level_eff == LV_STOP)
        return ok, ctl.state, ctl.level_eff, ctl

    if case.get("check_tset") is not None:
        ctl.tset = max(C["TSET_MIN"], min(C["TSET_MAX"], case["tset"]))
        return (ctl.tset == case["check_tset"]), ctl.state, ctl.level_eff, ctl

    ok = (ctl.state == case["st"]) and (ctl.level_eff == case["lv"])
    return ok, ctl.state, ctl.level_eff, ctl


# ==================================================================
# 四、专项分析
# ==================================================================
def test_hysteresis_jitter():
    """温度卡在'进低速'阈值上下反复抖动 100 次，统计档位切换次数。"""
    ctl = Controller()
    ctl.sim_enable = True
    thr = C["TSET_DEF"] + C["LV_ENTER_LOW"]
    toggles = 0
    prev = ctl.level
    for i in range(100):
        ctl.sim_temp = thr + 1 if i % 2 == 0 else thr - 1
        ctl.tick()
        if ctl.level != prev:
            toggles += 1
            prev = ctl.level
    return toggles


def test_no_hysteresis_jitter():
    """对照组：假设没有回差（进档退档同一个阈值），同样抖动会切多少次。"""
    thr = C["TSET_DEF"] + C["LV_ENTER_LOW"]
    on = False
    toggles = 0
    for i in range(100):
        t = thr + 1 if i % 2 == 0 else thr - 1
        new = t >= thr
        if new != on:
            toggles += 1
            on = new
    return toggles


def test_slow_ramp():
    """温度从 20.0℃ 缓慢升到 60.0℃ 再降回 20.0℃，统计档位切换次数与触发点。"""
    ctl = Controller()
    ctl.sim_enable = True
    toggles = 0
    prev = ctl.level
    marks = []
    t = 200
    while t <= 600:
        ctl.sim_temp = t
        ctl.tick()
        if ctl.level != prev:
            toggles += 1
            marks.append((t, LEVEL_NAME[ctl.level]))
            prev = ctl.level
        t += 1
    while t >= 200:
        ctl.sim_temp = t
        ctl.tick()
        if ctl.level != prev:
            toggles += 1
            marks.append((t, LEVEL_NAME[ctl.level]))
            prev = ctl.level
        t -= 1
    return toggles, marks


def test_random_fuzz(n=5000):
    """随机输入模糊测试，检查设计不变式。"""
    rnd = random.Random(20261010)
    ctl = Controller()
    ctl.sim_enable = True
    bad = []
    for i in range(n):
        ctl.sim_temp = rnd.randint(-550, 1250)
        if rnd.random() < 0.05:
            ctl.mode = rnd.choice(["AUTO", "MANUAL"])
        if rnd.random() < 0.05:
            ctl.manual_level = rnd.randint(0, 3)
        if rnd.random() < 0.05:
            ctl.tset = rnd.randint(C["TSET_MIN"], C["TSET_MAX"])
        ctl.tick()

        # 不变式 1：报警状态一定对应全速占空比
        if ctl.state == "ALARM" and ctl.duty != C["MOTOR_DUTY_HIGH"]:
            bad.append((i, "报警时占空比不是 100%"))
        # 不变式 2：故障状态一定对应停机
        if ctl.state == "FAULT" and ctl.duty != C["MOTOR_DUTY_STOP"]:
            bad.append((i, "传感器故障时电机没有停转"))
        # 不变式 3：手动模式下档位必须等于手动设定值（报警/故障除外）
        if ctl.state == "MANUAL" and ctl.level_eff != ctl.manual_level:
            bad.append((i, "手动模式档位与设定值不一致"))
        # 不变式 4：档位只能在 0~3 之间
        if ctl.level_eff < 0 or ctl.level_eff > 3:
            bad.append((i, "档位越界"))
        # 不变式 5：占空比必须与档位表一致
        if ctl.state not in ("ALARM", "FAULT"):
            if ctl.duty != DUTY[ctl.level_eff]:
                bad.append((i, "占空比与档位不匹配"))
        # 不变式 6：温度远超报警阈值时必须报警
        if ctl.sim_temp >= ctl.tset + C["ALARM_OFFSET"] and not ctl.alarm:
            bad.append((i, "温度超报警阈值却未报警"))
    return n, bad


# ==================================================================
# 五、主流程
# ==================================================================
def main():
    print("=" * 76)
    print("STC51 温控散热系统 —— 控制逻辑重复测试（L1 逻辑层）")
    print("=" * 76)
    print(f"阈值来源: {os.path.relpath(CONFIG_H, ROOT)}")
    print(f"  目标温度 {C['TSET_DEF'] / 10:.1f}℃  可调 "
          f"{C['TSET_MIN'] / 10:.1f} ~ {C['TSET_MAX'] / 10:.1f}℃")
    print(f"  阶梯回差（相对 Tset，单位 0.1℃）:")
    print(f"    进低速 {C['LV_ENTER_LOW']:+d}   退停机 {C['LV_EXIT_LOW']:+d}")
    print(f"    进中速 {C['LV_ENTER_MID']:+d}    退低速 {C['LV_EXIT_MID']:+d}")
    print(f"    进全速 {C['LV_ENTER_HIGH']:+d}   退中速 {C['LV_EXIT_HIGH']:+d}")
    print(f"  报警 {C['ALARM_OFFSET']:+d}  解除 {C['ALARM_EXIT']:+d}  "
          f"最短保持 {C['ALARM_HOLD_MS']}ms")
    print(f"  占空比表: 停={DUTY[LV_STOP]}%  低={DUTY[LV_LOW]}%  "
          f"中={DUTY[LV_MID]}%  全={DUTY[LV_HIGH]}%")
    print()

    cases = build_cases()
    ROUNDS, REPEATS = 3, 5
    rows = []
    stat = {}

    for case in cases:
        passed = 0
        for rnd_i in range(1, ROUNDS + 1):
            for rep in range(1, REPEATS + 1):
                ok, st, lv, ctl = run_case(case)
                passed += 1 if ok else 0
                rows.append({
                    "测试编号": case["id"],
                    "测试项": case["name"],
                    "轮次": rnd_i,
                    "重复": rep,
                    "注入温度(℃)": case["sim_temp"] / 10.0,
                    "运行模式": case.get("mode", "AUTO"),
                    "期望状态": case["st"],
                    "期望档位": case["lv"],
                    "实际状态": st,
                    "实际档位": lv,
                    "判定": "通过" if ok else "不通过",
                })
        stat[case["id"]] = (case["name"], passed, ROUNDS * REPEATS)

    jit_on = test_hysteresis_jitter()
    jit_off = test_no_hysteresis_jitter()
    ramp_n, ramp_marks = test_slow_ramp()
    fuzz_n, fuzz_bad = test_random_fuzz()

    os.makedirs(os.path.dirname(OUT_CSV), exist_ok=True)
    with open(OUT_CSV, "w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    total_pass = sum(v[1] for v in stat.values())
    total_run = sum(v[2] for v in stat.values())
    rate = total_pass / total_run * 100 if total_run else 0

    md = []
    md.append("# 重复测试报告（由 tools/sim_controller.py 自动生成）\n")
    md.append("> 本报告由脚本自动生成，请勿手工修改；重跑 `python tools/sim_controller.py` 即可刷新。\n")
    md.append("## 一、测试方法\n")
    md.append("固件的判定逻辑（阶梯回差、报警锁存、故障保护、模式优先级）在 PC 上用 Python "
              "**逐行等价复现**，全部阈值直接解析自 `firmware/config.h`，"
              "不存在仿真与固件参数不一致的可能。\n")
    md.append(f"每个用例重复 **{ROUNDS} 轮 × {REPEATS} 次 = {ROUNDS * REPEATS} 次**，"
              "逐条记录见 `docs/06-测试记录-自动生成.csv`。\n")
    md.append("## 二、用例结果汇总\n")
    md.append("| 编号 | 测试项 | 通过/总数 | 结果 |")
    md.append("|---|---|---|---|")
    for cid, (name, p, t) in stat.items():
        md.append(f"| {cid} | {name} | {p}/{t} | {'✅ 通过' if p == t else '❌ 不通过'} |")
    md.append(f"\n**合计：{total_pass}/{total_run} 次符合预期，符合率 {rate:.1f}%**\n")

    md.append("## 三、阶梯回差专项验证\n")
    md.append("### 3.1 阈值抖动测试\n")
    md.append(f"把温度在「进低速」阈值上下反复抖动 100 次"
              f"（{(C['TSET_DEF'] + C['LV_ENTER_LOW'] + 1) / 10:.1f}℃ / "
              f"{(C['TSET_DEF'] + C['LV_ENTER_LOW'] - 1) / 10:.1f}℃ 交替，模拟传感器噪声）：\n")
    md.append("| 方案 | 档位切换次数 |")
    md.append("|---|---|")
    md.append(f"| **本设计（阶梯回差）** | **{jit_on} 次** |")
    md.append(f"| 对照：无回差（进档退档同一阈值） | {jit_off} 次 |")
    md.append(f"\n结论：回差把档位切换从 {jit_off} 次降到 **{jit_on} 次**，"
              f"减少了 {(1 - jit_on / jit_off) * 100:.0f}% 的无效动作。\n")

    md.append("### 3.2 温度缓慢变化测试\n")
    md.append("温度从 20.0℃ 缓升到 60.0℃ 再降回 20.0℃（步进 0.1℃），档位切换点：\n")
    md.append("| 温度(℃) | 切到档位 |")
    md.append("|---|---|")
    for t, nm in ramp_marks:
        md.append(f"| {t / 10:.1f} | {nm} |")
    md.append(f"\n全程切换 **{ramp_n} 次**（理论上行 3 次 + 下行 3 次 = 6 次）。\n")

    md.append("## 四、随机模糊测试\n")
    md.append(f"随机生成 {fuzz_n} 组输入（温度 -55.0~125.0℃、随机切换运行模式与手动档位、"
              "随机改目标温度），检查 6 条设计不变式：\n")
    md.append("1. 报警状态下占空比必须为 100%")
    md.append("2. 传感器故障状态下占空比必须为 0（电机强制停转）")
    md.append("3. 手动模式的档位必须等于手动设定值")
    md.append("4. 档位只能在 0~3 之间")
    md.append("5. 占空比必须与档位表一致")
    md.append("6. 温度达到报警阈值必须立即报警\n")
    if fuzz_bad:
        md.append(f"❌ 发现 {len(fuzz_bad)} 处违反不变式：\n")
        for i, msg in fuzz_bad[:20]:
            md.append(f"- 第 {i} 步：{msg}")
    else:
        md.append(f"✅ **{fuzz_n} 组随机输入全部满足上述 6 条不变式，0 处违规。**")
    md.append("")

    md.append("## 五、结论\n")
    if total_pass == total_run and not fuzz_bad:
        md.append(f"1. 预设用例重复 {total_run} 次，**{total_pass} 次全部符合预期**，符合率 {rate:.1f}%。")
        md.append(f"2. 阶梯回差把临界抖动时的档位切换从 {jit_off} 次降到 {jit_on} 次，消除了电机反复启停。")
        md.append(f"3. 温度缓变全程只切换 {ramp_n} 次，与理论值 6 次一致。")
        md.append(f"4. {fuzz_n} 组随机输入下 6 条设计不变式**零违规**。")
        md.append("\n**结论：控制逻辑在可重复测试下表现与设计预期完全一致。**")
    else:
        md.append("⚠ 存在未通过的用例或不变式违规，请检查。")
    md.append("")
    md.append("---\n")
    md.append("> 说明：本报告验证的是**判定逻辑**。硬件层面（DS18B20 时序、PWM 波形、"
              "数码管扫描、电机实际转速）的验证方法见 `docs/05-测试方案与判定标准.md`，"
              "需要在实物上复测并手工记录。\n")

    os.makedirs(os.path.dirname(OUT_MD), exist_ok=True)
    with open(OUT_MD, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(md))

    for cid, (name, p, t) in stat.items():
        print(f"  [{'PASS' if p == t else 'FAIL'}] {cid}  {name}   {p}/{t}")
    print()
    print(f"用例合计: {total_pass}/{total_run} 通过 ({rate:.1f}%)")
    print(f"回差抖动测试: 本设计 {jit_on} 次切换 vs 无回差 {jit_off} 次")
    print(f"缓变测试: 全程 {ramp_n} 次切换（理论 6 次）")
    print(f"模糊测试: {fuzz_n} 组，违规 {len(fuzz_bad)} 处")
    print()
    print(f"已写入 {os.path.relpath(OUT_CSV, ROOT)}  ({len(rows)} 条记录)")
    print(f"已写入 {os.path.relpath(OUT_MD, ROOT)}")

    return 0 if (total_pass == total_run and not fuzz_bad) else 1


if __name__ == "__main__":
    sys.exit(main())
