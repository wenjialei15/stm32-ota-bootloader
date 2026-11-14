#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

/*
 * CRC-32/ISO-HDLC（IEEE 802.3），多项式 0xEDB88320（反射），
 * 初值 0xFFFFFFFF，输出取反。
 *
 * 标准测试向量：crc32("123456789", 9) == 0xCBF43926
 *
 * 分成 begin/update/final 三段是为了流式计算：校验 24 KB 固件时
 * 不需要把它整块读进 RAM（只有 20 KB）。
 */

uint32_t crc32_begin(void);
uint32_t crc32_update(uint32_t state, const void *data, size_t len);
uint32_t crc32_final(uint32_t state);

/* 一次性计算，等价于 final(update(begin(), data, len)) */
uint32_t crc32(const void *data, size_t len);

#endif /* CRC32_H */
