#ifndef METADATA_H
#define METADATA_H

#include <stdint.h>

#define META_MAGIC 0x4154454Du /* "META" 小端 */
#define META_NONE  0xFFFFFFFFu

#define META_MAX_RETRY 3u

/*
 * 启动元数据。写入永远整条替换，不原地改字段——Flash 只能 1 变 0，
 * 改一个字节也得擦掉整页。这也是必须双备份的直接原因。
 *
 * crc32 覆盖它前面的所有字段。
 */
typedef struct {
    uint32_t seq;          /* 单调递增（允许回绕），越大越新 */
    uint32_t active_slot;  /* 0 = A, 1 = B */
    uint32_t pending_slot; /* META_NONE = 没有待确认的升级 */
    uint32_t retry_count;  /* 待确认升级连续启动失败的次数 */
    uint32_t version;      /* 当前镜像版本号，便于人看日志 */
    uint32_t reserved;
    uint32_t magic;
    uint32_t crc32;
} boot_meta_t;

/*
 * 存储抽象。Bootloader 里由 flash_if 实现，主机测试里由内存模拟实现。
 * 把硬件从这里摘出去，元数据逻辑就能在 PC 上跑完所有掉电场景。
 */
typedef struct {
    int (*read)(uint32_t addr, void *buf, uint32_t len);
    int (*erase)(uint32_t addr, uint32_t len);
    int (*write)(uint32_t addr, const void *buf, uint32_t len);
} storage_if_t;

/* 填一份出厂默认元数据（槽 A、无待确认升级）。 */
void meta_defaults(boot_meta_t *meta);

/* 重新计算并写入 crc32 字段。 */
void meta_seal(boot_meta_t *meta);

/* 单份记录是否完整可信。 */
int meta_check(const boot_meta_t *meta);

/*
 * 读最新的一份。两份都坏返回 -1。
 * 两份都有效时取 seq 更新的一份。
 */
int meta_load(const storage_if_t *storage, boot_meta_t *out);

/*
 * 提交：自动分配下一个 seq，擦掉较旧的那份备份，写入并回读校验。
 *
 * 成功返回 0。任意时刻掉电，另一份备份仍是完整的——这是整个方案
 * 不需要写"提交标志"的原因。
 */
int meta_commit(const storage_if_t *storage, boot_meta_t *meta);

#endif /* METADATA_H */
