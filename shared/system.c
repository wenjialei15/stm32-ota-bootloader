#include "stm32f103_regs.h"
#include "system.h"

void clock_init(void)
{
    /* 1. 打开内部 RC 并等就绪（复位后本来就是开的，但不要假设） */
    RCC_CR |= RCC_CR_HSION;
    while ((RCC_CR & RCC_CR_HSIRDY) == 0u) {
    }

    /*
     * 2. 先设 Flash 等待周期，再升频。
     *    顺序反了会在 64 MHz 下取指出错——这种错误表现为随机 HardFault，
     *    极难定位。
     */
    FLASH_ACR = (FLASH_ACR & ~0x7u) | 0x2u; /* 2 wait states */
    FLASH_ACR |= (1u << 4);                /* 预取缓冲 */

    /* 3. PLL: HSI(8 MHz) / 2 * 16 = 64 MHz */
    RCC_CFGR &= ~(1u << 16);                                  /* PLLSRC = HSI/2 */
    RCC_CFGR = (RCC_CFGR & ~(0xFu << 18)) | (0xEu << 18);     /* PLLMUL = x16 */
    RCC_CR |= RCC_CR_PLLON;
    while ((RCC_CR & RCC_CR_PLLRDY) == 0u) {
    }

    /* 4. 把系统时钟切到 PLL */
    RCC_CFGR = (RCC_CFGR & ~0x3u) | 0x2u;
    while (((RCC_CFGR >> 2) & 0x3u) != 0x2u) {
    }
}

void led_init(void)
{
    RCC_APB2ENR |= RCC_APB2ENR_IOPCEN;
    /* PC13 输出推挽 2 MHz：CRH 的 bit[23:20] 每 4 位配置一个引脚 */
    GPIOC_CRH = (GPIOC_CRH & ~(0xFu << 20)) | (0x2u << 20);
    led_off();
}

/* Blue Pill 的 LED 是灌电流接法：低电平点亮 */
void led_on(void) { GPIOC_ODR &= ~GPIOC_ODR_LED; }
void led_off(void) { GPIOC_ODR |= GPIOC_ODR_LED; }
void led_toggle(void) { GPIOC_ODR ^= GPIOC_ODR_LED; }

void delay_cycles(uint32_t cycles)
{
    for (volatile uint32_t i = 0; i < cycles; i++) {
        __asm__ volatile("nop");
    }
}
