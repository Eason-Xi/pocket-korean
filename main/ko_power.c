// main/ko_power.c —— 屏幕休眠策略。
#include "ko_power.h"

void ko_power_init(ko_power_t *power, uint32_t now_ms) {
    power->last_activity_ms = now_ms;
    power->level = KO_SCREEN_FULL;
}

bool ko_power_activity(ko_power_t *power, uint32_t now_ms) {
    const bool was_dimmed = power->level != KO_SCREEN_FULL;
    power->last_activity_ms = now_ms;
    power->level = KO_SCREEN_FULL;
    return was_dimmed;
}

ko_screen_level_t ko_power_tick(ko_power_t *power, uint32_t now_ms, bool *changed) {
    // 无符号减法：毫秒计数回绕后仍然正确。
    const uint32_t idle = (uint32_t)(now_ms - power->last_activity_ms);
    ko_screen_level_t next = KO_SCREEN_FULL;
    if (idle >= KO_OFF_AFTER_MS) next = KO_SCREEN_OFF;
    else if (idle >= KO_DIM_AFTER_MS) next = KO_SCREEN_DIM;

    if (changed) *changed = next != power->level;
    power->level = next;
    return next;
}

uint8_t ko_power_backlight_percent(ko_screen_level_t level, uint8_t user_percent) {
    if (user_percent > 100) user_percent = 100;
    switch (level) {
    case KO_SCREEN_OFF:
        return 0;
    case KO_SCREEN_DIM:
        return user_percent < KO_DIM_PERCENT_MAX ? user_percent : KO_DIM_PERCENT_MAX;
    default:
        return user_percent;
    }
}
