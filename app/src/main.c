#include "flash_if.h"
#include "flash_storage.h"
#include "image.h"
#include "metadata.h"
#include "stm32f103_regs.h"
#include "system.h"
#include "uart.h"

/* 链接脚本导出的符号：本镜像向量表的真实地址 */
extern uint32_t _image_base;

/* 反推自己在哪个槽：镜像头占 32 字节，减掉就是槽起始地址 */
static uint32_t self_slot(void)
{
    uint32_t base = (uint32_t)&_image_base - IMAGE_HEADER_SIZE;
    return (base == SLOT_B_BASE) ? SLOT_B : SLOT_A;
}

/* 粗略延时，不追求精度：64 MHz 下 delay_cycles 每圈约 4 个周期 */
static void delay_seconds(uint32_t seconds)
{
    while (seconds-- > 0u) {
        delay_cycles(SYSTEM_CLOCK_HZ / 4u);
    }
}

int main(void)
{
    /*
     * 第一件事：把向量表指到自己身上。
     * 这一步必须在开任何中断之前完成，否则第一个中断就会跳回
     * Bootloader 的向量表——那里已经不属于这次运行了。
     */
    SCB_VTOR = (uint32_t)&_image_base;
    dsb();
    isb();

    clock_init();
    led_init();
    uart_init(115200u);

    const uint32_t slot = self_slot();

    uart_puts("\r\n[app] slot ");
    uart_putc(slot == SLOT_B ? 'B' : 'A');
    uart_puts(" base=");
    uart_puthex((uint32_t)&_image_base - IMAGE_HEADER_SIZE, 8);
    uart_puts("\r\n");

#if defined(DEMO_CRASH)
    /*
     * 演示用坏固件：镜像完整、CRC 也过，但一跑就死。
     * 这正是 Bootloader 判断不了的情况——它只能验镜像，验不了行为。
     * 所以必须由应用自己"报到"，报不到就回滚。
     */
    uart_puts("[app] DEMO_CRASH: dying on purpose\r\n");
    delay_seconds(1u);
    *(volatile uint32_t *)0xFFFFFFF0u = 0u; /* 写非法地址，触发 HardFault */
    for (;;) {
    }
#endif

    /*
     * 等到外设都初始化完、心跳跑起来，再确认。
     *
     * 确认太早等于没确认：如果在 main 第一行就 confirm，后面初始化失败
     * 也不会回滚，用户拿到一块每次开机都卡死的板子。
     * 太晚也不好——用户可能在这期间断电，白白多耗一次重试。
     */
    delay_seconds(3u);

    if (flash_unlock() == 0) {
        int rc = meta_confirm_boot(&g_flash_storage, slot);
        flash_lock();

        if (rc == 0) {
            uart_puts("[app] boot confirmed, this slot is now active\r\n");
        } else {
            uart_puts("[app] confirm failed, will be retried next boot\r\n");
        }
    }

    uint32_t tick = 0u;
    for (;;) {
        led_toggle();
        delay_cycles(4000000u);

        tick++;
        if ((tick % 4u) == 0u) {
            uart_puts("[app] heartbeat ");
            uart_putu(tick / 4u);
            uart_puts("\r\n");
        }
    }
}
