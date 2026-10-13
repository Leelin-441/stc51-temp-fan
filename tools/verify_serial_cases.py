#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
verify_serial_cases.py —— 校验 L2 串口测试脚本（tools/serial_test.ps1）里的
                          期望值是否真的符合控制逻辑。

为什么需要它？
    serial_test.ps1 要在真实硬件上跑。如果脚本里某条用例的「期望状态 / 期望档位」
    写错了，硬件上就必然失败，而且很难判断到底是"固件有 bug"还是"用例写错了"。
    用 L1 的等价仿真器把每条用例按相同的命令与时序跑一遍，
    就能在**还没有硬件**的时候先把用例集本身验证正确。

用法（两步）：
    1. PowerShell 导出用例：
       powershell -Command "$t=Get-Content -Raw -Encoding UTF8 tools\serial_test.ps1;
                            $m=[regex]::Match($t,'(?s)\$Cases\s*=\s*@\(.*?\n\)');
                            $SettleMs=1000; $AlarmHoldMs=3500; Invoke-Expression $m.Value;
                            $Cases | ConvertTo-Json -Depth 5 | Set-Content build\ps_cases.json -Encoding UTF8"
    2. python tools/verify_serial_cases.py
"""

import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from sim_controller import C, Controller, LV_STOP, LV_LOW, LV_MID, LV_HIGH   # noqa: E402

CASES_JSON = os.path.join(ROOT, "build", "ps_cases.json")
LEVELS = {0: LV_STOP, 1: LV_LOW, 2: LV_MID, 3: LV_HIGH}


def apply_command(ctl, cmd):
    """与 app.c 的 handle_line() 等价的命令解析。"""
    p = cmd.strip()
    up = p.upper()

    if p.startswith("SIM"):
        if "OFF" in up:
            ctl.sim_enable = False
        else:
            m = re.search(r'T=(-?\d+(?:\.\d+)?)', p)
            if m:
                ctl.sim_temp = int(round(float(m.group(1)) * 10))
            ctl.sim_enable = True
        return

    if p.startswith("MODE"):
        for name in ("AUTO", "NIGHT", "MANUAL"):
            if name in up:
                ctl.mode = name if name != "NIGHT" else "AUTO"
        return

    if p.startswith("SET"):
        m = re.search(r'T=(-?\d+(?:\.\d+)?)', p)
        if m:
            v = int(round(float(m.group(1)) * 10))
            ctl.tset = max(C["TSET_MIN"], min(C["TSET_MAX"], v))
        return

    if p.startswith("LEVEL"):
        m = re.search(r'LEVEL\s+(\d)', p)
        if m:
            ctl.manual_level = LEVELS.get(int(m.group(1)), LV_STOP)
        return

    if p.startswith("RESET"):
        ctl.reset()
        ctl.sim_enable = True     # 导出用例时会在 RESET 后立刻发 SIM
        return


class Runner:
    """按真实时钟推进控制器，含"非仿真时 DHT 采样失败"的模拟。"""

    def __init__(self):
        self.ctl = Controller()
        self.sensor_ok = True

    def hold(self, ms):
        c = self.ctl
        for _ in range(max(1, int(ms) // C["TICK_MS"])):
            if not c.sim_enable:
                if self.sensor_ok:
                    c.ds_fail = 0
                    c.real_temp = c.real_temp
                else:
                    # 每 1s 采样一次，连续失败累加
                    if c.now % 1000 < C["TICK_MS"]:
                        c.ds_fail += 1
            c.tick()


def main():
    if not os.path.exists(CASES_JSON):
        print(f"找不到 {CASES_JSON}，请先按文件头注释用 PowerShell 导出用例。")
        return 2

    with open(CASES_JSON, "r", encoding="utf-8-sig") as f:
        cases = json.load(f)

    print("=" * 78)
    print("L2 串口测试脚本期望值校验（用 L1 等价仿真器复算）")
    print("=" * 78)
    print(f"共 {len(cases)} 条用例\n")

    bad = []
    for case in cases:
        cid = case["Id"]
        r = Runner()
        r.ctl.sim_enable = True
        r.ctl.sim_temp = C["TSET_DEF"]
        r.ctl.real_temp = C["TSET_DEF"]
        # R5-01（传感器故障）需要人工拔掉 DS18B20 数据线
        r.sensor_ok = not (cid.startswith("R5"))

        cmds = case.get("Cmds") or []
        delays = case.get("Delays") or []
        if isinstance(cmds, str):
            cmds = [cmds]
        if isinstance(delays, (int, float)):
            delays = [int(delays)]
        delays = [1000 if d is None else int(d) for d in delays]
        while len(delays) < len(cmds):
            delays.append(1000)

        for cmd, delay in zip(cmds, delays):
            apply_command(r.ctl, cmd)
            r.hold(delay)

        extra = int(case.get("ExtraWaitMs") or 0)
        if extra:
            r.hold(extra)

        # 脚本读取的是"读取窗口内的最后一帧"，状态取窗口末尾的值
        read_ms = int(case.get("ReadMs") or 2500)
        r.hold(read_ms)

        got_st = r.ctl.state
        got_lv = r.ctl.level_eff
        want_st = str(case["ST"]).strip().upper()
        want_lv = LEVELS.get(int(case["LEVEL"]), LV_STOP)

        ok = (got_st == want_st) and (got_lv == want_lv)
        print(f"  [{'OK  ' if ok else 'FAIL'}] {cid}  {case['Name']}")
        if not ok:
            print(f"         期望 ST={want_st} LEVEL={want_lv}"
                  f"   仿真复算 ST={got_st} LEVEL={got_lv}")
            print(f"         命令: {' | '.join(cmds)}")
            print(f"         等待: {delays}  ExtraWait={extra}  ReadMs={read_ms}")
            bad.append(cid)

    print()
    if bad:
        print(f"✗ {len(bad)} 条用例的期望值与控制逻辑不符，在真实硬件上必然失败：")
        for cid in bad:
            print(f"   - {cid}")
        return 1

    print(f"✓ {len(cases)} 条用例的期望状态与期望档位全部与控制逻辑一致")
    print("  （说明该脚本可以直接拿到硬件上跑，失败即代表固件问题）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
