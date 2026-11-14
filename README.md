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

## 5 分钟上手

```bash
# 1. 跑主机测试（不需要 ARM 工具链）
make host-test

# 2. 编译固件
make            # 产出 bootloader.bin / app_a.bin / app_b.bin
make size       # 看各镜像占用
make images     # 生成带头的 .img

# 3. 烧录（需要 st-link）
make flash
```

## 当前进度

| 里程碑 | 状态 |
| --- | --- |
| M0 骨架与工具链 | ✅ 完成 |
| M1 镜像格式与打包工具 | ✅ 完成 |
| M2 Bootloader 跳转 | ✅ 完成 |
| M3 Flash 与元数据双备份 | ✅ 完成 |
| M4 UART 升级协议 | ⬜ 待做 |
| M5 A/B 切换与回滚 | ⬜ 待做（决策逻辑已完成，缺 app 侧确认） |
| M6 打磨与发布 | ⬜ 待做 |

### 实测数据

| 项 | 实测值 | 预算 |
| --- | --- | --- |
| Bootloader Flash | 3868 字节 | 8192（47%） |
| Bootloader RAM | 1028 字节 | 20480（5%） |
| 应用固件 | 1520 字节 | 24544（6%） |
| 主机测试 | 109 项断言，0 失败 | — |
| 掉电注入点 | 6 个时机全部恢复 | — |

详细拆解见 [docs/milestones.md](docs/milestones.md)，设计取舍见 [docs/design.md](docs/design.md)。

## 两个避免踩的坑

**跳转前必须设 VTOR。** Cortex-M3 复位后 VTOR 指向 0x08000000。跳进 app 后不重设的话，app 里第一个中断就会跑回 Bootloader 的向量表——现象是"应用跑一会儿突然死机"，看你运气什么时候来中断。

**试运行计数要在跳转前写盘。** 如果把 `retry_count++` 放在 app 里做，app 在第一行代码就崩了的话，这个数永远加不上去，设备会无限重启在新固件上，回滚永远不会触发。

## 许可

MIT
