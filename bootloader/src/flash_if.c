#include "flash_if.h"
#include "stm32f103_regs.h"

#define FLASH_KEY1 0x45670123u
#define FLASH_KEY2 0xCDEF89ABu

/* 超时保护：Flash 控制器卡住时不要死等，宁可报错返回 */
static int wait_ready(void)
{
    uint32_t guard = 2000000u;
    while ((FLASH_SR & FLASH_SR_BSY) != 0u) {
        if (--guard == 0u) {
            return -1;
        }
    }
    return 0;
}

static void clear_flags(void)
{
    /* SR 的这几个位是"写 1 清零" */
    FLASH_SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPRTERR;
}

int flash_unlock(void)
{
    if ((FLASH_CR & FLASH_CR_LOCK) != 0u) {
        FLASH_KEYR = FLASH_KEY1;
        FLASH_KEYR = FLASH_KEY2;
    }
    return ((FLASH_CR & FLASH_CR_LOCK) != 0u) ? -1 : 0;
}

void flash_lock(void)
{
    FLASH_CR |= FLASH_CR_LOCK;
}

int flash_erase_page(uint32_t addr)
{
    if (wait_ready() != 0) {
        return -1;
    }
    clear_flags();

    FLASH_CR |= FLASH_CR_PER;
    FLASH_AR = addr;
    FLASH_CR |= FLASH_CR_STRT;

    int rc = wait_ready();
    FLASH_CR &= ~FLASH_CR_PER;

    if (rc != 0) {
        return -1;
    }
    if ((FLASH_SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)) != 0u) {
        return -1;
    }
    return 0;
}

int flash_write(uint32_t addr, const void *data, uint32_t len)
{
    if ((len & 1u) != 0u) {
        return -1; /* 半字编程，奇数长度无法处理 */
    }

    const uint16_t *half = (const uint16_t *)data;

    for (uint32_t i = 0; i < len; i += 2u) {
        volatile uint16_t *dst = (volatile uint16_t *)(addr + i);
        uint16_t value = half[i / 2u];

        if (wait_ready() != 0) {
            return -1;
        }
        clear_flags();

        FLASH_CR |= FLASH_CR_PG;
        *dst = value; /* 这一步触发编程 */
        int rc = wait_ready();
        FLASH_CR &= ~FLASH_CR_PG;

        if (rc != 0) {
            return -1;
        }
        /*
         * 回读比对。写入不报错不代表写对了：供电不足、页没擦干净
         * 都会让 Flash 单元停在中间状态。
         */
        if (*dst != value) {
            return -1;
        }
    }
    return 0;
}

int flash_read(uint32_t addr, void *buf, uint32_t len)
{
    const uint8_t *src = (const uint8_t *)addr;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
    return 0;
}
