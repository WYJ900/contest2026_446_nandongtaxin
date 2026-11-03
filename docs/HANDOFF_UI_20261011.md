# 界面改版交接（2026-10-11）

新对话先读本页，再读 [界面改版计划](UI_REDESIGN_PLAN_20261010.md)（各阶段详细结果、实测数据）和 [当前成果](CURRENT_STATE.md)。

## 1. 现在在哪一步

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| U1 | 纯黑色板、4 个主题色（设置→外观）、Outfit 数字字体、三款新表盘 | 已上板，用户确认 |
| U1b | 修方框、28 px 界面字体、设置页可滚动 | 已上板，用户确认 |
| U2/U2b/U2c | 弹簧落位、可接住、三种转场（跟手/百叶窗/圆形） | 跟手滑动 45 Hz 用户认可；百叶窗/圆形仍卡，**用户要求暂缓优化** |
| U3/U3b | 蜂窝与列表菜单，设置里切换；`velaride nav-stats` 统计 | 已上板；菜单约 20 Hz，优化暂缓 |
| U4/U4b | 骑行流程按样稿重做（三屏滑动、按住结束、防误触锁、提醒页、总结页） | 已上板，用户确认能滑能停 |
| U5/U5b/U5c | 指南针改版、夜骑灯、秒表、水平仪 | 已上板，用户确认（水平仪上下左右均已实物校正） |
| **U6** | 表盘下拉控制中心、上推通知中心、顶部通知横幅 | **已上板 U6c（图标修正、按键回表盘、关闭时中心离开、转场秒针实时），待用户实物确认** |
| U7 | 抬腕亮屏、自动熄屏（设置可选） | 已上板，用户确认 |

**手表当前安装：`artifacts/velaride-ui-u7-20261011.bin`**（SHA-256 见 `artifacts/SHA256SUMS`）。最新骑行记录第 11 条。

## 2. U6 已做的改动（未上板）

源码：`apps/examples/velaride/velaride_shell.c`（已同步到 `my-contest-repo/app/velaride/`）。

- 原来表盘上下滑进入的“骑行 / 安全提醒 / 复盘”三个旧功能页删除，`SCENE_RIDE/ALERT/REVIEW` 换成 `SCENE_CONTROL`、`SCENE_NOTES`。`function_index`：-1 表盘，0 控制中心，1 通知中心。
- 控制中心（表盘下拉）：夜骑灯、勿扰、自动亮度、亮度（20/50/80 循环）、连接、设置；顶部状态行（供电/蓝牙/照度）。点击按坐标路由（`cc_hit/cc_tap`）。
- 通知中心（表盘上推）：最多 4 条显示、8 条保存，新的在上；点“AI 复盘”打开复盘，“清除全部”。
- 横幅：`note_post()` 从顶部弹入 3.5 秒；勿扰时只进通知中心不弹横幅。来源：网关回传 AI 复盘（`review_watch()`，原来是直接跳复盘页）、蓝牙连上/断开、休息提醒（`note_watch()` 每秒检查）。
- 两个中心也做快照（`PREVIEW_COUNT = FACE_COUNT + 2`），纵向跟手滑动时页面随手指平移（`nav_slide_shift()`）；内容变化时 `preview_invalidate()` 只重做该页快照，控制中心每 30 秒刷新一次。
- 发现并修复 **LVGL 9.1 自身的内存泄漏**：`lv_draw_sw_arc.c` 圆角圆弧的遮罩参数未释放。已改两份 LVGL 源码（Windows 工作区和 WSL），并追加到 `patches/lvgl.patch`（`git apply --reverse --check` 通过）。
- 主机测试新增：下拉/返回、勿扰/自动亮度/亮度/夜骑灯跳转、断线横幅与通知、复盘到达、勿扰不弹横幅、通知中心点击与清除。ASan/UBSan 无泄漏。
- 截图已拼到 `design-preview/fw/u1-sheet.png`（cc-pulling、cc、cc-toggled、banner、notes、notes-empty），**还没人看过**，先目视检查再烧。

### U6 接续步骤

1. 看 `design-preview/fw/u1-sheet.png`，排版有问题先改。
2. 编译：`MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-22.04 bash /mnt/d/desktop/VelaRide_AI/design-preview/fw/sync_build.sh`，再跑 `warn.sh` 应为 `count: 0`。
3. 归档：把 `design-preview/fw/archive.sh` 里的 `velaride-ui-u5c-20261010.bin` 改成 `u6`，运行后把输出的哈希大写追加到 `artifacts/SHA256SUMS`。
4. 烧录（COM5，用户一直插着）：`./sftool.exe -c SF32LB52 -p COM5 -b 1000000 --before default_reset --after soft_reset write_flash "my-contest-repo/artifacts/<镜像>@0x12010000"`
5. 读回：`py -3.13 -X utf8 tools/serial_probe.py --port COM5 --command "velaride lcd-diagnose" --command "velaride ride-latest"`，正常为 `reg=0a 9c`、`reg=0c 55`、`sent=N done=N recoveries=0`。
6. 请用户实物试：表盘下拉/上推、控制中心各按钮、连接网页触发横幅。
7. 把结果追加到 `UI_REDESIGN_PLAN_20261010.md` 并更新 `CURRENT_STATE.md` 第 00 节。

## 3. 工具脚本（都在 `design-preview/fw/`，WSL 内运行）

| 脚本 | 作用 |
| --- | --- |
| `host_test.sh` | 编译并运行两套主机界面测试（ASan/UBSan），截图到 WSL `/tmp/velaride-u1-screens` |
| `sheet.sh` + `sheet.py` | 把指定截图拼成 `u1-sheet.png`；改 `sheet.sh` 里的名字列表 |
| `sync_build.sh` | 把应用源码拷进 WSL 构建树并构建，打印固件大小与配置区余量（必须 < `0x990000`） |
| `warn.sh` | 只统计本应用的编译警告 |
| `archive.sh` | 把 WSL 产物拷到 `artifacts/` 并打印 SHA-256 |
| `make_fonts.sh` | 生成 Outfit 数字字体（需 WSL `~/fonttools`，已装 lv_font_conv 1.5.3 与 fonttools） |
| `fonts3.sh` | 重新生成 19 px 中文字库和 28 px 界面字体 |

其他：骑行状态测试 `bash tests/ride_session/run.sh`（WSL 内，在 `my-contest-repo` 下）；字体覆盖检查 `node scripts/build-watch-ui-font.mjs --check`；转场与菜单帧率 `velaride nav-stats`（加 `reset` 清零）。

运行 WSL 脚本前缀必须 `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-22.04 bash /mnt/d/desktop/VelaRide_AI/...`，否则 Git Bash 会改写路径。

## 4. 踩过的坑

- **Git Bash 的 heredoc 会把 Python 字符串里的 `\n` 变成真换行**，C 代码里出现断开的字符串字面量。含转义的修改脚本一律用 Write 工具写成 `.py` 文件再运行。
- Python 在 Windows 写出的 `.sh` 带 CRLF，WSL 里跑会报 `set: -: invalid option`，先 `sed -i 's/\r$//'`。
- LVGL 源码文件是 CRLF，打补丁时要按原换行符匹配（见 `lvgl_arc_fix.py`）。
- **新增界面汉字后必须跑 `fonts3.sh`**（20 px 字表 `app/velaride/watch-font-symbols.txt` 先补字），否则实物显示方框。数字字体只含数字和少量符号，回退到 19 px 中文。
- LVGL 对象默认 `GESTURE_BUBBLE`，手势会被交给父对象；需要自己处理滑动的容器要去掉这个标志（U4b 骑行页无法滑动就是这个原因）。
- 测试要走真实指针设备（`lv_indev_read`），直接调用内部函数会漏掉手势、按下状态这类问题。
- 本板 CPU 绘制，整屏重画 100 ms 以上；每帧只失效小区域，转场用快照按行直拷（`nav_flush_start_cb`）。LVGL 一帧最多 32 个失效区，超过就整屏重画。
- 传感器方向（实物确认）：六轴 +X 指向屏幕底部，+Y 指向屏幕左侧。

## 5. 待办与限制

- U6 上板验收（见上），然后 U7 抬腕亮屏。
- 暂缓：百叶窗/圆形展开、菜单帧率优化（数据见改版计划 U2c、U3b 节）。
- 只存最近一条骑行记录，多条历史需改存储结构。
- 夜骑灯未提亮屏幕（供电与黑屏历史），RGB 灯 SK6812 无驱动。
- 指南针北向、安装方向、倾斜补偿未实物验收。
- 摔车后通知联系人需手机端，未做。
- 马达、喇叭未装，计划见改版计划“等待硬件”一节（H1–H3）。
- **所有改动尚未 git commit**（团队仓 `my-contest-repo` 有大量已修改/未跟踪文件）。提交前确认用户意愿。
- 备份：`design-preview/fw/bak-u4b/`、`bak-u5c/` 是对应阶段前的源码；样稿 `design-preview/velaride-watch-ui.html`。
