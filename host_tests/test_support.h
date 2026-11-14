#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "metadata.h"

extern int g_checks;
extern int g_failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                      \
    } while (0)

#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        long long a_ = (long long)(actual);                                    \
        long long e_ = (long long)(expected);                                  \
        g_checks++;                                                            \
        if (a_ != e_) {                                                        \
            g_failures++;                                                      \
            printf("  FAIL %s:%d  %s: got %lld, want %lld\n", __FILE__,       \
                   __LINE__, #actual, a_, e_);                                 \
        }                                                                      \
    } while (0)

#define FAKE_BASE 0x08000000u
#define FAKE_SIZE (64u * 1024u)

/*
 * 内存模拟 Flash。可以设定"第 N 次写操作时断电"，
 * 断电时只完成一半动作，用来验证元数据的双备份是否真的扛得住。
 */
typedef struct {
    uint8_t *mem;
    int op_count;
    int power_cut_at;   /* <0 表示不断电 */
    int power_lost;     /* 掉电后置 1，后续操作全失败 */
    int half_done;      /* 记录掉电时是否真的只做了一半 */
} fake_flash_t;

void fake_flash_init(fake_flash_t *ff);
void fake_flash_free(fake_flash_t *ff);
void fake_flash_reset(fake_flash_t *ff);

/* 把 fake_flash 包装成 metadata 用的 storage 接口 */
void fake_flash_storage(fake_flash_t *ff, storage_if_t *out);

/*
 * 在 buf 里构造一个合法镜像（头 + 负载），返回总字节数。
 * seed 决定负载内容，方便造出"内容不同但长度相同"的镜像。
 */
uint32_t make_image(uint8_t *buf, uint32_t load_addr, uint32_t payload_len,
                    uint32_t version, uint8_t seed);

#endif /* TEST_SUPPORT_H */
