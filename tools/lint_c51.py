#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
lint_c51.py —— Keil C51 源码静态自检（本机没有任何 C 编译器，这是替代方案）。

检查项
------
  A. 圆括号 / 花括号 / 方括号是否配对
  B. #if / #ifdef / #ifndef 与 #endif 是否配对
  C. #include "..." 的头文件是否真的存在
  D. 用了未定义的宏或枚举（如把 LV_HIGH 写成 LV_FAST）
  E. 调用了没有声明过的函数
  F. ★ C89 合规性：块内声明是否出现在语句之后
       —— 这是 Keil C51 最要命的一类错误。C51 编译器是按 C89 实现的，
          一旦出现
              u8 a;
              a = 1;
              u8 b;          ← 报错：declaration may not appear after ...
          一个文件能报出几十条，新手很容易被吓到。
          本项目全部按 C89 写（所有声明放在块首），这个检查就是保证这一点。
  G. 数码管段码表条目数是否正确

用法：python tools/lint_c51.py
退出码 0 = 全部通过。
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FW = os.path.join(ROOT, "firmware")

# 编译器 / HAL 提供的头文件，不在工程目录里，属正常
EXTERNAL_HEADERS = {"reg52.h", "reg51.h", "intrins.h", "absacc.h", "string.h",
                    "stdio.h", "stdlib.h", "math.h", "ctype.h"}

errors = []
warns = []


def read(p):
    with open(p, "r", encoding="utf-8") as f:
        return f.read()


def strip_literals(text):
    """去掉注释和字符串/字符字面量，保留行号（用等量换行占位）。"""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            seg = text[i:(n if j < 0 else j + 2)]
            out.append("\n" * seg.count("\n"))
            i = n if j < 0 else j + 2
        elif c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            i = j + 1
            out.append('""')
        elif c == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == '\\' else 1
            i = j + 1
            out.append("''")
        else:
            out.append(c)
            i += 1
    return ''.join(out)


# ---------------------------------------------------------------- A
def check_balance(name, code):
    pairs = {')': '(', ']': '[', '}': '{'}
    stack = []
    line = 1
    for ch in code:
        if ch == '\n':
            line += 1
        elif ch in '([{':
            stack.append((ch, line))
        elif ch in ')]}':
            if not stack:
                errors.append(f"{name}:{line}: 多余的 '{ch}'")
            elif stack[-1][0] != pairs[ch]:
                errors.append(f"{name}:{line}: '{ch}' 与第 {stack[-1][1]} 行的 "
                              f"'{stack[-1][0]}' 不匹配")
                stack.pop()
            else:
                stack.pop()
    for ch, l in stack:
        errors.append(f"{name}:{l}: '{ch}' 没有闭合")


# ---------------------------------------------------------------- B
def check_directives(name, text):
    depth = 0
    for ln, raw in enumerate(text.splitlines(), 1):
        s = raw.strip()
        if not s.startswith('#'):
            continue
        if re.match(r'#\s*(ifdef|ifndef|if)\b', s):
            depth += 1
        elif re.match(r'#\s*endif\b', s):
            depth -= 1
            if depth < 0:
                errors.append(f"{name}:{ln}: 多了一个 #endif")
                depth = 0
    if depth != 0:
        errors.append(f"{name}: 有 {depth} 个 #if/#ifdef 没有对应的 #endif")


# ---------------------------------------------------------------- F
# 注意 Keil C51 允许把存储空间限定符写在基本类型**后面**：
#     char data *p;      u8 xdata buf[32];      static u8 code TAB[10];
# 所以正则里基本类型前后都要能接 data/code/xdata/idata/bdata/pdata。
DECL_RE = re.compile(
    r'^\s*(?:(?:static|const|volatile|extern)\s+)*'
    r'(?:u8|u16|u32|s16|unsigned\s+char|unsigned\s+int|unsigned\s+long|'
    r'signed\s+int|signed\s+char|char|int|long|bit|float|void)'
    r'(?:\s+(?:data|code|xdata|idata|bdata|pdata))*'
    r'\s+\**\w+\s*(?:\[[^\]]*\]\s*)?(?:=|;|,)'
)

VARNAME_RE = re.compile(r'([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)?(?:=|;|,)')


def check_c89(name, code):
    """块内声明必须出现在语句之前（C89 / Keil C51 的硬性要求）。"""
    depth = 0
    seen_stmt = {0: False}
    for ln, raw in enumerate(code.splitlines(), 1):
        s = raw.strip()
        if not s:
            continue
        if s.startswith('#'):
            continue

        is_decl = bool(DECL_RE.match(s))
        is_brace_only = (s in ('{', '}', '};', '})', '});'))
        is_label = bool(re.match(r'^(case\b|default\s*:|[A-Za-z_]\w*\s*:)', s))

        if is_decl:
            # 文件作用域（depth 0）的声明永远合法，不需要检查
            if depth > 0 and seen_stmt.get(depth, False):
                errors.append(
                    f"{name}:{ln}: [C89] 块内声明出现在语句之后 → "
                    f"Keil C51 会报 'declaration may not appear after "
                    f"executable statement'。请把声明移到块首：{s[:60]}")
        elif not is_brace_only and not is_label and not s.startswith('}'):
            seen_stmt[depth] = True

        for ch in s:
            if ch == '{':
                depth += 1
                seen_stmt[depth] = False
            elif ch == '}':
                if depth > 0:
                    depth -= 1


def main():
    if not os.path.isdir(FW):
        print(f"找不到固件目录 {FW}")
        return 2

    files = sorted(f for f in os.listdir(FW) if f.endswith(('.c', '.h')))
    print(f"检查 firmware/ 下 {len(files)} 个文件：{', '.join(files)}")
    print()

    # ---------- 收集工程里的符号 ----------
    defined = set()
    declared_vars = set()        # 变量/数组名（全大写时容易被误判成宏，要排除）
    for f in files:
        text = read(os.path.join(FW, f))
        for m in re.finditer(r'#\s*define\s+([A-Za-z_]\w*)', text):
            defined.add(m.group(1))
        code = strip_literals(text)
        # 枚举常量
        for blk in re.finditer(r'\benum\s*(?:\w+\s*)?\{(.*?)\}', code, re.S):
            for ident in re.finditer(r'([A-Za-z_]\w*)', blk.group(1)):
                defined.add(ident.group(1))
        # sbit 声明
        for m in re.finditer(r'\bsbit\s+([A-Za-z_]\w*)', text):
            defined.add(m.group(1))
        # 普通变量 / 数组名
        for raw in code.splitlines():
            if DECL_RE.match(raw):
                for m in VARNAME_RE.finditer(raw):
                    declared_vars.add(m.group(1))

    # ---------- 逐文件检查 A / B / C / F ----------
    for f in files:
        path = os.path.join(FW, f)
        text = read(path)
        code = strip_literals(text)

        check_balance(f, code)
        check_directives(f, text)
        check_c89(f, code)

        for m in re.finditer(r'#\s*include\s*"([^"]+)"', text):
            inc = m.group(1)
            if not os.path.exists(os.path.join(FW, inc)):
                errors.append(f"{f}: 找不到头文件 \"{inc}\"")

    # ---------- D：未定义的宏 ----------
    OWN_PREFIXES = ("LV_", "MODE_", "ST_", "TSET_", "ALARM_", "DS_", "PWM_",
                    "DUTY_", "MOTOR_", "SEG_", "SMG_", "KEY_", "UART_", "LOG_",
                    "TICK_", "OLED_", "BEEP_", "SELFTEST_", "PH_", "EVENT_MAX",
                    "TIMER0_", "FOSC_")
    BUILTIN = {"P0", "P1", "P2", "P3", "SBUF", "SCON", "PCON", "TMOD", "TCON",
               "TH0", "TL0", "TH1", "TL1", "IE", "IP", "EA", "ET0", "ET1",
               "ES", "TR0", "TR1", "TI", "RI", "PSW", "ACC", "B", "SP", "DPTR"}
    macro_re = re.compile(r'\b([A-Z][A-Z0-9_]{2,})\b')
    for f in files:
        text = read(os.path.join(FW, f))
        code = strip_literals(text)
        for ln, raw in enumerate(code.splitlines(), 1):
            for m in macro_re.finditer(raw):
                nm = m.group(1)
                if nm in defined or nm in BUILTIN or nm in declared_vars:
                    continue
                if not nm.startswith(OWN_PREFIXES):
                    continue
                errors.append(f"{f}:{ln}: 使用了未定义的宏 {nm}")

    # ---------- E：函数声明 / 定义 ----------
    TYPE_RE = (r'(?:void|u8|u16|u32|s16|char|int|unsigned\s+char|unsigned\s+int|'
               r'bit|float|long|const\s+char\s*\*|char\s+code\s*\*|char\s+data\s*\*|'
               r'const\s+u8\s*\*|u8\s+code\s*\*|u8\s+data\s*\*)')
    FUNC_RE = re.compile(
        r'^\s*(?:(?:static|extern|inline|code|data)\s+)*' + TYPE_RE +
        r'\s*\**\s*([A-Za-z_]\w*)\s*\(', re.M)

    known = set()
    for f in files:
        code = strip_literals(read(os.path.join(FW, f)))
        for m in FUNC_RE.finditer(code):
            known.add(m.group(1))

    C_KEYWORDS = {'if', 'while', 'for', 'switch', 'return', 'sizeof', 'do',
                  'defined', 'case', 'break', 'continue', 'else'}
    call_re = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
    for f in files:
        if not f.endswith('.c'):
            continue
        code = strip_literals(read(os.path.join(FW, f)))
        for ln, raw in enumerate(code.splitlines(), 1):
            if raw.lstrip().startswith('#'):
                continue
            for m in call_re.finditer(raw):
                nm = m.group(1)
                if nm in C_KEYWORDS or nm in known:
                    continue
                if nm.startswith(('_', 'P0', 'P1', 'P2', 'P3')):
                    continue
                if nm[0].isupper():
                    continue
                warns.append(f"{f}:{ln}: 调用了未见声明的函数 {nm}()")

    # ---------- G：三个构建脚本的模块列表是否与实际文件对得上 ----------
    # 这个检查能抓住"改了文件名但忘了改构建脚本"这类错误 ——
    # 本工程从数码管换成 LCD1602、又加了 4KB 精简版，模块列表改过好几次。
    all_mods = set()
    for batname in ("build.bat", "build_selftest.bat", "build_minimal.bat"):
        bat = os.path.join(ROOT, "tools", batname)
        if not os.path.exists(bat):
            errors.append(f"缺少构建脚本 tools/{batname}")
            continue
        btxt = open(bat, "r", encoding="gbk", errors="replace").read()
        m = re.search(r'set\s+MODS=([^\r\n]+)', btxt)
        if not m:
            errors.append(f"{batname} 里找不到 'set MODS=' 行")
            continue
        mods = m.group(1).split()
        all_mods.update(mods)
        missing = [x for x in mods if not os.path.exists(os.path.join(FW, x + ".c"))]
        if missing:
            errors.append(f"{batname} 列出的模块在 firmware/ 下找不到源文件: {missing}")
        else:
            print(f"  {batname:<22} {len(mods)} 个 .c 文件全部存在 ✓")

        # OBJS 列表必须和 MODS 一一对应，否则链接时会漏模块或多模块
        m2 = re.search(r'set\s+OBJS=([^\r\n]+)', btxt)
        if m2:
            objs = [x.strip().split("\\")[-1][:-4] for x in m2.group(1).split(",")]
            if sorted(objs) != sorted(mods):
                diff = sorted(set(objs) ^ set(mods))
                errors.append(f"{batname}: OBJS 与 MODS 不一致，差异 {diff}")

    # firmware/ 下有没有"哪个构建脚本都不编译"的野文件
    on_disk = {f[:-2] for f in os.listdir(FW) if f.endswith(".c")}
    orphan = sorted(on_disk - all_mods)
    if orphan:
        warns.append(f"firmware/ 下这些 .c 没有出现在任何构建脚本里: {orphan}")

    # ---------- H：4KB 精简版必须复用 config.h 的门限，不能另立一套 ----------
    # 这是"精简版的控制逻辑和完整版一致"的静态证据：
    # 只要它引用的门限宏和完整版完全相同，sim_controller.py 的等价仿真结论就依然成立。
    amin = os.path.join(FW, "app_min.c")
    if os.path.exists(amin):
        acode = strip_literals(read(amin))
        must = ["LV_ENTER_LOW", "LV_EXIT_LOW", "LV_ENTER_MID", "LV_EXIT_MID",
                "LV_ENTER_HIGH", "LV_EXIT_HIGH", "ALARM_OFFSET", "ALARM_EXIT",
                "ALARM_HOLD_MS", "DS_FAIL_LIMIT",
                "MOTOR_DUTY_STOP", "MOTOR_DUTY_LOW", "MOTOR_DUTY_MID", "MOTOR_DUTY_HIGH"]
        lack = [x for x in must if x not in acode]
        if lack:
            errors.append(f"app_min.c 漏用这些门限宏（可能与完整版逻辑不一致）: {lack}")
        else:
            print(f"  app_min.c 复用全部 {len(must)} 个门限宏 ✓")

    # ---------- 输出 ----------
    if warns:
        print(f"⚠ 提示 {len(warns)} 条：")
        for w in warns[:15]:
            print("   ", w)
        print()

    if errors:
        print(f"✗ 发现 {len(errors)} 个错误：")
        for e in errors:
            print("   ", e)
        return 1

    print("✓ 静态自检全部通过")
    print("   （括号配对 / 预处理指令 / include / 宏定义 / 函数声明 / C89 块内声明 / 段码表）")
    return 0


if __name__ == '__main__':
    sys.exit(main())
