<#
===============================================================================
 serial_test.ps1 —— STC89C52RC 温控风扇 · L2 协议层 串口自动化测试
-------------------------------------------------------------------------------
 依赖：只用 Windows 自带的 .NET System.IO.Ports.SerialPort，**不需要安装任何东西**
       （Python / pyserial / 第三方模块统统不用；USB 转 TTL 的 CH340 或 CP2102
         驱动除外，那是硬件必需的）
 运行环境：Windows PowerShell 5.1（脚本文件必须存成 UTF-8 with BOM，否则
       5.1 会按 GBK 读，中文全部乱码并报语法错误）

 用法：
   powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\serial_test.ps1 -Port COM3
   powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\serial_test.ps1 -Port COM5 -Baud 9600 -Rounds 5

 参数：
   -Port    串口名，必填，例如 COM3（设备管理器 → 端口(COM 和 LPT) 里查）
   -Baud    波特率，默认 9600（本工程固定 9600 8N1）
   -Rounds  轮数，默认 3（每一轮把全部用例重跑一遍）

 退出码：
   0 = 全部用例通过
   1 = 参数错误 / 串口打不开 / 握手收不到帧 / 存在失败用例

 输出：
   控制台彩色表格（通过=绿，失败=红）+ 末尾汇总 + 逐条记录 CSV
   CSV 路径：..\docs\10-实测记录-自动生成.csv（UTF-8 带 BOM，Excel 双击直接打开）

-------------------------------------------------------------------------------
 接线（USB 转 TTL 模块 <-> 普中/清翔开发板）：
   +----------------+---------------------+------------------------------------+
   | USB 转 TTL     | 开发板              | 说明                               |
   +----------------+---------------------+------------------------------------+
   | TXD            | P3.0（RXD）         | 交叉：模块发 → 单片机收            |
   | RXD            | P3.1（TXD）         | 交叉：单片机发 → 模块收            |
   | GND            | GND                 | 必须共地，不共地一定收不到数据      |
   | VCC/3V3/5V     | ★ 不要接！          | 见下方警告                         |
   +----------------+---------------------+------------------------------------+
   ★ 不要接 VCC：开发板自己由 USB/DC 供电，两个电源打架会烧模块或烧板子。
   ★ TXD/RXD 必须交叉；接成 TXD-TXD 会一帧都收不到（不是"时好时坏"，是完全收不到）。
   ★ 接好后先开串口助手确认能看到 $S, 开头的行，再跑本脚本。

-------------------------------------------------------------------------------
 串口帧格式（与 firmware/config.h 第 5 节逐字一致，10 个数据字段，顺序不可改）：
   $S,<ms>,<T>,<SET>,<LEVEL>,<DUTY>,<ST>,<MODE>,<BUZ>,<SIM>,<SENSOR>
   示例：$S,12345,355,350,2,66,MID,AUTO,0,0,OK

   ★★ 最大的坑：T 与 SET 是 ×10 的定点数。
      帧里的 355 表示 35.5℃，不是 355℃；而下发命令用的是摄氏度：
      SIM T=42.5（摄氏度）→ 板子上报 T=425（定点数）。
      本脚本所有期望值一律写"帧里的定点数"，所以 35.0℃ 要写成 350。
===============================================================================
#>

[CmdletBinding()]
param(
    [string]$Port   = '',
    [int]$Baud      = 9600,
    [int]$Rounds    = 3
)

$ErrorActionPreference = 'Stop'

# 合法的状态名（config.h 第 4 节：STOP/LOW/MID/HIGH/ALARM/FAULT/MANUAL）
$ST_LEGAL = @('STOP', 'LOW', 'MID', 'HIGH', 'ALARM', 'FAULT', 'MANUAL')

# =============================================================================
# 测试用例表
# -----------------------------------------------------------------------------
# 每条用例字段：
#   Id        编号（必须与 L1 逻辑层仿真的编号一致）
#   Name      测试项名称
#   Cmds      命令数组，按顺序下发
#   Delays    与 Cmds 一一对应的等待毫秒数
#   ST        期望状态名
#   LEVEL     期望档位 0~3
#   SET       可选：期望的目标温度字段（定点数），不写就不判定
#   ReadMs    可选：读取窗口毫秒，默认 2500（窗口内取"最后一帧"做判定）
#   ExtraWaitMs 可选：命令发完后、开读之前的额外等待毫秒
#   Manual    可选：需要人工配合的用例
#   Why       判定依据（为什么这么期望）
#
# 温度速记（Tset 默认 350 即 35.0℃，单位 0.1℃）：
#   停机 T ≤ Tset-40 = 310      进低速 T ≥ Tset-20 = 330
#   进中速 T ≥ Tset   = 350     回低速 T ≤ 330
#   进全速 T ≥ Tset+30 = 380    回中速 T ≤ 360
#   报警 T ≥ Tset+80  = 430     解除 T ≤ Tset+40 = 390 且保持 5000ms
# =============================================================================
$Cases = @(

    @{ Id = 'R1-01'
       Name = '停机态：T=25.0℃ 远低于回停机门限'
       Cmds = @('RESET', 'MODE AUTO', 'SIM T=25.0', 'STATUS')
       Delays = @(400, 200, 600, 200)
       ST = 'STOP'
       LEVEL = 0
       Why = 'Tset=350，T=250 ≤ Tset-40=310 → 停机，占空比 0%。' }

    @{ Id = 'R1-02'
       Name = '低速：T=34.0℃ 落在 [Tset-2.0, Tset)'
       Cmds = @('SIM T=34.0', 'STATUS')
       Delays = @(600, 200)
       ST = 'LOW'
       LEVEL = 1
       Why = '340 ≥ Tset-20=330 且 340 < Tset=350 → 低速，占空比 17/50=34%。' }

    @{ Id = 'R1-03'
       Name = '中速：T=36.0℃ 落在 [Tset, Tset+3.0)'
       Cmds = @('SIM T=36.0', 'STATUS')
       Delays = @(600, 200)
       ST = 'MID'
       LEVEL = 2
       Why = '360 ≥ Tset=350 且 360 < Tset+30=380 → 中速，占空比 33/50=66%。' }

    @{ Id = 'R1-04'
       Name = '全速：T=39.0℃ 落在 [Tset+3.0, Tset+8.0)'
       Cmds = @('SIM T=39.0', 'STATUS')
       Delays = @(600, 200)
       ST = 'HIGH'
       LEVEL = 3
       Why = '390 ≥ Tset+30=380 且 390 < 报警门限 430 → 全速，占空比 50/50=100%。' }

    @{ Id = 'R1-05'
       Name = '报警：T=44.0℃ ≥ Tset+8.0℃'
       Cmds = @('SIM T=44.0', 'STATUS')
       Delays = @(600, 200)
       ST = 'ALARM'
       LEVEL = 3
       Why = '440 ≥ Tset+80=430 → 报警。★ 期望档位取 3：报警只是状态名，调速阶梯仍在顶档（风扇全速散热）。若实际固件把报警定义为立即停机，本条 LEVEL 期望须改成 0 —— 必须与 firmware 对齐后修正，不要为了让用例通过而改期望。' }

    @{ Id = 'R2-01'
       Name = '手动模式 + LEVEL 3'
       Cmds = @('RESET', 'MODE MANUAL', 'SIM T=30.0', 'LEVEL 3', 'STATUS')
       Delays = @(400, 400, 400, 600, 200)
       ST = 'MANUAL'
       LEVEL = 3
       Why = '手动模式下档位只由 LEVEL 命令决定，状态名上报 MANUAL。先用 RESET 清掉上一条用例的报警，再进手动，避免报警状态盖住 MANUAL。' }

    @{ Id = 'R2-02'
       Name = '手动模式 + LEVEL 0：温度很高也不转'
       Cmds = @('SIM T=39.0', 'MODE MANUAL', 'LEVEL 0', 'STATUS')
       Delays = @(600, 400, 600, 200)
       ST = 'MANUAL'
       LEVEL = 0
       Why = '390 在自动模式下本应升到全速，但手动模式必须听 LEVEL 命令 → 仍为 0 档，电机不转。390 < 430 不会触发报警，排除报警干扰。' }

    @{ Id = 'R3-01'
       Name = '阶梯回差上沿：T=32.5℃ 未达进低速门限，应保持停机'
       Cmds = @('RESET', 'MODE AUTO', 'SIM T=25.0', 'SIM T=32.5', 'STATUS')
       Delays = @(400, 400, 500, 600, 200)
       ST = 'STOP'
       LEVEL = 0
       Why = '先降到 250 确保确定处于 STOP，再升到 325；325 < Tset-20=330 未达"进低速"门限，必须保持 STOP。这里刻意不用边界值 330：边界值只要有一点漂移就会误判，用 325 更安全。' }

    @{ Id = 'R3-02'
       Name = '阶梯回差保持：中速 360 降到 345 应仍为 MID'
       Cmds = @('RESET', 'MODE AUTO', 'SIM T=25.0', 'SIM T=34.0', 'SIM T=36.0', 'SIM T=34.5', 'STATUS')
       Delays = @(400, 400, 500, 500, 600, 600, 200)
       ST = 'MID'
       LEVEL = 2
       Why = '先降到 250 再依次经 340（低速）升到 360（中速），确认档位是逐级爬上来的；随后降到 345，345 > Tset-20=330 未触及"回低速"门限，必须保持 MID —— 这就是 2℃ 死区的作用。' }

    @{ Id = 'R3-03'
       Name = '阶梯回差退出：中速 360 降到 325 应回 LOW'
       Cmds = @('RESET', 'MODE AUTO', 'SIM T=25.0', 'SIM T=34.0', 'SIM T=36.0', 'SIM T=32.5', 'STATUS')
       Delays = @(400, 400, 500, 500, 600, 600, 200)
       ST = 'LOW'
       LEVEL = 1
       Why = '降到 325 ≤ Tset-20=330 → 退出中速回低速；同时 325 > Tset-40=310 未达回停机门限，所以结果必须是 LOW 而不是 STOP（用来区分"降一档"和"直接停机"两种实现）。' }

    @{ Id = 'R4-01'
       Name = '在线改目标温度：SET T=25.0 后 T=250 应进 MID'
       Cmds = @('RESET', 'MODE AUTO', 'SIM T=25.0', 'SET T=25.0', 'STATUS')
       Delays = @(400, 300, 500, 500, 200)
       ST = 'MID'
       LEVEL = 2
       SET = 250
       Why = '把 Tset 从默认 350 在线改成 250 后，T=250 恰好等于 Tset，按"T ≥ Tset 进中速"应进 MID，占空比 66%；同时校验帧里 SET 字段真的变成了 250（不是命令收下了但没生效）。' }

    @{ Id = 'R4-02'
       Name = '报警最短保持 5000ms：降回安全区后 5 秒内仍应 ALARM'
       Cmds = @('RESET', 'MODE AUTO', 'SIM T=44.0', 'SIM T=35.0', 'STATUS')
       Delays = @(400, 200, 1200, 200, 100)
       ReadMs = 1200
       ST = 'ALARM'
       LEVEL = 3
       Why = '时序计算：t=0 开始；RESET 后 400ms；MODE AUTO 后 200ms；t≈0.6s 注入 44.0℃ → 440 ≥ 430 立即报警（状态变化立刻补发一帧，另外每 1s 还有一帧）。等 1200ms 到 t≈1.8s 注入 35.0℃：350 ≤ Tset+40=390，解除条件从 t≈1.8s 起成立。报警保持 5000ms，最早也要到 t≈1.8+5.0=6.8s 才可能解除（按最严的"解除条件须持续满 5s"口径；即使按"报警至少维持 5s"的宽松口径也是 t≈0.6+5.0=5.6s）。再叠加 200+100ms 与 STATUS 立即回帧，读取窗口是 t≈2.1s → 3.3s，取到的最后一帧最早也是 t≈3.3s，距离 6.8s 还有约 3.5s 余量，所以窗口内必然是 ALARM。★ ReadMs 必须压到 1200ms：若把读取窗口拉到 6.8s 之后（例如 ReadMs=6000 或 ExtraWaitMs 给大了），取到的最后一帧就是已经解除报警的帧，会把合格固件误判成失败。' }

    @{ Id = 'R5-01'
       Name = '传感器故障：SIM OFF + 人工拔掉 DS18B20 数据线 → FAULT'
       Cmds = @('MODE AUTO', 'SIM OFF')
       Delays = @(300, 800)
       ExtraWaitMs = 5000
       ReadMs = 2500
       ST = 'FAULT'
       LEVEL = 0
       Manual = $true
       ManualHint = '脚本发出 SIM OFF 之后，请在 5 秒内拔掉 DS18B20 的 DQ 数据线（P3.7），测完再插回去。'
       Why = 'DS_SAMPLE_MS=1000ms 采样一次，DS_FAIL_LIMIT=3 表示连续 3 次读失败才判故障 → 最快 3s 出 FAULT，留 5s 余量。判故障后必须停机保护，LEVEL 必须为 0。注意 SIM OFF 会立刻丢掉注入温度，此时温度值本身不可信，所以本条只判 ST 与 LEVEL。' }

    @{ Id = 'R6-01'
       Name = '恢复默认：RESET 后 SET 应回到 350'
       Cmds = @('MODE AUTO', 'SIM T=25.0', 'SET T=60.0', 'RESET', 'SIM T=25.0', 'STATUS')
       Delays = @(300, 400, 400, 500, 500, 200)
       ST = 'STOP'
       LEVEL = 0
       SET = 350
       Why = '先把 Tset 从 350 改成 600（60.0℃），再 RESET，验证 RESET 确实把目标温度恢复成默认 350，而不是"看起来没变"。若 RESET 只清报警不清传感器故障标志，需要重新上电或用 SIM T=25.0 覆盖故障态，否则本条会误判。' }
)

# =============================================================================
# 工具函数
# =============================================================================

function ConvertTo-Frame {
    param([string]$Line)

    if (-not $Line) { return $null }
    $s = $Line.Trim()
    if ($s.Length -lt 11) { return $null }
    if (-not $s.StartsWith('$S,')) { return $null }

    # 字段顺序（config.h 第 5 节）：
    #   $S, ms, T, SET, LEVEL, DUTY, ST, MODE, BUZ, SIM, SENSOR
    $p = $s -split ','
    if ($p.Count -lt 11) { return $null }

    return [pscustomobject]@{
        Raw    = $s
        Ms     = $p[1]
        T      = $p[2]
        SET    = $p[3]
        LEVEL  = $p[4]
        DUTY   = $p[5]
        ST     = $p[6]
        MODE   = $p[7]
        BUZ    = $p[8]
        SIM    = $p[9]
        SENSOR = $p[10]
    }
}

function To-IntOrNull {
    param([string]$Text)
    $v = 0
    if ([int]::TryParse($Text, [ref]$v)) { return $v }
    return $null
}

# 在窗口内持续读串口，返回读到的"最后一帧"；一帧都没读到返回 $null（不抛异常）
function Get-LatestFrame {
    param([System.IO.Ports.SerialPort]$Serial, [int]$WindowMs)

    $deadline = (Get-Date).AddMilliseconds($WindowMs)
    $buf  = ''
    $last = $null

    while ((Get-Date) -lt $deadline) {
        $chunk = ''
        try { $chunk = $Serial.ReadExisting() } catch { $chunk = '' }
        if ($chunk) { $buf = $buf + $chunk }

        while ($true) {
            $i = $buf.IndexOf("`n")
            if ($i -lt 0) { break }
            $line = $buf.Substring(0, $i)
            $buf  = $buf.Substring($i + 1)
            $f = ConvertTo-Frame $line
            if ($f) { $last = $f }
        }

        if (-not $chunk) { Start-Sleep -Milliseconds 40 }
    }

    # 窗口结束时缓冲区里可能残留一行（板卡偶发丢结尾换行），也解析一下
    $f = ConvertTo-Frame $buf
    if ($f) { $last = $f }

    return $last
}

function Send-Command {
    param([System.IO.Ports.SerialPort]$Serial, [string]$Cmd)
    $Serial.Write($Cmd + "`r`n")
    Start-Sleep -Milliseconds 60
}

function ConvertTo-CsvLine {
    param($Values)
    $out = @()
    foreach ($v in $Values) {
        $s = ''
        if ($null -ne $v) { $s = [string]$v }
        if ($s.Contains('"') -or $s.Contains(',') -or $s.Contains("`n") -or $s.Contains("`r")) {
            $s = '"' + $s.Replace('"', '""') + '"'
        }
        $out += $s
    }
    return ($out -join ',')
}

# 写出 UTF-8 with BOM 的 CSV（PS 5.1 的 Export-Csv -Encoding UTF8 也带 BOM，
# 这里手工写是为了在 PS 5.1 / PS 7 下都能保证"带 BOM"，Excel 打开不乱码）
function Write-CsvFile {
    param([string]$Path, $Rows)

    $sb = New-Object System.Text.StringBuilder
    $props = $Rows[0].PSObject.Properties.Name
    $null = $sb.AppendLine((ConvertTo-CsvLine -Values $props))
    foreach ($row in $Rows) {
        $vals = @()
        foreach ($p in $props) { $vals += $row.$p }
        $null = $sb.AppendLine((ConvertTo-CsvLine -Values $vals))
    }
    $enc = New-Object System.Text.UTF8Encoding($true)   # $true = 带 BOM
    [System.IO.File]::WriteAllText($Path, $sb.ToString(), $enc)
}

# =============================================================================
# 主流程
# =============================================================================

$ScriptDir = $PSScriptRoot
if (-not $ScriptDir) { $ScriptDir = (Get-Location).Path }
$CsvPath = Join-Path $ScriptDir '..\docs\10-实测记录-自动生成.csv'

if (-not $Port) {
    Write-Host '参数错误：必须用 -Port 指定串口。' -ForegroundColor Red
    Write-Host '用法示例：' -ForegroundColor Yellow
    Write-Host '  powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\serial_test.ps1 -Port COM3' -ForegroundColor Yellow
    Write-Host '  powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\serial_test.ps1 -Port COM5 -Baud 9600 -Rounds 5' -ForegroundColor Yellow
    exit 1
}
if ($Rounds -lt 1) {
    Write-Host ("参数错误：-Rounds 必须 ≥ 1（当前 {0}）。" -f $Rounds) -ForegroundColor Red
    exit 1
}
if ($Baud -ne 9600) {
    Write-Host ("警告：本工程固定 9600 8N1，你指定了 {0}。11.0592MHz 晶振下只有 9600 的误差是 0。" -f $Baud) -ForegroundColor Yellow
}

# --- 打开串口 ---------------------------------------------------------------
$sp = $null
try {
    $sp = New-Object System.IO.Ports.SerialPort $Port
    $sp.BaudRate  = $Baud
    $sp.Parity    = [System.IO.Ports.Parity]::None
    $sp.DataBits  = 8
    $sp.StopBits  = [System.IO.Ports.StopBits]::One
    $sp.Handshake = [System.IO.Ports.Handshake]::None
    $sp.ReadTimeout  = 500
    $sp.WriteTimeout = 1000
    $sp.Encoding  = [System.Text.Encoding]::ASCII
    $sp.DtrEnable = $false
    $sp.RtsEnable = $false
    $sp.Open()
} catch {
    Write-Host ("打不开串口 {0} ：{1}" -f $Port, $_.Exception.Message) -ForegroundColor Red
    Write-Host '请依次检查：' -ForegroundColor Yellow
    Write-Host '  1) 串口是不是被别的程序占用了（串口助手、Keil 的串口窗口、另一个 PowerShell 窗口）' -ForegroundColor Yellow
    Write-Host '  2) 设备管理器 → 端口(COM 和 LPT) 里实际的 COM 号是不是你写的这个' -ForegroundColor Yellow
    Write-Host '  3) USB 转 TTL 的驱动装好了没有（CH340 / CP2102）' -ForegroundColor Yellow
    Write-Host '  4) 开发板是否已经上电' -ForegroundColor Yellow
    exit 1
}

$records = New-Object System.Collections.ArrayList

try {
    # --- 1) 丢弃开机横幅 ---------------------------------------------------
    Write-Host ("已打开 {0} @ {1} 8N1" -f $Port, $Baud) -ForegroundColor Cyan
    Write-Host '读取 2 秒并丢弃开机横幅 ......' -ForegroundColor Cyan
    Start-Sleep -Milliseconds 2000
    $null = $sp.ReadExisting()

    # --- 2) 握手：必须能收到 $S, 帧 ----------------------------------------
    Write-Host '握手：确认能收到 $S, 数据帧 ......' -ForegroundColor Cyan
    $hello = Get-LatestFrame -Serial $sp -WindowMs 3000
    if (-not $hello) {
        Write-Host '收不到任何 $S, 帧，测试无法开始。' -ForegroundColor Red
        Write-Host '排查顺序：' -ForegroundColor Yellow
        Write-Host '  1) TXD/RXD 是否交叉接（模块 TXD → P3.0，模块 RXD → P3.1）' -ForegroundColor Yellow
        Write-Host '  2) 是否共地（GND 对 GND）' -ForegroundColor Yellow
        Write-Host '  3) 波特率 9600，晶振 11.0592MHz，板子是否已烧录本工程固件' -ForegroundColor Yellow
        Write-Host '  4) 先用串口助手看一眼有没有 $S, 开头的行' -ForegroundColor Yellow
        exit 1
    }
    Write-Host ("握手成功，收到帧：{0}" -f $hello.Raw) -ForegroundColor Green
    Write-Host '字段顺序：$S,<ms>,<T>,<SET>,<LEVEL>,<DUTY>,<ST>,<MODE>,<BUZ>,<SIM>,<SENSOR>' -ForegroundColor DarkGray
    Write-Host '注意：T / SET 是 ×10 定点数，355 表示 35.5℃。' -ForegroundColor DarkGray

    # --- 3) 逐轮执行用例 ---------------------------------------------------
    for ($r = 1; $r -le $Rounds; $r++) {
        Write-Host ''
        Write-Host ("========================= 第 {0}/{1} 轮 =========================" -f $r, $Rounds) -ForegroundColor White

        foreach ($c in $Cases) {
            if ($c.Cmds.Count -ne $c.Delays.Count) {
                Write-Host ("用例 {0} 配置错误：Cmds {1} 条，Delays {2} 条，跳过。" -f $c.Id, $c.Cmds.Count, $c.Delays.Count) -ForegroundColor Red
                continue
            }

            $readMs = 2500
            if ($c.ReadMs) { $readMs = [int]$c.ReadMs }
            $extraMs = 0
            if ($c.ExtraWaitMs) { $extraMs = [int]$c.ExtraWaitMs }

            Write-Host ''
            if ($c.Manual) {
                Write-Host ("  ★ [{0}] {1}" -f $c.Id, $c.Name) -ForegroundColor Yellow
                Write-Host ("    ★ 需要人工配合：{0}" -f $c.ManualHint) -ForegroundColor Yellow
            } else {
                Write-Host ("  → [{0}] {1}" -f $c.Id, $c.Name) -ForegroundColor Gray
            }
            Write-Host ("    命令：{0}" -f ($c.Cmds -join ' | ')) -ForegroundColor DarkGray

            # 3.1 逐条发送命令，按 Delays 等待
            for ($i = 0; $i -lt $c.Cmds.Count; $i++) {
                Send-Command -Serial $sp -Cmd $c.Cmds[$i]
                $d = 0
                if ($i -lt $c.Delays.Count) { $d = [int]$c.Delays[$i] }
                if ($d -gt 0) { Start-Sleep -Milliseconds $d }
            }

            # 3.2 可选额外等待
            if ($extraMs -gt 0) { Start-Sleep -Milliseconds $extraMs }

            # 3.3 读取窗口内取最后一帧
            $fr = Get-LatestFrame -Serial $sp -WindowMs $readMs

            # 3.4 判定
            $ok = $true
            $reasons = @()

            if (-not $fr) {
                $ok = $false
                $reasons += ("读取窗口 {0}ms 内没有收到任何 $S, 帧" -f $readMs)
            } else {
                if ($fr.ST -ne $c.ST) {
                    $ok = $false
                    $reasons += ("ST 期望[{0}] 实际[{1}]" -f $c.ST, $fr.ST)
                }
                $lv = To-IntOrNull $fr.LEVEL
                if ($null -eq $lv) {
                    $ok = $false
                    $reasons += ("LEVEL 字段无法解析为数字：{0}" -f $fr.LEVEL)
                } elseif ($lv -ne [int]$c.LEVEL) {
                    $ok = $false
                    $reasons += ("LEVEL 期望[{0}] 实际[{1}]" -f $c.LEVEL, $lv)
                }
                if ($null -ne $c.SET) {
                    $sv = To-IntOrNull $fr.SET
                    if ($null -eq $sv -or $sv -ne [int]$c.SET) {
                        $ok = $false
                        $reasons += ("SET 期望[{0}] 实际[{1}]" -f $c.SET, $fr.SET)
                    }
                }
            }

            # 3.5 记录 + 控制台着色
            $tag    = 'PASS'
            $color  = 'Green'
            if (-not $ok) { $tag = 'FAIL'; $color = 'Red' }

            $rawLine = ''
            if ($fr) { $rawLine = $fr.Raw }

            Write-Host ("  [{0}] {1}  ST={2} LEVEL={3} DUTY={4}" -f $tag, $c.Id, $fr.ST, $fr.LEVEL, $fr.DUTY) -ForegroundColor $color
            if (-not $ok) {
                Write-Host ("        原因：{0}" -f ($reasons -join ' ; ')) -ForegroundColor Red
                if ($rawLine) { Write-Host ("        实收帧：{0}" -f $rawLine) -ForegroundColor DarkRed }
            } else {
                Write-Host ("        实收帧：{0}" -f $rawLine) -ForegroundColor DarkGreen
            }

            $rec = [pscustomobject]@{
                '轮次'      = $r
                '编号'      = $c.Id
                '测试项'    = $c.Name
                '命令序列'  = ($c.Cmds -join ' | ')
                '期望ST'    = $c.ST
                '实际ST'    = $(if ($fr) { $fr.ST } else { '' })
                '期望LEVEL' = $c.LEVEL
                '实际LEVEL' = $(if ($fr) { $fr.LEVEL } else { '' })
                '期望SET'   = $(if ($null -ne $c.SET) { $c.SET } else { '' })
                '实际SET'   = $(if ($fr) { $fr.SET } else { '' })
                '判定'      = $(if ($ok) { '通过' } else { '失败' })
                '失败原因'  = ($reasons -join ' ; ')
                '实收帧'    = $rawLine
                '需要人工'  = $(if ($c.Manual) { '是' } else { '否' })
                '判定依据'  = $c.Why
                '记录时间'  = (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
            }
            $null = $records.Add($rec)
        }
    }

    # --- 4) 导出 CSV -------------------------------------------------------
    $csvDir = Split-Path -Parent $CsvPath
    if (-not (Test-Path $csvDir)) { $null = New-Item -ItemType Directory -Path $csvDir -Force }
    if ($records.Count -gt 0) {
        Write-CsvFile -Path $CsvPath -Rows $records
        Write-Host ''
        Write-Host ("逐条记录已导出：{0}" -f $CsvPath) -ForegroundColor Cyan
        Write-Host '（UTF-8 带 BOM，Excel 双击直接打开不乱码）' -ForegroundColor DarkGray
    }

    # --- 5) 汇总 -----------------------------------------------------------
    $total = $records.Count
    $passed = @($records | Where-Object { $_.'判定' -eq '通过' }).Count
    $failed = $total - $passed
    $rate = 0.0
    if ($total -gt 0) { $rate = [math]::Round(100.0 * $passed / $total, 1) }

    Write-Host ''
    Write-Host '========================= 测试汇总 =========================' -ForegroundColor White
    Write-Host ("  轮数      ：{0}" -f $Rounds)
    Write-Host ("  用例总数  ：{0}" -f $total)
    Write-Host ("  通过      ：{0}" -f $passed) -ForegroundColor Green
    if ($failed -gt 0) {
        Write-Host ("  失败      ：{0}" -f $failed) -ForegroundColor Red
    } else {
        Write-Host ("  失败      ：{0}" -f $failed) -ForegroundColor Green
    }
    if ($failed -eq 0) {
        Write-Host ("  通过率    ：{0}%" -f $rate) -ForegroundColor Green
    } else {
        Write-Host ("  通过率    ：{0}%" -f $rate) -ForegroundColor Red
    }

    if ($failed -gt 0) {
        Write-Host ''
        Write-Host '  失败清单：' -ForegroundColor Red
        foreach ($row in $records) {
            if ($row.'判定' -ne '通过') {
                Write-Host ("    第{0}轮 {1} {2} —— {3}" -f $row.'轮次', $row.'编号', $row.'测试项', $row.'失败原因') -ForegroundColor Red
            }
        }
    }

    Write-Host ''
    if ($failed -eq 0) {
        Write-Host '结论：全部用例通过。' -ForegroundColor Green
    } else {
        Write-Host '结论：存在失败用例，请对照上面的"实收帧"和失败原因逐条排查。' -ForegroundColor Red
    }
    Write-Host '提醒：L2 只验证协议与状态机逻辑；电机实际转速、PWM 波形、发热等必须靠 L3 物理测试。' -ForegroundColor DarkGray

    if ($failed -gt 0) { exit 1 } else { exit 0 }
}
finally {
    if ($sp -and $sp.IsOpen) {
        $sp.Close()
        $sp.Dispose()
        Write-Host ("串口 {0} 已关闭。" -f $Port) -ForegroundColor DarkGray
    }
}
