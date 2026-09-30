// tests/test_ko_session.c —— 卡片学习队列。
#include <string.h>

#include "ko_progress.h"
#include "ko_session.h"
#include "ko_test.h"

static bool contains(const ko_session_t *s, uint16_t item) {
    for (uint8_t i = 0; i < s->len; i++) {
        if (s->item[i] == item) return true;
    }
    return false;
}

static void test_begin_order_and_rotation(void) {
    uint8_t states[32];
    memset(states, 0, sizeof(states));
    ko_session_t s;

    // 全新的 12 张卡：一轮只取 10 张，按顺序。
    ko_session_begin(&s, states, 4, 12, 0);
    CHECK(s.total == KO_SESSION_MAX && s.len == KO_SESSION_MAX);
    for (int i = 0; i < KO_SESSION_MAX; i++) CHECK(s.item[i] == 4 + i);

    // 轮转：下一轮从别的位置开始，覆盖到之前被落下的卡。
    ko_session_begin(&s, states, 4, 12, 10);
    CHECK(s.item[0] == 4 + 10);
    CHECK(s.item[1] == 4 + 11);
    CHECK(s.item[2] == 4 + 0);
    CHECK(contains(&s, 4 + 11));

    // 卡片比一轮上限少：全部取出。
    ko_session_begin(&s, states, 0, 5, 0);
    CHECK(s.total == 5 && s.len == 5);
    // 空区间：不能崩。
    ko_session_begin(&s, states, 0, 0, 0);
    CHECK(!ko_session_active(&s));
    CHECK(ko_session_current(&s) == 0);
}

static void test_priority(void) {
    uint8_t states[16];
    memset(states, 0, sizeof(states));
    // 卡 0..3 已经很熟（盒子 5），卡 4 看过但盒子 0，其余没看过。
    for (int i = 0; i < 4; i++) {
        uint8_t st = 0;
        for (int k = 0; k < KO_BOX_MAX; k++) st = ko_state_rate(st, true);
        states[i] = st;
    }
    states[4] = ko_state_rate(0, false);   // 看过、盒子 0

    ko_session_t s;
    ko_session_begin(&s, states, 0, 12, 0);
    CHECK(s.len == KO_SESSION_MAX);
    // 12 张里没看过的是 5..11 共 7 张，先取它们；再取"看过但盒子 0"的 4；
    // 最后才轮到盒子 5 的熟卡（按位置补满一轮的 10 张）。
    CHECK(s.item[0] == 5);
    CHECK(s.item[6] == 11);
    CHECK(s.item[7] == 4);
    CHECK(s.item[8] == 0);   // 熟卡按位置补满
    CHECK(s.item[9] == 1);
    CHECK(!contains(&s, 2) && !contains(&s, 3));
}

static void test_rating(void) {
    uint8_t states[8];
    memset(states, 0, sizeof(states));
    ko_session_t s;
    ko_session_begin(&s, states, 0, 6, 0);   // 队列 0 1 2 3 4 5

    // 记住：直接移出，不再出现。
    CHECK(ko_session_rate(&s, true));
    CHECK(s.len == 5 && s.known == 1 && s.item[0] == 1);
    CHECK(ko_session_done(&s) == 1);

    // 没记住：隔 KO_REINSERT_GAP 张后再出现（队列 2 3 4 5 之间的第 3 个位置之后）。
    CHECK(!ko_session_rate(&s, false));   // 卡 1
    CHECK(s.len == 5);
    CHECK(s.item[0] == 2 && s.item[1] == 3 && s.item[2] == 4 && s.item[3] == 1 && s.item[4] == 5);
    CHECK(ko_session_done(&s) == 1);      // 没记住的卡不算完成

    // 再次没记住：达到 KO_REPEAT_LIMIT，放弃并计入"再看"。
    CHECK(ko_session_rate(&s, true));  // 2
    CHECK(ko_session_rate(&s, true));  // 3
    CHECK(ko_session_rate(&s, true));  // 4
    CHECK(s.item[0] == 1);
    CHECK(ko_session_rate(&s, false)); // 卡 1 第二次没记住 → 终结
    CHECK(s.unknown == 1);
    CHECK(s.len == 1 && s.item[0] == 5);

    // 队尾只剩一张时"没记住"：不能丢，也不能越界，留在队首再来一次。
    CHECK(!ko_session_rate(&s, false));
    CHECK(s.len == 1 && s.item[0] == 5 && s.again[0] == 1);
    CHECK(ko_session_rate(&s, false));   // 第二次 → 放弃
    CHECK(!ko_session_active(&s));
    CHECK(s.known + s.unknown == s.total);
    CHECK(!ko_session_rate(&s, true));   // 空队列上评级无效果
}

// 任意评级序列下不变量都成立，且一轮一定会结束。
static void test_random_ratings_terminate(void) {
    uint8_t states[16];
    memset(states, 0, sizeof(states));
    uint32_t seed = 12345;
    for (int round = 0; round < 300; round++) {
        ko_session_t s;
        ko_session_begin(&s, states, 2, 12, (uint32_t)round);
        int steps = 0;
        while (ko_session_active(&s)) {
            seed = seed * 1664525u + 1013904223u;
            const uint16_t before = ko_session_current(&s);
            CHECK(before >= 2 && before < 14);
            (void)ko_session_rate(&s, (seed >> 16) % 3 == 0);
            CHECK(s.len <= s.total);
            CHECK(ko_session_done(&s) == s.total - s.len);
            CHECK(++steps <= KO_SESSION_MAX * KO_REPEAT_LIMIT);   // 有界
        }
        CHECK(s.known + s.unknown == s.total);
    }
}

int main(void) {
    test_begin_order_and_rotation();
    test_priority();
    test_rating();
    test_random_ratings_terminate();
    puts("test_ko_session: PASS");
    return 0;
}
