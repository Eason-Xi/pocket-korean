// main/ko_power.h —— 屏幕休眠策略：一段时间没有操作先调暗，再熄灭。纯逻辑，可在主机上测试。
//
// 背光是这块板子上最大的耗电项之一，可穿戴设备不能一直亮着。
// 说明：这里只管背光。整机 light / deep sleep 需要按键唤醒接口，当前 BSP 没有提供
// （只有定时器唤醒），所以熄屏后 CPU 仍在运行，只是不再刷新和驱动背光。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KO_DIM_AFTER_MS 20000    // 无操作多久后调暗
#define KO_OFF_AFTER_MS 60000    // 无操作多久后熄屏
#define KO_DIM_PERCENT_MAX 15    // 调暗时的最大亮度（不高于用户设定值）

typedef enum {
    KO_SCREEN_FULL = 0,
    KO_SCREEN_DIM,
    KO_SCREEN_OFF,
} ko_screen_level_t;

typedef struct {
    uint32_t last_activity_ms;
    ko_screen_level_t level;
} ko_power_t;

void ko_power_init(ko_power_t *power, uint32_t now_ms);

// 有用户操作或音频在播放时调用。返回 true 表示屏幕之前不是全亮，需要恢复亮度。
bool ko_power_activity(ko_power_t *power, uint32_t now_ms);

// 周期性调用（约每秒一次）。返回当前应处的等级，等级变化时 *changed = true。
ko_screen_level_t ko_power_tick(ko_power_t *power, uint32_t now_ms, bool *changed);

// 等级对应的背光百分比：user_percent 是用户在设置里选的亮度（10..100）。
uint8_t ko_power_backlight_percent(ko_screen_level_t level, uint8_t user_percent);
