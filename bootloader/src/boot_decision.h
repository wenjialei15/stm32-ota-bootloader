#ifndef BOOT_DECISION_H
#define BOOT_DECISION_H

#include "metadata.h"

typedef enum {
    BOOT_ACTION_JUMP = 0,    /* 跳转到某个槽 */
    BOOT_ACTION_RECOVERY,    /* 进升级模式，等串口 */
} boot_action_t;

typedef enum {
    BOOT_REASON_OK = 0,      /* 正常启动活动槽 */
    BOOT_REASON_NO_META,     /* 元数据全坏，靠镜像头判断 */
    BOOT_REASON_PENDING,     /* 有待确认的升级，去试新槽 */
    BOOT_REASON_ROLLBACK,    /* 新槽连续失败，退回旧槽 */
    BOOT_REASON_FALLBACK,    /* 活动槽坏了，试另一个 */
    BOOT_REASON_BOTH_BAD,    /* 两个槽都没有可用镜像 */
} boot_reason_t;

typedef struct {
    boot_action_t action;
    uint32_t slot;        /* action == JUMP 时有效：0 = A, 1 = B */
    int bump_retry;       /* 是否要把 retry_count + 1 写回元数据 */
    int clear_pending;    /* 回滚时需要把 pending 清掉，否则元数据永远是脏的 */
    boot_reason_t reason;
} boot_plan_t;

/*
 * 纯函数：给定元数据和两个槽的内容，算出该跳到哪。
 * 不碰 Flash、不碰寄存器，所以能在 PC 上穷举测试所有分支。
 *
 * slots[0] / slots[1] 直接指向槽起始的映射地址，允许为 NULL。
 */
boot_plan_t boot_plan_make(const boot_meta_t *meta, int meta_valid,
                           const uint8_t *slots[2]);

const char *boot_reason_str(boot_reason_t reason);

#endif /* BOOT_DECISION_H */
