#include "image.h"
#include "crc32.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(image_header_t) == IMAGE_HEADER_SIZE,
               "image_header_t 必须恰好 32 字节，字段增删要同步改版本号");

uint32_t slot_base_of(uint32_t slot_index)
{
    return (slot_index == SLOT_B) ? SLOT_B_BASE : SLOT_A_BASE;
}

image_status_t image_validate(const uint8_t *slot_base, uint32_t slot_size,
                              uint32_t expect_addr, image_header_t *out_header)
{
    if (slot_base == NULL || slot_size < IMAGE_HEADER_SIZE) {
        return IMAGE_ERR_TOO_SHORT;
    }

    image_header_t h;
    memcpy(&h, slot_base, sizeof(h));
    if (out_header != NULL) {
        *out_header = h;
    }

    /* 1. 先确认这是镜像，不是空白 Flash 或别的数据 */
    if (h.magic != IMAGE_MAGIC) {
        return IMAGE_ERR_MAGIC;
    }

    /* 2. 头的完整性。这一步之后才能信任 h 里的其余字段 */
    if (crc32(&h, offsetof(image_header_t, header_crc)) != h.header_crc) {
        return IMAGE_ERR_HEADER_CRC;
    }

    /* 3. 是不是给这个槽的 */
    if (h.load_addr != expect_addr) {
        return IMAGE_ERR_ADDR;
    }

    /* 4. 长度合法性：非零且装得下，防止后面越界读 */
    if (h.image_size == 0u || h.image_size > slot_size - IMAGE_HEADER_SIZE) {
        return IMAGE_ERR_SIZE;
    }

    /* 5. 最后才是负载内容 */
    if (crc32(slot_base + IMAGE_HEADER_SIZE, h.image_size) != h.payload_crc) {
        return IMAGE_ERR_PAYLOAD_CRC;
    }

    return IMAGE_OK;
}

const char *image_status_str(image_status_t status)
{
    switch (status) {
    case IMAGE_OK:              return "ok";
    case IMAGE_ERR_TOO_SHORT:   return "slot too small";
    case IMAGE_ERR_MAGIC:       return "bad magic";
    case IMAGE_ERR_HEADER_CRC:  return "header crc";
    case IMAGE_ERR_ADDR:        return "load addr mismatch";
    case IMAGE_ERR_SIZE:        return "bad image size";
    case IMAGE_ERR_PAYLOAD_CRC: return "payload crc";
    default:                    return "unknown";
    }
}
