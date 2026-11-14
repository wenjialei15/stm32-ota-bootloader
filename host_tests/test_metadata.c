#include "test_support.h"
#include "image.h"
#include "metadata.h"

void run_metadata_tests(void);

/* 掉电场景：在第 cut_at 次擦/写操作时断电，检查还能恢复到一致状态 */
static int try_power_cut(int cut_at, uint32_t *seq_out)
{
    fake_flash_t ff;
    fake_flash_init(&ff);
    storage_if_t st;
    fake_flash_storage(&ff, &st);

    boot_meta_t m;
    meta_defaults(&m);

    /* 先建立两份有效备份：A(seq1)、B(seq2) */
    if (meta_commit(&st, &m) != 0) {
        fake_flash_free(&ff);
        return -1;
    }
    m.active_slot = SLOT_B;
    if (meta_commit(&st, &m) != 0) {
        fake_flash_free(&ff);
        return -1;
    }

    /* 从这一刻开始计数，模拟下一次提交时断电 */
    ff.op_count = 0;
    ff.power_lost = 0;
    ff.power_cut_at = cut_at;

    boot_meta_t attempt = m;
    attempt.pending_slot = SLOT_A;
    (void)meta_commit(&st, &attempt);

    /* 恢复供电（读操作本身不受掉电影响） */
    ff.power_lost = 0;
    ff.power_cut_at = -1;

    boot_meta_t out;
    int ok = (meta_load(&st, &out) == 0) && meta_check(&out);
    if (ok && seq_out != NULL) {
        *seq_out = out.seq;
    }

    fake_flash_free(&ff);
    return ok ? 0 : -1;
}

void run_metadata_tests(void)
{
    printf("[metadata]\n");

    CHECK_EQ(sizeof(boot_meta_t), 32u);
    CHECK_EQ(offsetof(boot_meta_t, crc32) + 4u, 32u);

    fake_flash_t ff;
    fake_flash_init(&ff);
    storage_if_t st;
    fake_flash_storage(&ff, &st);

    boot_meta_t m;
    meta_defaults(&m);
    CHECK(meta_check(&m));
    CHECK_EQ(m.active_slot, SLOT_A);
    CHECK_EQ(m.pending_slot, META_NONE);

    /* 空白 Flash 里读不出元数据 */
    boot_meta_t out;
    CHECK_EQ(meta_load(&st, &out), -1);

    /* 第一次提交写到 A */
    CHECK_EQ(meta_commit(&st, &m), 0);
    CHECK_EQ(meta_load(&st, &out), 0);
    CHECK_EQ(out.seq, 1u);
    CHECK_EQ(out.active_slot, SLOT_A);

    /* 后续提交交替写两份备份，seq 单调增长 */
    for (uint32_t i = 2; i <= 6; i++) {
        m.active_slot = (i % 2u == 0u) ? SLOT_B : SLOT_A;
        CHECK_EQ(meta_commit(&st, &m), 0);
        CHECK_EQ(meta_load(&st, &out), 0);
        CHECK_EQ(out.seq, i);
        CHECK_EQ(out.active_slot, m.active_slot);
    }

    /* 抹掉一份备份，仍能从另一份读出来 */
    memset(ff.mem + (META_B_ADDR - FAKE_BASE), 0xFF, META_REGION);
    CHECK_EQ(meta_load(&st, &out), 0);
    CHECK_EQ(out.seq, 6u);

    /* 两份都抹掉才报错 */
    memset(ff.mem + (META_A_ADDR - FAKE_BASE), 0xFF, META_REGION);
    CHECK_EQ(meta_load(&st, &out), -1);

    fake_flash_free(&ff);

    /* ---- 掉电安全：遍历断电时机 ---- */
    int recovered = 0;
    for (int cut = 1; cut <= 6; cut++) {
        uint32_t seq = 0;
        if (try_power_cut(cut, &seq) == 0) {
            recovered++;
            /* 至少保住上一份完整的记录，不能回到"什么都没有" */
            CHECK(seq >= 2u);
            /*
             * 断电发生在擦除或写入中途时，新记录根本没写成，
             * 读回来的只能是那份没被碰过的 seq=2。
             */
            if (cut <= 2) {
                CHECK_EQ(seq, 2u);
            }
        }
    }
    printf("  power-cut points recovered: %d/6\n", recovered);
    CHECK_EQ(recovered, 6);

    /* ---- 序号回绕 ---- */
    fake_flash_t ff2;
    fake_flash_init(&ff2);
    storage_if_t st2;
    fake_flash_storage(&ff2, &st2);

    boot_meta_t old_meta;
    meta_defaults(&old_meta);
    old_meta.seq = 0xFFFFFFFEu;
    old_meta.active_slot = SLOT_A;
    meta_seal(&old_meta);
    CHECK_EQ(st2.write(META_A_ADDR, &old_meta, sizeof(old_meta)), 0);

    boot_meta_t new_meta = old_meta;
    new_meta.seq = 1u; /* 回绕之后 */
    new_meta.active_slot = SLOT_B;
    meta_seal(&new_meta);
    CHECK_EQ(st2.write(META_B_ADDR, &new_meta, sizeof(new_meta)), 0);

    /* 朴素的 a.seq > b.seq 会选错，必须用有符号差值比较 */
    CHECK_EQ(meta_load(&st2, &out), 0);
    CHECK_EQ(out.active_slot, SLOT_B);
    CHECK_EQ(out.seq, 1u);

    fake_flash_free(&ff2);
}
