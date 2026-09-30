// main/ko_content.c —— 内容表的查询函数（数据表在生成的 ko_content_gen.c）。
#include "ko_content.h"

size_t ko_kind_count(ko_kind_t kind) {
    switch (kind) {
    case KO_KIND_LETTER: return KO_LETTER_COUNT;
    case KO_KIND_WORD:   return KO_WORD_COUNT;
    case KO_KIND_PHRASE: return KO_PHRASE_COUNT;
    default:             return 0;
    }
}

// deck 是按 first 递增的连续区间，线性扫描即可（最多十几个）。
static int deck_of(const ko_deck_t *decks, size_t deck_count, uint16_t item) {
    for (size_t i = 0; i < deck_count; i++) {
        if (item >= decks[i].first && item < decks[i].first + decks[i].count) return (int)i;
    }
    return -1;
}

int ko_word_topic(uint16_t word) {
    return deck_of(ko_topics, KO_TOPIC_COUNT, word);
}

int ko_phrase_group_of(uint16_t phrase) {
    return deck_of(ko_phrase_groups, KO_PHRASE_GROUP_COUNT, phrase);
}

int ko_letter_group_of(uint16_t letter) {
    for (size_t i = 0; i < KO_LETTER_GROUP_COUNT; i++) {
        const ko_letter_group_t *g = &ko_letter_groups[i];
        if (letter >= g->first && letter < (uint16_t)(g->first + g->count)) return (int)i;
    }
    return -1;
}

uint16_t ko_word_clip(uint16_t word) {
    return word < KO_WORD_COUNT ? ko_words[word].clip : KO_NO_CLIP;
}

uint16_t ko_phrase_clip(uint16_t phrase) {
    return phrase < KO_PHRASE_COUNT ? ko_phrases[phrase].clip : KO_NO_CLIP;
}
