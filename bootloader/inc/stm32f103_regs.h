#ifndef STM32F103_REGS_H
#define STM32F103_REGS_H

#include <stdint.h>

/*
 * STM32F103C8T6 最小寄存器定义。
 *
 * 不用 HAL/StdPeriph，一是 Bootloader 只有 8 KB，二是这些项目想证明的
 * 恰恰是"知道寄存器在哪、每一位是什么意思"。只定义用到的部分。
 */

#define REG32(addr) (*(volatile uint32_t *)(addr))

/* ---- RCC ---- */
#define RCC_BASE      0x40021000u
#define RCC_CR        REG32(RCC_BASE + 0x00u)
#define RCC_CFGR      REG32(RCC_BASE + 0x04u)
#define RCC_APB2ENR   REG32(RCC_BASE + 0x18u)

#define RCC_CR_HSION      (1u << 0)
#define RCC_CR_HSIRDY     (1u << 1)
#define RCC_CR_PLLON      (1u << 24)
#define RCC_CR_PLLRDY     (1u << 25)
#define RCC_APB2ENR_IOPAEN   (1u << 2)
#define RCC_APB2ENR_IOPCEN   (1u << 4)
#define RCC_APB2ENR_USART1EN (1u << 14)

/* ---- GPIO ---- */
#define GPIOA_BASE    0x40010800u
#define GPIOA_CRL     REG32(GPIOA_BASE + 0x00u)
#define GPIOA_CRH     REG32(GPIOA_BASE + 0x04u)
#define GPIOA_ODR     REG32(GPIOA_BASE + 0x0Cu)

#define GPIOC_BASE    0x40011000u
#define GPIOC_CRH     REG32(GPIOC_BASE + 0x04u)
#define GPIOC_ODR     REG32(GPIOC_BASE + 0x0Cu)
#define GPIOC_ODR_LED (1u << 13)

/* ---- USART1 ---- */
#define USART1_BASE   0x40013800u
#define USART1_SR     REG32(USART1_BASE + 0x00u)
#define USART1_DR     REG32(USART1_BASE + 0x04u)
#define USART1_BRR    REG32(USART1_BASE + 0x08u)
#define USART1_CR1    REG32(USART1_BASE + 0x0Cu)

#define USART_SR_RXNE (1u << 5)
#define USART_SR_TXE  (1u << 7)
#define USART_CR1_RE  (1u << 2)
#define USART_CR1_TE  (1u << 3)
#define USART_CR1_UE  (1u << 13)

/* ---- FLASH ---- */
#define FLASH_BASE    0x40022000u
#define FLASH_ACR     REG32(FLASH_BASE + 0x00u)
#define FLASH_KEYR    REG32(FLASH_BASE + 0x04u)
#define FLASH_SR      REG32(FLASH_BASE + 0x0Cu)
#define FLASH_CR      REG32(FLASH_BASE + 0x10u)
#define FLASH_AR      REG32(FLASH_BASE + 0x14u)

#define FLASH_SR_BSY       (1u << 0)
#define FLASH_SR_PGERR     (1u << 2)
#define FLASH_SR_WRPRTERR  (1u << 4)
#define FLASH_SR_EOP       (1u << 5)
#define FLASH_CR_PG        (1u << 0)
#define FLASH_CR_PER       (1u << 1)
#define FLASH_CR_STRT      (1u << 6)
#define FLASH_CR_LOCK      (1u << 7)

/* ---- Cortex-M3 系统控制块 ---- */
#define SCB_VTOR      REG32(0xE000ED08u)
#define SCB_AIRCR     REG32(0xE000ED0Cu)
#define SYST_CSR      REG32(0xE000E010u)
#define SYST_RVR      REG32(0xE000E014u)
#define SYST_CVR      REG32(0xE000E018u)

/* ---- 内核内联操作 ----
 * cpsid/cpsie 里的 "memory" clobber 不能省：编译器不知道中断被关了，
 * 否则会把中断里访问的变量缓存到寄存器里。
 */
static inline void irq_disable(void) { __asm__ volatile("cpsid i" ::: "memory"); }
static inline void irq_enable(void)  { __asm__ volatile("cpsie i" ::: "memory"); }
static inline void dsb(void) { __asm__ volatile("dsb" ::: "memory"); }
static inline void isb(void) { __asm__ volatile("isb" ::: "memory"); }

static inline void set_msp(uint32_t sp)
{
    __asm__ volatile("msr msp, %0" ::"r"(sp));
}

#endif /* STM32F103_REGS_H */
