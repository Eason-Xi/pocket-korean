// main/ko_audio.h —— 发音与提示音播放（ESP-IDF 侧，复用 BSP 的 bsp_audio_*）。
//
// 线程模型：一个常驻的音频任务负责所有 PCM 写入（bsp_audio_write 会阻塞），
// 按键回调 / 输入任务只往"邮箱"里投递最新的命令（不阻塞，后来的命令打断正在播的）。
// 音频任务从不访问 LVGL，所以不需要 bsp_lvgl_lock()；界面靠轮询 ko_audio_is_playing()
// 更新喇叭图标。
//
// 音频任务的优先级高于 LVGL 任务：整页重绘可能占用 CPU 几十毫秒，而 I2S 的 DMA 缓冲只有
// 约 90 ms（见 docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md 8.1）。任务大部分时间
// 阻塞在 bsp_audio_write 上，高优先级不会饿死其他任务。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    KO_AUDIO_PACK_ABSENT = 0,   // 分区是空的：没有安装发音包，属于正常情况
    KO_AUDIO_PACK_READY,        // 头与索引校验通过
    KO_AUDIO_PACK_BROKEN,       // 有内容但不合法，或分区读不出来
} ko_audio_pack_state_t;

typedef enum {
    KO_TONE_KIND_RIGHT = 0,
    KO_TONE_KIND_WRONG,
    KO_TONE_KIND_TICK,
} ko_tone_kind_t;

// 必须在 bsp_audio_init() 成功之后调用。创建音频任务并检查 kopack 分区。
// 失败（例如内存不足）时返回错误，应用仍可无声运行——之后的播放调用都是空操作。
esp_err_t ko_audio_init(void);

ko_audio_pack_state_t ko_audio_pack_state(void);
// 包里的发音槽位数（READY 时有意义，否则 0）。
uint32_t ko_audio_pack_slots(void);
// 这条发音现在能不能播（包就绪且该条目存在）。开机时建好位图，调用很便宜。
bool ko_audio_has_clip(uint16_t clip);

// 音量 0..100。只记录，在下一次播放开始时由音频任务应用，避免与正在进行的 PCM 写入竞争。
void ko_audio_set_volume(uint8_t percent);
uint8_t ko_audio_volume(void);

// 播放：立即打断当前播放。发音不可用时是空操作。
void ko_audio_play_clip(uint16_t clip);
// 先播 first，停 gap_ms 毫秒，再播 second（字母名 + 例词）。任一条不可用就跳过它。
void ko_audio_play_pair(uint16_t first, uint16_t second, uint16_t gap_ms);
void ko_audio_play_tone(ko_tone_kind_t kind);
void ko_audio_stop(void);

// 是否正在出声（含两条之间的停顿）。存档写入要避开播放期间，见 ko_store.h。
bool ko_audio_is_playing(void);
