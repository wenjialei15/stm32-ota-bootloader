# 硬件接线与上机手册

这份文档假设你已经拿到板子，准备第一次上电。按顺序做，每一步都有明确的现象可以对照。

## 1. 物料

| 件 | 型号 | 参考价 | 说明 |
| --- | --- | --- | --- |
| 最小系统板 | STM32F103C8T6（Blue Pill） | ¥15–25 | 注意选 64 KB Flash 版本，别买成 C6T6 |
| 下载器 | ST-Link V2 | ¥15 | 克隆版即可，能跑 SWD 就行 |
| USB 转串口 | CH340 / CP2102 | ¥5 | 需要 3.3 V 电平 |

三样加起来不到 50 块。不要省 CH340——没有串口你就看不到 Bootloader 的任何日志，出问题只能瞎猜。

## 2. 接线

### ST-Link（烧录用，SWD 四根线）

```
        ST-Link V2                          Blue Pill
     ┌──────────────┐                    ┌──────────────┐
     │              │                    │  ┌────────┐  │
     │   3.3V  ●────┼────────────────────┼──┤ 3.3V   │  │
     │   GND   ●────┼────────────────────┼──┤ GND    │  │
     │   SWDIO ●────┼────────────────────┼──┤ PA13   │  │
     │   SWCLK ●────┼────────────────────┼──┤ PA14   │  │
     └──────────────┘                    │  └────────┘  │
                                         └──────────────┘
```

| ST-Link | Blue Pill | 作用 |
| --- | --- | --- |
| 3.3V | 3.3V | 供电（也可以用板子的 USB 供电，但别同时接两个电源） |
| GND | GND | 共地，必须接 |
| SWDIO | PA13 | 数据 |
| SWCLK | PA14 | 时钟 |

### CH340（串口，三根线，注意交叉）

```
        CH340                              Blue Pill
     ┌──────────────┐                    ┌──────────────┐
     │   TX    ●────┼────────────────────┼──┤ PA10 RX│  │
     │   RX    ●────┼────────────────────┼──┤ PA9  TX│  │
     │   GND   ●────┼────────────────────┼──┤ GND    │  │
     └──────────────┘                    └──────────────┘
                （TX 接 RX，RX 接 TX）
```

**TX 和 RX 一定要交叉。** 这是最常见的接线错误：两边都接 TX，结果一个字都收不到，还会让人怀疑是固件问题。

GND 必须共地，否则串口会收到一堆乱码。

## 3. 烧录

### 3.1 先烧 Bootloader

```bash
make                    # 编译
make flash              # 烧到 0x08000000
```

`make flash` 内部是：

```bash
st-flash write build/bootloader.bin 0x08000000
```

看到 `Flash written and verified!` 就成功了。如果没有 `st-flash`，用 STM32CubeProgrammer 图形界面选同一个 bin 和地址即可。

### 3.2 再烧应用（注意是 .img 不是 .bin）

```bash
make flash-app
```

等价于：

```bash
make images
st-flash write build/app_a.img 0x08003000
```

**这一步最容易错。** `app_a.bin` 是裸固件，没有 32 字节镜像头；Bootloader 打开一看 magic 不对，直接判为非法镜像，然后进升级模式。现象是：串口打印 `no valid image, entering upgrade mode`，板子永远不跑应用。

要么烧 `.img`，要么用 OTA 推 `.img`。`.bin` 只在打包工具里出现，不该直接烧进槽里。

## 4. 首次上电

插上串口，用任意串口工具（波特率 **115200 8N1**）打开，然后按一下板子上的复位键。

你应该看到：

```
[ota-boot] Nov 15 2026 00:56:12
  meta: unreadable, fall back to image headers
  plan: slot A reason=ok
  meta: initialised from slot A
  jump -> 0x08003000

[app] slot A base=0x08003000
[app] boot confirmed, this slot is now active
[app] heartbeat 1
[app] heartbeat 2
```

逐行解释：

| 输出 | 含义 |
| --- | --- |
| `meta: unreadable` | 第一次上电，元数据区还是空白 Flash，正常 |
| `plan: slot A reason=ok` | 靠镜像头判断出 A 槽有效 |
| `meta: initialised from slot A` | 把 A 记为活动槽，元数据落地 |
| `jump -> 0x08003000` | 跳转 |
| `boot confirmed` | 应用自己确认存活 |

**第二次复位**时输出会变成 `meta: seq=1 active=A pending=none retry=0`，这是正常状态。

## 5. 走一遍 OTA

```bash
pip install pyserial
python tools/ota_send.py --port COM3 --image build/app_b.img
```

（Linux 上是 `/dev/ttyUSB0`）

推送过程中板子会留在 Bootloader 里，因为工具一开始就发了字节，触发了 300 ms 升级窗口。推完后设备自动重启，跳到 B 槽，应用确认后 B 变成活动槽。

再推一次 `app_a.img`，就切回 A 槽。**这就是 A/B 双槽的作用：可以来回切，且每次切换前旧固件都还在。**

## 6. 走一遍回滚

这是整个项目最值得录视频的 90 秒。

```bash
python tools/ota_send.py --port COM3 --image build/app_crash.img
```

`app_crash.img` 镜像完整、CRC 正确，但启动后立刻 HardFault。预期看到：

```
[app] DEMO_CRASH: dying on purpose        <- 第 1 次
[ota-boot]  plan: slot B reason=trial boot
[app] DEMO_CRASH: dying on purpose        <- 第 2 次
[ota-boot]  plan: slot B reason=trial boot
[app] DEMO_CRASH: dying on purpose        <- 第 3 次
[ota-boot]  plan: slot A reason=rollback  <- 放弃，退回 A
[app] slot A
[app] boot confirmed
```

注意每次崩溃后板子会自己复位（HardFault 后如果没有看门狗，实际会卡死，需要手动按复位或等看门狗）。

> **关于复位**：当前版本没有开看门狗，HardFault 后是死循环。要让它自动重启，可以在 `Default_Handler` 或 HardFault 处理里调用 `system_reset()`，或者开 IWDG。这是 M6 之后值得补的一项。

## 7. 故障排查

| 现象 | 最可能的原因 | 怎么查 |
| --- | --- | --- |
| 烧录时报 "no STM32 target found" | SWD 线没接对，或板子没供电 | 检查 PA13/PA14/GND；换一根短一点的杜邦线 |
| 串口一个字都没有 | TX/RX 没交叉；或波特率不是 115200 | 把两根线对调试试 |
| 串口全是乱码 | 没共地；或波特率不对 | 先接 GND，再确认 115200 8N1 |
| 打印 `no valid image` | 烧的是 .bin 不是 .img | 重新 `make flash-app` |
| 卡在 `jump ->` 之后没动静 | 应用没重设 VTOR，或栈没设对 | 检查 app 的 `SCB_VTOR` 赋值 |
| 应用跑几秒就死 | 中断打到了 Bootloader 的向量表 | 同上，VTOR 必须在开中断前设好 |
| 升级卡在 BEGIN | 擦 24 页要 300–500 ms，工具超时太短 | 已设 5 s，若仍超时检查 Flash 供电 |
| 推完固件但没切槽 | END 的 CRC 没过，或元数据写失败 | 看串口有没有 `ota: accepted` |
| 升级后一直重启，不回滚 | `boot_confirm()` 没被调用，或太早调用 | 确认 3 秒延时；看有没有 `boot confirmed` |
| 回滚不触发 | 元数据里 retry 没累加 | 每次启动应打印 `retry=N`，逐次加 1 |

## 8. 注意事项

**不要同时接两个电源。** ST-Link 的 3.3V 和板子的 USB 同时供电路径不同，可能倒灌。选一个。

**烧 Bootloader 时不要断电。** 虽然 0x08000000 这段不参与升级，但烧录过程本身被打断会让芯片进入需要连 BOOT0 才能救的状态。

**改分区表要三处同步**：`shared/image.h` 的宏、`bootloader/linker/bootloader.ld`、`app/linker/app.ld`。改一处不改另一处，链接出来的固件会跑到错误地址。

**Blue Pill 的 Flash 有假货。** 市面上有些板子标称 64 KB 实际只有 32 KB，或者 128 KB 的降级片。跑一下 `st-flash info` 看真实容量，如果 Flash 明显小于 64 KB，A/B 分区就不成立。
