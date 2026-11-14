# 里程碑拆解

按每周 10–15 小时投入估算，总计约 11 周。每个里程碑都有**可演示的产出**，不存在"写了三周还看不出在干嘛"的阶段。

原则：**先让最简单的东西在真板子上跑起来，再往上加复杂度**。所以 M2 只做"跳转"，不做升级；M3 只做"存得稳"，不做传输。每一层都能独立验证，出错时知道是哪一层的锅。

---

## M0 · 骨架与工具链（第 1 周）

**目标**：仓库能编译出两个 bin，CI 绿。

任务：
- [ ] 目录结构、`.gitignore`、Makefile（ARM 交叉编译 + 主机测试两个目标）
- [ ] 启动文件 `startup_stm32f103.c`：向量表、Reset_Handler、`.data`/`.bss` 初始化
- [ ] 链接脚本 `bootloader.ld`（8 KB @ 0x08000000）与 `app.ld`（24 KB @ SLOT_BASE 可配）
- [ ] 最小 `main()`：只在 PC13 上闪灯，确认时钟和 GPIO 正常
- [ ] `tools/size_report.py`：输出 bin 大小、槽占用率，防止固件悄悄超限
- [ ] GitHub Actions：`arm-none-eabi-gcc` 编 ARM 目标 + `gcc` 编主机测试

**验收**：`make` 产出 `build/bootloader.bin` 和 `build/app_a.bin`；CI 两个 job 全绿；烧进去 LED 以 1 Hz 闪烁。

**风险**：板子还没到货 → 先用 QEMU 的 `stm32vldiscovery` 验证能跑（Flash 布局不同，只验启动流程）。

---

## M1 · 镜像格式与打包工具（第 2 周）

**目标**：能从任意 bin 生成带头的合法镜像，并独立校验。

任务：
- [ ] `shared/image.h`：32 字节头结构，字段偏移用 `_Static_assert` 钉死
- [ ] `shared/crc32.c`：查表 CRC-32（IEEE 802.3，多项式 0xEDB88320）
- [ ] `bootloader/src/image.c`：`image_validate()` 逐项校验顺序（magic → header_crc → load_addr → payload_crc）
- [ ] `tools/ota_pack.py`：读 bin、填头、算 CRC、输出 `.img`
- [ ] `tools/ota_verify.py`：独立实现一遍校验，交叉验证 C 和 Python 结果一致
- [ ] `host_tests/test_image.c`：覆盖正常镜像、magic 错、CRC 错、load_addr 不匹配、截断镜像
- [ ] `host_tests/test_crc.c`：用标准测试向量 `"123456789"` → `0xCBF43926`

**验收**：`python tools/ota_pack.py build/app_a.bin --load-addr 0x08003000` 生成可被 `ota_verify.py` 校验通过的镜像；主机测试 15+ 用例全过；故意改一个字节能被检出。

**简历素材**："设计 32 字节镜像头，先校验头再信任头内字段，避免头被写坏时误信 payload CRC。"

---

## M2 · Bootloader 跳转（第 3–4 周）

**目标**：上电直接跳进 app，app 的 LED 闪、串口有输出。

任务：
- [ ] `flash_if.c`：`FLASH->KEYR` 解锁序列、页擦除、半字编程、写入后回读比对
- [ ] `boot_jump.c`：关全局中断 → 关 SysTick → 关 USART → `SCB->VTOR = app_base` → `__set_MSP(app_stack)` → 跳 `Reset_Handler`
- [ ] `boot_decision.c`：纯逻辑状态机，返回"跳哪个槽 / 进升级模式 / 回滚"
- [ ] app 侧：重定位向量表（`SCB->VTOR` 在 `SystemInit` 里设成 `SLOT_BASE`）
- [ ] 串口打印启动日志：读到什么元数据、选了哪个槽、为什么

**验收**：`make flash` 后上电，串口打印 `jump -> slot A @ 0x08003000`，接着 app 打印自己的心跳；撕掉 app 区（全 0xFF）后自动进升级模式而不是死在 HardFault。

**关键陷阱**（这里会卡最久，先记下来）：
- 忘设 VTOR → app 里一开中断就跑回 Bootloader 的向量表
- 忘关 SysTick → 进 app 后 SysTick 中断打到 Bootloader 已失效的地址
- 栈没重设 → app 用着 Bootloader 的栈，爆栈位置极其诡异

**简历素材**："处理 Cortex-M3 三级跳转前置条件（VTOR/MSP/中断屏蔽），排查并解决了 SysTick 残留导致的跳转后 HardFault。"

---

## M3 · Flash 与元数据双备份（第 5–6 周）

**目标**：元数据反复写入不掉、掉电不丢。

任务：
- [ ] `metadata.c`：序列化/反序列化，`storage_if_t` 回调注入（便于主机测试）
- [ ] 双备份提交：擦旧份 → 写新份 → 回读校验
- [ ] `metadata_load()`：两份都读，取 `seq` 最大且 CRC 正确的
- [ ] 主机测试：模拟掉电（在第 N 次写操作后中断），验证仍能恢复到某个一致状态
- [ ] 压力测试：连续提交 1000 次，检查 Flash 擦写计数和 seq 回绕

**验收**：主机测试里用内存模拟 Flash，注入"写到一半断电"20 种时机，全部能恢复到有效元数据；真板上连续提交 500 次不死。

**关键陷阱**：Flash 写入是"1 变 0"，写之前必须擦；擦除期间不能读同一块；HAL 的 `FLASH_EraseInitTypeDef` 要正确设置 `PageAddress` 和 `NbPages`。

**简历素材**："用双备份 + 序号单调递增实现掉电安全的元数据存储，在 20 种断电时机下验证均可恢复，无需额外日志或文件系统。"

---

## M4 · UART 升级协议（第 7–8 周）

**目标**：PC 端一条命令把固件推进设备。

任务：
- [ ] `uart.c`：轮询收发 + 环形缓冲，超时机制
- [ ] `ota_proto.c`：帧解析状态机（收集 SOF → CMD → SEQ → LEN → PAYLOAD → CRC）
- [ ] 命令实现：HELLO / BEGIN / DATA / END / ABORT / STATUS
- [ ] 写 Flash：按 1 KB 页对齐擦写，最后不足一页补 0xFF
- [ ] `tools/ota_send.py`：pyserial 实现，带进度条、超时重传、每帧 CRC
- [ ] 端到端：`python tools/ota_send.py --port COM3 --image build/app_b.img`

**验收**：115200 下推 6 KB 固件约 2 秒完成；故意拔线再插，能续传或明确报错；推一个 CRC 错的镜像，设备拒绝并保持原固件可启动。

**关键陷阱**：帧解析要能处理半包、粘包（USB 转串口的 chunk 边界不可信）；LEN 字段要防越界（收到 0xFFFF 不能直接 malloc）。

**简历素材**："设计带独立 CRC 和显式偏移的升级协议，支持断线重传；帧解析器按字节推进状态机，可容忍任意分片。"

---

## M5 · A/B 切换与自动回滚（第 9–10 周）

**目标**：故意推一个会崩的固件，设备自己退回来。

任务：
- [ ] 提交升级：写 `pending_slot`、`retry = 0`，不改 `active`
- [ ] 启动计数：跳转前 `retry++` 写回
- [ ] app 侧 `boot_confirm()`：启动 3 秒后（外设初始化完成）提交确认
- [ ] 回滚：`retry >= 3` 时切回旧槽、清 pending
- [ ] 演示用"坏固件"：读到某个魔数故意 HardFault，或直接死循环

**验收**：推入坏固件 → 设备重启 3 次 → 自动回到旧固件并串口打印 `rollback to slot A`；推入好固件 → 重启 1 次即确认。

**关键陷阱**：`boot_confirm()` 不能太早（外设还没初始化完就确认，之后崩了不会回滚），也不能太晚（用户可能在这期间断电）。3 秒是折中。

**简历素材**："实现启动确认 + 重试计数的回滚机制，新固件连续 3 次未确认即自动回退，实测可抵御固件崩溃、死循环、上电掉电三类故障。"

---

## M6 · 打磨与发布（第 11 周）

**目标**：别人能按 README 复现，你能对着它讲 20 分钟。

任务：
- [ ] README：架构图、接线图、5 分钟快速开始、演示 GIF
- [ ] `docs/design.md` 补实测数据（升级耗时、Flash 占用、擦写次数）
- [ ] 写一篇技术博客：讲清"为什么需要双备份"和"VTOR 那个坑"
- [ ] 录制 90 秒演示视频：正常升级 + 坏固件回滚
- [ ] 打 tag `v1.0`，Release 里附 `app_a.img` / `app_b.img`

**验收**：找个同学，只给 README，他能在 30 分钟内跑通升级流程。

---

## 时间线与取舍

| 周 | 里程碑 | 若时间不足可砍掉的部分 |
| --- | --- | --- |
| 1 | M0 | — |
| 2 | M1 | `ota_verify.py`（C 侧测试已覆盖） |
| 3–4 | M2 | — |
| 5–6 | M3 | 1000 次压力测试改为 200 次 |
| 7–8 | M4 | 断线续传（改为整体重传） |
| 9–10 | M5 | — |
| 11 | M6 | 博客可以延后，但**演示视频必须有** |

**M2 和 M5 不能砍**。M2 是整个项目的地基，M5 是"OTA"三个字里唯一值钱的部分——只能升级不能回滚的 OTA，工业界不敢用，面试官也清楚这一点。

## 硬件清单

| 件 | 型号 | 参考价 |
| --- | --- | --- |
| 最小系统板 | STM32F103C8T6（Blue Pill） | ¥15–25 |
| 下载器 | ST-Link V2 | ¥15 |
| USB 转串口 | CH340 | ¥5 |

板子到手前，M0–M1 的全部工作和 M2–M5 的逻辑层都可以在 PC 上完成并测试。
