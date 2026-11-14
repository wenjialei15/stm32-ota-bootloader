<div align="center">

# STM32 OTA Bootloader

**带 A/B 双槽与自动回滚的嵌入式 OTA 升级方案** —— 面向 STM32F103C8T6，纯寄存器操作，不依赖 HAL。

[![build](https://github.com/wenjialei15/stm32-ota-bootloader/actions/workflows/build.yml/badge.svg)](https://github.com/wenjialei15/stm32-ota-bootloader/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](./LICENSE)

**[项目主页](https://wenjialei15.github.io/stm32-ota-bootloader/)** — 分区图、启动流程、实测数据

</div>

---

## 这是什么

一块装在现场的板子，固件有 bug 了。派人去插 ST-Link 不现实，所以要让它自己把新固件收下来、验证、切换；跑不起来还得能自己退回去。

这个项目实现的就是这套机制：

- 上电先跑 Bootloader（8 KB，永不擦除）
- 通过 UART 接收新固件，写进**备用槽**
- 新固件启动后必须在 3 次机会内"报到确认"
- 3 次都没确认 → 自动回滚到旧固件
- 全程任意时刻断电，设备都不会变砖

难点不在"传固件"，在**掉电**。擦除或写入 Flash 的中途断电，不能留下半截状态。

## 在线模拟器

> **<https://wenjialei15.github.io/stm32-ota-bootloader/simulator/>**

没买板子也能把整个流程走一遍。模拟器用 JS 复刻了真实固件的规则——同样的 CRC 校验、同样的双备份元数据、同样的试运行计数与回滚条件——跑在浏览器里，不依赖任何工具链。

能做的事：

- 烧录 Bootloader、安装固件到 A 槽
- OTA 推固件到 B 槽（可选「正常固件」或「一跑就崩的坏固件」）
- 重启看启动决策，观察 `retry` 逐次累加
- 3 次失败后自动回滚到旧槽
- **随时模拟断电**：下一次擦除或写入会中断，之后重启看它能否恢复

地址后面加 `?demo=1` 会自动播放完整剧本（装 A → 升级 B → 推坏固件 → 回滚）。

左侧的 Flash 网格是 64 个 1 KB 页，鼠标悬停能看到地址和所属区域。元数据卡片会显示 A / B 两份备份各自的 `seq`、`active`、`pending`、`retry`，以及哪一份是最新的——这是理解双备份最直观的方式。

## 硬件

| 项 | 值 |
| --- | --- |
| MCU | STM32F103C8T6（Blue Pill） |
| Flash | 64 KB / 20 KB RAM |
| 时钟 | HSI 8 MHz → PLL 64 MHz（不用外部晶振） |
| 串口 | USART1，PA9/PA10，115200 8N1 |
| 指示灯 | PC13 板载 LED |

接线只需要一根 USB 转串口：`PA9 → RX`、`PA10 → TX`、`GND → GND`。

## Flash 分区

```
0x08000000  ┌──────────────────┐
            │  Bootloader 8 KB │  永不擦除
0x08002000  ├──────────────────┤
            │  Meta A     2 KB │  ┐ 元数据双备份
0x08002800  ├──────────────────┤  │
            │  Meta B     2 KB │  ┘
0x08003000  ├──────────────────┤
            │  Slot A    24 KB │  应用槽 A
0x08009000  ├──────────────────┤
            │  Slot B    24 KB │  应用槽 B
0x0800F000  ├──────────────────┤
            │  Reserved   4 KB │
0x08010000  └──────────────────┘
```

64 KB 做 A/B 很紧：Bootloader 8 KB 是下限（含 UART 协议和 Flash 驱动），剩下的对半分只剩 24 KB 一槽。

## 源码结构

```
shared/            两边共用
  crc32.c          CRC-32/ISO-HDLC，流式计算
  image.h          镜像头定义 + 分区地址（唯一真相来源）
  system.c         时钟 / LED / 延时
  startup_stm32f103.c  向量表 + Reset_Handler（C 写的）

bootloader/src/
  main.c           启动入口、日志、跳转
  boot_decision.c  启动决策状态机        ← 纯逻辑，可主机测试
  metadata.c       元数据双备份提交       ← 纯逻辑，可主机测试
  image.c          镜像头校验            ← 纯逻辑，可主机测试
  flash_if.c       Flash 擦写（寄存器直写）
  uart.c           USART1 轮询收发

app/src/main.c     被升级的应用
host_tests/        主机侧单元测试（gcc，不需要任何嵌入式工具链）
tools/ota_pack.py  给 bin 加镜像头
tools/ota_verify.py 独立校验镜像
```

**分层的用意**：`image` / `metadata` / `boot_decision` 三个模块不碰任何寄存器，通过 `storage_if_t` 回调访问 Flash。所以掉电安全这种最难的逻辑，可以在 PC 上把断电点一个一个试过去。

## 文档

| 文档 | 内容 |
| --- | --- |
| [docs/design.md](docs/design.md) | 分区表、镜像格式、元数据格式、掉电安全分析、已知难点 |
| [docs/milestones.md](docs/milestones.md) | M0–M6 里程碑、每周任务、验收标准、风险预案 |
| [docs/hardware.md](docs/hardware.md) | 接线图、烧录步骤、首次上电现象对照、故障排查表 |
| [docs/measurements.md](docs/measurements.md) | 实测记录模板（板子到了按表填） |
| [docs/blog-firmware-ota-pitfalls.md](docs/blog-firmware-ota-pitfalls.md) | 技术博客：四个我踩过的坑 |

## 5 分钟上手

```bash
# 1. 跑主机测试（不需要 ARM 工具链）
make host-test

# 2. 编译固件
make            # 产出 bootloader.bin / app_a.bin / app_b.bin
make size       # 看各镜像占用
make images     # 生成带头的 .img

# 3. 烧录 bootloader（需要 st-link）
make flash

# 4. 推送固件（需要 pip install pyserial）
python tools/ota_send.py --port COM3 --image build/app_b.img
```

推送工具会自动读镜像头判断目标槽，每帧等 ACK，超时用同序号重发。设备收到完整镜像并通过 CRC 校验后会重启并试运行新固件。

## 当前进度

| 里程碑 | 状态 |
| --- | --- |
| M0 骨架与工具链 | ✅ 完成 |
| M1 镜像格式与打包工具 | ✅ 完成 |
| M2 Bootloader 跳转 | ✅ 完成 |
| M3 Flash 与元数据双备份 | ✅ 完成 |
| M4 UART 升级协议 | ✅ 完成 |
| M5 A/B 切换与回滚 | ✅ 完成 |
| M6 打磨与发布 | ⬜ 待做（需真板实测数据） |

### 实测数据

| 项 | 实测值 | 预算 |
| --- | --- | --- |
| Bootloader Flash | 6128 字节 | 8192（75%） |
| Bootloader RAM | 5196 字节 | 20480（25%） |
| 应用固件 Flash | 2788 字节 | 24544（11%） |
| 应用固件 RAM | 1028 字节 | 20480（5%） |
| 主机测试 | 210 项断言，0 失败 | — |
| 掉电注入点 | 6 个时机全部恢复 | — |

协议层吃掉了 4 KB 静态缓冲（帧缓冲、会话页缓冲、响应缓冲各 1 KB）。20 KB RAM 的板子上这是笔不小的开销，可以复用缓冲省下来，但会牺牲可读性——当前选择是"留 15 KB 余量，换清晰的分层"。

## 演示一次回滚

这是整个项目唯一值得录成视频的部分：**推一个能通过校验、但一跑就崩的固件，看它自己退回来。**

```bash
# 1. 烧 bootloader 和 A 槽的正常固件
make flash
st-flash write build/app_a.img 0x08003000

# 2. 把一个"会崩"的固件推到 B 槽
make images
python tools/ota_send.py --port COM3 --image build/app_crash.img
```

串口应该看到：

```
[ota-boot]  trial boot, retry=1        <- 新固件第一次试运行
[app]       DEMO_CRASH: dying on purpose
[ota-boot]  trial boot, retry=2        <- 崩了，再试
[app]       DEMO_CRASH: dying on purpose
[ota-boot]  trial boot, retry=3        <- 第三次
[app]       DEMO_CRASH: dying on purpose
[ota-boot]  rollback, slot A           <- 判定失败，退回旧固件
[app]       slot A, boot confirmed
```

`app_crash` 固件镜像完整、CRC 正确——Bootloader 校验不出任何问题。它只能靠"新固件有没有自己报到"来判断，这就是为什么回滚必须由应用参与。

详细拆解见 [docs/milestones.md](docs/milestones.md)，设计取舍见 [docs/design.md](docs/design.md)。

## 两个避免踩的坑

**跳转前必须设 VTOR。** Cortex-M3 复位后 VTOR 指向 0x08000000。跳进 app 后不重设的话，app 里第一个中断就会跑回 Bootloader 的向量表——现象是"应用跑一会儿突然死机"，看你运气什么时候来中断。

**试运行计数要在跳转前写盘。** 如果把 `retry_count++` 放在 app 里做，app 在第一行代码就崩了的话，这个数永远加不上去，设备会无限重启在新固件上，回滚永远不会触发。

## 常见问题

**为什么不用 MCUboot / 现成的 OTA 库？**

它们更成熟，但用它们学不到东西——而这个项目的目的就是搞懂 Flash 时序和向量表重定位。另外 MCUboot 在 64 KB Flash 上塞得比较勉强，它的最小配置通常需要 128 KB 以上。

如果目标是在产品里上 OTA，直接用 MCUboot 或芯片厂商的方案，别自己造。

**为什么不做差分升级？**

差分能把 3 KB 的固件压到几百字节，在 NB-IoT 这种按流量计费的场景很有价值。但它需要设备端实现 bsdiff 之类的算法，还要维护"当前固件上一版是什么"的索引，复杂度至少翻倍。先把完整升级做对，差分是锦上添花。

**为什么不做一个大槽 + 一个备份区？**

那样"备份区"只能存旧固件，新固件必须覆写主槽——覆写过程中掉电就没得救了。A/B 双槽的价值在于**两个槽随时都能独立启动**，切换只是改一个指针。

**为什么两个槽各只有 24 KB？**

64 KB 减去 8 KB Bootloader 和 4 KB 元数据/预留，对半分就这么多。这是硬约束，也是这个项目最真实的部分——真实的嵌入式开发就是在这种约束下做取舍。

**为什么必须编两份固件？**

Cortex-M 的向量表项和字面量池里的函数地址都是绝对地址，链接到 `0x08003000` 的固件放到 `0x08009000` 会全线跑飞。要么做位置无关代码（PIC），要么每个槽单独链接。项目选后者，代价是发布两个 bin，换来的是零运行时重定位开销。

**这个能直接上生产吗？**

不能，还差几样：

- **固件签名**：现在只有 CRC，能防传输错误，防不了恶意固件。生产环境要用 ECDSA 之类做数字签名，公钥烧在 Bootloader 里
- **看门狗**：当前 HardFault 后是死循环，需要手动复位。生产环境应该开 IWDG，让崩溃自动重启进入下一轮试运行
- **回滚前的健康检查**：现在只判断"应用有没有报到"，生产环境还应该检查外设自检、通信是否正常
- **升级授权**：谁能推固件、推哪个版本，需要一套策略

这些不在当前范围里，但每一项该怎么做，[docs/design.md](docs/design.md) 里都留了位置。

## 许可

MIT
