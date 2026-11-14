#include "test_support.h"
#include "crc32.h"
#include "image.h"
#include "metadata.h"
#include "ota_proto.h"

void run_ota_proto_tests(void);

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/* 发一帧，返回响应里的 status */
static ota_status_t do_cmd(ota_session_t *session, const storage_if_t *storage,
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
    /* 响应帧：SOF, CMD, SEQ, LEN_L, LEN_H, payload[0] */
    return (ota_status_t)resp[5];
}

static void test_parser(void)
{
    printf("  -- frame parser\n");

    ota_parser_t parser;
    memset(&parser, 0, sizeof(parser));
    ota_parser_reset(&parser);

    ota_frame_t frame;
    uint8_t buf[64];

    /* 逐字节喂入：模拟串口按字节到达 */
    uint16_t n = (uint16_t)ota_build_frame(buf, sizeof(buf), OTA_CMD_STATUS, 7u, NULL, 0u);
    CHECK_EQ(n, 9u); /* 1 + 1 + 1 + 2 + 0 + 4 */

    int frames = 0;
    for (uint16_t i = 0; i < n; i++) {
        if (ota_parser_feed(&parser, buf[i], &frame) == 1) {
            frames++;
            CHECK_EQ(frame.cmd, OTA_CMD_STATUS);
            CHECK_EQ(frame.seq, 7u);
            CHECK_EQ(frame.len, 0u);
        }
    }
    CHECK_EQ(frames, 1);

    /* 粘包：两帧连在一起，中间没有间隔 */
    uint8_t body[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint16_t a = (uint16_t)ota_build_frame(buf, sizeof(buf), OTA_CMD_DATA, 1u, body, 4u);
    uint16_t b = (uint16_t)ota_build_frame(&buf[a], sizeof(buf) - a, OTA_CMD_DATA, 2u, body, 4u);

    frames = 0;
    for (uint16_t i = 0; i < (uint16_t)(a + b); i++) {
        if (ota_parser_feed(&parser, buf[i], &frame) == 1) {
            frames++;
        }
    }
    CHECK_EQ(frames, 2);

    /* 噪声前缀：SOF 之前的字节应当被安静地跳过 */
    frames = 0;
    for (int i = 0; i < 7; i++) {
        ota_parser_feed(&parser, (uint8_t)(0x10 + i), &frame);
    }
    for (uint16_t i = 0; i < a; i++) {
        if (ota_parser_feed(&parser, buf[i], &frame) == 1) {
            frames++;
        }
    }
    CHECK_EQ(frames, 1);
    CHECK_EQ(parser.dropped_frames, 0u);

    /* CRC 被改坏：整帧丢弃 */
    uint8_t crc_buf[64];
    uint16_t c = (uint16_t)ota_build_frame(crc_buf, sizeof(crc_buf), OTA_CMD_END, 3u, body, 4u);
    crc_buf[c - 1] ^= 0xFFu; /* 动 CRC 最高字节 */
    int last = 0;
    for (uint16_t i = 0; i < c; i++) {
        last = ota_parser_feed(&parser, crc_buf[i], &frame);
    }
    CHECK_EQ(last, -1);
    CHECK_EQ(parser.dropped_frames, 1u);

    /* LEN 声明超限：立刻放弃，不要傻等 64 KB */
    uint8_t huge[5] = {OTA_SOF, OTA_CMD_DATA, 0x01, 0xFF, 0xFF};
    int rc = 0;
    for (int i = 0; i < 5; i++) {
        rc = ota_parser_feed(&parser, huge[i], &frame);
    }
    CHECK_EQ(rc, -1);
    CHECK_EQ(parser.dropped_frames, 2u);

    /* 重同步：被丢弃之后，下一帧仍然能被正确接收 */
    uint8_t resync_buf[64];
    uint16_t rn = (uint16_t)ota_build_frame(resync_buf, sizeof(resync_buf), OTA_CMD_HELLO,
                                            9u, NULL, 0u);
    frames = 0;
    for (uint16_t i = 0; i < rn; i++) {
        if (ota_parser_feed(&parser, resync_buf[i], &frame) == 1) {
            frames++;
        }
    }
    CHECK_EQ(frames, 1);
    CHECK_EQ(frame.cmd, OTA_CMD_HELLO);
}

static void test_full_upgrade(void)
{
    printf("  -- full upgrade flow\n");

    static uint8_t img[4096] __attribute__((aligned(4)));
    uint32_t img_len = make_image(img, SLOT_B_BASE, 2000u, 0x000200u, 0x42u);
    uint32_t img_crc = crc32(img, img_len);

    fake_flash_t ff;
    fake_flash_init(&ff);
    storage_if_t st;
    fake_flash_storage(&ff, &st);

    ota_session_t session;
    ota_session_init(&session);

    /* HELLO */
    CHECK_EQ(do_cmd(&session, &st, OTA_CMD_HELLO, 1u, NULL, 0u), OTA_OK);

    /* BEGIN：装到 B 槽 */
    uint8_t begin[9];
    begin[0] = (uint8_t)SLOT_B;
    put_u32(&begin[1], img_len);
    put_u32(&begin[5], img_crc);
    CHECK_EQ(do_cmd(&session, &st, OTA_CMD_BEGIN, 2u, begin, 9u), OTA_OK);
    CHECK_EQ(session.state, OTA_STATE_RECEIVING);

    /* DATA：按 1020 字节一块推送 */
    uint32_t offset = 0u;
    uint8_t seq = 3u;
    uint8_t chunk[4u + OTA_DATA_CHUNK];

    while (offset < img_len) {
        uint32_t n = img_len - offset;
        if (n > OTA_DATA_CHUNK) {
            n = OTA_DATA_CHUNK;
        }
        put_u32(chunk, offset);
        memcpy(&chunk[4], img + offset, n);

        CHECK_EQ(do_cmd(&session, &st, OTA_CMD_DATA, seq, chunk, (uint16_t)(4u + n)), OTA_OK);

        /* 重传幂等：同一序号再发一次，必须回 OK 且不重复写 */
        CHECK_EQ(do_cmd(&session, &st, OTA_CMD_DATA, seq, chunk, (uint16_t)(4u + n)), OTA_OK);
        CHECK_EQ(session.received, offset + n);

        offset += n;
        seq++;
    }

    /* END */
    CHECK_EQ(do_cmd(&session, &st, OTA_CMD_END, seq, NULL, 0u), OTA_OK);
    CHECK_EQ(session.state, OTA_STATE_IDLE);

    /* 槽里的内容必须和镜像逐字节一致 */
    CHECK(memcmp(ff.mem + (SLOT_B_BASE - FAKE_BASE), img, img_len) == 0);

    /* 元数据应当把 B 标成待确认，而 active 保持不变 */
    boot_meta_t meta;
    CHECK_EQ(meta_load(&st, &meta), 0);
    CHECK_EQ(meta.pending_slot, SLOT_B);
    CHECK_EQ(meta.active_slot, SLOT_A);
    CHECK_EQ(meta.retry_count, 0u);

    /* 换个解析视角：写进去的东西确实能被 bootloader 认成合法镜像 */
    CHECK_EQ(image_validate(ff.mem + (SLOT_B_BASE - FAKE_BASE), SLOT_SIZE, SLOT_B_BASE, NULL),
             IMAGE_OK);

    fake_flash_free(&ff);
}

static void test_rejections(void)
{
    printf("  -- rejection paths\n");

    static uint8_t img[4096] __attribute__((aligned(4)));
    uint32_t img_len = make_image(img, SLOT_A_BASE, 1200u, 0x000300u, 0x11u);

    fake_flash_t ff;
    fake_flash_init(&ff);
    storage_if_t st;
    fake_flash_storage(&ff, &st);

    ota_session_t session;
    ota_session_init(&session);

    uint8_t payload[64];

    /* 没 BEGIN 就发 DATA */
    put_u32(payload, 0u);
    payload[4] = 0xAA;
    CHECK_EQ(do_cmd(&session, &st, OTA_CMD_DATA, 1u, payload, 5u), OTA_ERR_STATE);

    /* BEGIN 参数非法：槽号越界 */
    uint8_t begin[9];
    begin[0] = 9u;
    put_u32(&begin[1], img_len);
    put_u32(&begin[5], crc32(img, img_len));
    CHECK_EQ(do_cmd(&session, &st, OTA_CMD_BEGIN, 2u, begin, 9u), OTA_ERR_PARAM);

    /* BEGIN 参数非法：长度超过槽容量 */
    begin[0] = (uint8_t)SLOT_A;
    put_u32(&begin[1], SLOT_SIZE + 1u);
    CHECK_EQ(do_cmd(&session, &st, OTA_CMD_BEGIN, 3u, begin, 9u), OTA_ERR_SIZE);

    /* 正常 BEGIN，但声明的整镜像 CRC 是错的 */
    uint8_t wrong_begin[9];
    wrong_begin[0] = (uint8_t)SLOT_A;
    put_u32(&wrong_begin[1], img_len);
    put_u32(&wrong_begin[5], crc32(img, img_len) ^ 0x1234u);
    CHECK_EQ(do_cmd(&session, &st, OTA_CMD_BEGIN, 4u, wrong_begin, 9u), OTA_OK);

    uint32_t offset = 0u;
    uint8_t seq = 5u;
    uint8_t chunk[4u + OTA_DATA_CHUNK];
    while (offset < img_len) {
        uint32_t n = img_len - offset;
        if (n > OTA_DATA_CHUNK) {
            n = OTA_DATA_CHUNK;
        }
        put_u32(chunk, offset);
        memcpy(&chunk[4], img + offset, n);
        CHECK_EQ(do_cmd(&session, &st, OTA_CMD_DATA, seq++, chunk, (uint16_t)(4u + n)), OTA_OK);
        offset += n;
    }

    /* CRC 不符必须拒绝，并且不能落元数据 */
    CHECK_EQ(do_cmd(&session, &st, OTA_CMD_END, seq, NULL, 0u), OTA_ERR_CRC);

    boot_meta_t meta;
    CHECK(meta_load(&st, &meta) != 0); /* 仍然没有有效元数据 */

    /* 乱序 offset */
    ota_session_init(&session);
    uint8_t good_begin[9];
    good_begin[0] = (uint8_t)SLOT_A;
    put_u32(&good_begin[1], img_len);
    put_u32(&good_begin[5], crc32(img, img_len));
    CHECK_EQ(do_cmd(&session, &st, OTA_CMD_BEGIN, 6u, good_begin, 9u), OTA_OK);

    put_u32(chunk, 100u); /* 跳着来 */
    memcpy(&chunk[4], img, 16u);
    CHECK_EQ(do_cmd(&session, &st, OTA_CMD_DATA, 7u, chunk, 20u), OTA_ERR_SEQ);

    /* 数据量不够就 END */
    CHECK_EQ(do_cmd(&session, &st, OTA_CMD_END, 8u, NULL, 0u), OTA_ERR_SIZE);

    /* ABORT 回到空闲 */
    CHECK_EQ(do_cmd(&session, &st, OTA_CMD_ABORT, 9u, NULL, 0u), OTA_OK);
    CHECK_EQ(session.state, OTA_STATE_IDLE);

    /* 未知命令 */
    CHECK_EQ(do_cmd(&session, &st, 0x7Fu, 10u, NULL, 0u), OTA_ERR_PARAM);

    fake_flash_free(&ff);
}

void run_ota_proto_tests(void)
{
    printf("[ota_proto]\n");

    CHECK(sizeof(ota_frame_t) > OTA_MAX_PAYLOAD);

    uint8_t info[32];
    uint16_t info_len = 0u;
    ota_device_info(info, &info_len);
    CHECK_EQ(info_len, 18u); /* 版本2 + flash2 + 页2 + 槽4 + 两槽地址8 */
    CHECK_EQ(info[0], 0u);   /* 主版本 */
    CHECK_EQ(info[2], 64u);  /* Flash 64 KB */

    /* 容量不足时必须拒绝组帧，而不是越界写 */
    uint8_t small[4];
    CHECK_EQ(ota_build_frame(small, sizeof(small), OTA_CMD_HELLO, 0u, NULL, 0u), 0u);

    test_parser();
    test_full_upgrade();
    test_rejections();
}
