#include "test_support.h"

#include "crc32.h"
#include "image.h"
#include "metadata.h"

int g_checks = 0;
int g_failures = 0;

static fake_flash_t *s_current = NULL;

void fake_flash_init(fake_flash_t *ff)
{
    ff->mem = (uint8_t *)malloc(FAKE_SIZE);
    fake_flash_reset(ff);
    s_current = ff;
}

void fake_flash_free(fake_flash_t *ff)
{
    free(ff->mem);
    ff->mem = NULL;
    if (s_current == ff) {
        s_current = NULL;
    }
}

void fake_flash_reset(fake_flash_t *ff)
{
    /* 擦除态是 0xFF，不是 0 */
    memset(ff->mem, 0xFF, FAKE_SIZE);
    ff->op_count = 0;
    ff->power_cut_at = -1;
    ff->power_lost = 0;
    ff->half_done = 0;
}

static int ff_offset(uint32_t addr, uint32_t len, uint32_t *out)
{
    if (s_current == NULL || s_current->mem == NULL) {
        return -1;
    }
    if (addr < FAKE_BASE) {
        return -1;
    }
    uint32_t off = addr - FAKE_BASE;
    if (off + len > FAKE_SIZE) {
        return -1;
    }
    *out = off;
    return 0;
}

static int ff_read(uint32_t addr, void *buf, uint32_t len)
{
    if (s_current->power_lost) {
        return -1;
    }
    uint32_t off;
    if (ff_offset(addr, len, &off) != 0) {
        return -1;
    }
    memcpy(buf, s_current->mem + off, len);
    return 0;
}

static int ff_erase(uint32_t addr, uint32_t len)
{
    if (s_current->power_lost) {
        return -1;
    }
    s_current->op_count++;

    uint32_t off;
    if (ff_offset(addr, len, &off) != 0) {
        return -1;
    }

    if (s_current->power_cut_at >= 0 && s_current->op_count >= s_current->power_cut_at) {
        /* 擦到一半掉电：前半已经变 0xFF，后半还是旧数据 */
        memset(s_current->mem + off, 0xFF, len / 2);
        s_current->power_lost = 1;
        s_current->half_done = 1;
        return -1;
    }

    memset(s_current->mem + off, 0xFF, len);
    return 0;
}

static int ff_write(uint32_t addr, const void *buf, uint32_t len)
{
    if (s_current->power_lost) {
        return -1;
    }
    s_current->op_count++;

    uint32_t off;
    if (ff_offset(addr, len, &off) != 0) {
        return -1;
    }

    if (s_current->power_cut_at >= 0 && s_current->op_count >= s_current->power_cut_at) {
        /* 写到一半掉电 */
        memcpy(s_current->mem + off, buf, len / 2);
        s_current->power_lost = 1;
        s_current->half_done = 1;
        return -1;
    }

    memcpy(s_current->mem + off, buf, len);
    return 0;
}

void fake_flash_storage(fake_flash_t *ff, storage_if_t *out)
{
    s_current = ff;
    out->read = ff_read;
    out->erase = ff_erase;
    out->write = ff_write;
}

uint32_t make_image(uint8_t *buf, uint32_t load_addr, uint32_t payload_len,
                    uint32_t version, uint8_t seed)
{
    image_header_t h;
    memset(&h, 0, sizeof(h));
    h.magic = IMAGE_MAGIC;
    h.version = version;
    h.image_size = payload_len;
    h.load_addr = load_addr;
    h.entry = load_addr + IMAGE_HEADER_SIZE + 4u;
    h.flags = 0u;

    uint8_t *payload = buf + IMAGE_HEADER_SIZE;
    for (uint32_t i = 0; i < payload_len; i++) {
        /* 确定性伪随机，便于复现 */
        payload[i] = (uint8_t)(seed + (uint8_t)(i * 31u) + (uint8_t)(i >> 3));
    }

    h.payload_crc = crc32(payload, payload_len);
    h.header_crc = crc32(&h, offsetof(image_header_t, header_crc));

    memcpy(buf, &h, sizeof(h));
    return IMAGE_HEADER_SIZE + payload_len;
}
