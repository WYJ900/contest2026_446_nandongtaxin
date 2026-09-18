# VelaRide AI 编译与烧录指南

黄山派（SF32LB52）openvela 固件的编译与烧录流程。**编译在 WSL 里做，烧录在 Windows 侧做。**

> 想知道项目做到哪一步了，看 [DEV_STATUS.md](DEV_STATUS.md)。

## 快速开始

环境已初始化过的话，日常只需两条命令：

```bash
# 1. 编译（WSL，约 18 秒增量 / 65 秒全量）
wsl -d Ubuntu-22.04 bash /mnt/d/desktop/VelaRide_AI/tools/wsl_build.sh
```

```powershell
# 2. 烧录（Windows PowerShell，在项目根目录执行）
.\tools\flash.ps1 -Port COM5
```

固件产物：`\\wsl$\Ubuntu-22.04\home\wyj\velaride_ai\cmake_out\lckfb_huangshan_pi\nuttx.bin`（当前约 9.3 MB）

验证跑起来了：

```powershell
.\tools\nsh.ps1 -Cmd "uname -a","date","ls /dev"
```

## 固件备份

`backup\factory_rtthread.bin`（8 MB）是板子出厂的 RT-Thread 固件，2026-08-27 烧 openvela 前备份的。想回退：

```powershell
.\sftool.exe -c SF32LB52 -p COM5 -b 1000000 --before default_reset --after soft_reset `
  write_flash "backup/factory_rtthread.bin@0x12010000"
```

区分方法：RT-Thread 的提示符是 `msh />`，openvela 是 `nsh>`。

备份新固件：

```powershell
.\sftool.exe -c SF32LB52 -p COM5 -b 1000000 --before default_reset `
  read_flash "backup/xxx.bin@0x12010000:0x800000"
```

## 环境信息

| 项目 | 值 |
|---|---|
| Windows 侧源码 | `D:\desktop\VelaRide_AI` |
| WSL 侧编译目录 | `/home/wyj/velaride_ai`（ext4） |
| WSL 发行版 | Ubuntu-22.04 |
| 工具链 | ARM GNU Toolchain 13.3.rel1（`/opt/arm-gnu-toolchain`） |
| 板级配置 | `vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh` |
| 芯片 | SF32LB52（Cortex-M33） |
| 串口波特率 | 1000000 |
| Flash 烧录地址 | `0x12010000` |
| 持久化配置区 | `0x129a0000` 起，4 MiB |
| 烧录工具 | `sftool.exe` 0.2.5 |

## 为什么编译要放在 WSL 内部

源码放在 `D:\` 时，WSL 通过 9p 协议访问（`/mnt/d`）。NuttX 编译涉及 2000+ 个小文件，9p 的每次元数据操作都要跨 VM 边界，实测吞吐只有约 3 MB/s，编译慢到像单线程——**瓶颈是 I/O，不是 CPU 并行度**（8 核 `-j8` 本来就生效）。

搬到 WSL 原生 ext4 后：全量编译 65 秒，增量 18 秒。

代价是源码有两份。**建议直接在 WSL 里开发**（VS Code 装 Remote-WSL 扩展，打开 `/home/wyj/velaride_ai`）。若在 Windows 侧改代码，改完要重跑同步脚本。

## 首次初始化

在一台新机器上，或 WSL 环境被清空后：

```bash
# 1. 先在 Windows 侧拉取 Git LFS 预编译库（必须，见下方"坑位"第 5 条）
cd D:/desktop/VelaRide_AI/vendor/sifli/boards/sf32lb52/libs
git lfs pull

# 2. 一键初始化（WSL，约 15-20 分钟，主要耗在跨 9p 同步）
wsl -d Ubuntu-22.04 bash /mnt/d/desktop/VelaRide_AI/tools/wsl_setup.sh

# 3. 编译
wsl -d Ubuntu-22.04 bash /mnt/d/desktop/VelaRide_AI/tools/wsl_build.sh
```

`wsl_setup.sh` 会依次跑完 7 个步骤，全部幂等，可以放心重跑。

## 脚本说明

都在 `tools/` 下，均可独立重复执行。

| 脚本 | 作用 |
|---|---|
| `wsl_setup.sh` | 一键初始化，按顺序调用下面 6 个脚本 |
| `wsl_toolchain.sh` | 下载安装 ARM GNU Toolchain 13.3 到 `/opt` |
| `wsl_sync.sh` | 同步 `nuttx/apps/vendor/build/packages/frameworks` 到 WSL |
| `wsl_sync_external.sh` | 同步 `external/` 下实际用到的 7 个第三方库 |
| `wsl_fixlinks.sh` | 修复被 Windows 拍平的符号链接 |
| `wsl_fix_env.sh` | 同步 `prebuilts/tools`（含 `jidl_gen_cpp`） |
| `wsl_fix_crlf.sh` | 批量修 CRLF 行尾 |
| `wsl_check_lfs.sh` | 检查 LFS 是否拉取并同步预编译库 |
| `wsl_build.sh` | 编译（满核并行，自动补 mbedtls 配置） |
| `flash.ps1` | 从 WSL 取出固件并用 sftool 烧录 |
| `nsh.ps1` | 通过串口给 NSH 发命令并回显 |
| `reset.ps1` | 用 CH340N 的 RTS 软复位，可选抢占启动指定程序 |
| `wsl_sync_board.sh` | 同步板级 `src/`（rcS + romfs 里的快应用）到 WSL |
| `deploy_quickapp.ps1` | 把 `dist/*.rpk` 解包进固件 romfs |
| `wsl_check_net.sh` | 查网络相关 Kconfig（排查 vapp 卡死用） |

### 排障第一工具：dumpstack

进程卡在 `Waiting Semaphore` 时，别猜配置，直接看 C 层调用栈：

```powershell
.\tools\nsh.ps1 -Cmd "ps"              # 先拿 PID
.\tools\nsh.ps1 -Cmd "dumpstack 8"     # 看它阻塞在哪一行
```

vapp 黑屏那次就是靠它定位到 `Curl_socketpair → accept4 → net_sem_timedwait2`，才发现是缺 `CONFIG_NET_TCPBACKLOG`。在此之前猜了好几轮配置全是错的。

### 快应用完整流程

改完 `.ux` 后四步，全程约 3 分钟：

```powershell
cd vela-quickapp; npx aiot build       # .ux -> .js，产出 dist/*.rpk
cd ..; .\tools\deploy_quickapp.ps1     # rpk -> romfs
wsl -d Ubuntu-22.04 bash /mnt/d/desktop/VelaRide_AI/tools/wsl_sync_board.sh
wsl -d Ubuntu-22.04 bash /mnt/d/desktop/VelaRide_AI/tools/wsl_build.sh
.\tools\flash.ps1 -Port COM5
```

改 `rcS` 换启动哪个应用后，同样要跑 `wsl_sync_board.sh`。

**打包工具链必须是 `aiot-toolkit`，不能用 `hap-toolkit`** —— 后者产出的模块格式 vapp 加载不了（报 `eval_module: file is not module`）。两者名字很像，别装错。详见 [DEV_STATUS.md](DEV_STATUS.md)。

### PowerShell 脚本必须存为 UTF-8 with BOM

Windows PowerShell 5.1 对无 BOM 的 UTF-8 文件按 GBK 解析，中文注释会让脚本直接语法报错：

```
Unrecognized token in source text.
The string is missing the terminator: ".
```

新建 `.ps1` 后转换一次：

```powershell
$p = "tools\xxx.ps1"
$c = [System.IO.File]::ReadAllText($p, [System.Text.Encoding]::UTF8)
[System.IO.File]::WriteAllText($p, $c, (New-Object System.Text.UTF8Encoding($true)))
```

`build.sh`、`emulator.sh` 在项目根目录，是 openvela 官方自带的，与上面这套无关。

### wsl_build.sh 用法

```bash
bash tools/wsl_build.sh          # 增量编译
bash tools/wsl_build.sh clean    # 删掉 cmake_out 重新配置并全量编译
```

### flash.ps1 用法

```powershell
.\tools\flash.ps1                    # 自动探测串口（取最后一个）
.\tools\flash.ps1 -Port COM5         # 指定串口
.\tools\flash.ps1 -Port COM5 -Compat # 烧录失败时用兼容模式重试
.\tools\flash.ps1 -Port COM5 -NoPostReset # 仅诊断时跳过烧录后的 RTS 复位
```

烧录成功后脚本会自动追加一次 RTS 完整复位。原因是 `sftool --after soft_reset` 只做 SoC 软复位，CO5300 AMOLED 偶发不会重新亮屏；RTS 复位会重新执行板级 LCD 上电、面板初始化和 `velaride` 自启动。除故障诊断外不要使用 `-NoPostReset`。

日常 `flash.ps1` 只把 `nuttx.bin` 写到 `0x12010000`，不会整片擦除 Flash。当前构建脚本强制要求固件小于 `0x990000` 字节，避免镜像越过 `0x129a0000` 的持久化配置区；越界会直接构建失败。

因此正常重新烧录不需要重新初始化持久化，也不会删除骑行记录。只有显式执行 `velaride persist-format` 才会擦除配置区，该命令仅用于首次初始化或故障维修，日常禁止执行。配置区原始备份见 `backup/config_partition_before_nvs_20260829_213007.bin`。

## 九个坑位（排障参考）

搭建过程中踩到的问题，报错信息大多和真实原因对不上，这里留个索引。

### 1. 编译慢

`/mnt/d` 走 9p 协议，小文件 I/O 是瓶颈。解决：搬到 ext4。

### 2. libcxx 头文件大量报错

```
error: '__builtin_bit_cast' was not declared in this scope
error: there are no arguments to '__is_nothrow_assignable' that depend on a template parameter
```

Ubuntu 22.04 的 apt 只提供 `arm-none-eabi-gcc` **10.3**，不支持 libcxx 依赖的这些编译器内建。项目自带的 Windows 预编译工具链是 GCC 13.4，所以装版本对齐的 Linux 版 GCC 13.3。

`wsl_build.sh` 会检查 `/opt/arm-gnu-toolchain` 是否存在并把它放到 PATH 最前。

### 3. `Not a directory` / 整块子目录不参与编译

```
cc1: error: /home/wyj/velaride_ai/apps/external/libpng: Not a directory
```

仓库里 `apps/{external,packages,frameworks,vendor,tests}` 都是指向上层目录的**符号链接**，但 Windows 无符号链接权限时 git 会把它们存成内容为目标路径的普通文本文件（如 `../external/`，12 字节）。

rsync 照搬后，CMake 里 `if(EXISTS .../external/CMakeLists.txt)` 判断失败，对应子目录整个不会被 `add_subdirectory`，症状是"某个库莫名不参与编译"。

`wsl_fixlinks.sh` 读出文本内容再重建为真正的 symlink。

### 4. CRLF 行尾（两种表现，第二种极隐蔽）

**表现一**，脚本 shebang 被污染：

```
/usr/bin/env: 'perl\r': No such file or directory
```

内核解析 `#!/usr/bin/env perl\r` 时把 `\r` 当成解释器名的一部分。

**表现二**，`.jidl` 接口定义文件解析失败：

`jidl_gen_cpp` 在 `\r` 上词法分析失败，**但退出码仍是 0**，于是 ninja 认为生成成功，留下 0 字节头文件。最终在编译期报几十个：

```
error: 'system_cipher_RSAParam' has not been declared
error: 'FeatureGetProtoHandle' was not declared in this scope
```

看起来像框架代码有问题，实际根因是行尾符。判断方法：检查 `cmake_out/.../jidl_generated/jidl/*.h` 是否为 0 字节。

`wsl_fix_crlf.sh` 覆盖 `.jidl/.pl/.py/.sh/.cmake`，共修了 688 个文件。

**注意**：任何一次从 Windows 侧同步之后都要重跑 CRLF 修复，否则会被重新污染。`wsl_sync_external.sh` 已内置这步。

### 5. 链接期 `file format not recognized`

```
ld: .../libs/nsh_cmake/libgui_wrapper.a: file format not recognized; treating as linker script
ld: .../libgui_wrapper.a:1: syntax error
```

`libgui_wrapper.a` 是 Git LFS 文件，未拉取时只是 134 字节的指针文本（实际应为 125 MB）。报错完全看不出跟 LFS 有关。

解决：在 Windows 侧 `cd vendor/sifli/boards/sf32lb52/libs && git lfs pull`。

同目录其他 `.a` 当时已正常拉取，只有这个漏了，所以容易忽略。`wsl_check_lfs.sh` 专门检查这个。

### 6. curl 缺 mbedtls 头文件

```
fatal error: mbedtls/des.h: No such file or directory
```

板级 defconfig 有依赖缺口：`CONFIG_UTILS_CURL=y`，而 curl 的 Kconfig 声明 `depends on CRYPTO_MBEDTLS`，但后者 `default n` 且没有任何地方 `select` 它。结果 curl 参与编译却找不到 mbedtls 头文件。

`wsl_build.sh` 会在 cmake 配置前往 defconfig 追加 `CONFIG_CRYPTO_MBEDTLS=y`，让 kconfig 自行解析依赖。

> 这一步会修改 `vendor/sifli/.../configs/nsh/defconfig`（vendor 目录下的文件）。追加前有 grep 判断，不会重复写入。

### 7. rcS 启动脚本不能写 `#` 注释

`vendor/.../src/etc/init.d/rcS` 在构建时会先过 **C 预处理器**（`arm-none-eabi-gcc -E -P -x c`），`#` 开头的行被当成预处理指令：

```
error: invalid preprocessing directive #\U0000539f\U0000672c...
```

改这个文件时只写命令，注释放到文档里。

### 8. 改了 Windows 侧却编出旧固件（最容易白折腾）

编译在 **WSL 侧**（`/home/wyj/velaride_ai`）进行，Windows 侧 `D:\desktop\VelaRide_AI` 只是源，**两者不自动同步**。改了 rcS、板级 `src/`、romfs 里的快应用之后不同步就编译，产物还是旧的。

实际踩过：rcS 已改成启动 `com.velaride.ai`，但只改了 Windows 侧，设备上跑的一直是内置 demo，于是误判成「我们的快应用也黑屏」——它根本没被启动过。

改动后按对应脚本同步：

| 改了什么 | 跑哪个 |
|---|---|
| `vendor/.../src/`（rcS、romfs 里的快应用） | `tools/wsl_sync_board.sh` |
| `apps/`、`nuttx/` 等源码 | `tools/wsl_sync.sh` |
| `external/` 下的库 | `tools/wsl_sync_external.sh` |

验证固件里到底是什么（改 rcS 后务必查一次）：

```bash
cat /home/wyj/velaride_ai/cmake_out/lckfb_huangshan_pi/boards/exclude_board/src/romfs_etc/init.d/rcS
```

设备上再对一次：`.\tools\nsh.ps1 -Cmd "cat /etc/init.d/rcS","uname -a"`，`uname -a` 的编译时间要和刚才的产物对得上。

**另外注意**：在 `wsl.exe -- bash -c "..."` 里写带 `$` 变量的多行脚本，变量会被多层引号吃掉（`for d in ...; do ... $d ...` 里的 `$d` 会变成空），导致 rsync 命令看起来跑了实际没同步。**一律写成脚本文件再调用**，不要内联。

### 9. LVGL 程序栈要给足

LVGL 要求可用栈 ≥ 32768 字节，但实际可用值比 Kconfig 里配的小（任务结构本身占几百字节）。配 32768 时实测只有 32664，仍报：

```
check_stack_size: Stack size is too small. Please increase it to 32768 bytes or more.
```

留余量，配 40960。

## 关于同步范围

`external/` 下有上百个第三方库，但本次板级配置只用到 7 个。分析 `compile_commands.json` 可知实际只引用 `nuttx`、`apps`、`vendor` 三个顶层目录。

全量同步会在无关的海量小文件上耗掉十几分钟，所以脚本只同步：

- 核心目录：`nuttx` `apps` `vendor` `build` `packages` `frameworks`
- external 库：`libpng` `freetype` `rapidjson` `yoga` `curl` `harfbuzz` `protobuf-c` `zblue`
- 工具：`prebuilts/tools`

如果以后启用新的 Kconfig 选项引入了别的第三方库，需要往 `wsl_sync_external.sh` 的 `LIBS` 变量里加。判断方法：编译报某个头文件找不到，去 `D:\desktop\VelaRide_AI\external\` 下找对应目录名。

`lvgl`、`libuv`、`quickjs`、`zlib`、`cJSON`、`mbedtls` 这些**不在** `external/` 下，它们在 `apps/` 里（如 `apps/graphics/lvgl`、`apps/crypto/mbedtls`），已随核心目录同步。

## 串口调试

波特率 1000000。**COM5 是独占的**，`tools\nsh.ps1` 和串口工具（EK-OmniProbe 等）不能同时连，用之前要先在工具里断开。

```powershell
.\tools\nsh.ps1 -Cmd "date"                    # 单条命令
.\tools\nsh.ps1 -Cmd "ps","free" -Wait 3000    # 多条命令，加长等待
```

NSH 是交互式 shell，**不主动输出**，静听串口会是 0 字节，发个回车才回 `nsh>`。这不是故障。

### 上电时间诊断

黄山派仅 USB 供电时 RTC 可能在完全断电后回到 2000 年。VelaRide 会在创建
表盘前从配置区恢复最近一次校准时间。诊断命令：

```powershell
.\tools\nsh.ps1 -Cmd "date","timedatectl","velaride time-read"
```

手动校准系统和 RTC 后，用 `velaride time-save` 同步更新 Flash 兜底：

```powershell
$now = (Get-Date).ToString("MMM dd HH:mm:ss yyyy", [System.Globalization.CultureInfo]::InvariantCulture)
.\tools\nsh.ps1 -Cmd "date -s `"$now`"","velaride time-save"
```

完整设计、验证证据和断电限制见 [RTC_BOOT_TIME_20260830.md](RTC_BOOT_TIME_20260830.md)。

### 复位板子

**这块黄山派没有独立的 RESET 按键。** 复位复用在核心板正面右侧边缘、两个侧按键中**靠下**那个，标注 **Power Key / KEY1**（开关机键），**长按约 10 秒**即可复位。上面那个是 **Function Key**，不是复位键。

> 来源：立创·黄山派开发板使用指南

三种复位方式，可靠性从高到低：

| 方式 | 操作 | 说明 |
|---|---|---|
| **拔插 USB** | 断电重新插 | **最彻底**，验证 vapp / LVGL 问题时用这个 |
| 长按 Power Key | 靠下那个侧键按约 10 秒 | 手边没法拔线时用 |
| RTS 软复位 | `.\tools\reset.ps1` | 最方便，但**清不干净显示驱动状态**，调 LVGL/vapp 时别只靠它 |

也可以通过 CH340N 的 RTS 信号软复位，比长按方便，而且能绕开僵死进程：

```powershell
.\tools\reset.ps1              # 复位并打印启动日志
.\tools\reset.ps1 -Then "velaclock &"   # 复位后抢在 rcS 之前启动指定程序
```

`tools\nsh.ps1` 里刻意关掉了 DTR/RTS（`$sp.DtrEnable = $false`），否则每次连串口都会触发复位。要复位就用 `reset.ps1`，它会主动拉一下 RTS。

**不要用 `reset.ps1 -Then "vapp ... &"` 抢占启动 vapp。** rcS 的 `sleep 3` 窗口挡不住：两个 vapp 实例都会在 3.5 秒左右起来，撞在一起直接 hardfault 崩溃：

```
Assertion failed panic: at arm_hardfault.c:186 task: vapp
  uv_timer_start → lv_nuttx_uv_timer_resume → lv_timer_create
  → lv_nuttx_uv_init_partial → gui_loop_start → vapp_main
```

要换启动哪个应用，改 rcS 重新编译烧录，再冷启动。

### 校准 RTC

```powershell
$now = (Get-Date).ToString("MMM dd HH:mm:ss yyyy", [System.Globalization.CultureInfo]::InvariantCulture)
.\tools\nsh.ps1 -Cmd "date -s `"$now`""
```

必须用 `InvariantCulture`，中文系统直接 `Get-Date -Format` 会输出「8月 27」，NSH 报 `argument invalid`。

### 已验证的硬件（2026-08-27）

| 能力 | 设备节点 | 状态 |
|---|---|---|
| 屏幕 | `fb0` `lcd0` | 390×450，RGB565，16bpp，fb 位于 `0x60000010`（351000 字节，stride 780） |
| 触摸 | `input0` | 已注册（FT6146） |
| 六轴 | `lsm6dsl0` | 已注册 |
| RTC | `rtc0` | 可读写 |
| 其他 | `adc0` `buttons` `pwm0` `i2c0/1` `gpio0-2` `watchdog0` | 已注册 |

内存 8.6 MB，空闲约 7.7 MB。

### 内置应用

```
vapp  vapp_c        # 快应用运行时
lvgldemo            # LVGL demo
fb                  # framebuffer 测试，会画纯色矩形（屏幕变紫是正常的）
lsm6dsl_reader      # 六轴读数
adc buttons pwm i2c gpio timer wdog curl
```

**注意**：系统启动后 `vapp` 会自动跑一个预置 demo 快应用（`hap://app/com.application.lyra.demo`）并占用 LVGL，此时手动跑 `lvgldemo` 会报：

```
[LVGL] [Error] lvgldemo_main: LVGL already initialized! aborting.
```

这是预期行为，不是故障。用 `ps` 能看到那个 vapp 进程。

## 性能参考

| | 时间 |
|---|---|
| 首次初始化（含同步 + 下载工具链） | 15-20 分钟 |
| 全量编译（`clean`） | 约 65 秒（CPU 时间 7 分钟，8 核并行） |
| 增量编译（改单个 .c） | 约 18 秒 |
