// main/ko_progress.h —— 学习进度：每个条目一个状态字节 + 测验累计数，以及带校验的序列化。
// 纯逻辑，不依赖 ESP-IDF / LVGL，可在主机上直接测试。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ko_content.h"
#include "ko_crc32.h"

// 条目状态字节：bit7 = 是否看过；bit0..2 = 熟练度盒子 0..KO_BOX_MAX。
//   盒子 0 = 新学 / 刚答错；答对/记住一次 +1；答错/没记住回到 0。
// 没有时钟（板上无 RTC 时间源），所以不做"按天到期"，只按盒子高低决定复习优先级。
#define KO_BOX_MAX 5
// 盒子达到这个值算"已掌握"（连续答对/记住 3 次）。
#define KO_MASTERED_BOX 3

uint8_t ko_state_box(uint8_t state);
bool ko_state_seen(uint8_t state);
bool ko_state_mastered(uint8_t state);
// 看过（例如翻过字母卡）：置 seen，盒子不变。
uint8_t ko_state_mark_seen(uint8_t state);
// 评级：known → 盒子 +1（封顶），否则回到 0；同时置 seen。
uint8_t ko_state_rate(uint8_t state, bool known);

typedef struct {
    uint8_t letters[KO_LETTER_COUNT];
    uint8_t words[KO_WORD_COUNT];
    uint8_t phrases[KO_PHRASE_COUNT];
    uint32_t quiz_correct_total;   // 测验累计答对
    uint32_t quiz_answered_total;  // 测验累计作答
    uint32_t sessions;             // 学习轮次；用作下一轮复习起点的轮转偏移
} ko_progress_t;

// 取某类别的状态数组；kind 非法返回 NULL。
uint8_t *ko_progress_states(ko_progress_t *progress, ko_kind_t kind);
const uint8_t *ko_progress_states_const(const ko_progress_t *progress, ko_kind_t kind);

// 统计区间 [first, first+count) 内看过 / 已掌握的条目数。
uint16_t ko_progress_seen(const uint8_t *states, uint16_t first, uint16_t count);
uint16_t ko_progress_mastered(const uint8_t *states, uint16_t first, uint16_t count);

// 序列化 / 解析。格式（小端）：
//   "KOPG" | ver u8 | 0 | n_letters u16 | n_words u16 | n_phrases u16 |
//   状态字节... | correct u32 | answered u32 | sessions u32 | crc32 u32
// 解析时条目数与当前内容不同（内容后来增删）也能读：按较小的数量拷贝，多出的置 0。
// 所以内容更新只要"只在末尾追加条目"，用户已有进度就不会丢。
#define KO_PROGRESS_MAX_BYTES (12 + KO_LETTER_COUNT + KO_WORD_COUNT + KO_PHRASE_COUNT + 12 + 4)

// 返回写入字节数；缓冲区不够返回 0。
size_t ko_progress_serialize(const ko_progress_t *progress, uint8_t *buf, size_t cap);
// 魔数 / 版本 / 长度 / CRC 任一不符返回 false，此时 *out 保持不变。
bool ko_progress_parse(ko_progress_t *out, const uint8_t *buf, size_t len);
