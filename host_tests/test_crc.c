#include "test_support.h"
#include "crc32.h"

void run_crc_tests(void);

void run_crc_tests(void)
{
    printf("[crc32]\n");

    /* CRC-32/ISO-HDLC 的标准校验值 */
    CHECK_EQ(crc32("123456789", 9), 0xCBF43926u);

    /* 空数据的 CRC 是 0（初值取反） */
    CHECK_EQ(crc32("", 0), 0u);

    /* 分段累加与一次性计算必须一致 */
    const char *msg = "123456789";
    uint32_t state = crc32_begin();
    state = crc32_update(state, msg, 4);
    state = crc32_update(state, msg + 4, 5);
    CHECK_EQ(crc32_final(state), 0xCBF43926u);

    /* 改一个 bit 应当被检出 */
    char tampered[10];
    memcpy(tampered, msg, 10);
    tampered[0] = '2';
    CHECK(crc32(tampered, 9) != 0xCBF43926u);

    /* 0xFF 填充的空白 Flash 不应碰巧算出合法 CRC 之外的值 */
    uint8_t blank[64];
    memset(blank, 0xFF, sizeof(blank));
    CHECK(crc32(blank, sizeof(blank)) != crc32(msg, 9));
}
