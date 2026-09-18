# 骑行记录持久化说明（2026-08-29）

## 结论

骑行记录持久化已在黄山派真机通过三轮验收：写入后立即读取、RTS 复位后读取、重新烧录同一固件后读取，字段均完全一致。日常烧录流程不变，不需要额外格式化或恢复数据。

## 存储布局

- 应用镜像地址：`0x12010000`
- 配置区地址：`0x129a0000`
- 应用镜像最大长度：`0x990000` 字节
- 配置区大小：4 MiB
- 设备节点：`/dev/config0`
- KVDB 键：`persist.velaride.latest`（NVS 内部名为 `velaride.latest`）

`tools/wsl_build.sh` 会检查 `nuttx.bin` 大小，达到或越过配置区边界时直接失败。`tools/flash.ps1` 只写镜像实际长度，因此普通烧录会保留配置区。

## 记录内容

当前保存最近一条骑行记录，包括序号、开始/结束时间、总时长、移动时长、静止时长、暂停次数、冲击次数和休息提醒次数。磁盘记录包含 magic、版本、结构长度和 CRC32；CRC 的覆盖范围截止到 `crc32` 字段偏移，不包含结构尾部填充。

## 真机验收证据

固定测试记录三次读回均为：

```text
VELARIDE_PERSIST: seq=1 end=4 elapsed=321 moving=234 still=87 pause=3 impact=2 rest=1
```

复位后的自动加载日志为：

```text
VELARIDE_RIDE: latest seq=1 duration=321 moving=234 impacts=2
```

## 运维命令

```text
velaride persist-probe
velaride persist-write
velaride persist-read
velaride persist-format
```

前三个命令用于诊断。`persist-format` 会清空配置区，只能在首次初始化或确认配置区损坏时使用，普通编译、烧录、复位均不应执行。

配置区启用 NVS 前的原始备份：`backup/config_partition_before_nvs_20260829_213007.bin`，SHA-256 为 `3BB9F7EEFB60C822F2101A1273E9DD0AC2978BCAEF44A06810994FFC1A9BD4E5`。

最终已验收并烧录的固件：`backup/nuttx_ride_persistence_verified_20260829_230045.bin`（9,752,644 B），SHA-256 为 `35DEE3232FA8D5B4EE03446697F31B88874B7FE37552ABE5967300BC737D6E2D`。

## 最后人工验收

在手表上完成一次真实流程：双击 KEY2 进入运动，开始骑行，暂停/继续，结束并确认复盘页显示“记录已持久保存”；随后复位设备，确认启动日志能加载最新记录。
