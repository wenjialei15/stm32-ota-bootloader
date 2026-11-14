#include "crc32.h"

#define CRC32_POLY 0xEDB88320u

static uint32_t s_table[256];
static int s_table_ready = 0;

/*
 * 表在首次使用时生成，而不是编译期常量数组——省 1 KB Flash，
 * 代价只是第一次调用多花约 2048 次移位。Bootloader 只有 8 KB，
 * 这个取舍值得。
 *
 * 单线程环境下无需加锁：即使两个中断同时进来，生成的表内容也一致。
 */
static void build_table(void)
{
    for (uint32_t i = 0; i < 256u; i++) {
        uint32_t c = i;
        for (int bit = 0; bit < 8; bit++) {
            c = (c & 1u) ? (CRC32_POLY ^ (c >> 1)) : (c >> 1);
        }
        s_table[i] = c;
    }
    s_table_ready = 1;
}

uint32_t crc32_begin(void)
{
    if (!s_table_ready) {
        build_table();
    }
    return 0xFFFFFFFFu;
}

uint32_t crc32_update(uint32_t state, const void *data, size_t len)
{
    if (!s_table_ready) {
        build_table();
    }

    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = state;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ s_table[(crc ^ p[i]) & 0xFFu];
    }
    return crc;
}

uint32_t crc32_final(uint32_t state)
{
    return state ^ 0xFFFFFFFFu;
}

uint32_t crc32(const void *data, size_t len)
{
    return crc32_final(crc32_update(crc32_begin(), data, len));
}
