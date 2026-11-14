#include "flash_storage.h"

#include "flash_if.h"
#include "image.h"

static int storage_read(uint32_t addr, void *buf, uint32_t len)
{
    return flash_read(addr, buf, len);
}

static int storage_erase(uint32_t addr, uint32_t len)
{
    for (uint32_t off = 0; off < len; off += FLASH_PAGE_SIZE) {
        if (flash_erase_page(addr + off) != 0) {
            return -1;
        }
    }
    return 0;
}

static int storage_write(uint32_t addr, const void *buf, uint32_t len)
{
    return flash_write(addr, buf, len);
}

const storage_if_t g_flash_storage = {
    storage_read,
    storage_erase,
    storage_write,
};
