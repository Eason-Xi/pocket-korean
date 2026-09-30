// main/ko_quiz.c —— 测验出题与作答状态机。
#include "ko_quiz.h"

#include <string.h>

#include "ko_rng.h"

// 抽样时用的"已选"标记数组大小；条目数超过它需要同步加大。
#define QUIZ_MAX_ITEMS 256
_Static_assert(KO_LETTER_COUNT <= QUIZ_MAX_ITEMS, "raise QUIZ_MAX_ITEMS");
_Static_assert(KO_WORD_COUNT <= QUIZ_MAX_ITEMS, "raise QUIZ_MAX_ITEMS");

ko_kind_t ko_quiz_kind(ko_quiz_mode_t mode) {
    return mode == KO_QUIZ_LETTER ? KO_KIND_LETTER : KO_KIND_WORD;
}

const char *ko_quiz_option_label(ko_quiz_mode_t mode, uint16_t item) {
    if (mode == KO_QUIZ_LETTER) {
        return item < KO_LETTER_COUNT ? ko_letters[item].sound : "";
    }
    if (item >= KO_WORD_COUNT) return "";
    return mode == KO_QUIZ_ZH2KO ? ko_words[item].ko : ko_words[item].zh;
}

uint8_t ko_quiz_grade(uint8_t score, uint8_t total) {
    if (total == 0) return 3;
    if (score >= total) return 0;
    if ((unsigned)score * 100u >= (unsigned)total * 80u) return 1;
    if ((unsigned)score * 100u >= (unsigned)total * 50u) return 2;
    return 3;
}

// 干扰项优先取自目标所在的主题 / 字母组（同类才有迷惑性）。
static void distractor_range(ko_kind_t kind, uint16_t target, uint16_t *first, uint16_t *count) {
    *first = 0;
    *count = (uint16_t)ko_kind_count(kind);
    if (kind == KO_KIND_WORD) {
        const int topic = ko_word_topic(target);
        if (topic >= 0 && ko_topics[topic].count >= KO_QUIZ_OPTIONS) {
            *first = ko_topics[topic].first;
            *count = ko_topics[topic].count;
        }
    } else {
        const int group = ko_letter_group_of(target);
        if (group >= 0 && ko_letter_groups[group].count >= KO_QUIZ_OPTIONS) {
            *first = ko_letter_groups[group].first;
            *count = ko_letter_groups[group].count;
        }
    }
}

// 候选项的显示文字不能与已选的任何一个相同，否则界面上会出现两个一样的选项。
static bool label_conflicts(ko_quiz_mode_t mode, uint16_t candidate, const uint16_t *chosen,
                            uint8_t chosen_count) {
    const char *label = ko_quiz_option_label(mode, candidate);
    for (uint8_t i = 0; i < chosen_count; i++) {
        if (strcmp(label, ko_quiz_option_label(mode, chosen[i])) == 0) return true;
    }
    return false;
}

// 按熟练度加权、不重复地抽一个目标：没见过的、盒子低的权重高。
static int pick_target(const uint8_t *states, uint16_t count, const uint8_t *taken, uint32_t *rng) {
    uint32_t total = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (taken[i]) continue;
        const uint8_t state = states[i];
        total += 1u + (uint32_t)(KO_BOX_MAX - ko_state_box(state)) +
                 (ko_state_seen(state) ? 0u : 2u);
    }
    if (total == 0) return -1;

    uint32_t roll = ko_rng_below(rng, total);
    for (uint16_t i = 0; i < count; i++) {
        if (taken[i]) continue;
        const uint8_t state = states[i];
        const uint32_t weight = 1u + (uint32_t)(KO_BOX_MAX - ko_state_box(state)) +
                                (ko_state_seen(state) ? 0u : 2u);
        if (roll < weight) return i;
        roll -= weight;
    }
    return -1;
}

uint8_t ko_quiz_build(ko_question_t *out, uint8_t max, ko_quiz_mode_t mode,
                      const ko_progress_t *progress, uint32_t *rng) {
    const ko_kind_t kind = ko_quiz_kind(mode);
    const uint16_t item_count = (uint16_t)ko_kind_count(kind);
    const uint8_t *states = ko_progress_states_const(progress, kind);
    uint8_t wanted = max < KO_QUIZ_ROUND ? max : KO_QUIZ_ROUND;
    if (wanted > item_count) wanted = (uint8_t)item_count;

    uint8_t taken[QUIZ_MAX_ITEMS];
    memset(taken, 0, sizeof(taken));

    uint8_t built = 0;
    while (built < wanted) {
        const int target = pick_target(states, item_count, taken, rng);
        if (target < 0) break;
        taken[target] = 1;

        // chosen[0] 是目标，后面是干扰项。
        uint16_t chosen[KO_QUIZ_OPTIONS];
        uint8_t n = 0;
        chosen[n++] = (uint16_t)target;

        uint16_t first, count;
        distractor_range(kind, (uint16_t)target, &first, &count);
        for (int tries = 0; tries < 64 && n < KO_QUIZ_OPTIONS; tries++) {
            const uint16_t candidate = (uint16_t)(first + ko_rng_below(rng, count));
            if (!label_conflicts(mode, candidate, chosen, n)) chosen[n++] = candidate;
        }
        // 随机没凑够（范围很小时可能）：顺序扫一遍整个类别兜底。
        for (uint16_t i = 0; i < item_count && n < KO_QUIZ_OPTIONS; i++) {
            if (!label_conflicts(mode, i, chosen, n)) chosen[n++] = i;
        }
        if (n < KO_QUIZ_OPTIONS) break;   // 内容里选项不够分，宁可少出题也不出坏题

        ko_question_t *q = &out[built++];
        q->target = (uint16_t)target;
        q->correct = (uint8_t)ko_rng_below(rng, KO_QUIZ_OPTIONS);
        uint8_t next_distractor = 1;
        for (uint8_t slot = 0; slot < KO_QUIZ_OPTIONS; slot++) {
            q->option[slot] = slot == q->correct ? chosen[0] : chosen[next_distractor++];
        }
    }
    return built;
}

// ---------------------------------------------------------------------------
// 作答状态机
// ---------------------------------------------------------------------------

void ko_quiz_run_begin(ko_quiz_run_t *run, ko_quiz_mode_t mode, const ko_progress_t *progress,
                       uint32_t *rng) {
    memset(run, 0, sizeof(*run));
    run->mode = mode;
    run->count = ko_quiz_build(run->question, KO_QUIZ_ROUND, mode, progress, rng);
    run->phase = run->count > 0 ? KO_QUIZ_PHASE_ASK : KO_QUIZ_PHASE_DONE;
    run->selected = 0;
}

const ko_question_t *ko_quiz_run_question(const ko_quiz_run_t *run) {
    return run->index < run->count ? &run->question[run->index] : NULL;
}

bool ko_quiz_run_has_replay_row(const ko_quiz_run_t *run) {
    return run->mode == KO_QUIZ_LISTEN;
}

ko_quiz_event_t ko_quiz_run_move(ko_quiz_run_t *run, int direction) {
    if (run->phase != KO_QUIZ_PHASE_ASK || direction == 0) return KO_QUIZ_EVENT_NONE;
    const int low = ko_quiz_run_has_replay_row(run) ? -1 : 0;
    int next = run->selected + (direction > 0 ? 1 : -1);
    if (next > KO_QUIZ_OPTIONS - 1) next = low;
    if (next < low) next = KO_QUIZ_OPTIONS - 1;
    run->selected = (int8_t)next;
    return KO_QUIZ_EVENT_MOVED;
}

ko_quiz_event_t ko_quiz_run_confirm(ko_quiz_run_t *run) {
    if (run->phase == KO_QUIZ_PHASE_ASK) {
        if (run->selected < 0) return KO_QUIZ_EVENT_REPLAY;
        run->chosen = (uint8_t)run->selected;
        if (run->chosen == run->question[run->index].correct) run->score++;
        run->phase = KO_QUIZ_PHASE_REVEAL;
        return KO_QUIZ_EVENT_ANSWERED;
    }
    if (run->phase == KO_QUIZ_PHASE_REVEAL) {
        run->index++;
        if (run->index >= run->count) {
            run->phase = KO_QUIZ_PHASE_DONE;
            return KO_QUIZ_EVENT_FINISHED;
        }
        run->phase = KO_QUIZ_PHASE_ASK;
        run->selected = 0;
        return KO_QUIZ_EVENT_NEXT;
    }
    return KO_QUIZ_EVENT_NONE;
}

bool ko_quiz_run_last_correct(const ko_quiz_run_t *run) {
    return run->index < run->count &&
           run->chosen == run->question[run->index].correct;
}
