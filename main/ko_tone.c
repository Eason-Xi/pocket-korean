// main/ko_tone.c —— 提示音合成：查表正弦 + 线性起音 / 收音。
// 全程整数运算：ESP32-C3 没有 FPU，浮点是软件模拟，而且主机测试也不必链接 libm。
#include "ko_tone.h"

static const ko_tone_note_t RIGHT_NOTES[] = { { 880, 90 }, { 1175, 150 } };
static const ko_tone_note_t WRONG_NOTES[] = { { 196, 260 } };
static const ko_tone_note_t TICK_NOTES[] = { { 1000, 60 } };

const ko_tone_seq_t KO_TONE_RIGHT = { RIGHT_NOTES, 2 };
const ko_tone_seq_t KO_TONE_WRONG = { WRONG_NOTES, 1 };
const ko_tone_seq_t KO_TONE_TICK = { TICK_NOTES, 1 };

#define TABLE_BITS 6
#define TABLE_SIZE (1 << TABLE_BITS)
#define ENVELOPE_MS 8   // 起音 / 收音各 8 ms

// 一个周期 64 点的正弦表（多存一个点 = 第 0 点，省掉插值时的回绕判断）。
// 值 = round(32767 * sin(2π·i/64))，i = 0..64。
static const int16_t s_table[TABLE_SIZE + 1] = {
    0, 3212, 6393, 9512, 12539, 15446, 18204, 20787,
    23170, 25329, 27245, 28898, 30273, 31356, 32137, 32609,
    32767, 32609, 32137, 31356, 30273, 28898, 27245, 25329,
    23170, 20787, 18204, 15446, 12539, 9512, 6393, 3212,
    0, -3212, -6393, -9512, -12539, -15446, -18204, -20787,
    -23170, -25329, -27245, -28898, -30273, -31356, -32137, -32609,
    -32767, -32609, -32137, -31356, -30273, -28898, -27245, -25329,
    -23170, -20787, -18204, -15446, -12539, -9512, -6393, -3212,
    0,
};

static uint32_t note_samples(const ko_tone_note_t *note, uint32_t rate) {
    return (uint32_t)((uint64_t)note->ms * rate / 1000u);
}

uint32_t ko_tone_total_samples(const ko_tone_seq_t *seq, uint32_t sample_rate) {
    uint32_t total = 0;
    for (uint8_t i = 0; i < seq->note_count; i++) total += note_samples(&seq->notes[i], sample_rate);
    return total;
}

void ko_tone_start(ko_tone_state_t *state, const ko_tone_seq_t *seq) {
    state->seq = seq;
    state->note = 0;
    state->pos = 0;
    state->phase = 0;
}

size_t ko_tone_render(ko_tone_state_t *state, uint32_t sample_rate, int16_t amplitude,
                      int16_t *out, size_t max) {
    size_t written = 0;

    while (written < max && state->note < state->seq->note_count) {
        const ko_tone_note_t *note = &state->seq->notes[state->note];
        const uint32_t length = note_samples(note, sample_rate);
        if (state->pos >= length) {
            state->note++;
            state->pos = 0;
            state->phase = 0;
            continue;
        }

        const uint32_t ramp = sample_rate * ENVELOPE_MS / 1000u;
        // 相位增量（Q16）：freq / rate * 65536。
        const uint32_t step = (uint32_t)(((uint64_t)note->freq_hz << 16) / sample_rate);

        int32_t value = 0;
        if (note->freq_hz != 0) {
            // 高 6 位查表，低 10 位在相邻两点之间线性插值。
            const uint32_t index = (state->phase >> 10) & (TABLE_SIZE - 1);
            const int32_t frac = (int32_t)(state->phase & 0x3FFu);
            const int32_t a = s_table[index];
            const int32_t b = s_table[index + 1];
            value = a + (((b - a) * frac) >> 10);

            // 包络：起音、收音线性过渡；音符比两段包络还短时取较小者。
            int32_t gain = 1024;
            if (state->pos < ramp) gain = (int32_t)(state->pos * 1024u / ramp);
            const uint32_t remain = length - state->pos;
            if (remain < ramp) {
                const int32_t tail = (int32_t)(remain * 1024u / ramp);
                if (tail < gain) gain = tail;
            }
            value = (int32_t)(((int64_t)value * amplitude / 32767) * gain / 1024);
            state->phase += step;
        }
        out[written++] = (int16_t)value;
        state->pos++;
    }
    return written;
}
