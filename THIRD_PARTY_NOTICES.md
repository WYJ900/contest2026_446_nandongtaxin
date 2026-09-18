# 第三方组件与素材说明

## openvela 与 Apache NuttX

- openvela `apps`、`packages/ai_agent` 及本项目新增代码按各仓根目录许可使用，
  主要许可证为 Apache License 2.0。
- Apache NuttX 修改基于 Apache License 2.0。
- 本仓 `LICENSE` 保存 Apache License 2.0 全文。

## LVGL

设备界面基于 openvela 工作区中的 LVGL。LVGL 使用 MIT License，具体版本和许可
以工作区 `apps/graphics/lvgl/lvgl` 仓为准。

## SiFli SDK 与黄山派 BSP

黄山派板级代码、EPIC 驱动和表盘视觉方向参考 SiFli SDK 与 `vendor/sifli`。
`vendor/sifli` 仓包含独立 `LICENSE`，提交前应以该文件为准。VelaRide 的导航和
运动界面为面向 NuttX/LVGL 9.1 的独立实现，不依赖 RT-Thread `gui_app_fwk`。

项目曾收集 Apple Watch 界面作为设计参考，这些参考截图没有进入本提交仓、作品
介绍或最终交付物。未参与当前构建的历史 `mickey` 素材也被 `.gitignore` 排除。

## 字体

中文位图由 Noto Sans SC 按需生成。Noto 字体采用 SIL Open Font License 1.1。
本仓只包含编译所需的字形子集 C 数据，不包含原始字体文件。

## Xiaomi MiMo

Xiaomi MiMo 通过运行时 API 调用，不在仓库保存模型、Token 或服务端凭据。模型与
Token Plan 的使用遵循 Xiaomi MiMo 平台条款。
