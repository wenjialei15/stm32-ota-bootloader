#include "boot_decision.h"
#include "image.h"

#include <stddef.h>

static int slot_is_valid(const uint8_t *slots[2], uint32_t index)
{
    const uint8_t *base = slots[index];
    if (base == NULL) {
        return 0;
    }
    return image_validate(base, SLOT_SIZE, slot_base_of(index), NULL) == IMAGE_OK;
}

boot_plan_t boot_plan_make(const boot_meta_t *meta, int meta_valid,
                           const uint8_t *slots[2])
{
    boot_plan_t plan;
    plan.action = BOOT_ACTION_RECOVERY;
    plan.slot = 0u;
    plan.bump_retry = 0;
    plan.reason = BOOT_REASON_NO_META;

    /* 没有可用元数据：退回"看镜像头说话"，A 优先 */
    if (!meta_valid || meta == NULL) {
        if (slot_is_valid(slots, SLOT_A)) {
            plan.action = BOOT_ACTION_JUMP;
            plan.slot = SLOT_A;
            plan.reason = BOOT_REASON_OK;
        } else if (slot_is_valid(slots, SLOT_B)) {
            plan.action = BOOT_ACTION_JUMP;
            plan.slot = SLOT_B;
            plan.reason = BOOT_REASON_FALLBACK;
        } else {
            plan.reason = BOOT_REASON_BOTH_BAD;
        }
        return plan;
    }

    const uint32_t active = (meta->active_slot == SLOT_B) ? SLOT_B : SLOT_A;
    const uint32_t other = active ^ 1u;

    if (meta->pending_slot != META_NONE) {
        const uint32_t pending = (meta->pending_slot == SLOT_B) ? SLOT_B : SLOT_A;

        /* 新固件连续 N 次没确认自己活着 -> 判定它坏了，退回旧槽 */
        if (meta->retry_count >= META_MAX_RETRY) {
            if (slot_is_valid(slots, active)) {
                plan.action = BOOT_ACTION_JUMP;
                plan.slot = active;
                plan.reason = BOOT_REASON_ROLLBACK;
            } else {
                plan.reason = BOOT_REASON_BOTH_BAD;
            }
            return plan;
        }

        /* 镜像本身都不完整就别浪费时间了，直接回滚 */
        if (!slot_is_valid(slots, pending)) {
            if (slot_is_valid(slots, active)) {
                plan.action = BOOT_ACTION_JUMP;
                plan.slot = active;
                plan.reason = BOOT_REASON_ROLLBACK;
            } else {
                plan.reason = BOOT_REASON_BOTH_BAD;
            }
            return plan;
        }

        plan.action = BOOT_ACTION_JUMP;
        plan.slot = pending;
        plan.reason = BOOT_REASON_PENDING;
        /*
         * 关键：跳转前就把重试次数写回。如果新固件在第一行代码就崩了，
         * 这次计数已经落盘，下次上电才能继续累加，最终触发回滚。
         */
        plan.bump_retry = 1;
        return plan;
    }

    if (slot_is_valid(slots, active)) {
        plan.action = BOOT_ACTION_JUMP;
        plan.slot = active;
        plan.reason = BOOT_REASON_OK;
        return plan;
    }

    if (slot_is_valid(slots, other)) {
        plan.action = BOOT_ACTION_JUMP;
        plan.slot = other;
        plan.reason = BOOT_REASON_FALLBACK;
        return plan;
    }

    plan.action = BOOT_ACTION_RECOVERY;
    plan.reason = BOOT_REASON_BOTH_BAD;
    return plan;
}

const char *boot_reason_str(boot_reason_t reason)
{
    switch (reason) {
    case BOOT_REASON_OK:        return "ok";
    case BOOT_REASON_NO_META:   return "no meta";
    case BOOT_REASON_PENDING:   return "trial boot";
    case BOOT_REASON_ROLLBACK:  return "rollback";
    case BOOT_REASON_FALLBACK:  return "fallback";
    case BOOT_REASON_BOTH_BAD:  return "no valid image";
    default:                    return "unknown";
    }
}
