# 上电时间恢复（2026-08-30）

## 结论

黄山派当前仅 USB 供电时，RTC 日历在主控复位或完全断电后会回到
`2000-01-01`。本次修复实现了：

- 修正 SiFli RTC 年份到 `struct tm::tm_year` 的转换；
- 按思澈参考驱动区分 RTC 首次初始化和保留初始化；
- 在独立配置分区保存最近一次已校准时间；
- `velaride` 创建表盘前检测 RTC，有效则直接使用，无效则从 Flash 恢复；
- 新增 `velaride time-save` 和 `velaride time-read` 诊断命令。

Flash 使用 KVDB 键 `persist.velaride.wallclock`，包含版本、长度和 CRC32。
它和骑行记录共用配置分区，但键相互独立。固件仍只写入
`0x12010000`，未覆盖 `0x129A0000` 配置区。

## 已验证

校准和保存：

```text
VELARIDE_TIME: save epoch=1788069600 ret=0
Sun, Aug 30 14:00:01 2026
```

RTS 复位后：

```text
VELARIDE_TIME: restored epoch=1788069538
Sun, Aug 30 13:59:00 2026
VELARIDE_PERSIST: seq=3 ...
```

同时确认 `rcS` 自动启动 `ai_agent` 和 `velaride`，表盘、蜂窝菜单、运动
UI 及骑行记录均保留。

## 限制与后续

Flash 只能保存断电前的时间，设备完全断电期间没有计时源，因此无法凭软件
推算断电时长。最终产品要实现任意断电时长后精确显示当前时间，需要至少一种
外部校时来源：

1. 手机连接 BLE 后自动下发 Unix 时间（推荐，与原生 ZBlue GATT 一并完成）；
2. 已配置网络时使用 NTP；
3. 硬件为 RTC 域提供后备电源。

BLE/NTP 校时成功后必须调用与 `velaride time-save` 相同的保存入口，更新 Flash
兜底时间。
