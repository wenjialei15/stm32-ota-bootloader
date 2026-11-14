#include "image.h"
#include "stm32f103_regs.h"
#include "system.h"
#include "uart.h"

/* 链接脚本导出的符号：本镜像向量表的真实地址 */
extern uint32_t _image_base;

/*
 * M5 会在这里接入启动确认：等到外设都初始化完、确认自己没崩，
 * 才调用 boot_confirm() 把 pending 清掉。现在先留占位。
 */
static void boot_confirm_hook(void)
{
    /* TODO(M5): 写元数据确认本次启动成功 */
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

    uart_puts("\r\n[app] slot base=");
    uart_puthex((uint32_t)&_image_base - IMAGE_HEADER_SIZE, 8);
    uart_puts(" running\r\n");

    boot_confirm_hook();

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
