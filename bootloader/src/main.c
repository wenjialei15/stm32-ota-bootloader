#include "boot_decision.h"
#include "flash_if.h"
#include "flash_storage.h"
#include "image.h"
#include "metadata.h"
#include "ota_proto.h"
#include "stm32f103_regs.h"
#include "system.h"
#include "uart.h"

#include <string.h>

/* ---- 跳转 ---- */

/*
 * 跳到某个槽里的应用。
 *
 * 四件事缺一不可，顺序也不能乱：
 *   1. 关全局中断   —— 否则中断可能打到还没准备好的 app 向量表
 *   2. 关 SysTick    —— 它是唯一会自己触发的异常源
 *   3. 设 VTOR       —— Cortex-M3 复位后 VTOR 指向 0x08000000，
 *                       不重设的话 app 里所有中断都会跳回 Bootloader
 *   4. 设 MSP        —— app 有自己的栈顶，沿用 Bootloader 的会踩到数据
 */
static void boot_jump(uint32_t slot_base) __attribute__((noreturn));

static void boot_jump(uint32_t slot_base)
{
    const uint32_t app_base = slot_base + IMAGE_HEADER_SIZE;

    /* 向量表第 0 项是初始 MSP，第 1 项是 Reset_Handler */
    const uint32_t app_sp = *(volatile const uint32_t *)app_base;
    const uint32_t app_entry = *(volatile const uint32_t *)(app_base + 4u);

    irq_disable();
    SYST_CSR = 0u;
    SYST_RVR = 0u;
    SYST_CVR = 0u;
    USART1_CR1 = 0u;

    SCB_VTOR = app_base;

    /* 屏障：确保 VTOR 生效、中断真的关掉了，再动栈指针 */
    dsb();
    isb();

    set_msp(app_sp);

    ((void (*)(void))app_entry)();

    /* 正常情况下永远到不了这里 */
    for (;;) {
    }
}

/* ---- 日志 ---- */

static void log_meta(const boot_meta_t *meta)
{
    uart_puts("  meta: seq=");
    uart_putu(meta->seq);
    uart_puts(" active=");
    uart_putc(meta->active_slot == SLOT_B ? 'B' : 'A');
    uart_puts(" pending=");
    if (meta->pending_slot == META_NONE) {
        uart_puts("none");
    } else {
        uart_putc(meta->pending_slot == SLOT_B ? 'B' : 'A');
    }
    uart_puts(" retry=");
    uart_putu(meta->retry_count);
    uart_puts("\r\n");
}

/* ---- 升级模式 ---- */

/*
 * 这些缓冲区放 static 而不是栈上：parser + session + frame 加起来约 3 KB，
 * 放栈上会让 20 KB 的 RAM 一下子紧张起来，而且 Bootloader 的栈没有保护。
 */
static ota_parser_t g_parser;
static ota_session_t g_session;
static ota_frame_t g_frame;
static uint8_t g_resp[OTA_MAX_PAYLOAD + 8];

static void system_reset(void) __attribute__((noreturn));

static void system_reset(void)
{
    /* AIRCR：VECTKEY 0x05FA 必须写在低 16 位之外，否则写入被忽略 */
    SCB_AIRCR = (0x05FAu << 16) | (1u << 2); /* SYSRESETREQ */
    for (;;) {
    }
}

static void ota_loop(int first_byte) __attribute__((noreturn));

static void ota_loop(int first_byte)
{
    memset(&g_parser, 0, sizeof(g_parser));
    ota_parser_reset(&g_parser);
    ota_session_init(&g_session);

    uart_puts("  ota: ready at 115200 8N1\r\n");

    int pending = first_byte;

    for (;;) {
        int c = pending;
        pending = -1;
        if (c < 0) {
            c = uart_getc_timeout(400u);
        }
        if (c < 0) {
            led_toggle(); /* 空闲闪灯，表示还活着而不是卡死 */
            continue;
        }

        if (ota_parser_feed(&g_parser, (uint8_t)c, &g_frame) != 1) {
            continue;
        }

        uint32_t n = ota_session_handle(&g_session, &g_frame, &g_flash_storage, g_resp,
                                        sizeof(g_resp));
        for (uint32_t i = 0; i < n; i++) {
            uart_putc((char)g_resp[i]);
        }

        /*
         * END 成功就重启。不要在这里手动跳转——重启后会重新走一遍
         * 元数据决策，那条路径已经测过了，没必要再造一条。
         */
        if (g_frame.cmd == OTA_CMD_END && n >= 6u && g_resp[5] == (uint8_t)OTA_OK) {
            uart_puts("\r\n  ota: accepted, rebooting\r\n");
            delay_cycles(40000000u);
            system_reset();
        }
    }
}

int main(void)
{
    clock_init();
    led_init();
    uart_init(115200u);

    uart_puts("\r\n[ota-boot] " __DATE__ " " __TIME__ "\r\n");

    boot_meta_t meta;
    int meta_ok = (meta_load(&g_flash_storage, &meta) == 0);

    if (meta_ok) {
        log_meta(&meta);
    } else {
        uart_puts("  meta: unreadable, fall back to image headers\r\n");
    }

    const uint8_t *slots[2] = {
        (const uint8_t *)SLOT_A_BASE,
        (const uint8_t *)SLOT_B_BASE,
    };

    boot_plan_t plan = boot_plan_make(&meta, meta_ok, slots);

    uart_puts("  plan: slot ");
    uart_putc(plan.slot == SLOT_B ? 'B' : 'A');
    uart_puts(" reason=");
    uart_puts(boot_reason_str(plan.reason));
    uart_puts("\r\n");

    /*
     * 首次上电（元数据区还是空白的 0xFF）：把找到的那个槽记为 active，
     * 让元数据真正落地。
     *
     * 不这么做的话，设备每次启动都走 "no meta" 分支——功能上没毛病，
     * 但应用那边的 confirm 会一直失败，STATUS 也永远是空的，
     * 排查问题时会以为坏了。
     */
    if (!meta_ok && plan.action == BOOT_ACTION_JUMP) {
        meta_defaults(&meta);
        meta.active_slot = plan.slot;
        meta.version = 0u;
        if (meta_commit(&g_flash_storage, &meta) == 0) {
            uart_puts("  meta: initialised from slot ");
            uart_putc(plan.slot == SLOT_B ? 'B' : 'A');
            uart_puts("\r\n");
        }
    }

    /*
     * 试运行计数必须在跳转之前落盘。
     * 如果放到 app 里做，app 在第一行就崩了的话这个数永远加不上去，
     * 设备会无限重启在新固件上。
     *
     * 回滚时还要把 pending 清干净，否则元数据一直停在「待确认」，
     * 虽然功能上不影响启动，但状态是矛盾脏的。
     */
    if (meta_ok && (plan.bump_retry != 0 || plan.clear_pending != 0)) {
        if (plan.bump_retry != 0) {
            meta.retry_count += 1u;
        }
        if (plan.clear_pending != 0) {
            meta.pending_slot = META_NONE;
            meta.active_slot = plan.slot;
            meta.retry_count = 0u;
        }
        if (meta_commit(&g_flash_storage, &meta) != 0) {
            uart_puts("  warn: failed to persist boot state\r\n");
        }
    }

    /*
     * 有可用固件，但不代表永远不升级了。
     * 开机给主机一个 300 ms 的窗口：这期间收到任何字节就留在 Bootloader。
     *
     * 没有这个窗口，设备一装上正常固件就再也进不去升级模式了——
     * 这是很多人第一次做 OTA 时会漏掉的一步。
     */
    if (plan.action == BOOT_ACTION_JUMP) {
        int c = uart_getc_timeout(300u);
        if (c < 0) {
            uart_puts("  jump -> ");
            uart_puthex(slot_base_of(plan.slot), 8);
            uart_puts("\r\n");

            /* 跳之前闪一下灯，肉眼确认走到了这一步 */
            led_on();
            delay_cycles(4000000u);
            led_off();

            boot_jump(slot_base_of(plan.slot));
        }

        uart_puts("  host detected, staying in bootloader\r\n");
        flash_unlock();
        ota_loop(c);
    }

    /* 两个槽都没有可用镜像：直接进升级模式等主机救场 */
    uart_puts("  no valid image, entering upgrade mode\r\n");
    flash_unlock();
    ota_loop(-1);
}
