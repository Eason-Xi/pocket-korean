// main/ko_content.h —— 韩语学习内容的数据结构与访问接口（纯数据，不依赖 ESP-IDF / LVGL）。
//
// 数据表本身由 tools/gen_korean_assets.py 从 assets/korean/content.json 生成
// （main/ko_content_gen.c），驻留 Flash 的 const 数据，运行时不占 RAM。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ko_content_gen.h"

// 条目没有对应发音片段。
#define KO_NO_CLIP 0xFFFFu

// 三类学习内容。进度、测验、卡片都按"类别 + 类别内序号"定位一个条目。
typedef enum {
    KO_KIND_LETTER = 0,
    KO_KIND_WORD,
    KO_KIND_PHRASE,
    KO_KIND_COUNT,
} ko_kind_t;

typedef struct {
    const char *zh;       // 中文组名
    const char *ko;       // 韩文组名
    uint8_t first;        // 该组第一个字母在 ko_letters 里的序号
    uint8_t count;
} ko_letter_group_t;

typedef struct {
    const char *ch;       // 兼容字母，如 "ㄱ"
    const char *name;     // 字母名，如 "기역"
    const char *name_rom; // 字母名罗马音
    const char *sound;    // 简短读音（测验选项用，ASCII）
    const char *hint;     // 近似汉语读音提示（中文）
    const char *pair;     // 拼读式左半，如 "ㄱ + ㅏ"
    const char *syl;      // 拼出的音节，如 "가"
    const char *syl_rom;
    const char *ex;       // 例词
    const char *ex_rom;
    const char *ex_zh;
    uint16_t name_clip;   // 字母名发音片段
    uint16_t ex_clip;     // 例词发音片段
} ko_letter_t;

// 词汇或短语条目。
typedef struct {
    const char *ko;
    const char *rom;
    const char *zh;
    uint16_t clip;
} ko_item_t;

// 一个主题（词汇）或场景（短语）：在对应条目数组里的连续区间。
typedef struct {
    const char *zh;
    uint16_t first;
    uint16_t count;
} ko_deck_t;

extern const ko_letter_group_t ko_letter_groups[KO_LETTER_GROUP_COUNT];
extern const ko_letter_t ko_letters[KO_LETTER_COUNT];
extern const ko_item_t ko_words[KO_WORD_COUNT];
extern const ko_deck_t ko_topics[KO_TOPIC_COUNT];
extern const ko_item_t ko_phrases[KO_PHRASE_COUNT];
extern const ko_deck_t ko_phrase_groups[KO_PHRASE_GROUP_COUNT];

// 某类别的条目总数。
size_t ko_kind_count(ko_kind_t kind);

// 找到包含条目的主题 / 场景 / 字母组序号；越界返回 -1。
int ko_word_topic(uint16_t word);
int ko_phrase_group_of(uint16_t phrase);
int ko_letter_group_of(uint16_t letter);

// 条目的发音片段；越界或没有返回 KO_NO_CLIP。
uint16_t ko_word_clip(uint16_t word);
uint16_t ko_phrase_clip(uint16_t phrase);
