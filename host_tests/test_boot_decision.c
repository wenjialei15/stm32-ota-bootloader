#include "test_support.h"
#include "boot_decision.h"
#include "image.h"
#include "metadata.h"

void run_boot_decision_tests(void);

static uint8_t s_a[SLOT_SIZE] __attribute__((aligned(4)));
static uint8_t s_b[SLOT_SIZE] __attribute__((aligned(4)));

static void wipe_slots(void)
{
    memset(s_a, 0xFF, sizeof(s_a));
    memset(s_b, 0xFF, sizeof(s_b));
}

static void put_image(uint32_t slot, uint32_t payload_len, uint8_t seed)
{
    uint8_t *buf = (slot == SLOT_B) ? s_b : s_a;
    uint32_t base = slot_base_of(slot);
    make_image(buf, base, payload_len, 0x000100, seed);
}

static boot_plan_t plan_with(const boot_meta_t *meta, int valid)
{
    const uint8_t *slots[2] = { s_a, s_b };
    return boot_plan_make(meta, valid, slots);
}

void run_boot_decision_tests(void)
{
    printf("[boot_decision]\n");

    boot_meta_t m;

    /* --- 完全没有元数据 --- */
    wipe_slots();
    put_image(SLOT_A, 1024, 1);
    boot_plan_t p = plan_with(NULL, 0);
    CHECK_EQ(p.action, BOOT_ACTION_JUMP);
    CHECK_EQ(p.slot, SLOT_A);
    CHECK_EQ(p.reason, BOOT_REASON_OK);

    /* 只有 B 可用时退到 B */
    wipe_slots();
    put_image(SLOT_B, 1024, 2);
    p = plan_with(NULL, 0);
    CHECK_EQ(p.action, BOOT_ACTION_JUMP);
    CHECK_EQ(p.slot, SLOT_B);
    CHECK_EQ(p.reason, BOOT_REASON_FALLBACK);

    /* 两个都空 -> 进升级模式 */
    wipe_slots();
    p = plan_with(NULL, 0);
    CHECK_EQ(p.action, BOOT_ACTION_RECOVERY);
    CHECK_EQ(p.reason, BOOT_REASON_BOTH_BAD);

    /* --- 正常启动 --- */
    wipe_slots();
    put_image(SLOT_A, 2048, 3);
    meta_defaults(&m);
    m.active_slot = SLOT_A;
    p = plan_with(&m, 1);
    CHECK_EQ(p.action, BOOT_ACTION_JUMP);
    CHECK_EQ(p.slot, SLOT_A);
    CHECK_EQ(p.bump_retry, 0);
    CHECK_EQ(p.reason, BOOT_REASON_OK);

    /* active 指向 A 但 A 坏了、B 好 -> 回退到 B */
    wipe_slots();
    put_image(SLOT_B, 2048, 4);
    p = plan_with(&m, 1);
    CHECK_EQ(p.action, BOOT_ACTION_JUMP);
    CHECK_EQ(p.slot, SLOT_B);
    CHECK_EQ(p.reason, BOOT_REASON_FALLBACK);

    /* --- 待确认的升级（正常试运行） --- */
    wipe_slots();
    put_image(SLOT_A, 2048, 5); /* 旧固件 */
    put_image(SLOT_B, 2048, 6); /* 新固件 */
    meta_defaults(&m);
    m.active_slot = SLOT_A;
    m.pending_slot = SLOT_B;
    m.retry_count = 0;

    p = plan_with(&m, 1);
    CHECK_EQ(p.action, BOOT_ACTION_JUMP);
    CHECK_EQ(p.slot, SLOT_B);
    CHECK_EQ(p.reason, BOOT_REASON_PENDING);
    /* 必须在跳转前就把重试次数写盘，否则新固件秒崩时永远数不到 3 */
    CHECK_EQ(p.bump_retry, 1);

    /* 第 3 次仍然跳新槽，还没到阈值 */
    m.retry_count = META_MAX_RETRY - 1;
    p = plan_with(&m, 1);
    CHECK_EQ(p.slot, SLOT_B);
    CHECK_EQ(p.bump_retry, 1);

    /* 达到阈值 -> 回滚到旧槽，且不再累加计数 */
    m.retry_count = META_MAX_RETRY;
    p = plan_with(&m, 1);
    CHECK_EQ(p.action, BOOT_ACTION_JUMP);
    CHECK_EQ(p.slot, SLOT_A);
    CHECK_EQ(p.reason, BOOT_REASON_ROLLBACK);
    CHECK_EQ(p.bump_retry, 0);

    /* 新槽镜像本身就写坏了 -> 立刻回滚，不必浪费三次启动 */
    wipe_slots();
    put_image(SLOT_A, 2048, 7);
    m.retry_count = 0;
    p = plan_with(&m, 1);
    CHECK_EQ(p.slot, SLOT_A);
    CHECK_EQ(p.reason, BOOT_REASON_ROLLBACK);

    /* 回滚目标也坏了 -> 只能进升级模式，而不是硬跳一个坏地址 */
    wipe_slots();
    m.retry_count = META_MAX_RETRY;
    p = plan_with(&m, 1);
    CHECK_EQ(p.action, BOOT_ACTION_RECOVERY);
    CHECK_EQ(p.reason, BOOT_REASON_BOTH_BAD);

    /* 未初始化的 pending 值（不是 META_NONE 也不是有效槽）按 B 处理，不应崩 */
    wipe_slots();
    put_image(SLOT_A, 1024, 8);
    put_image(SLOT_B, 1024, 9);
    m.retry_count = 0;
    m.pending_slot = 0x00000007u;
    p = plan_with(&m, 1);
    CHECK_EQ(p.action, BOOT_ACTION_JUMP);

    /* 原因字符串用于串口日志 */
    for (int r = 0; r <= BOOT_REASON_BOTH_BAD; r++) {
        CHECK(boot_reason_str((boot_reason_t)r) != NULL);
    }
}
