// main/ko_progress.c —— 学习进度的状态字节与序列化。
#include "ko_progress.h"

#include <string.h>

#define STATE_SEEN 0x80u
#define STATE_BOX_MASK 0x07u

uint8_t ko_state_box(uint8_t state) {
    uint8_t box = state & STATE_BOX_MASK;
    return box > KO_BOX_MAX ? KO_BOX_MAX : box;
}

bool ko_state_seen(uint8_t state) {
    return (state & STATE_SEEN) != 0;
}

bool ko_state_mastered(uint8_t state) {
    return ko_state_seen(state) && ko_state_box(state) >= KO_MASTERED_BOX;
}

uint8_t ko_state_mark_seen(uint8_t state) {
    return (uint8_t)(STATE_SEEN | ko_state_box(state));
}

uint8_t ko_state_rate(uint8_t state, bool known) {
    uint8_t box = ko_state_box(state);
    if (known) {
        if (box < KO_BOX_MAX) box++;
    } else {
        box = 0;
    }
    return (uint8_t)(STATE_SEEN | box);
}

uint8_t *ko_progress_states(ko_progress_t *progress, ko_kind_t kind) {
    switch (kind) {
    case KO_KIND_LETTER: return progress->letters;
    case KO_KIND_WORD:   return progress->words;
    case KO_KIND_PHRASE: return progress->phrases;
    default:             return NULL;
    }
}

const uint8_t *ko_progress_states_const(const ko_progress_t *progress, ko_kind_t kind) {
    return ko_progress_states((ko_progress_t *)progress, kind);
}

uint16_t ko_progress_seen(const uint8_t *states, uint16_t first, uint16_t count) {
    uint16_t seen = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (ko_state_seen(states[first + i])) seen++;
    }
    return seen;
}

uint16_t ko_progress_mastered(const uint8_t *states, uint16_t first, uint16_t count) {
    uint16_t mastered = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (ko_state_mastered(states[first + i])) mastered++;
    }
    return mastered;
}

// ---------------------------------------------------------------------------
// 序列化
// ---------------------------------------------------------------------------

#define HEADER_BYTES 12
#define TRAILER_BYTES 12
#define CRC_BYTES 4

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

size_t ko_progress_serialize(const ko_progress_t *progress, uint8_t *buf, size_t cap) {
    const size_t total = KO_PROGRESS_MAX_BYTES;
    if (cap < total) return 0;

    uint8_t *p = buf;
    memcpy(p, "KOPG", 4);
    p[4] = 1;  // 版本
    p[5] = 0;
    put_u16(p + 6, KO_LETTER_COUNT);
    put_u16(p + 8, KO_WORD_COUNT);
    put_u16(p + 10, KO_PHRASE_COUNT);
    p += HEADER_BYTES;

    memcpy(p, progress->letters, KO_LETTER_COUNT);
    p += KO_LETTER_COUNT;
    memcpy(p, progress->words, KO_WORD_COUNT);
    p += KO_WORD_COUNT;
    memcpy(p, progress->phrases, KO_PHRASE_COUNT);
    p += KO_PHRASE_COUNT;

    put_u32(p, progress->quiz_correct_total);
    put_u32(p + 4, progress->quiz_answered_total);
    put_u32(p + 8, progress->sessions);
    p += TRAILER_BYTES;

    put_u32(p, ko_crc32(buf, (size_t)(p - buf)));
    return total;
}

// 把不合法的位清掉：存档可能来自别的版本或被破坏，不能让脏位污染逻辑。
static uint8_t sanitize_state(uint8_t raw) {
    return (uint8_t)((raw & STATE_SEEN) | ko_state_box(raw));
}

static void copy_states(uint8_t *dst, size_t dst_count, const uint8_t *src, size_t src_count) {
    memset(dst, 0, dst_count);
    size_t n = src_count < dst_count ? src_count : dst_count;
    for (size_t i = 0; i < n; i++) dst[i] = sanitize_state(src[i]);
}

bool ko_progress_parse(ko_progress_t *out, const uint8_t *buf, size_t len) {
    if (len < HEADER_BYTES + TRAILER_BYTES + CRC_BYTES) return false;
    if (memcmp(buf, "KOPG", 4) != 0 || buf[4] != 1) return false;

    const size_t n_letters = get_u16(buf + 6);
    const size_t n_words = get_u16(buf + 8);
    const size_t n_phrases = get_u16(buf + 10);
    const size_t expected =
        HEADER_BYTES + n_letters + n_words + n_phrases + TRAILER_BYTES + CRC_BYTES;
    if (len != expected) return false;
    if (ko_crc32(buf, len - CRC_BYTES) != get_u32(buf + len - CRC_BYTES)) return false;

    ko_progress_t parsed;
    const uint8_t *p = buf + HEADER_BYTES;
    copy_states(parsed.letters, KO_LETTER_COUNT, p, n_letters);
    p += n_letters;
    copy_states(parsed.words, KO_WORD_COUNT, p, n_words);
    p += n_words;
    copy_states(parsed.phrases, KO_PHRASE_COUNT, p, n_phrases);
    p += n_phrases;
    parsed.quiz_correct_total = get_u32(p);
    parsed.quiz_answered_total = get_u32(p + 4);
    parsed.sessions = get_u32(p + 8);

    *out = parsed;
    return true;
}
