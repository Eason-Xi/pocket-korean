// tests/test_ko_quiz.c —— 出题正确性与作答状态机。
#include <string.h>

#include "ko_quiz.h"
#include "ko_rng.h"
#include "ko_test.h"

static void test_rng(void) {
    uint32_t a = 1, b = 1;
    for (int i = 0; i < 100; i++) CHECK(ko_rng_next(&a) == ko_rng_next(&b));   // 可复现
    uint32_t zero = 0;
    CHECK(ko_rng_next(&zero) != 0);   // 0 种子不会卡死在 0
    CHECK(zero != 0);
    uint32_t s = 7;
    CHECK(ko_rng_below(&s, 0) == 0);
    for (int i = 0; i < 1000; i++) CHECK(ko_rng_below(&s, 4) < 4);
}

static void check_question_valid(ko_quiz_mode_t mode, const ko_question_t *q) {
    const ko_kind_t kind = ko_quiz_kind(mode);
    const size_t count = ko_kind_count(kind);
    CHECK(q->correct < KO_QUIZ_OPTIONS);
    CHECK(q->target < count);
    CHECK(q->option[q->correct] == q->target);

    for (int i = 0; i < KO_QUIZ_OPTIONS; i++) {
        CHECK(q->option[i] < count);
        for (int j = i + 1; j < KO_QUIZ_OPTIONS; j++) {
            // 显示文字互不相同：界面上不能出现两个一样的选项。
            CHECK(strcmp(ko_quiz_option_label(mode, q->option[i]),
                         ko_quiz_option_label(mode, q->option[j])) != 0);
            CHECK(q->option[i] != q->option[j]);
        }
    }
    // 干扰项来自同一个主题 / 字母组。
    for (int i = 0; i < KO_QUIZ_OPTIONS; i++) {
        if (kind == KO_KIND_WORD) CHECK(ko_word_topic(q->option[i]) == ko_word_topic(q->target));
        else CHECK(ko_letter_group_of(q->option[i]) == ko_letter_group_of(q->target));
    }
}

static void test_build_all_modes(void) {
    ko_progress_t progress;
    memset(&progress, 0, sizeof(progress));

    for (int mode = 0; mode < KO_QUIZ_MODE_COUNT; mode++) {
        for (uint32_t seed = 1; seed <= 200; seed++) {
            uint32_t rng = seed;
            ko_question_t q[KO_QUIZ_ROUND];
            const uint8_t n = ko_quiz_build(q, KO_QUIZ_ROUND, (ko_quiz_mode_t)mode, &progress, &rng);
            CHECK(n == KO_QUIZ_ROUND);
            for (int i = 0; i < n; i++) {
                check_question_valid((ko_quiz_mode_t)mode, &q[i]);
                for (int j = i + 1; j < n; j++) CHECK(q[i].target != q[j].target);   // 目标不重复
            }
        }
    }
}

// 逐字段比较：结构体尾部有对齐填充字节，memcmp 会比到未初始化的填充。
static bool same_questions(const ko_question_t *a, const ko_question_t *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i].target != b[i].target || a[i].correct != b[i].correct) return false;
        for (int k = 0; k < KO_QUIZ_OPTIONS; k++) {
            if (a[i].option[k] != b[i].option[k]) return false;
        }
    }
    return true;
}

static void test_build_limits_and_determinism(void) {
    ko_progress_t progress;
    memset(&progress, 0, sizeof(progress));
    ko_question_t a[KO_QUIZ_ROUND], b[KO_QUIZ_ROUND];

    uint32_t r1 = 99, r2 = 99;
    CHECK(ko_quiz_build(a, 3, KO_QUIZ_KO2ZH, &progress, &r1) == 3);   // max 生效
    CHECK(ko_quiz_build(b, 3, KO_QUIZ_KO2ZH, &progress, &r2) == 3);
    CHECK(same_questions(a, b, 3));                                    // 同种子同题

    r1 = 100;
    CHECK(ko_quiz_build(b, 3, KO_QUIZ_KO2ZH, &progress, &r1) == 3);
    CHECK(!same_questions(a, b, 3));                                   // 不同种子不同题

    CHECK(ko_quiz_build(a, 0, KO_QUIZ_KO2ZH, &progress, &r1) == 0);
    CHECK(ko_quiz_build(a, 200, KO_QUIZ_LETTER, &progress, &r1) == KO_QUIZ_ROUND);   // 上限 10
}

// 熟练度权重：没见过的卡应该明显更常被抽到。
static void test_weighting_prefers_unseen(void) {
    ko_progress_t progress;
    memset(&progress, 0, sizeof(progress));
    for (int i = 0; i < KO_WORD_COUNT; i++) {
        uint8_t st = 0;
        for (int k = 0; k < KO_BOX_MAX; k++) st = ko_state_rate(st, true);
        progress.words[i] = st;   // 全部滚瓜烂熟
    }
    for (int i = 0; i < 10; i++) progress.words[i] = 0;   // 前 10 个没见过

    int unseen_targets = 0, rounds = 300;
    for (int seed = 1; seed <= rounds; seed++) {
        uint32_t rng = (uint32_t)seed * 2654435761u;
        ko_question_t q[KO_QUIZ_ROUND];
        const uint8_t n = ko_quiz_build(q, KO_QUIZ_ROUND, KO_QUIZ_KO2ZH, &progress, &rng);
        for (int i = 0; i < n; i++) unseen_targets += q[i].target < 10;
    }
    // 均匀抽样时每轮期望约 10*10/167 ≈ 0.6 个；加权后应显著更高。
    CHECK(unseen_targets > rounds * 2);
}

static void test_grade(void) {
    CHECK(ko_quiz_grade(10, 10) == 0);
    CHECK(ko_quiz_grade(9, 10) == 1);
    CHECK(ko_quiz_grade(8, 10) == 1);
    CHECK(ko_quiz_grade(7, 10) == 2);
    CHECK(ko_quiz_grade(5, 10) == 2);
    CHECK(ko_quiz_grade(4, 10) == 3);
    CHECK(ko_quiz_grade(0, 10) == 3);
    CHECK(ko_quiz_grade(0, 0) == 3);
}

static void test_run_selection(void) {
    ko_progress_t progress;
    memset(&progress, 0, sizeof(progress));
    uint32_t rng = 5;
    ko_quiz_run_t run;

    ko_quiz_run_begin(&run, KO_QUIZ_KO2ZH, &progress, &rng);
    CHECK(run.count == KO_QUIZ_ROUND && run.phase == KO_QUIZ_PHASE_ASK && run.selected == 0);
    CHECK(!ko_quiz_run_has_replay_row(&run));
    CHECK(ko_quiz_run_move(&run, 1) == KO_QUIZ_EVENT_MOVED && run.selected == 1);
    CHECK(ko_quiz_run_move(&run, -1) == KO_QUIZ_EVENT_MOVED && run.selected == 0);
    ko_quiz_run_move(&run, -1);
    CHECK(run.selected == KO_QUIZ_OPTIONS - 1);   // 上翻到头循环到最后一个
    ko_quiz_run_move(&run, 1);
    CHECK(run.selected == 0);                      // 下翻到头回到第一个（没有"再听一遍"行）
    CHECK(ko_quiz_run_move(&run, 0) == KO_QUIZ_EVENT_NONE);

    // 听音模式多一行"再听一遍"，选中它并确认只触发重播，不会作答。
    ko_quiz_run_begin(&run, KO_QUIZ_LISTEN, &progress, &rng);
    CHECK(ko_quiz_run_has_replay_row(&run));
    ko_quiz_run_move(&run, -1);
    CHECK(run.selected == -1);
    CHECK(ko_quiz_run_confirm(&run) == KO_QUIZ_EVENT_REPLAY);
    CHECK(run.phase == KO_QUIZ_PHASE_ASK && run.score == 0);
    ko_quiz_run_move(&run, -1);
    CHECK(run.selected == KO_QUIZ_OPTIONS - 1);
    ko_quiz_run_move(&run, 1);
    CHECK(run.selected == -1);                     // 下翻到头回到"再听一遍"行
}

static void test_run_full_round(void) {
    ko_progress_t progress;
    memset(&progress, 0, sizeof(progress));
    uint32_t rng = 31337;
    ko_quiz_run_t run;
    ko_quiz_run_begin(&run, KO_QUIZ_ZH2KO, &progress, &rng);

    int correct_answers = 0;
    for (int q = 0; q < KO_QUIZ_ROUND; q++) {
        const ko_question_t *question = ko_quiz_run_question(&run);
        CHECK(question != NULL && run.index == q);

        // 偶数题答对，奇数题故意选错。
        run.selected = (int8_t)(q % 2 == 0 ? question->correct
                                            : (question->correct + 1) % KO_QUIZ_OPTIONS);
        CHECK(ko_quiz_run_confirm(&run) == KO_QUIZ_EVENT_ANSWERED);
        CHECK(run.phase == KO_QUIZ_PHASE_REVEAL);
        CHECK(ko_quiz_run_last_correct(&run) == (q % 2 == 0));
        correct_answers += q % 2 == 0;

        // 展示对错阶段不接受移动。
        CHECK(ko_quiz_run_move(&run, 1) == KO_QUIZ_EVENT_NONE);

        const ko_quiz_event_t next = ko_quiz_run_confirm(&run);
        CHECK(next == (q == KO_QUIZ_ROUND - 1 ? KO_QUIZ_EVENT_FINISHED : KO_QUIZ_EVENT_NEXT));
    }
    CHECK(run.phase == KO_QUIZ_PHASE_DONE);
    CHECK(run.score == correct_answers && run.score == 5);
    CHECK(ko_quiz_run_confirm(&run) == KO_QUIZ_EVENT_NONE);
    CHECK(ko_quiz_run_question(&run) == NULL);
}

int main(void) {
    test_rng();
    test_build_all_modes();
    test_build_limits_and_determinism();
    test_weighting_prefers_unseen();
    test_grade();
    test_run_selection();
    test_run_full_round();
    puts("test_ko_quiz: PASS");
    return 0;
}
