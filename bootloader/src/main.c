#include "boot_decision.h"
#include "flash_if.h"
#include "image.h"
#include "metadata.h"
#include "stm32f103_regs.h"
#include "system.h"
#include "uart.h"

#include <string.h>

/* ---- 把 flash_if 适配成 metadata 需要的存储接口 ---- */

static int storage_read(uint32_t addr, void *buf, uint32_t len)
{
    return flash_read(addr, buf, len);
}

static int storage_erase(uint32_t addr, uint32_t len)
{
    for (uint32_t off = 0; off < len; off += FLASH_PAGE_SIZE) {
        if (flash_erase_page(addr + off) != 0) {
            return -1;
        }
    }
    return 0;
}

static int storage_write(uint32_t addr, const void *buf, uint32_t len)
{
    return flash_write(addr, buf, len);
}

static const storage_if_t g_storage = {
    storage_read,
    storage_erase,
    storage_write,
};

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

int main(void)
{
    clock_init();
    led_init();
    uart_init(115200u);

    uart_puts("\r\n[ota-boot] " __DATE__ " " __TIME__ "\r\n");

    boot_meta_t meta;
    int meta_ok = (meta_load(&g_storage, &meta) == 0);

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
     * 试运行计数必须在跳转之前落盘。
     * 如果放到 app 里做，app 在第一行就崩了的话这个数永远加不上去，
     * 设备会无限重启在新固件上。
     */
    if (meta_ok && plan.bump_retry != 0) {
        meta.retry_count += 1u;
        if (meta_commit(&g_storage, &meta) != 0) {
            uart_puts("  warn: failed to persist retry counter\r\n");
        }
    }

    if (plan.action == BOOT_ACTION_JUMP) {
        uart_puts("  jump -> ");
        uart_puthex(slot_base_of(plan.slot), 8);
        uart_puts("\r\n");

        /* 跳之前闪一下灯，肉眼确认走到了这一步 */
        led_on();
        delay_cycles(4000000u);
        led_off();

        boot_jump(slot_base_of(plan.slot));
    }

    /*
     * 升级模式。M4 会把这里替换成协议状态机，当前版本只把状态打出来，
     * 方便在没有 PC 端工具时确认设备活着。
     */
    uart_puts("  recovery mode: no valid image, waiting on UART1\r\n");
    flash_unlock();

    for (;;) {
        led_toggle();
        delay_cycles(2000000u);
        int c = uart_getc();
        if (c >= 0) {
            uart_putc((char)c);
        }
    }
}
