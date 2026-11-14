#ifndef OTA_PROTO_H
#define OTA_PROTO_H

#include <stdint.h>

#include "metadata.h"

/*
 * 升级协议：UART 上的帧格式
 *
 *   ┌──────┬─────┬─────┬───────┬───────┬─────────┬──────────┐
 *   │ SOF  │ CMD │ SEQ │ LEN_L │ LEN_H │ PAYLOAD │ CRC32 x4 │
 *   │ 0xA5 │ 1B  │ 1B  │  1B   │  1B   │ 0..1024 │ 小端      │
 *   └──────┴─────┴─────┴───────┴───────┴─────────┴──────────┘
 *
 * CRC 覆盖 CMD..PAYLOAD（不含 SOF），所以 SOF 只是给状态机找起点用的，
 * 不参与校验——它的作用是「快速跳过噪声」，不是「保证帧完整」。
 *
 * 为什么每帧都要独立 CRC：UART 没有重传，一个字节错位就能让后面所有帧
 * 全废。帧级校验让错误只影响这一帧，PC 端重发即可。
 */

#define OTA_SOF 0xA5u

/* LEN 上限。定死而不是动态分配：Bootloader 没有堆，也不该有 */
#define OTA_MAX_PAYLOAD 1024u

/* 一个 DATA 帧里除偏移字段外能带的数据量 */
#define OTA_DATA_CHUNK (OTA_MAX_PAYLOAD - 4u)

typedef enum {
    OTA_CMD_HELLO = 0x01,  /* 握手，拿设备信息 */
    OTA_CMD_BEGIN = 0x02,  /* 开始升级：槽 + 总长 + 整镜像 CRC */
    OTA_CMD_DATA = 0x03,   /* 数据块：偏移 + 数据 */
    OTA_CMD_END = 0x04,    /* 提交：校验并切换 */
    OTA_CMD_ABORT = 0x05,  /* 放弃本次升级 */
    OTA_CMD_STATUS = 0x06, /* 查询状态 */
} ota_cmd_t;

typedef enum {
    OTA_OK = 0,
    OTA_ERR_STATE = 1,  /* 当前状态不允许这个命令 */
    OTA_ERR_PARAM = 2,  /* 参数非法（槽号、长度） */
    OTA_ERR_FLASH = 3,  /* 擦写失败 */
    OTA_ERR_CRC = 4,    /* 整镜像 CRC 不符 */
    OTA_ERR_SEQ = 5,    /* 序号错乱 */
    OTA_ERR_SIZE = 6,   /* 数据量不符 */
    OTA_ERR_HEADER = 7, /* 帧本身校验失败 */
} ota_status_t;

typedef struct {
    uint8_t cmd;
    uint8_t seq;
    uint16_t len;
    uint8_t payload[OTA_MAX_PAYLOAD];
} ota_frame_t;

/* ---- 帧解析：逐字节推进，天然容忍任意分片 ---- */

typedef struct {
    uint8_t stage;
    uint8_t cmd;
    uint8_t seq;
    uint16_t len;
    uint16_t filled;
    uint32_t crc_calc;
    uint32_t crc_recv;
    uint8_t crc_bytes;
    /* 统计用：因为长度非法或 CRC 错被丢掉的帧数 */
    uint32_t dropped_frames;
    uint8_t payload[OTA_MAX_PAYLOAD];
} ota_parser_t;

void ota_parser_reset(ota_parser_t *parser);

/*
 * 喂一个字节。
 *   1  = 组出一帧，已写入 out
 *   0  = 继续收
 *  -1  = 这一帧有问题，已丢弃（状态机自动复位重新找 SOF）
 */
int ota_parser_feed(ota_parser_t *parser, uint8_t byte, ota_frame_t *out);

/* 组帧，返回总字节数；容量不够返回 0 */
uint32_t ota_build_frame(uint8_t *buf, uint32_t cap, uint8_t cmd, uint8_t seq,
                         const uint8_t *payload, uint16_t len);

/* ---- 升级会话 ---- */

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_RECEIVING,
} ota_state_t;

typedef struct {
    ota_state_t state;
    uint32_t target_slot;
    uint32_t base_addr;   /* 目标槽起始地址 */
    uint32_t image_size;  /* BEGIN 声明的总长 */
    uint32_t image_crc;   /* BEGIN 声明的整镜像 CRC */
    uint32_t received;    /* 已接收字节数 */
    uint8_t last_seq;     /* 上一帧序号，用于识别重传 */
    uint8_t page_buf[1024];
    uint32_t page_filled;
    uint32_t page_addr;   /* 当前正在攒的那一页的地址 */
} ota_session_t;

void ota_session_init(ota_session_t *session);

/*
 * 处理一帧，把响应帧写进 resp（调用方提供 OTA_MAX_PAYLOAD + 8 的缓冲），
 * 返回响应帧长度；返回 0 表示不需要响应。
 *
 * session / frame / storage 分离，所以整个升级流程可以在 PC 上跑完，
 * 不需要板子也不需要串口。
 */
uint32_t ota_session_handle(ota_session_t *session, const ota_frame_t *frame,
                            const storage_if_t *storage, uint8_t *resp,
                            uint32_t resp_cap);

/* 设备信息，HELLO 响应用 */
void ota_device_info(uint8_t *out, uint16_t *out_len);

#endif /* OTA_PROTO_H */
