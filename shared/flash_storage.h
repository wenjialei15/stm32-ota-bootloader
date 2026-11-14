#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include "metadata.h"

/*
 * 把 flash_if 适配成 metadata / ota_proto 需要的 storage_if_t。
 *
 * 抽出来共用是因为两边都要写元数据：Bootloader 在升级完成时写 pending，
 * 应用在启动确认时清 pending。同一份代码，避免两边对"擦除粒度"的理解跑偏。
 */
extern const storage_if_t g_flash_storage;

#endif /* FLASH_STORAGE_H */
