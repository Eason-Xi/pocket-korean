// tests/test_ko_content.c —— 内容数据表的结构不变量。
//
// 这些是"内容一旦破坏，界面 / 测验 / 发音就会出错"的约束，放在测试里，
// 改 content.json 时如果不小心违反，静态门禁立刻会报。
#include <string.h>

#include "ko_content.h"
#include "ko_quiz.h"
#include "ko_session.h"
#include "ko_test.h"

static bool is_ascii(const char *s) {
    for (; *s; s++) {
        if ((unsigned char)*s >= 0x80) return false;
    }
    return true;
}

static void test_counts_and_kinds(void) {
    CHECK(ko_kind_count(KO_KIND_LETTER) == KO_LETTER_COUNT);
    CHECK(ko_kind_count(KO_KIND_WORD) == KO_WORD_COUNT);
    CHECK(ko_kind_count(KO_KIND_PHRASE) == KO_PHRASE_COUNT);
    CHECK(ko_kind_count(KO_KIND_COUNT) == 0);
    CHECK(KO_LETTER_COUNT == 40);   // 19 个辅音 + 21 个元音
}

static void test_letter_groups(void) {
    uint16_t next = 0;
    for (int g = 0; g < KO_LETTER_GROUP_COUNT; g++) {
        CHECK(ko_letter_groups[g].first == next);   // 连续、无空洞
        CHECK(ko_letter_groups[g].count >= KO_QUIZ_OPTIONS);   // 字母测验要凑够 4 个不同选项
        CHECK(ko_letter_groups[g].zh[0] && ko_letter_groups[g].ko[0]);
        for (int i = 0; i < ko_letter_groups[g].count; i++) {
            CHECK(ko_letter_group_of((uint16_t)(next + i)) == g);
            // 同一组里读音文字不重复，字母测验的选项才不会撞车。
            for (int j = i + 1; j < ko_letter_groups[g].count; j++) {
                CHECK(strcmp(ko_letters[next + i].sound, ko_letters[next + j].sound) != 0);
            }
        }
        next = (uint16_t)(next + ko_letter_groups[g].count);
    }
    CHECK(next == KO_LETTER_COUNT);
    CHECK(ko_letter_group_of(KO_LETTER_COUNT) == -1);
}

static void test_letters_fields(void) {
    for (int i = 0; i < KO_LETTER_COUNT; i++) {
        const ko_letter_t *l = &ko_letters[i];
        CHECK(l->ch[0] && l->name[0] && l->hint[0] && l->pair[0] && l->syl[0] && l->ex[0] && l->ex_zh[0]);
        // 罗马音 / 读音只能是 ASCII：字体里 ASCII 是保证覆盖的。
        CHECK(is_ascii(l->name_rom) && is_ascii(l->sound) && is_ascii(l->syl_rom) && is_ascii(l->ex_rom));
        CHECK(l->name_clip < KO_CLIP_COUNT && l->ex_clip < KO_CLIP_COUNT);
        for (int j = i + 1; j < KO_LETTER_COUNT; j++) CHECK(strcmp(l->ch, ko_letters[j].ch) != 0);
    }
}

static void check_decks(const ko_deck_t *decks, size_t deck_count, const ko_item_t *items,
                        size_t item_count, int (*owner)(uint16_t)) {
    uint16_t next = 0;
    for (size_t d = 0; d < deck_count; d++) {
        CHECK(decks[d].first == next);
        CHECK(decks[d].count >= KO_QUIZ_OPTIONS);   // 测验要在同一组里凑 4 个选项
        CHECK(decks[d].zh[0]);
        for (uint16_t i = 0; i < decks[d].count; i++) {
            const uint16_t idx = (uint16_t)(next + i);
            CHECK(owner(idx) == (int)d);
            CHECK(items[idx].ko[0] && items[idx].rom[0] && items[idx].zh[0]);
            CHECK(is_ascii(items[idx].rom));
            CHECK(items[idx].clip < KO_CLIP_COUNT);
            // 同一组内韩文、中文都不重复：干扰项不会和正确答案显示成一样。
            for (uint16_t j = (uint16_t)(i + 1); j < decks[d].count; j++) {
                CHECK(strcmp(items[idx].ko, items[next + j].ko) != 0);
                CHECK(strcmp(items[idx].zh, items[next + j].zh) != 0);
            }
        }
        next = (uint16_t)(next + decks[d].count);
    }
    CHECK(next == item_count);
    CHECK(owner((uint16_t)item_count) == -1);
}

static void test_decks(void) {
    check_decks(ko_topics, KO_TOPIC_COUNT, ko_words, KO_WORD_COUNT, ko_word_topic);
    check_decks(ko_phrase_groups, KO_PHRASE_GROUP_COUNT, ko_phrases, KO_PHRASE_COUNT,
                ko_phrase_group_of);

    // 一轮学习的队列上限要能容纳整个 deck 的选取（deck 更大也没问题，只是取前 N 张）。
    for (int t = 0; t < KO_TOPIC_COUNT; t++) CHECK(ko_topics[t].count <= 32);
    for (int g = 0; g < KO_PHRASE_GROUP_COUNT; g++) CHECK(ko_phrase_groups[g].count <= 32);

    // 全部词汇的韩文唯一：看中文选韩文 / 听音选词按韩文区分，同形异义词会有歧义。
    for (int i = 0; i < KO_WORD_COUNT; i++) {
        for (int j = i + 1; j < KO_WORD_COUNT; j++) CHECK(strcmp(ko_words[i].ko, ko_words[j].ko) != 0);
    }
}

// 发音片段：相同文本共用同一个 id，不同文本 id 不同，没有孤立的 id。
static const char *text_of_clip(uint16_t clip) {
    for (int i = 0; i < KO_LETTER_COUNT; i++) {
        if (ko_letters[i].name_clip == clip) return ko_letters[i].name;
        if (ko_letters[i].ex_clip == clip) return ko_letters[i].ex;
    }
    for (int i = 0; i < KO_WORD_COUNT; i++) {
        if (ko_words[i].clip == clip) return ko_words[i].ko;
    }
    for (int i = 0; i < KO_PHRASE_COUNT; i++) {
        if (ko_phrases[i].clip == clip) return ko_phrases[i].ko;
    }
    return NULL;
}

static void check_clip_text(uint16_t clip, const char *text) {
    const char *owner = text_of_clip(clip);
    CHECK(owner != NULL);
    CHECK(strcmp(owner, text) == 0);   // 同一个 clip id 永远对应同一段文字
}

static void test_clips(void) {
    for (int i = 0; i < KO_LETTER_COUNT; i++) {
        check_clip_text(ko_letters[i].name_clip, ko_letters[i].name);
        check_clip_text(ko_letters[i].ex_clip, ko_letters[i].ex);
    }
    for (int i = 0; i < KO_WORD_COUNT; i++) check_clip_text(ko_words[i].clip, ko_words[i].ko);
    for (int i = 0; i < KO_PHRASE_COUNT; i++) check_clip_text(ko_phrases[i].clip, ko_phrases[i].ko);

    // 每个 id 都被用到，没有生成了却永远不会播放的发音。
    for (uint16_t id = 0; id < KO_CLIP_COUNT; id++) CHECK(text_of_clip(id) != NULL);

    CHECK(ko_word_clip(0) == ko_words[0].clip);
    CHECK(ko_word_clip(KO_WORD_COUNT) == KO_NO_CLIP);
    CHECK(ko_phrase_clip(0) == ko_phrases[0].clip);
    CHECK(ko_phrase_clip(KO_PHRASE_COUNT) == KO_NO_CLIP);
}

int main(void) {
    test_counts_and_kinds();
    test_letter_groups();
    test_letters_fields();
    test_decks();
    test_clips();
    puts("test_ko_content: PASS");
    return 0;
}
