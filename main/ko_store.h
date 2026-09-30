// main/ko_store.h —— 学习进度与设置的持久化（NVS）。
//
// 写入策略：进度改动后不立刻写 Flash，而是打上"脏"标记，由后台任务在满足条件时才写：
//   * 距离最后一次改动已过 KO_STORE_SETTLE_MS（合并连续操作，少写 Flash）；
//   * 没有在播放声音——Flash 写 / 擦会让缓存暂停，可能让 I2S 的 DMA 断粮而产生爆音
//     （见 docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md 8.1）。
// 离开学习会话等关键点可以用 ko_store_flush() 立即写（那时通常没有声音在播）。
//
// NVS 初始化失败时不擦除分区（仓库规定不能用擦除来掩盖分区错误）：应用退化为
// 本次运行不保存，并在设置页如实显示"进度存储：不可用"。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "ko_progress.h"

#define KO_STORE_SETTLE_MS 1500

typedef struct {
    uint8_t volume;       // 0..100
    uint8_t brightness;   // 10..100
} ko_settings_t;

// 初始化 NVS 并读出进度与设置。任何一步失败都不阻塞应用：进度回到全新状态，设置回到默认值。
esp_err_t ko_store_init(void);

// 持久化是否可用（NVS 打开成功）。
bool ko_store_ok(void);

// 读 / 改进度。进度只被输入任务修改；后台任务写 Flash 前会在互斥锁内拷一份快照。
// 修改进度前后必须调用 ko_store_lock()/ko_store_unlock()，改完调 ko_store_mark_dirty()。
ko_progress_t *ko_store_progress(void);
void ko_store_lock(void);
void ko_store_unlock(void);
void ko_store_mark_dirty(uint32_t now_ms);

ko_settings_t ko_store_settings(void);
void ko_store_set_settings(ko_settings_t settings, uint32_t now_ms);

// 清除全部学习进度（保留设置）。立即写盘。
void ko_store_reset_progress(uint32_t now_ms);

// 后台任务周期性调用。返回本次是否写了 Flash。
bool ko_store_tick(uint32_t now_ms, bool audio_busy);
// 立即写（如果有改动）。
void ko_store_flush(void);
