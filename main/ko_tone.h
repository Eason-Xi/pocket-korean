// main/ko_tone.h —— 答题反馈提示音的合成。纯逻辑，可在主机上测试。
//
// 提示音不依赖发音包：没有发音包时，答对 / 答错仍然有声音反馈。
// 每个音符带起音和收音包络，避免开头结尾出现"啪"的爆音。
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t freq_hz;   // 0 表示静音间隔
    uint16_t ms;
} ko_tone_note_t;

typedef struct {
    const ko_tone_note_t *notes;
    uint8_t note_count;
} ko_tone_seq_t;

// 内置提示音。
extern const ko_tone_seq_t KO_TONE_RIGHT;   // 答对：上行两音
extern const ko_tone_seq_t KO_TONE_WRONG;   // 答错：低沉一声
extern const ko_tone_seq_t KO_TONE_TICK;    // 音量调节时的确认音

// 流式渲染：状态由调用方持有，每次取一块采样，直到返回 0 表示播完。
typedef struct {
    const ko_tone_seq_t *seq;
    uint8_t note;         // 当前音符
    uint32_t pos;         // 当前音符内已输出的采样数
    uint32_t phase;       // 相位累加器（Q16，1.0 = 65536）
} ko_tone_state_t;

void ko_tone_start(ko_tone_state_t *state, const ko_tone_seq_t *seq);

// 写最多 max 个采样，返回实际写出数；播完后返回 0。amplitude 是峰值（<= 32767）。
size_t ko_tone_render(ko_tone_state_t *state, uint32_t sample_rate, int16_t amplitude,
                      int16_t *out, size_t max);

// 整段序列的总采样数（用于测试和预估时长）。
uint32_t ko_tone_total_samples(const ko_tone_seq_t *seq, uint32_t sample_rate);
