#include "metadata.h"
#include "image.h"
#include "crc32.h"

#include <stddef.h>
#include <string.h>

static uint32_t meta_calc_crc(const boot_meta_t *meta)
{
    /* 覆盖 crc32 之前的所有字段 */
    return crc32(meta, offsetof(boot_meta_t, crc32));
}

void meta_seal(boot_meta_t *meta)
{
    meta->magic = META_MAGIC;
    meta->crc32 = meta_calc_crc(meta);
}

void meta_defaults(boot_meta_t *meta)
{
    memset(meta, 0, sizeof(*meta));
    meta->seq = 1u;
    meta->active_slot = SLOT_A;
    meta->pending_slot = META_NONE;
    meta->retry_count = 0u;
    meta->version = 0u;
    meta_seal(meta);
}

int meta_check(const boot_meta_t *meta)
{
    if (meta->magic != META_MAGIC) {
        return 0;
    }
    return meta->crc32 == meta_calc_crc(meta);
}

/*
 * 序列号比较要能扛住回绕：直接用 a > b 在 0xFFFFFFFF -> 0 时会判错，
 * 改成有符号差值比较（TCP 里比较序号用的就是这招）。
 */
static int seq_is_newer(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

static int read_one(const storage_if_t *storage, uint32_t addr, boot_meta_t *out)
{
    boot_meta_t tmp;
    if (storage->read(addr, &tmp, sizeof(tmp)) != 0) {
        return -1;
    }
    if (!meta_check(&tmp)) {
        return -1;
    }
    *out = tmp;
    return 0;
}

int meta_load(const storage_if_t *storage, boot_meta_t *out)
{
    boot_meta_t a;
    boot_meta_t b;
    int ok_a = read_one(storage, META_A_ADDR, &a) == 0;
    int ok_b = read_one(storage, META_B_ADDR, &b) == 0;

    if (!ok_a && !ok_b) {
        return -1;
    }
    if (ok_a && !ok_b) {
        *out = a;
        return 0;
    }
    if (!ok_a && ok_b) {
        *out = b;
        return 0;
    }
    *out = seq_is_newer(a.seq, b.seq) ? a : b;
    return 0;
}

int meta_commit(const storage_if_t *storage, boot_meta_t *meta)
{
    boot_meta_t a;
    boot_meta_t b;
    int ok_a = read_one(storage, META_A_ADDR, &a) == 0;
    int ok_b = read_one(storage, META_B_ADDR, &b) == 0;

    uint32_t target;
    uint32_t next_seq;

    if (!ok_a && !ok_b) {
        /* 两份都不可用：从 A 开始，seq 从 1 起 */
        target = META_A_ADDR;
        next_seq = 1u;
    } else if (ok_a && !ok_b) {
        target = META_B_ADDR;
        next_seq = a.seq + 1u;
    } else if (!ok_a && ok_b) {
        target = META_A_ADDR;
        next_seq = b.seq + 1u;
    } else if (seq_is_newer(b.seq, a.seq)) {
        /* 两份都好：擦掉旧的（seq 小的那份），写另一份 */
        target = META_A_ADDR;
        next_seq = b.seq + 1u;
    } else {
        target = META_B_ADDR;
        next_seq = a.seq + 1u;
    }

    meta->seq = next_seq;
    meta_seal(meta);

    if (storage->erase(target, META_REGION) != 0) {
        return -1;
    }
    if (storage->write(target, meta, sizeof(*meta)) != 0) {
        return -1;
    }

    /* 回读校验：写入返回成功不代表内容正确（供电不足时 Flash 会写花） */
    boot_meta_t verify;
    if (read_one(storage, target, &verify) != 0) {
        return -1;
    }
    if (memcmp(&verify, meta, sizeof(*meta)) != 0) {
        return -1;
    }

    return 0;
}

int meta_confirm_boot(const storage_if_t *storage, uint32_t running_slot)
{
    boot_meta_t meta;

    if (meta_load(storage, &meta) != 0) {
        return -1;
    }

    /* 没有待确认的升级：正常启动，什么都不用做 */
    if (meta.pending_slot == META_NONE) {
        return 0;
    }

    /*
     * 正在跑的槽不是待确认的那个，说明元数据和应用对不上。
     * 这种情况不该盲目确认——宁可让它继续试运行，最后走回滚。
     */
    if ((meta.pending_slot == SLOT_B ? SLOT_B : SLOT_A) != (running_slot == SLOT_B ? SLOT_B : SLOT_A)) {
        return -1;
    }

    meta.pending_slot = META_NONE;
    meta.active_slot = running_slot;
    meta.retry_count = 0u;

    return meta_commit(storage, &meta);
}
