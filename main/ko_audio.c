// main/ko_audio.c —— 发音与提示音播放任务。
#include "ko_audio.h"

#include <string.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ko_adpcm.h"
#include "ko_content.h"
#include "ko_pack.h"
#include "ko_tone.h"

static const char *TAG = "ko_audio";

// kopack 分区：type data，自定义 subtype 0x40，见 partitions.csv。
#define KO_PACK_PARTITION_LABEL "kopack"
#define KO_PACK_PARTITION_SUBTYPE ((esp_partition_subtype_t)0x40)

#define AUDIO_TASK_STACK 4096
#define AUDIO_TASK_PRIORITY 6      // 高于 LVGL 任务（4）和输入任务（5）
#define PCM_CHUNK_SAMPLES 512      // 每次写 I2S 的采样数（32 ms），也是打断的响应粒度
#define RAW_CHUNK_BYTES (PCM_CHUNK_SAMPLES / 2)
#define TONE_AMPLITUDE 9000        // 提示音峰值（满幅 32767），比人声略轻

typedef enum { CMD_CLIP = 1, CMD_PAIR, CMD_TONE } cmd_type_t;

typedef struct {
    uint8_t type;
    uint8_t tone;
    uint16_t clip[2];
    uint16_t gap_ms;
    uint32_t serial;
} cmd_t;

static QueueHandle_t s_queue;          // 深度 1 的"邮箱"：新命令覆盖旧命令
static TaskHandle_t s_task;
static volatile uint32_t s_serial;     // 最新命令的序号；任务据此判断自己是否被打断
static volatile uint32_t s_done_serial;// 任务已处理完的最后一个序号
static volatile uint8_t s_volume = 60;
static bool s_format_ok;
static int s_applied_volume = -1;

static const esp_partition_t *s_partition;
static ko_pack_info_t s_info;
static ko_audio_pack_state_t s_state = KO_AUDIO_PACK_ABSENT;
static uint8_t s_present[(KO_CLIP_COUNT + 7) / 8];

// 只在音频任务里使用的缓冲：放静态区，不占任务栈。
static int16_t s_pcm[PCM_CHUNK_SAMPLES];
static uint8_t s_raw[RAW_CHUNK_BYTES];

// ---------------------------------------------------------------------------
// 发音包读取
// ---------------------------------------------------------------------------

static bool partition_read(void *ctx, uint32_t offset, void *dst, uint32_t len) {
    (void)ctx;
    return esp_partition_read(s_partition, offset, dst, len) == ESP_OK;
}

static const ko_pack_reader_t s_reader_template = { .read = partition_read };

static ko_pack_reader_t reader(void) {
    ko_pack_reader_t r = s_reader_template;
    r.capacity = s_partition ? s_partition->size : 0;
    return r;
}

static void scan_pack(void) {
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, KO_PACK_PARTITION_SUBTYPE,
                                           KO_PACK_PARTITION_LABEL);
    if (!s_partition) {
        ESP_LOGW(TAG, "分区表里没有 %s，发音不可用", KO_PACK_PARTITION_LABEL);
        s_state = KO_AUDIO_PACK_ABSENT;
        return;
    }
    const ko_pack_reader_t r = reader();
    switch (ko_pack_open(&r, &s_info)) {
    case KO_PACK_OK: {
        uint32_t present = 0;
        const uint32_t limit = s_info.clip_count < KO_CLIP_COUNT ? s_info.clip_count : KO_CLIP_COUNT;
        for (uint32_t id = 0; id < limit; id++) {
            ko_pack_clip_t clip;
            if (ko_pack_clip(&r, &s_info, id, &clip)) {
                s_present[id >> 3] |= (uint8_t)(1u << (id & 7));
                present++;
            }
        }
        s_state = KO_AUDIO_PACK_READY;
        ESP_LOGI(TAG, "发音包就绪：%u 个槽位，%u 条可播（内容需要 %d 条）",
                 (unsigned)s_info.clip_count, (unsigned)present, KO_CLIP_COUNT);
        if (s_info.clip_count != KO_CLIP_COUNT) {
            ESP_LOGW(TAG, "发音包槽位数与固件内容不一致，可能是旧包；重新生成并烧录 kopack");
        }
        break;
    }
    case KO_PACK_EMPTY:
        ESP_LOGI(TAG, "kopack 分区是空的：未安装发音包");
        s_state = KO_AUDIO_PACK_ABSENT;
        break;
    default:
        ESP_LOGE(TAG, "kopack 内容不合法（头 / 索引校验失败）");
        s_state = KO_AUDIO_PACK_BROKEN;
        break;
    }
}

// ---------------------------------------------------------------------------
// 播放
// ---------------------------------------------------------------------------

static bool aborted(uint32_t serial) {
    return serial != s_serial;
}

static bool write_pcm(const int16_t *pcm, size_t samples) {
    if (bsp_audio_write(pcm, samples * sizeof(int16_t)) == ESP_OK) return true;
    // 写失败：下次播放前重新设一次格式，别用半开的状态继续写。
    ESP_LOGW(TAG, "bsp_audio_write 失败，下次播放前重设格式");
    s_format_ok = false;
    return false;
}

static bool prepare_output(void) {
    if (!s_format_ok) {
        if (bsp_audio_set_format(KO_PACK_SAMPLE_RATE, 16, 1) != ESP_OK) {
            ESP_LOGW(TAG, "设置 16 kHz 单声道失败");
            return false;
        }
        s_format_ok = true;
        s_applied_volume = -1;
    }
    const uint8_t volume = s_volume;
    if (s_applied_volume != volume) {
        bsp_audio_set_volume(volume);
        s_applied_volume = volume;
    }
    return true;
}

// 返回 false 表示出错或被打断，调用方不再继续后面的内容。
static bool play_clip(uint16_t clip_id, uint32_t serial) {
    const ko_pack_reader_t r = reader();
    ko_pack_clip_t clip;
    if (s_state != KO_AUDIO_PACK_READY || !ko_pack_clip(&r, &s_info, clip_id, &clip)) {
        return true;   // 没有这条发音：当作无声播完，不算错误
    }

    uint8_t header[4];
    if (!partition_read(NULL, clip.offset, header, sizeof(header))) return false;
    ko_adpcm_state_t state;
    s_pcm[0] = ko_adpcm_init(&state, header);

    // 第一块先放头里的第 0 个采样，再解 510 个码（凑成偶数），其余每块解 512 个。
    // 除最后一块外每块码数必须是偶数：两个码共用一个字节。
    uint32_t codes_left = clip.samples - 1;
    uint32_t position = clip.offset + sizeof(header);
    size_t produced = 1;
    uint32_t chunk_codes = PCM_CHUNK_SAMPLES - 2;

    for (;;) {
        const uint32_t codes = codes_left < chunk_codes ? codes_left : chunk_codes;
        if (codes > 0) {
            const uint32_t bytes = (codes + 1) / 2;
            if (!partition_read(NULL, position, s_raw, bytes)) return false;
            ko_adpcm_decode(&state, s_raw, codes, s_pcm + produced);
            position += bytes;
            codes_left -= codes;
            produced += codes;
        }
        if (aborted(serial)) return false;
        if (!write_pcm(s_pcm, produced)) return false;
        if (codes_left == 0) return true;
        produced = 0;
        chunk_codes = PCM_CHUNK_SAMPLES;
    }
}

static bool play_silence(uint16_t ms, uint32_t serial) {
    uint32_t remaining = (uint32_t)ms * KO_PACK_SAMPLE_RATE / 1000u;
    memset(s_pcm, 0, sizeof(s_pcm));
    while (remaining > 0) {
        const size_t n = remaining < PCM_CHUNK_SAMPLES ? remaining : PCM_CHUNK_SAMPLES;
        if (aborted(serial) || !write_pcm(s_pcm, n)) return false;
        remaining -= n;
    }
    return true;
}

static bool play_tone(ko_tone_kind_t kind, uint32_t serial) {
    const ko_tone_seq_t *seq = kind == KO_TONE_KIND_RIGHT ? &KO_TONE_RIGHT
                              : kind == KO_TONE_KIND_WRONG ? &KO_TONE_WRONG
                                                           : &KO_TONE_TICK;
    ko_tone_state_t state;
    ko_tone_start(&state, seq);
    for (;;) {
        const size_t n = ko_tone_render(&state, KO_PACK_SAMPLE_RATE, TONE_AMPLITUDE, s_pcm,
                                        PCM_CHUNK_SAMPLES);
        if (n == 0) return true;
        if (aborted(serial) || !write_pcm(s_pcm, n)) return false;
    }
}

static void run_command(const cmd_t *cmd) {
    // 停止命令（type 0）只负责打断，不需要打开输出。
    if (cmd->type == 0 || aborted(cmd->serial) || !prepare_output()) return;
    switch (cmd->type) {
    case CMD_CLIP:
        play_clip(cmd->clip[0], cmd->serial);
        break;
    case CMD_PAIR:
        if (play_clip(cmd->clip[0], cmd->serial) && ko_audio_has_clip(cmd->clip[1]) &&
            play_silence(cmd->gap_ms, cmd->serial)) {
            play_clip(cmd->clip[1], cmd->serial);
        }
        break;
    case CMD_TONE:
        play_tone((ko_tone_kind_t)cmd->tone, cmd->serial);
        break;
    default:
        break;
    }
}

static void audio_task(void *arg) {
    (void)arg;
    cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_queue, &cmd, portMAX_DELAY) != pdTRUE) continue;
        run_command(&cmd);
        // 只记"处理完了哪一条"；是否还在播由 serial 与 done_serial 是否相等推出，
        // 这样"播放期间又来了新命令"不会被这里误判成已经播完。
        s_done_serial = cmd.serial;
    }
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------

esp_err_t ko_audio_init(void) {
    if (s_task) return ESP_OK;
    scan_pack();

    s_queue = xQueueCreate(1, sizeof(cmd_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(audio_task, "ko_audio", AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIORITY,
                    &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

ko_audio_pack_state_t ko_audio_pack_state(void) {
    return s_state;
}

uint32_t ko_audio_pack_slots(void) {
    return s_state == KO_AUDIO_PACK_READY ? s_info.clip_count : 0;
}

bool ko_audio_has_clip(uint16_t clip) {
    if (s_state != KO_AUDIO_PACK_READY || clip >= KO_CLIP_COUNT) return false;
    return (s_present[clip >> 3] >> (clip & 7)) & 1u;
}

void ko_audio_set_volume(uint8_t percent) {
    s_volume = percent > 100 ? 100 : percent;
}

uint8_t ko_audio_volume(void) {
    return s_volume;
}

static void submit(cmd_t *cmd) {
    if (!s_queue) return;
    // 先递增序号：正在播放的任务下一个检查点就会发现被打断，然后取走邮箱里的新命令。
    cmd->serial = __atomic_add_fetch(&s_serial, 1, __ATOMIC_SEQ_CST);
    xQueueOverwrite(s_queue, cmd);
}

void ko_audio_play_clip(uint16_t clip) {
    if (!ko_audio_has_clip(clip)) return;
    cmd_t cmd = { .type = CMD_CLIP, .clip = { clip, KO_NO_CLIP } };
    submit(&cmd);
}

void ko_audio_play_pair(uint16_t first, uint16_t second, uint16_t gap_ms) {
    const bool a = ko_audio_has_clip(first);
    const bool b = ko_audio_has_clip(second);
    if (!a && !b) return;
    if (!a) {   // 只有第二条可播：退化成播单条，不要先空等一个 gap
        ko_audio_play_clip(second);
        return;
    }
    cmd_t cmd = { .type = CMD_PAIR, .clip = { first, second }, .gap_ms = gap_ms };
    submit(&cmd);
}

void ko_audio_play_tone(ko_tone_kind_t kind) {
    cmd_t cmd = { .type = CMD_TONE, .tone = (uint8_t)kind };
    submit(&cmd);
}

void ko_audio_stop(void) {
    // 用一个"无内容"的命令打断：序号递增即可，任务会在下一个检查点退出当前播放。
    cmd_t cmd = { .type = 0 };
    submit(&cmd);
}

bool ko_audio_is_playing(void) {
    return s_done_serial != s_serial;
}
