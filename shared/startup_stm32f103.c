/*
 * 启动文件：向量表 + Reset_Handler。
 *
 * 用 C 写而不是 .s 汇编，是因为 Cortex-M 的向量表本质上就是一个函数指针
 * 数组，C 的指定初始化器比汇编的 .word 列表更不容易错位。真正必须用汇编
 * 的只有 msr msp 那一条（见 stm32f103_regs.h 的 set_msp）。
 */

#include <stdint.h>

extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;

extern int main(void);

void Reset_Handler(void) __attribute__((noreturn));

void Default_Handler(void)
{
    /*
     * 真机上进入这里意味着有未处理的中断或异常。
     * 死循环是刻意的：让调试器能在这里停下来看到是哪个异常。
     */
    for (;;) {
    }
}

/*
 * 除 Reset 外的所有入口默认都是弱符号，应用可以定义同名强符号覆盖
 * （比如 app 里实现 SysTick_Handler 接管 SysTick）。
 */
#define WEAK_HANDLER(name) \
    void name(void) __attribute__((weak, alias("Default_Handler")))

WEAK_HANDLER(NMI_Handler);
WEAK_HANDLER(HardFault_Handler);
WEAK_HANDLER(MemManage_Handler);
WEAK_HANDLER(BusFault_Handler);
WEAK_HANDLER(UsageFault_Handler);
WEAK_HANDLER(SVC_Handler);
WEAK_HANDLER(DebugMon_Handler);
WEAK_HANDLER(PendSV_Handler);
WEAK_HANDLER(SysTick_Handler);

typedef void (*vector_fn)(void);

__attribute__((section(".isr_vector"), used))
const vector_fn g_vectors[64] = {
    [0] = (vector_fn)(uintptr_t)&_estack, /* 复位后硬件自动载入 MSP */
    [1] = Reset_Handler,
    [2] = NMI_Handler,
    [3] = HardFault_Handler,
    [4] = MemManage_Handler,
    [5] = BusFault_Handler,
    [6] = UsageFault_Handler,
    [11] = SVC_Handler,
    [12] = DebugMon_Handler,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    /* 外设中断 16..63 统一走 Default_Handler，本项目暂不使用 */
    [16 ... 63] = Default_Handler,
};

void Reset_Handler(void)
{
    /* .data 段：初值存在 Flash，运行前要搬到 RAM */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* .bss 段：清零。C 标准要求静态变量初值为 0，硬件不会帮你做 */
    for (dst = &_sbss; dst < &_ebss;) {
        *dst++ = 0u;
    }

    (void)main();

    for (;;) {
    }
}
