// tests/test_ko_progress.c —— CRC-32、条目状态、进度序列化。
#include <string.h>

#include "ko_crc32.h"
#include "ko_progress.h"
#include "ko_test.h"

static void test_crc32(void) {
    CHECK(ko_crc32("123456789", 9) == 0xCBF43926u);   // zlib 的标准校验值
    CHECK(ko_crc32("", 0) == 0u);

    // 增量计算必须和一次性计算一致（发音包索引分块校验用的就是这条路径）。
    uint32_t crc = 0xFFFFFFFFu;
    crc = ko_crc32_update(crc, "1234", 4);
    crc = ko_crc32_update(crc, "56789", 5);
    CHECK(~crc == 0xCBF43926u);
}

static void test_state_transitions(void) {
    uint8_t s = 0;
    CHECK(!ko_state_seen(s));
    CHECK(ko_state_box(s) == 0);
    CHECK(!ko_state_mastered(s));

    // 连续"记住"：盒子逐级升高，封顶 KO_BOX_MAX。
    for (int i = 1; i <= KO_BOX_MAX + 3; i++) {
        s = ko_state_rate(s, true);
        CHECK(ko_state_seen(s));
        const int expected = i < KO_BOX_MAX ? i : KO_BOX_MAX;
        CHECK(ko_state_box(s) == expected);
        CHECK(ko_state_mastered(s) == (expected >= KO_MASTERED_BOX));
    }
    // 一次"没记住"就回到 0，但仍然算看过。
    s = ko_state_rate(s, false);
    CHECK(ko_state_box(s) == 0);
    CHECK(ko_state_seen(s));
    CHECK(!ko_state_mastered(s));

    // mark_seen 不改盒子。
    s = ko_state_rate(0, true);
    s = ko_state_rate(s, true);
    CHECK(ko_state_box(ko_state_mark_seen(s)) == 2);
    CHECK(ko_state_seen(ko_state_mark_seen(0)));

    // 存档里的脏位不能造成越界：盒子夹到 KO_BOX_MAX，"已掌握"必须同时看过。
    CHECK(ko_state_box(0x07) == KO_BOX_MAX);
    CHECK(!ko_state_mastered(0x04));   // 盒子 4 但没有 seen 位
}

static void test_counts(void) {
    uint8_t states[8] = { 0 };
    states[1] = ko_state_mark_seen(0);
    states[2] = ko_state_rate(ko_state_rate(ko_state_rate(0, true), true), true);   // 盒子 3
    states[5] = ko_state_rate(0, true);
    CHECK(ko_progress_seen(states, 0, 8) == 3);
    CHECK(ko_progress_mastered(states, 0, 8) == 1);
    CHECK(ko_progress_seen(states, 3, 3) == 1);    // 区间 [3,6)：只有 5 看过
    CHECK(ko_progress_mastered(states, 3, 3) == 0);   // 它只是盒子 1
    CHECK(ko_progress_seen(states, 6, 2) == 0);
}

static void fill_sample(ko_progress_t *p) {
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < KO_LETTER_COUNT; i++) p->letters[i] = (uint8_t)(0x80 | (i % 6));
    for (int i = 0; i < KO_WORD_COUNT; i++) p->words[i] = (uint8_t)((i % 3) ? 0x80 | (i % 6) : 0);
    for (int i = 0; i < KO_PHRASE_COUNT; i++) p->phrases[i] = (uint8_t)(0x80 | ((i * 5) % 6));
    p->quiz_correct_total = 123456;
    p->quiz_answered_total = 200000;
    p->sessions = 42;
}

static void test_roundtrip(void) {
    ko_progress_t in, out;
    fill_sample(&in);
    memset(&out, 0xEE, sizeof(out));

    uint8_t buf[KO_PROGRESS_MAX_BYTES];
    CHECK(ko_progress_serialize(&in, buf, sizeof(buf) - 1) == 0);   // 缓冲区不够
    CHECK(ko_progress_serialize(&in, buf, sizeof(buf)) == sizeof(buf));
    CHECK(ko_progress_parse(&out, buf, sizeof(buf)));
    // 逐字段比较：结构体数组后面有对齐填充字节，memcmp 会比到未初始化的填充。
    CHECK(memcmp(in.letters, out.letters, sizeof(in.letters)) == 0);
    CHECK(memcmp(in.words, out.words, sizeof(in.words)) == 0);
    CHECK(memcmp(in.phrases, out.phrases, sizeof(in.phrases)) == 0);
    CHECK(in.quiz_correct_total == out.quiz_correct_total);
    CHECK(in.quiz_answered_total == out.quiz_answered_total);
    CHECK(in.sessions == out.sessions);
}

static void test_corruption_is_rejected(void) {
    ko_progress_t in, out;
    fill_sample(&in);
    uint8_t buf[KO_PROGRESS_MAX_BYTES];
    CHECK(ko_progress_serialize(&in, buf, sizeof(buf)) == sizeof(buf));

    // 任何一个字节被改动都必须被发现（CRC 覆盖整个存档，含魔数与计数）。
    for (size_t i = 0; i < sizeof(buf); i++) {
        uint8_t copy[KO_PROGRESS_MAX_BYTES];
        memcpy(copy, buf, sizeof(buf));
        copy[i] ^= 0x10;
        ko_progress_t untouched;
        memset(&untouched, 0x5A, sizeof(untouched));
        CHECK(!ko_progress_parse(&untouched, copy, sizeof(copy)));
        // 解析失败时不能改动调用方的数据，否则会把半截脏数据当作进度。
        ko_progress_t marker;
        memset(&marker, 0x5A, sizeof(marker));
        CHECK(memcmp(&untouched, &marker, sizeof(marker)) == 0);   // 两边都由 memset 初始化，填充一致
    }

    CHECK(!ko_progress_parse(&out, buf, sizeof(buf) - 1));   // 截断
    CHECK(!ko_progress_parse(&out, buf, 0));
    CHECK(!ko_progress_parse(&out, buf, 5));
}

// 手工造一份"内容数量不同"的存档（模拟固件升级前后条目增减），并补上正确的 CRC。
static size_t build_archive(uint8_t *buf, size_t n_letters, size_t n_words, size_t n_phrases,
                            uint8_t fill_letters, uint8_t fill_words, uint8_t fill_phrases) {
    uint8_t *p = buf;
    memcpy(p, "KOPG", 4);
    p[4] = 1;
    p[5] = 0;
    p[6] = (uint8_t)n_letters; p[7] = (uint8_t)(n_letters >> 8);
    p[8] = (uint8_t)n_words;   p[9] = (uint8_t)(n_words >> 8);
    p[10] = (uint8_t)n_phrases; p[11] = (uint8_t)(n_phrases >> 8);
    p += 12;
    memset(p, fill_letters, n_letters); p += n_letters;
    memset(p, fill_words, n_words);     p += n_words;
    memset(p, fill_phrases, n_phrases); p += n_phrases;
    memset(p, 0, 12);
    p[0] = 7;   // quiz_correct_total = 7
    p += 12;
    const uint32_t crc = ko_crc32(buf, (size_t)(p - buf));
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(crc >> (8 * i));
    return (size_t)(p - buf) + 4;
}

static void test_content_size_changes(void) {
    uint8_t buf[1024];
    ko_progress_t out;
    memset(&out, 0xEE, sizeof(out));

    // 旧版本内容更少：已有的进度保留，新增的条目从 0 开始。
    size_t len = build_archive(buf, KO_LETTER_COUNT, KO_WORD_COUNT - 5, KO_PHRASE_COUNT,
                               0x81, 0x82, 0x83);
    CHECK(ko_progress_parse(&out, buf, len));
    CHECK(out.words[0] == 0x82);
    CHECK(out.words[KO_WORD_COUNT - 6] == 0x82);
    CHECK(out.words[KO_WORD_COUNT - 5] == 0);
    CHECK(out.words[KO_WORD_COUNT - 1] == 0);
    CHECK(out.letters[KO_LETTER_COUNT - 1] == 0x81);
    CHECK(out.phrases[KO_PHRASE_COUNT - 1] == 0x83);
    CHECK(out.quiz_correct_total == 7);

    // 新版本内容更少：多出来的存档字节被丢弃，不越界。
    len = build_archive(buf, KO_LETTER_COUNT + 4, KO_WORD_COUNT + 9, KO_PHRASE_COUNT + 2,
                        0x81, 0x82, 0x83);
    CHECK(ko_progress_parse(&out, buf, len));
    CHECK(out.words[KO_WORD_COUNT - 1] == 0x82);
    CHECK(out.letters[KO_LETTER_COUNT - 1] == 0x81);

    // 脏位被清理：bit3..6 置位、盒子越界的字节不能原样进入内存。
    len = build_archive(buf, KO_LETTER_COUNT, KO_WORD_COUNT, KO_PHRASE_COUNT, 0xFF, 0x7F, 0x78);
    CHECK(ko_progress_parse(&out, buf, len));
    CHECK(out.letters[0] == (0x80 | KO_BOX_MAX));
    CHECK(out.words[0] == KO_BOX_MAX);   // 没有 seen 位，只保留夹紧后的盒子
    CHECK(out.phrases[0] == 0);
}

int main(void) {
    test_crc32();
    test_state_transitions();
    test_counts();
    test_roundtrip();
    test_corruption_is_rejected();
    test_content_size_changes();
    puts("test_ko_progress: PASS");
    return 0;
}
