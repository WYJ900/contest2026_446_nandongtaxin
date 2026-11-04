# 红米联动与久坐提醒（2026-11-04）

配套 App 0.2.1、手表合并固件 `f43ccbbaf7a9962043dee48bcd32c06f70f9a634d91ce4c90b6abfad0d6f73a7`（9,808,412 字节）。静态配置不变，应用区余 218,596 字节。归档在 `artifacts/phone-bridge-stage-20261104/`，保留实际应用源码、父提交差异、测试源码、APK 和逐文件哈希；未认证完整依赖树的干净重建。

## 已实现

- 设置 → 久坐提醒：默认 09:00–21:00、50 分钟，可设置间隔、时间段、确认活动、延后 10 分钟。遵守勿扰，不主动发声。根据手腕活动估计，不能识别坐姿或佩戴，静置桌面可能提醒；骑行和无效采样不累计。详情见 [久坐交接](HANDOFF_ACTIVITY_20261104.md)。
- 设置 → 手机与天气：最多 8 条手机摘要，仅 RAM 保存，可清除；温度、城市、天气说明及两小时有效期。通知横幅默认只显示通用提示，点击才进入摘要页。
- Android：用户选择应用并授权系统通知使用权后，才转发新通知。转发默认关闭，内容预览默认关闭；过滤秘密、常驻、分组通知，预览过滤常见验证码关键词，限制长度、去重和速率。没有短信、通讯录权限，也不读取应用私有数据库。系统可能进一步隐藏敏感内容。
- 用户连接手表时启动 connectedDevice 前台服务；界面退出不再主动断开 BLE。通知队列有容量和时效限制，不补发旧通知。在线 AI/天气查询使用独立工作线程，避免阻塞 BLE 指令通道。
- 点击“同步小米天气”尝试读取系统本地缓存。接口拒绝、格式不符或过期会报错，不生成假天气。提供天气、运动健康应用入口；运动健康直接读写尚未接入，已有骑行 CSV 导出保留。
- 增加仅在未骑行、未录音、TF 未挂载时运行的 `velaride tf-probe`：400kHz、32 字节 CMD0 诊断，不读写扇区或格式化。未修改 SPI、引脚、时钟、电源等底层驱动。

## 手机安装与验证

用户选择稍后手动安装 APK，不能把主机测试视作红米实机验收。手机截图为 REDMI K100 Pro Max、HyperOS 3.0.310.0.WGNCNXM.C10；Android 版本尚未显示。

1. 安装 `VelaRide-0.2.1-debug.apk` 并打开，授权附近设备后连接 VelaRide。
2. 在手机联动卡片中选择应用，进入系统设置授权“通知使用权”，开启转发。需要内容摘要时再开启预览。
3. 允许连接通知；根据实际系统设置允许自启动和后台运行。切到其他应用后发一条普通通知验证。天气按钮单独验证系统接口是否允许。

后台 BLE 已有实现，但 HyperOS 后台限制需要真机检查。GPS 仍只在 App 前台运行，锁屏/后台持续定位尚未完成。久坐的真实 50 分钟提醒、手机后台转发、天气提供器、GPS/GPX/在线 AI/录音试听仍待对应实测。

## 软件证据

实际构建树 LVGL、ASan/UBSan、19 组骑行状态与持久化、旧 BLE 格式、memo、久坐状态机、通知/天气 UTF-8 和有效期、TF 诊断边界测试通过。安卓 10 组主机测试及编译/签名校验通过。证据在 `validation-phone-bridge-20261104/`。界面截图为明确标注的模拟内容。

## 官方接口依据

- [Android 通知监听](https://developer.android.com/reference/android/service/notification/NotificationListenerService)、[后台 BLE](https://developer.android.com/develop/connectivity/bluetooth/ble/background)。用户可撤销权限，系统限制不能由 App 绕过。
- [小米天气主题接口](https://zhuti.designer.xiaomi.com/docs/blog/weatherApi.html)：文档面向主题环境，普通 App 在不同 ROM 上是否可访问仍需实测。
- [小米运动健康服务](https://dev.mi.com/xiaomihyperos/documentation/detail?pId=2333)：需要开发者服务/权限申请及用户 OAuth 授权，不能仅凭安装本 App 就同步系统健康数据。

上板和 TF 实测结果另记 `validation-phone-bridge-20261104/hardware-summary.json`，未完成前不推定硬件行为通过。
