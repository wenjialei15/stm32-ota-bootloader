#include "ota_proto.h"
#include "crc32.h"
#include "image.h"

#include <string.h>

#define OTA_VERSION_MAJOR 0u
#define OTA_VERSION_MINOR 4u

enum {
    ST_SOF = 0,
    ST_CMD,
    ST_SEQ,
    ST_LEN_L,
    ST_LEN_H,
    ST_PAYLOAD,
    ST_CRC,
};

void ota_parser_reset(ota_parser_t *parser)
{
    /* 不动 dropped_frames：它是跨帧累计的统计量 */
    parser->stage = ST_SOF;
    parser->cmd = 0u;
    parser->seq = 0u;
    parser->len = 0u;
    parser->filled = 0u;
    parser->crc_calc = 0u;
    parser->crc_recv = 0u;
    parser->crc_bytes = 0u;
}

int ota_parser_feed(ota_parser_t *parser, uint8_t byte, ota_frame_t *out)
{
    switch (parser->stage) {
    case ST_SOF:
        /* 只在见到 SOF 时才开始，噪声字节直接被跳过 */
        if (byte == OTA_SOF) {
            parser->stage = ST_CMD;
        }
        return 0;

    case ST_CMD:
        parser->cmd = byte;
        parser->crc_calc = crc32_update(crc32_begin(), &byte, 1u);
        parser->stage = ST_SEQ;
        return 0;

    case ST_SEQ:
        parser->seq = byte;
        parser->crc_calc = crc32_update(parser->crc_calc, &byte, 1u);
        parser->stage = ST_LEN_L;
        return 0;

    case ST_LEN_L:
        parser->len = byte;
        parser->crc_calc = crc32_update(parser->crc_calc, &byte, 1u);
        parser->stage = ST_LEN_H;
        return 0;

    case ST_LEN_H: {
        parser->len |= (uint16_t)((uint16_t)byte << 8);
        parser->crc_calc = crc32_update(parser->crc_calc, &byte, 1u);

        /*
         * 长度超限必须立刻放弃。真实故障场景里噪声很容易凑出一个
         * 巨大的 LEN，如果继续等，接收方会卡在收 64 KB 垃圾上，
         * 而这期间所有正常帧都被吞掉。
         */
        if (parser->len > OTA_MAX_PAYLOAD) {
            ota_parser_reset(parser);
            parser->dropped_frames++;
            return -1;
        }

        parser->filled = 0u;
        parser->crc_recv = 0u;
        parser->crc_bytes = 0u;
        parser->stage = (parser->len == 0u) ? ST_CRC : ST_PAYLOAD;
        return 0;
    }

    case ST_PAYLOAD:
        parser->payload[parser->filled++] = byte;
        parser->crc_calc = crc32_update(parser->crc_calc, &byte, 1u);
        if (parser->filled >= parser->len) {
            parser->stage = ST_CRC;
        }
        return 0;

    case ST_CRC:
        parser->crc_recv |= (uint32_t)byte << (8u * parser->crc_bytes);
        parser->crc_bytes++;
        if (parser->crc_bytes < 4u) {
            return 0;
        }

        if (crc32_final(parser->crc_calc) != parser->crc_recv) {
            ota_parser_reset(parser);
            parser->dropped_frames++;
            return -1;
        }

        out->cmd = parser->cmd;
        out->seq = parser->seq;
        out->len = parser->len;
        if (parser->len > 0u) {
            memcpy(out->payload, parser->payload, parser->len);
        }
        ota_parser_reset(parser);
        return 1;

    default:
        ota_parser_reset(parser);
        return -1;
    }
}

uint32_t ota_build_frame(uint8_t *buf, uint32_t cap, uint8_t cmd, uint8_t seq,
                         const uint8_t *payload, uint16_t len)
{
    if (len > OTA_MAX_PAYLOAD) {
        return 0u;
    }
    uint32_t total = 5u + (uint32_t)len + 4u;
    if (cap < total) {
        return 0u;
    }

    uint32_t i = 0u;
    buf[i++] = OTA_SOF;
    buf[i++] = cmd;
    buf[i++] = seq;
    buf[i++] = (uint8_t)(len & 0xFFu);
    buf[i++] = (uint8_t)((len >> 8) & 0xFFu);

    if (len > 0u) {
        memcpy(&buf[i], payload, len);
        i += len;
    }

    /* CRC 覆盖 CMD..PAYLOAD，不含 SOF */
    uint32_t crc = crc32(&buf[1], i - 1u);
    buf[i++] = (uint8_t)(crc & 0xFFu);
    buf[i++] = (uint8_t)((crc >> 8) & 0xFFu);
    buf[i++] = (uint8_t)((crc >> 16) & 0xFFu);
    buf[i++] = (uint8_t)((crc >> 24) & 0xFFu);
    return i;
}

void ota_device_info(uint8_t *out, uint16_t *out_len)
{
    uint32_t i = 0u;
    out[i++] = OTA_VERSION_MAJOR;
    out[i++] = OTA_VERSION_MINOR;

    /* Flash 总容量 64 KB，页 1 KB，槽 24 KB */
    out[i++] = (uint8_t)(64u & 0xFFu);
    out[i++] = 0u;
    out[i++] = (uint8_t)(FLASH_PAGE_SIZE & 0xFFu);
    out[i++] = (uint8_t)((FLASH_PAGE_SIZE >> 8) & 0xFFu);

    uint32_t slot_size = SLOT_SIZE;
    for (uint32_t k = 0; k < 4u; k++) {
        out[i++] = (uint8_t)((slot_size >> (8u * k)) & 0xFFu);
    }
    for (uint32_t k = 0; k < 4u; k++) {
        out[i++] = (uint8_t)((SLOT_A_BASE >> (8u * k)) & 0xFFu);
    }
    for (uint32_t k = 0; k < 4u; k++) {
        out[i++] = (uint8_t)((SLOT_B_BASE >> (8u * k)) & 0xFFu);
    }

    *out_len = (uint16_t)i;
}

void ota_session_init(ota_session_t *session)
{
    memset(session, 0, sizeof(*session));
    session->state = OTA_STATE_IDLE;
}

/* 把攒够的一页写进 Flash。数据不足一页时按 0xFF 补齐 */
static int flush_page(ota_session_t *session, const storage_if_t *storage, int force)
{
    if (session->page_filled == 0u && !force) {
        return 0;
    }

    for (uint32_t i = session->page_filled; i < FLASH_PAGE_SIZE; i++) {
        session->page_buf[i] = 0xFFu;
    }

    if (storage->erase(session->page_addr, FLASH_PAGE_SIZE) != 0) {
        return -1;
    }
    if (storage->write(session->page_addr, session->page_buf, FLASH_PAGE_SIZE) != 0) {
        return -1;
    }

    session->page_addr += FLASH_PAGE_SIZE;
    session->page_filled = 0u;
    return 0;
}

static uint32_t respond(uint8_t *resp, uint32_t cap, uint8_t cmd, uint8_t seq,
                        ota_status_t status)
{
    uint8_t body = (uint8_t)status;
    return ota_build_frame(resp, cap, cmd, seq, &body, 1u);
}

static uint32_t handle_begin(ota_session_t *session, const ota_frame_t *frame,
                             const storage_if_t *storage, uint8_t *resp, uint32_t cap)
{
    if (frame->len != 9u) {
        return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_PARAM);
    }

    uint32_t slot = frame->payload[0];
    uint32_t size = (uint32_t)frame->payload[1] | ((uint32_t)frame->payload[2] << 8) |
                    ((uint32_t)frame->payload[3] << 16) | ((uint32_t)frame->payload[4] << 24);
    uint32_t crc = (uint32_t)frame->payload[5] | ((uint32_t)frame->payload[6] << 8) |
                   ((uint32_t)frame->payload[7] << 16) | ((uint32_t)frame->payload[8] << 24);

    if (slot > 1u) {
        return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_PARAM);
    }
    /* 至少要装得下一个镜像头 */
    if (size <= IMAGE_HEADER_SIZE || size > SLOT_SIZE) {
        return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_SIZE);
    }

    uint32_t base = slot_base_of(slot);

    /* 先把整槽擦干净：中途失败就明确报错，不要留下"擦了一半"的槽 */
    for (uint32_t off = 0; off < SLOT_SIZE; off += FLASH_PAGE_SIZE) {
        if (storage->erase(base + off, FLASH_PAGE_SIZE) != 0) {
            return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_FLASH);
        }
    }

    session->state = OTA_STATE_RECEIVING;
    session->target_slot = slot;
    session->base_addr = base;
    session->image_size = size;
    session->image_crc = crc;
    session->received = 0u;
    session->last_seq = frame->seq;
    session->page_addr = base;
    session->page_filled = 0u;

    return respond(resp, cap, frame->cmd, frame->seq, OTA_OK);
}

static uint32_t handle_data(ota_session_t *session, const ota_frame_t *frame,
                            const storage_if_t *storage, uint8_t *resp, uint32_t cap)
{
    if (session->state != OTA_STATE_RECEIVING) {
        return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_STATE);
    }
    if (frame->len < 5u) {
        return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_PARAM);
    }

    /*
     * 重传幂等：PC 端超时重发同一帧时，这一帧其实已经写进去了。
     * 直接回 OK 而不是报错，让上层可以简单粗暴地重发。
     */
    if (frame->seq == session->last_seq) {
        return respond(resp, cap, frame->cmd, frame->seq, OTA_OK);
    }

    uint32_t offset = (uint32_t)frame->payload[0] | ((uint32_t)frame->payload[1] << 8) |
                      ((uint32_t)frame->payload[2] << 16) | ((uint32_t)frame->payload[3] << 24);
    uint32_t chunk = (uint32_t)frame->len - 4u;

    if (offset != session->received) {
        return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_SEQ);
    }
    if (offset + chunk > session->image_size) {
        return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_SIZE);
    }

    const uint8_t *data = &frame->payload[4];

    for (uint32_t i = 0; i < chunk; i++) {
        session->page_buf[session->page_filled++] = data[i];
        if (session->page_filled == FLASH_PAGE_SIZE) {
            if (flush_page(session, storage, 0) != 0) {
                session->state = OTA_STATE_IDLE;
                return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_FLASH);
            }
        }
    }

    session->received += chunk;
    session->last_seq = frame->seq;

    return respond(resp, cap, frame->cmd, frame->seq, OTA_OK);
}

static uint32_t handle_end(ota_session_t *session, const ota_frame_t *frame,
                           const storage_if_t *storage, uint8_t *resp, uint32_t cap)
{
    if (session->state != OTA_STATE_RECEIVING) {
        return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_STATE);
    }
    if (session->received != session->image_size) {
        return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_SIZE);
    }

    /* 最后不足一页的部分也要写进去，否则尾部是 0xFF */
    if (flush_page(session, storage, 1) != 0) {
        session->state = OTA_STATE_IDLE;
        return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_FLASH);
    }

    /*
     * 从 Flash 读回来重新算一遍 CRC，而不是依赖收数据时累加的值。
     * 前者能发现 Flash 写入错误，后者只能发现传输错误——两者都要防。
     */
    uint32_t crc = crc32_begin();
    uint8_t buf[256];
    uint32_t left = session->image_size;
    uint32_t addr = session->base_addr;

    while (left > 0u) {
        uint32_t n = (left > sizeof(buf)) ? (uint32_t)sizeof(buf) : left;
        if (storage->read(addr, buf, n) != 0) {
            session->state = OTA_STATE_IDLE;
            return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_FLASH);
        }
        crc = crc32_update(crc, buf, n);
        addr += n;
        left -= n;
    }

    if (crc32_final(crc) != session->image_crc) {
        session->state = OTA_STATE_IDLE;
        return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_CRC);
    }

    /*
     * 校验通过才动元数据：把目标槽标记为待确认，active 保持不变。
     * 新固件跑起来并调用 confirm 之前，旧槽的信息必须一直留着，
     * 否则回滚时不知道该退回哪。
     */
    boot_meta_t meta;
    if (meta_load(storage, &meta) != 0) {
        meta_defaults(&meta);
    }
    meta.pending_slot = session->target_slot;
    meta.retry_count = 0u;

    if (meta_commit(storage, &meta) != 0) {
        session->state = OTA_STATE_IDLE;
        return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_FLASH);
    }

    session->state = OTA_STATE_IDLE;
    return respond(resp, cap, frame->cmd, frame->seq, OTA_OK);
}

uint32_t ota_session_handle(ota_session_t *session, const ota_frame_t *frame,
                            const storage_if_t *storage, uint8_t *resp, uint32_t cap)
{
    switch (frame->cmd) {
    case OTA_CMD_HELLO: {
        uint8_t info[32];
        uint16_t info_len = 0u;
        ota_device_info(info, &info_len);

        uint8_t body[33];
        body[0] = (uint8_t)OTA_OK;
        memcpy(&body[1], info, info_len);
        return ota_build_frame(resp, cap, frame->cmd, frame->seq, body,
                               (uint16_t)(info_len + 1u));
    }

    case OTA_CMD_BEGIN:
        return handle_begin(session, frame, storage, resp, cap);

    case OTA_CMD_DATA:
        return handle_data(session, frame, storage, resp, cap);

    case OTA_CMD_END:
        return handle_end(session, frame, storage, resp, cap);

    case OTA_CMD_ABORT:
        /* 不擦目标槽：它没有合法元数据指向，永远不会被选中 */
        ota_session_init(session);
        return respond(resp, cap, frame->cmd, frame->seq, OTA_OK);

    case OTA_CMD_STATUS: {
        uint8_t body[6];
        body[0] = (uint8_t)OTA_OK;
        body[1] = (uint8_t)session->state;
        body[2] = (uint8_t)(session->received & 0xFFu);
        body[3] = (uint8_t)((session->received >> 8) & 0xFFu);
        body[4] = (uint8_t)((session->received >> 16) & 0xFFu);
        body[5] = (uint8_t)((session->received >> 24) & 0xFFu);
        return ota_build_frame(resp, cap, frame->cmd, frame->seq, body, 6u);
    }

    default:
        return respond(resp, cap, frame->cmd, frame->seq, OTA_ERR_PARAM);
    }
}
