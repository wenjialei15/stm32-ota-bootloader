#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

/* "OTA1" 的小端表示，防止把空白 Flash（全 0xFF）误判成镜像 */
#define IMAGE_MAGIC 0x3141544Fu

#define IMAGE_HEADER_SIZE 32u

/* Flash 分区（详见 docs/design.md）。这些地址链接脚本和 Bootloader 必须一致。 */
#define BOOT_BASE      0x08000000u
#define BOOT_SIZE      (8u * 1024u)
#define META_A_ADDR    0x08002000u
#define META_B_ADDR    0x08002800u
#define META_REGION    2048u
#define SLOT_A_BASE    0x08003000u
#define SLOT_B_BASE    0x08009000u
#define SLOT_SIZE      (24u * 1024u)
#define FLASH_PAGE_SIZE 1024u

#define SLOT_A 0u
#define SLOT_B 1u

/*
 * 镜像头，紧贴槽起始地址。负载紧随其后，所以复位向量在
 * load_addr + sizeof(header) 处，Reset_Handler 在再往后 4 字节。
 *
 * 字段顺序不能随便改：header_crc 覆盖它前面的所有字段，
 * 而 payload_crc 只有通过 header_crc 校验后才可信。
 */
typedef struct {
    uint32_t magic;       /* IMAGE_MAGIC */
    uint32_t version;     /* 主 << 16 | 次 << 8 | 修订 */
    uint32_t image_size;  /* 负载长度，不含头 */
    uint32_t load_addr;   /* 该镜像应放置的槽地址 */
    uint32_t entry;       /* 复位向量地址 */
    uint32_t payload_crc; /* 负载 CRC-32 */
    uint32_t header_crc;  /* 本结构前 24 字节的 CRC-32 */
    uint32_t flags;       /* 预留 */
} image_header_t;

typedef enum {
    IMAGE_OK = 0,
    IMAGE_ERR_TOO_SHORT,   /* 槽容量装不下一个头 */
    IMAGE_ERR_MAGIC,       /* 没找到镜像 */
    IMAGE_ERR_HEADER_CRC,  /* 头被写坏 */
    IMAGE_ERR_ADDR,        /* 镜像不是给这个槽的 */
    IMAGE_ERR_SIZE,        /* 负载长度非法或超出槽容量 */
    IMAGE_ERR_PAYLOAD_CRC, /* 负载内容损坏 */
} image_status_t;

/*
 * 校验一个槽里的镜像。
 *
 * slot_base     槽起始地址（直接映射的 Flash 指针）
 * slot_size     槽容量
 * expect_addr   这个槽期望的 load_addr，不匹配直接拒绝
 * out_header    可为 NULL；非 NULL 时返回读到的头（即使校验失败）
 *
 * 校验顺序是有意为之：magic -> header_crc -> load_addr -> size -> payload_crc。
 * 头没通过完整性校验之前，头里的任何字段都不能信。
 */
image_status_t image_validate(const uint8_t *slot_base, uint32_t slot_size,
                              uint32_t expect_addr, image_header_t *out_header);

const char *image_status_str(image_status_t status);

/* 便捷封装：槽下标 0/1 -> 基地址 */
uint32_t slot_base_of(uint32_t slot_index);

#endif /* IMAGE_H */
