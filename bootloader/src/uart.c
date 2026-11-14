#include "uart.h"
#include "stm32f103_regs.h"
#include "system.h"

void uart_init(uint32_t baud)
{
    RCC_APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

    /*
     * PA9  = TX，复用推挽输出 50 MHz -> 0xB
     * PA10 = RX，浮空输入          -> 0x4
     * 都在 GPIOA_CRH（引脚 8..15）里，PA9 占 bit[7:4]，PA10 占 bit[11:8]。
     */
    GPIOA_CRH = (GPIOA_CRH & ~(0xFFu << 4)) | (0xBu << 4) | (0x4u << 8);

    /* USART 的波特率分频：BRR = fPCLK / baud，四舍五入 */
    if (baud == 0u) {
        baud = 115200u;
    }
    USART1_BRR = (SYSTEM_CLOCK_HZ + baud / 2u) / baud;

    USART1_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void uart_putc(char c)
{
    uint32_t guard = 100000u;
    while (((USART1_SR & USART_SR_TXE) == 0u) && (--guard != 0u)) {
    }
    USART1_DR = (uint32_t)(uint8_t)c;
}

void uart_puts(const char *s)
{
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

void uart_puthex(uint32_t value, uint32_t digits)
{
    static const char kHex[] = "0123456789ABCDEF";
    if (digits == 0u || digits > 8u) {
        digits = 8u;
    }
    uart_puts("0x");
    for (uint32_t i = digits; i > 0u; i--) {
        uint32_t nibble = (value >> ((i - 1u) * 4u)) & 0xFu;
        uart_putc(kHex[nibble]);
    }
}

void uart_putu(uint32_t value)
{
    char buf[12];
    uint32_t n = 0;

    if (value == 0u) {
        uart_putc('0');
        return;
    }
    while (value > 0u && n < sizeof(buf)) {
        buf[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n > 0u) {
        uart_putc(buf[--n]);
    }
}

int uart_getc(void)
{
    if ((USART1_SR & USART_SR_RXNE) == 0u) {
        return -1;
    }
    return (int)(USART1_DR & 0xFFu);
}

int uart_getc_timeout(uint32_t timeout_ms)
{
    /* 64 MHz 下每次循环约 4 个周期，粗略换算成毫秒 */
    uint32_t budget = timeout_ms * (SYSTEM_CLOCK_HZ / 4000u);
    while (budget-- > 0u) {
        int c = uart_getc();
        if (c >= 0) {
            return c;
        }
    }
    return -1;
}
