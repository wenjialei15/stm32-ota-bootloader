#include "test_support.h"
#include "crc32.h"
#include "image.h"

#include <stddef.h>

void run_image_tests(void);

static uint8_t s_buf[SLOT_SIZE] __attribute__((aligned(4)));

static void prepare(uint32_t load_addr, uint32_t payload_len)
{
    memset(s_buf, 0xFF, sizeof(s_buf));
    make_image(s_buf, load_addr, payload_len, 0x000100, 0x5A);
}

void run_image_tests(void)
{
    printf("[image]\n");

    /* 结构体大小是二进制格式的一部分，不能被编译器悄悄填充改掉 */
    CHECK_EQ(sizeof(image_header_t), IMAGE_HEADER_SIZE);

    CHECK_EQ(slot_base_of(SLOT_A), SLOT_A_BASE);
    CHECK_EQ(slot_base_of(SLOT_B), SLOT_B_BASE);

    /* 正常路径 */
    prepare(SLOT_A_BASE, 1024);
    image_header_t h;
    CHECK_EQ(image_validate(s_buf, SLOT_SIZE, SLOT_A_BASE, &h), IMAGE_OK);
    CHECK_EQ(h.load_addr, SLOT_A_BASE);
    CHECK_EQ(h.image_size, 1024u);
    CHECK_EQ(h.entry, SLOT_A_BASE + IMAGE_HEADER_SIZE + 4u);

    /* 空白 Flash 全 0xFF，不能当成镜像 */
    memset(s_buf, 0xFF, sizeof(s_buf));
    CHECK_EQ(image_validate(s_buf, SLOT_SIZE, SLOT_A_BASE, NULL), IMAGE_ERR_MAGIC);

    /* magic 对但头被改坏 —— 必须在读其他字段之前就拒绝 */
    prepare(SLOT_A_BASE, 1024);
    s_buf[8] ^= 0x01; /* 动 image_size */
    CHECK_EQ(image_validate(s_buf, SLOT_SIZE, SLOT_A_BASE, NULL), IMAGE_ERR_HEADER_CRC);

    /* 头完好，但这是给 B 槽的镜像，A 槽必须拒绝 */
    prepare(SLOT_B_BASE, 1024);
    CHECK_EQ(image_validate(s_buf, SLOT_SIZE, SLOT_A_BASE, NULL), IMAGE_ERR_ADDR);

    /* 负载被改一个字节 */
    prepare(SLOT_A_BASE, 1024);
    s_buf[IMAGE_HEADER_SIZE + 10] ^= 0xFF;
    CHECK_EQ(image_validate(s_buf, SLOT_SIZE, SLOT_A_BASE, NULL), IMAGE_ERR_PAYLOAD_CRC);

    /* 负载长度声明超过槽容量，必须被挡下来（否则后面会越界读） */
    prepare(SLOT_A_BASE, 1024);
    image_header_t *hp = (image_header_t *)s_buf;
    hp->image_size = SLOT_SIZE; /* 头 + 负载超出槽 */
    hp->header_crc = crc32(hp, offsetof(image_header_t, header_crc));
    CHECK_EQ(image_validate(s_buf, SLOT_SIZE, SLOT_A_BASE, NULL), IMAGE_ERR_SIZE);

    /* 负载长度为 0 同样非法 */
    prepare(SLOT_A_BASE, 1024);
    hp->image_size = 0;
    hp->header_crc = crc32(hp, offsetof(image_header_t, header_crc));
    CHECK_EQ(image_validate(s_buf, SLOT_SIZE, SLOT_A_BASE, NULL), IMAGE_ERR_SIZE);

    /* 槽比一个头还小 */
    CHECK_EQ(image_validate(s_buf, 16, SLOT_A_BASE, NULL), IMAGE_ERR_TOO_SHORT);

    /* 状态字符串对日志友好，不能返回 NULL */
    for (int st = 0; st <= IMAGE_ERR_PAYLOAD_CRC; st++) {
        CHECK(image_status_str((image_status_t)st) != NULL);
    }
}
