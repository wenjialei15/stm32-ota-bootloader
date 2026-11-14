#include "test_support.h"
#include "boot_decision.h"
#include "crc32.h"
#include "image.h"
#include "metadata.h"
#include "ota_proto.h"

void run_lifecycle_tests(void);

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static ota_status_t send_cmd(const storage_if_t *storage, ota_session_t *session,
                             uint8_t cmd, uint8_t seq, const uint8_t *payload,
                             uint16_t len)
{
    ota_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.cmd = cmd;
    frame.seq = seq;
    frame.len = len;
    if (len > 0u) {
        memcpy(frame.payload, payload, len);
    }

    uint8_t resp[OTA_MAX_PAYLOAD + 8];
    uint32_t n = ota_session_handle(session, &frame, storage, resp, sizeof(resp));
    if (n < 6u) {
        return OTA_ERR_STATE;
    }
    return (ota_status_t)resp[5];
}

/* 走一遍完整的 OTA 协议把镜像推进某个槽 */
static int perform_ota(const storage_if_t *storage, ota_session_t *session,
                       uint32_t slot, const uint8_t *img, uint32_t len)
{
    uint8_t begin[9];
    begin[0] = (uint8_t)slot;
    put_u32(&begin[1], len);
    put_u32(&begin[5], crc32(img, len));

    if (send_cmd(storage, session, OTA_CMD_BEGIN, 1u, begin, 9u) != OTA_OK) {
        return -1;
    }

    uint32_t offset = 0u;
    uint8_t seq = 2u;
    uint8_t chunk[4u + OTA_DATA_CHUNK];

    while (offset < len) {
        uint32_t n = len - offset;
        if (n > OTA_DATA_CHUNK) {
            n = OTA_DATA_CHUNK;
        }
        put_u32(chunk, offset);
        memcpy(&chunk[4], img + offset, n);

        if (send_cmd(storage, session, OTA_CMD_DATA, seq, chunk, (uint16_t)(4u + n)) != OTA_OK) {
            return -1;
        }
        offset += n;
        seq++;
    }

    return (send_cmd(storage, session, OTA_CMD_END, seq, NULL, 0u) == OTA_OK) ? 0 : -1;
}

/*
 * 模拟一次上电，逻辑与 bootloader/src/main.c 一一对应：
 * 读元数据 -> 决策 -> 落盘试运行计数 / 清 pending -> 返回跳转的槽。
 * 返回 -1 表示进了升级模式。
 */
static int simulate_boot(fake_flash_t *ff, const storage_if_t *storage, boot_reason_t *reason)
{
    boot_meta_t meta;
    int ok = (meta_load(storage, &meta) == 0);

    const uint8_t *slots[2] = {
        ff->mem + (SLOT_A_BASE - FAKE_BASE),
        ff->mem + (SLOT_B_BASE - FAKE_BASE),
    };

    boot_plan_t plan = boot_plan_make(&meta, ok, slots);

    if (ok && (plan.bump_retry != 0 || plan.clear_pending != 0)) {
        if (plan.bump_retry != 0) {
            meta.retry_count += 1u;
        }
        if (plan.clear_pending != 0) {
            meta.pending_slot = META_NONE;
            meta.active_slot = plan.slot;
            meta.retry_count = 0u;
        }
        (void)meta_commit(storage, &meta);
    }

    if (reason != NULL) {
        *reason = plan.reason;
    }
    return (plan.action == BOOT_ACTION_JUMP) ? (int)plan.slot : -1;
}

/* 出厂状态：A 槽有固件，元数据 active=A */
static void setup_factory(fake_flash_t *ff, storage_if_t *storage)
{
    static uint8_t tmp[8192] __attribute__((aligned(4)));

    fake_flash_reset(ff);
    fake_flash_storage(ff, storage);

    uint32_t len = make_image(tmp, SLOT_A_BASE, 1024u, 0x000100u, 0x11u);
    memcpy(ff->mem + (SLOT_A_BASE - FAKE_BASE), tmp, len);

    boot_meta_t meta;
    meta_defaults(&meta);
    meta.active_slot = SLOT_A;
    (void)meta_commit(storage, &meta);
}

static void test_happy_path(fake_flash_t *ff, const storage_if_t *storage)
{
    printf("  -- upgrade then confirm\n");

    static uint8_t img[8192] __attribute__((aligned(4)));
    uint32_t len = make_image(img, SLOT_B_BASE, 1500u, 0x000200u, 0x33u);

    ota_session_t session;
    ota_session_init(&session);

    setup_factory(ff, storage);

    CHECK_EQ(simulate_boot(ff, storage, NULL), (int)SLOT_A);

    /* 走完整协议把新固件推进 B 槽 */
    CHECK_EQ(perform_ota(storage, &session, SLOT_B, img, len), 0);

    boot_meta_t meta;
    CHECK_EQ(meta_load(storage, &meta), 0);
    CHECK_EQ(meta.active_slot, SLOT_A);
    CHECK_EQ(meta.pending_slot, SLOT_B);

    /* 第一次上电：跳新槽，试运行计数 +1 */
    boot_reason_t reason = BOOT_REASON_OK;
    CHECK_EQ(simulate_boot(ff, storage, &reason), (int)SLOT_B);
    CHECK_EQ(reason, BOOT_REASON_PENDING);

    CHECK_EQ(meta_load(storage, &meta), 0);
    CHECK_EQ(meta.retry_count, 1u);
    CHECK_EQ(meta.pending_slot, SLOT_B);

    /* 应用活了 3 秒后确认 */
    CHECK_EQ(meta_confirm_boot(storage, SLOT_B), 0);

    CHECK_EQ(meta_load(storage, &meta), 0);
    CHECK_EQ(meta.pending_slot, META_NONE);
    CHECK_EQ(meta.active_slot, SLOT_B);
    CHECK_EQ(meta.retry_count, 0u);

    /* 之后再启动就是正常路径，不再累加计数 */
    CHECK_EQ(simulate_boot(ff, storage, &reason), (int)SLOT_B);
    CHECK_EQ(reason, BOOT_REASON_OK);

    CHECK_EQ(meta_load(storage, &meta), 0);
    CHECK_EQ(meta.retry_count, 0u);
}

static void test_rollback(fake_flash_t *ff, const storage_if_t *storage)
{
    printf("  -- bad image triggers rollback\n");

    static uint8_t img[8192] __attribute__((aligned(4)));
    uint32_t len = make_image(img, SLOT_B_BASE, 1200u, 0x000300u, 0x55u);

    ota_session_t session;
    ota_session_init(&session);

    setup_factory(ff, storage);
    CHECK_EQ(perform_ota(storage, &session, SLOT_B, img, len), 0);

    boot_meta_t meta;
    CHECK_EQ(meta_load(storage, &meta), 0);
    CHECK_EQ(meta.pending_slot, SLOT_B);

    /*
     * 新固件会崩（演示固件故意 HardFault），所以每次上电都不会 confirm。
     * 前 3 次仍然跳过去试，第 4 次判定失败回滚。
     */
    boot_reason_t reason = BOOT_REASON_OK;
    for (int i = 0; i < (int)META_MAX_RETRY; i++) {
        CHECK_EQ(simulate_boot(ff, storage, &reason), (int)SLOT_B);
        CHECK_EQ(reason, BOOT_REASON_PENDING);

        CHECK_EQ(meta_load(storage, &meta), 0);
        CHECK_EQ(meta.retry_count, (uint32_t)(i + 1));
    }

    /* 达到上限：回滚到 A */
    CHECK_EQ(simulate_boot(ff, storage, &reason), (int)SLOT_A);
    CHECK_EQ(reason, BOOT_REASON_ROLLBACK);

    /* 回滚后元数据必须是干净的：pending 清掉、active 修正、计数归零 */
    CHECK_EQ(meta_load(storage, &meta), 0);
    CHECK_EQ(meta.pending_slot, META_NONE);
    CHECK_EQ(meta.active_slot, SLOT_A);
    CHECK_EQ(meta.retry_count, 0u);

    /* 再启动：正常跳旧固件，不会再看到 rollback */
    CHECK_EQ(simulate_boot(ff, storage, &reason), (int)SLOT_A);
    CHECK_EQ(reason, BOOT_REASON_OK);

    /* 回滚后旧固件仍然完整可用 */
    CHECK_EQ(image_validate(ff->mem + (SLOT_A_BASE - FAKE_BASE), SLOT_SIZE, SLOT_A_BASE, NULL),
             IMAGE_OK);
}

static void test_confirm_rules(fake_flash_t *ff, const storage_if_t *storage)
{
    printf("  -- confirm rules\n");

    setup_factory(ff, storage);

    boot_meta_t meta;
    CHECK_EQ(meta_load(storage, &meta), 0);
    uint32_t seq_before = meta.seq;

    /* 没有 pending 时确认：什么都不做，不该消耗 Flash 擦写次数 */
    CHECK_EQ(meta_confirm_boot(storage, SLOT_A), 0);

    CHECK_EQ(meta_load(storage, &meta), 0);
    CHECK_EQ(meta.seq, seq_before);

    /* pending 指向 A，但应用却在 B 槽报告：必须拒绝 */
    meta_defaults(&meta);
    meta.active_slot = SLOT_B;
    meta.pending_slot = SLOT_A;
    CHECK_EQ(meta_commit(storage, &meta), 0);

    CHECK_EQ(meta_confirm_boot(storage, SLOT_B), -1);

    CHECK_EQ(meta_load(storage, &meta), 0);
    CHECK_EQ(meta.pending_slot, SLOT_A); /* 没被误清 */
}

void run_lifecycle_tests(void)
{
    printf("[lifecycle]\n");

    fake_flash_t ff;
    fake_flash_init(&ff);
    storage_if_t storage;
    fake_flash_storage(&ff, &storage);

    test_happy_path(&ff, &storage);
    test_rollback(&ff, &storage);
    test_confirm_rules(&ff, &storage);

    fake_flash_free(&ff);
}
