#ifndef FLASH_IF_H
#define FLASH_IF_H

#include <stdint.h>

/*
 * STM32F103 内部 Flash 操作。
 *
 * 三条硬件事实决定了这里的接口形状：
 *   1. 擦除最小单位是 1 KB 页
 *   2. 编程最小单位是 16 位半字
 *   3. 只能把 1 写成 0，想改回 1 必须整页擦除
 */

int flash_unlock(void);
void flash_lock(void);

/* 擦除 addr 所在的 1 KB 页，addr 必须页对齐 */
int flash_erase_page(uint32_t addr);

/* 半字编程，len 必须是偶数；写完逐半字回读比对 */
int flash_write(uint32_t addr, const void *data, uint32_t len);

/* Flash 是内存映射的，直接读即可 */
int flash_read(uint32_t addr, void *buf, uint32_t len);

#endif /* FLASH_IF_H */
