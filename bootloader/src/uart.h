#ifndef UART_H
#define UART_H

#include <stdint.h>

/*
 * USART1 轮询收发。不用中断和 DMA：Bootloader 里一切从简，
 * 少一个中断源就少一个跳转后残留的可能。
 */

void uart_init(uint32_t baud);

void uart_putc(char c);
void uart_puts(const char *s);

/* 打印 32 位十六进制，digits 为最少位数（补 0） */
void uart_puthex(uint32_t value, uint32_t digits);
void uart_putu(uint32_t value);

/* 读到返回字节，没数据返回 -1 */
int uart_getc(void);

/* 阻塞等待一个字节，超时（约 timeout_ms）返回 -1 */
int uart_getc_timeout(uint32_t timeout_ms);

#endif /* UART_H */
