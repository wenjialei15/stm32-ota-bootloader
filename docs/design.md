# 设计文档

## 1. 要解决的问题

一块已经装在现场的板子，固件发现 bug 了。你要么派人爬上去插 ST-Link，要么让设备自己把新固件收下来、验证、切换、跑不起来自动退回旧版本。

第二件事就是 Bootloader + OTA。它看起来是"加个下载功能"，实际难点全在**掉电**：擦写 Flash 的任意时刻断电，设备都不能变砖。

这个项目要交付的是：一块 STM32F103C8T6，上电先跑 Bootloader，通过 UART 接收新固件写入备用槽，校验通过后切换；新固件启动失败（比如 3 次没确认）自动回滚到旧固件。

## 2. 硬件前提

| 项 | 值 |
| --- | --- |
| MCU | STM32F103C8T6（Cortex-M3） |
| Flash | 64 KB，页大小 1 KB，共 64 页 |
| RAM | 20 KB |
| 时钟 | 内部 HSI 8 MHz × PLL = 64 MHz |
| 通信 | USART1，PA9/PA10，115200 8N1 |
| 状态指示 | PC13（Blue Pill 板载 LED） |

Flash 页大小 1 KB 是硬约束：擦除最小单位是 1 KB，写入只能把 1 变 0，所以**任何"修改"都是"擦整页再写"**。这条约束直接决定了元数据必须双备份。

## 3. Flash 分区

```
0x08000000  ┌──────────────────┐
            │  Bootloader 8 KB │  页 0-7    永不擦除，不参与升级
0x08002000  ├──────────────────┤
            │  Meta A     2 KB │  页 8-9    元数据主份
0x08002800  ├──────────────────┤
            │  Meta B     2 KB │  页 10-11   元数据备份
0x08003000  ├──────────────────┤
            │  Slot A    24 KB │  页 12-35   应用槽 A
0x08009000  ├──────────────────┤
            │  Slot B    24 KB │  页 36-59   应用槽 B
0x0800F000  ├──────────────────┤
            │  Reserved   4 KB │  页 60-63   参数/日志，留给后面
0x08010000  └──────────────────┘
```

64 KB 做 A/B 是很紧的：Bootloader 8 KB 是下限（含 UART 协议和 Flash 驱动），剩下的空间对半分只剩 24 KB。这个约束本身就是设计的一部分——**小 Flash 下的分区取舍**是面试时能聊半小时的话题。

每个槽的可执行区是 24 KB，减去 32 字节镜像头，实际固件上限 24544 字节。作为参照：本项目 app 带 UART 心跳和 Modbus 从机，实测约 6 KB，余量充足。

## 4. 镜像格式

镜像 = 32 字节头 + 负载。头紧贴槽起始地址，复位向量就落在头后面 32 字节处。

```c
typedef struct {
    uint32_t magic;        /* 0x3141544F = "OTA1"，小端 */
    uint32_t version;      /* 主<<16 | 次<<8 | 修订 */
    uint32_t image_size;   /* 负载长度，不含头 */
    uint32_t load_addr;    /* 应放置的 Flash 地址 */
    uint32_t entry;        /* 复位向量地址 = load_addr + 32 + 4 */
    uint32_t payload_crc;  /* 负载 CRC-32 */
    uint32_t header_crc;   /* 前 24 字节的 CRC-32 */
    uint32_t flags;        /* bit0 = 强制校验，预留 */
} image_header_t;
```

**为什么头里要存 `load_addr`？**
Bootloader 要能拒绝"投错槽"的固件。如果拿 slot A 的镜像往 slot B 写，`load_addr` 对不上就直接拒绝，而不是写完跳过去撞死。

**为什么 `header_crc` 单独一字段？**
头本身也可能被写坏（掉电写一半）。先校验头、再信任头里的 `payload_crc`，顺序不能反。

**`entry` 为什么是 `load_addr + 32 + 4`？**
Cortex-M 的复位向量表第 0 项是初始 MSP，第 1 项才是 Reset_Handler。向量表在负载开头，所以入口是负载首地址 + 4。

## 5. 启动决策流程

```
上电
 │
 ├─ 读元数据（双备份取 seq 最大且 CRC 正确的）
 │   └─ 两份都坏 → 进升级模式（等待串口）
 │
 ├─ 检查目标槽镜像头（magic / header_crc / payload_crc / load_addr）
 │   └─ 不合法 → 尝试另一个槽 → 两个都坏 → 进升级模式
 │
 ├─ 有 pending 升级？
 │   ├─ 是，且 retry_count >= 3  → 回滚：active 切回另一槽，清 pending，跳转
 │   └─ 是，且 retry < 3        → retry_count++ 写回元数据，跳转
 │
 └─ 跳转：关中断 → 关 SysTick → 设 VTOR → 设 MSP → 跳 Reset_Handler
```

## 6. 元数据与掉电安全

元数据记录（32 字节）：

```c
typedef struct {
    uint32_t seq;          /* 单调递增，越大越新 */
    uint32_t active_slot;  /* 0 = A, 1 = B */
    uint32_t pending_slot; /* 0xFFFFFFFF = 无升级待确认 */
    uint32_t retry_count;  /* 连续未确认的启动次数 */
    uint32_t version;      /* 当前镜像版本，便于回滚判断 */
    uint32_t reserved;
    uint32_t magic;        /* "META" 防止空白 Flash 误判 */
    uint32_t crc32;        /* 前 28 字节 CRC */
} boot_meta_t;
```

**双备份怎么用**

写入永远走"擦最旧的那份，写新的"：

1. 读 A、B，比较 `seq`，确定哪份是旧的
2. 擦除旧的那份所在页
3. 写入新记录
4. 回读校验 CRC
5. 成功才认为提交完成

任意时刻掉电，磁盘上**至少有一份完整且 CRC 正确的记录**：

| 掉电时机 | 结果 |
| --- | --- |
| 擦除中 | 旧份损坏，新份（另一份）仍完整 → 用另一份，seq 小一版本，最多丢失最后一次提交 |
| 写入中 | 同上，写入的记录 CRC 不过 → 被忽略 |
| 写入完成、未校验 | 两份都完整，seq 大的胜出 → 正确 |

关键点：**擦之前不需要任何"标记"**，因为另一份天然是完好的。这比"先写标志再改数据"的方案少了状态，也就少了出错的可能。

## 7. 回滚机制

只有"新固件跑起来了"才算升级成功，而这个判断只能由新固件自己做：

1. Bootloader 升级完成 → 写元数据 `{pending = new_slot, retry = 0, active = old_slot}`
2. Bootloader 跳转前发现 `pending != NONE` → `retry++` 写回，跳 new_slot
3. 新固件启动，初始化完外设后调用 `boot_confirm()` → 写 `{pending = NONE, active = new_slot, retry = 0}`
4. 如果新固件在 `boot_confirm()` 之前就崩了（HardFault、看门狗、死循环），下次上电 `retry` 已经加过
5. `retry >= 3` 时，Bootloader 判定失败 → `active` 切回旧槽，清 pending，跳旧固件

**为什么是 3 次而不是 1 次？**
第一次启动可能因为掉电、供电不稳而失败，不是固件的错。3 次是个经验值，可配置。

**为什么 `active` 在跳转前不改？**
如果跳转前就改 `active`，而新固件崩了，元数据里已经没有"旧固件在哪"的信息了。必须保留旧槽信息直到新固件确认。

## 8. 升级协议

UART 帧格式（小端）：

```
┌──────┬─────┬─────┬───────┬───────┬─────────┬───────────┐
│ SOF  │ CMD │ SEQ │ LEN_L │ LEN_H │ PAYLOAD │ CRC32 x4  │
│ 0xA5 │ 1B  │ 1B  │  1B   │  1B   │ 0..1024 │  覆盖前面  │
└──────┴─────┴─────┴───────┴───────┴─────────┴───────────┘
```

| CMD | 含义 | 载荷 |
| --- | --- | --- |
| 0x01 | HELLO | 空，响应返回 Bootloader 版本、Flash 大小、页大小 |
| 0x02 | BEGIN | 目标槽(1B) + 镜像总长(4B) + 镜像 CRC32(4B) |
| 0x03 | DATA | 偏移(4B) + 数据块(≤1024B) |
| 0x04 | END | 空，Bootloader 校验整镜像并提交元数据 |
| 0x05 | ABORT | 空，放弃本次升级 |
| 0x06 | STATUS | 空，返回当前状态机状态 |

设计要点：
- **每帧独立 CRC**，不依赖传输层可靠。UART 没有重传，必须自己保证。
- **DATA 带显式偏移**而不是依赖顺序，允许 PC 端重传单块。
- **SEQ 单调递增**，Bootloader 可以识别重复帧（PC 端超时重发时不会重复写）。
- 数据块 1024 字节对齐 Flash 页，一块擦一页写一页。

## 9. 已知难点

| 难点 | 处理方式 |
| --- | --- |
| 跳转前必须彻底关外设 | 关全局中断、关 SysTick、关 USART，否则 app 里中断会跳回 Bootloader 的向量表 |
| VTOR 必须重设 | Cortex-M3 复位后 VTOR = 0x08000000，跳转到 0x08003000 后不重设，所有中断都会跑飞 |
| MSP 必须重设 | app 的栈顶来自它自己的向量表第 0 项，不能沿用 Bootloader 的栈 |
| Flash 写入只能 1→0 | 所有"改元数据"都是"擦整页 + 重写整条记录"，不允许原地改字段 |
| 编译器可能优化掉跳转后的代码 | `__attribute__((noreturn))` + 内存屏障，避免编译器认为跳转后还能返回 |
| 两份 app 固件 | 代码是绝对地址链接，slot A 和 slot B 必须各编一份（除非做 PIC，作为进阶话题） |

## 10. 目录结构

```
bootloader/src/
  main.c            启动入口、模式选择
  boot_decision.c   启动决策状态机（纯逻辑，可主机测试）
  metadata.c        元数据序列化与双备份提交（纯逻辑）
  image.c           镜像头校验（纯逻辑）
  flash_if.c        Flash 擦写驱动（寄存器直写）
  uart.c            极简 UART 收发
  ota_proto.c       升级协议解析与状态机
shared/
  crc32.c           查表 CRC-32（IEEE 802.3）
  image.h           镜像头定义
host_tests/         主机侧单元测试，gcc 直接编译，不碰硬件
tools/
  ota_pack.py       给 bin 加头、算 CRC
  ota_send.py       通过串口推送固件
```

纯逻辑（`image` / `metadata` / `boot_decision`）不引用任何寄存器，通过 `storage_if_t` 回调访问 Flash，所以能在 PC 上跑单元测试。这是刻意的分层——**硬件相关的部分越薄，能测的部分就越多**。
