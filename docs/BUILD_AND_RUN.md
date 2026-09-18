# 构建与运行

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

提交包中的已验证固件：

```text
artifacts/velaride-huangshan-pi-verified.bin
```

烧录前先核对 `artifacts/SHA256SUMS`。

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
.\start-mimo.ps1 -Model mimo-v2.5-pro
```

打开 `http://127.0.0.1:4173`，录入手表结束页数据后点击“生成复盘”。Token 只在
当前进程内存在。关闭启动窗口后环境变量会被清除。

## 本轮验证边界

提交整理阶段没有重新运行完整固件 build 或重新烧录。已有构建和真机证据来自
`docs/development-notes/`，最终提交前建议在干净工作区执行一次上述构建流程。
