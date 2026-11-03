# 构建与运行

当前镜像与接续顺序先看 [当前成果与接续说明](CURRENT_STATE.md)。以下区分通用新工作区
构建方法与本机 WSL 流程，不能把本机增量成功写成全新工作区已复现。

## 环境

- Ubuntu 22.04
- 完整 openvela `dev-ai-contest-2026` 工作区
- Git LFS 预编译资源已拉取
- ARM GNU Toolchain 13
- Node.js 20 或更高版本，仅 Web 网关需要

## 初始化

```bash
repo init -u https://github.com/open-vela/contest2026_446_nandongtaxin \
  -b dev-ai-contest-2026 -m contest2026_446_nandongtaxin.xml
repo sync -c -j8
cd contest2026_446_nandongtaxin
bash scripts/apply-integration.sh --check
bash scripts/apply-integration.sh
```

## 编译固件

```bash
bash scripts/build-firmware.sh
```

脚本使用黄山派配置：

```text
vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh
```

`app/velaride` 和 `agent_skills/ride-safety.md` 由 repo manifest 映射到工作区；
`patches/` 与 `overlays/` 由集成脚本处理。

## 烧录

本项目历史验证使用 SiFli `sftool` 将 `nuttx.bin` 写入 `0x12010000`。串口号由实际
设备决定，烧录工具和驱动请使用黄山派官方发布版本。普通固件烧录不应覆盖从
`0x129A0000` 开始的持久化配置区。

当前硬件阶段、保留的产品候选与显示恢复基线：

```text
artifacts/velaride-timer-fix-20261010.bin       # 当前：长时间运行计时修复
artifacts/velaride-watch-product-20261010.bin   # 历史：图标设置、灵敏调光、地磁校准
artifacts/velaride-menu-key-20261010.bin        # 保留：取消按键进入骑行
artifacts/velaride-hardware-s3-20261009.bin      # S3 环境光与屏幕设置基线
artifacts/velaride-hardware-s01-20261009.bin     # S0/S1 回退基线
artifacts/velaride-product-20261009.bin          # 产品候选，验收状态见 CURRENT_STATE.md
artifacts/velaride-huangshan-pi-20261009.bin     # 显示恢复回退基线
```

烧录前先核对 `artifacts/SHA256SUMS`。
名称为 `velaride-huangshan-pi-verified.bin` 的文件是 2026-08-30 历史版本。

本机 Windows 工作区为 `D:\desktop\VelaRide_AI`，WSL 构建副本为 Ubuntu-22.04 的
`/home/wyj/velaride_ai`。修改文件先定向同步，再运行：

```powershell
wsl -d Ubuntu-22.04 bash /mnt/d/desktop/VelaRide_AI/tools/wsl_build.sh
```

确认新镜像正确且需要安装时，才在工作区根目录执行 `.\tools\flash.ps1 -Port COM5`。
该脚本复制 WSL 当前构建产物并烧录，不自动选择团队仓归档镜像。

## 设备操作

- KEY2 单击：表盘与蜂窝菜单切换。
- KEY2 快速双击：进入运动流程。
- 依次选择运动类型、训练目标和设置后开始骑行。
- 运动页支持暂停、继续、结束确认和本地复盘。
- KEY1 涉及电源、下载和复位，不作为业务按键。

## Web 网关

```powershell
cd web-gateway
npm test
.\start-ai.ps1
```

打开 `http://127.0.0.1:4173`，连接 BLE 后读取手表最近记录，生成复盘并回传。
启动器支持 MiMo、OpenAI/Anthropic 兼容接口及中转站；Token 不写入文件，进程结束后
需在本机安全输入。历史 `start-mimo.ps1` 继续保留，但不作为当前多接口入口。

## 本轮验证边界

2026-10-09 已在现有 WSL 工作树完成重新编译、多次烧录和实物联调；配置变更期间也
重新生成配置头并重编全部目标。尚未在全新下载的工作区复现构建。7 份补丁、4 份
overlay 的本机集成检查通过；完整静态风格检查仍有既有厂商代码问题，不能称为全通过。
