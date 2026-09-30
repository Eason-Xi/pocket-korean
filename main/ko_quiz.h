// main/ko_quiz.h —— 测验出题与作答状态机。纯逻辑，不依赖 ESP-IDF / LVGL。
//
// 一轮 KO_QUIZ_ROUND 题、每题 KO_QUIZ_OPTIONS 个选项。出题时：
//   * 目标题优先取熟练度低 / 没见过的条目（按权重抽样，不重复）；
//   * 干扰项取自目标所在的主题 / 字母组，并且显示文字必须与正确答案不同，
//     保证"只有一个选项是对的"。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ko_content.h"
#include "ko_progress.h"

#define KO_QUIZ_ROUND 10
#define KO_QUIZ_OPTIONS 4

typedef enum {
    KO_QUIZ_KO2ZH = 0,   // 看韩文，选中文释义
    KO_QUIZ_ZH2KO,       // 看中文，选韩文
    KO_QUIZ_LETTER,      // 看字母，选读音
    KO_QUIZ_LISTEN,      // 听发音，选中文释义（需要发音包）
    KO_QUIZ_MODE_COUNT,
} ko_quiz_mode_t;

typedef struct {
    uint16_t target;                    // 目标条目序号（词汇=单词，字母模式=字母）
    uint16_t option[KO_QUIZ_OPTIONS];   // 四个选项对应的条目序号
    uint8_t correct;                    // option[] 里正确项的位置
} ko_question_t;

// 该模式出题用的条目类别。
ko_kind_t ko_quiz_kind(ko_quiz_mode_t mode);

// 出一轮题，返回题数（<= max 且 <= 条目总数）。rng 是种子状态，会被推进。
uint8_t ko_quiz_build(ko_question_t *out, uint8_t max, ko_quiz_mode_t mode,
                      const ko_progress_t *progress, uint32_t *rng);

// 选项在界面上显示的文字（用于去重与显示）。
const char *ko_quiz_option_label(ko_quiz_mode_t mode, uint16_t item);

// 成绩评语档位：0 全对，1 = 80% 以上，2 = 50% 以上，3 = 其余。
uint8_t ko_quiz_grade(uint8_t score, uint8_t total);

// ---------------------------------------------------------------------------
// 一轮测验的作答状态机
// ---------------------------------------------------------------------------

typedef enum {
    KO_QUIZ_PHASE_ASK = 0,   // 等待作答
    KO_QUIZ_PHASE_REVEAL,    // 已作答，展示对错
    KO_QUIZ_PHASE_DONE,      // 整轮结束
} ko_quiz_phase_t;

typedef enum {
    KO_QUIZ_EVENT_NONE = 0,
    KO_QUIZ_EVENT_MOVED,      // 选中项移动
    KO_QUIZ_EVENT_REPLAY,     // 选中"再听一遍"行并确认（仅听音模式）
    KO_QUIZ_EVENT_ANSWERED,   // 提交了答案
    KO_QUIZ_EVENT_NEXT,       // 进入下一题
    KO_QUIZ_EVENT_FINISHED,   // 最后一题看完，整轮结束
} ko_quiz_event_t;

typedef struct {
    ko_quiz_mode_t mode;
    ko_question_t question[KO_QUIZ_ROUND];
    uint8_t count;
    uint8_t index;       // 当前题
    int8_t selected;     // -1 = "再听一遍"行（仅听音模式），0..3 = 选项
    uint8_t phase;       // ko_quiz_phase_t
    uint8_t chosen;      // 已作答时选中的选项
    uint8_t score;
} ko_quiz_run_t;

void ko_quiz_run_begin(ko_quiz_run_t *run, ko_quiz_mode_t mode, const ko_progress_t *progress,
                       uint32_t *rng);
const ko_question_t *ko_quiz_run_question(const ko_quiz_run_t *run);
bool ko_quiz_run_has_replay_row(const ko_quiz_run_t *run);
// direction: -1 上移，+1 下移；到头循环。只在 ASK 阶段有效。
ko_quiz_event_t ko_quiz_run_move(ko_quiz_run_t *run, int direction);
ko_quiz_event_t ko_quiz_run_confirm(ko_quiz_run_t *run);
// REVEAL 阶段：刚才这题是否答对。
bool ko_quiz_run_last_correct(const ko_quiz_run_t *run);
