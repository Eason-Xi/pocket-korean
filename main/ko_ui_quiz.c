// main/ko_ui_quiz.c —— 测验答题页与结果页。
#include <stdio.h>

#include "ko_ui.h"
#include "ko_ui_internal.h"

#define TOP_ROW_H 20
#define ROW_H 30
#define ROW_GAP 3
#define PROMPT_W KO_CONTENT_W
#define PROMPT_TEXT_W (PROMPT_W - 4 - 16)
#define PROMPT_INNER_H (72 - 4)   // 普通题干卡片去掉描边后的高度

typedef struct {
    lv_obj_t *box;
    lv_obj_t *text;
    lv_obj_t *mark;
} option_row_t;

static struct {
    lv_obj_t *number;       // "第 3/10 题"
    lv_obj_t *note;         // 作答前是提示语，作答后是"答对了 / 答错了"
    lv_obj_t *prompt_box;
    lv_obj_t *prompt;       // 题干文字
    lv_obj_t *prompt_icon;  // 听音题的喇叭
    option_row_t replay;    // 听音题独有的"再听一遍"行
    option_row_t option[KO_QUIZ_OPTIONS];
    ko_quiz_mode_t mode;
} s;

// 单行放得下的最大韩文字号。
static int single_line_font_px(const char *text) {
    static const int sizes[] = { 48, 32 };
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        if (ko_text_lines(text, sizes[i], PROMPT_TEXT_W) == 1 &&
            ko_text_longest_token_px(text, sizes[i]) <= PROMPT_TEXT_W) {
            return sizes[i];
        }
    }
    return 24;
}

static void make_row(option_row_t *row, int y, const lv_font_t *font) {
    row->box = ko_ui_box(ko_ui_content, KO_MARGIN, y, KO_CONTENT_W, ROW_H, KO_COL_CARD, 8);
    ko_ui_set_frame(row->box, KO_COL_LINE);
    row->text = ko_ui_label(row->box, font, KO_COL_INK, "");
    lv_label_set_long_mode(row->text, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(row->text, KO_CONTENT_W - 4 - 16 - 24);
    lv_obj_align(row->text, LV_ALIGN_LEFT_MID, 6, -1);
    row->mark = ko_ui_label(row->box, KO_FONT_ZH_M, KO_COL_OK, "");
    lv_obj_align(row->mark, LV_ALIGN_RIGHT_MID, -4, -1);
}

void ko_ui_show_quiz(const ko_quiz_view_t *view) {
    s.mode = view->run->mode;
    const bool listen = s.mode == KO_QUIZ_LISTEN;
    ko_ui_begin_page(KO_S_QZ_TITLE);
    lv_obj_t *page = ko_ui_content;

    s.number = ko_ui_label(page, KO_FONT_ZH_S, KO_COL_SUB, "");
    ko_ui_label_box(s.number, KO_MARGIN, 1, 100, LV_TEXT_ALIGN_LEFT);
    s.note = ko_ui_label(page, KO_FONT_ZH_S, KO_COL_SUB, "");
    ko_ui_label_box(s.note, KO_MARGIN + 90, 1, KO_CONTENT_W - 90, LV_TEXT_ALIGN_RIGHT);

    // 题干卡片：听音题只放喇叭和提示语，矮一些，给"再听一遍"行腾地方。
    const int prompt_h = listen ? 44 : 72;
    s.prompt_box = ko_ui_box(page, KO_MARGIN, TOP_ROW_H + 2, PROMPT_W, prompt_h, KO_COL_CARD, 12);
    ko_ui_set_frame(s.prompt_box, KO_COL_LINE);
    s.prompt = ko_ui_label(s.prompt_box, KO_FONT_ZH_M, KO_COL_INK, "");
    lv_obj_set_width(s.prompt, PROMPT_TEXT_W);
    lv_obj_set_style_text_align(s.prompt, LV_TEXT_ALIGN_CENTER, 0);
    s.prompt_icon = NULL;
    if (listen) {
        s.prompt_icon = ko_ui_label(s.prompt_box, KO_FONT_ZH_M, KO_COL_SUB, KO_S_SYM_NOTE);
        ko_ui_register_speaker(s.prompt_icon);
    }

    int y = TOP_ROW_H + 2 + prompt_h + 6;
    const lv_font_t *option_font = s.mode == KO_QUIZ_ZH2KO ? ko_font_hangul(24) : KO_FONT_ZH_M;
    if (listen) {
        make_row(&s.replay, y, KO_FONT_ZH_M);
        lv_label_set_text(s.replay.text, KO_S_SYM_NOTE " " KO_S_QZ_REPLAY);
        y += ROW_H + ROW_GAP;
    }
    for (int i = 0; i < KO_QUIZ_OPTIONS; i++) {
        make_row(&s.option[i], y, option_font);
        y += ROW_H + ROW_GAP;
    }

    ko_ui_update_quiz(view);
}

static void style_row(option_row_t *row, uint32_t frame, uint32_t bg, uint32_t text_color,
                      const char *mark, uint32_t mark_color) {
    lv_obj_set_style_border_color(row->box, lv_color_hex(frame), 0);
    lv_obj_set_style_bg_color(row->box, lv_color_hex(bg), 0);
    lv_obj_set_style_text_color(row->text, lv_color_hex(text_color), 0);
    lv_label_set_text(row->mark, mark);
    lv_obj_set_style_text_color(row->mark, lv_color_hex(mark_color), 0);
}

void ko_ui_update_quiz(const ko_quiz_view_t *view) {
    const ko_quiz_run_t *run = view->run;
    const ko_question_t *q = &run->question[run->index < run->count ? run->index : run->count - 1];
    const bool revealed = run->phase == KO_QUIZ_PHASE_REVEAL;

    lv_label_set_text_fmt(s.number, KO_S_QZ_NUMBER, run->index + 1, run->count);

    // 题干。
    // 题干与提示语都是常驻 Flash 的字符串：用 static 文本，不占 LVGL 内存池。
    switch (run->mode) {
    case KO_QUIZ_KO2ZH: {
        const int px = single_line_font_px(ko_words[q->target].ko);
        lv_obj_set_style_text_font(s.prompt, ko_font_hangul(px), 0);
        lv_label_set_text_static(s.prompt, ko_words[q->target].ko);
        lv_label_set_text_static(s.note, KO_S_QZ_PICK_ZH);
        lv_obj_align(s.prompt, LV_ALIGN_CENTER, 0, -1);
        break;
    }
    case KO_QUIZ_ZH2KO:
        lv_obj_set_style_text_font(s.prompt, KO_FONT_ZH_M, 0);
        lv_label_set_text_static(s.prompt, ko_words[q->target].zh);
        lv_label_set_text_static(s.note, KO_S_QZ_PICK_KO);
        lv_obj_align(s.prompt, LV_ALIGN_CENTER, 0, -1);
        break;
    case KO_QUIZ_LETTER:
        // 字母比整行文字矮得多，按字形度量居中，否则会显得偏上。
        lv_obj_set_style_text_font(s.prompt, ko_font_hangul(48), 0);
        lv_label_set_text_static(s.prompt, ko_letters[q->target].ch);
        lv_label_set_text_static(s.note, KO_S_QZ_PICK_SOUND);
        ko_ui_center_glyph(s.prompt, ko_font_hangul(48), ko_letters[q->target].ch, 0, PROMPT_TEXT_W,
                           PROMPT_INNER_H);
        break;
    case KO_QUIZ_LISTEN:
    default:
        // 听音题：喇叭在左，提示语在右，整体在卡片里居中。
        lv_obj_set_style_text_font(s.prompt, KO_FONT_ZH_M, 0);
        lv_label_set_text_static(s.prompt, KO_S_QZ_LISTEN_PROMPT);
        lv_label_set_text_static(s.note, "");
        // 喇叭占 10..30，提示语从 34 起、宽 168：8 个汉字（160 px）刚好一行放下。
        lv_obj_set_width(s.prompt, PROMPT_W - 4 - 34 - 6);
        lv_obj_set_style_text_align(s.prompt, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(s.prompt, LV_ALIGN_LEFT_MID, 34, -1);
        lv_obj_align(s.prompt_icon, LV_ALIGN_LEFT_MID, 10, -1);
        break;
    }

    // 作答后用文字 + 颜色 + 符号三重提示对错（不只靠颜色，红绿色盲也能分辨）。
    if (revealed) {
        const bool right = ko_quiz_run_last_correct(run);
        lv_label_set_text(s.note, right ? KO_S_QZ_RIGHT : KO_S_QZ_WRONG);
        lv_obj_set_style_text_color(s.note, lv_color_hex(right ? KO_COL_OK : KO_COL_BAD), 0);
    } else {
        lv_obj_set_style_text_color(s.note, lv_color_hex(KO_COL_SUB), 0);
    }

    // 选项。
    const bool has_replay = run->mode == KO_QUIZ_LISTEN;
    if (has_replay) {
        const bool on = !revealed && run->selected < 0;
        style_row(&s.replay, on ? KO_COL_RED : KO_COL_LINE, 0xEFE8D8, KO_COL_INK, "", KO_COL_OK);
    }
    for (int i = 0; i < KO_QUIZ_OPTIONS; i++) {
        option_row_t *row = &s.option[i];
        lv_label_set_text_static(row->text, ko_quiz_option_label(run->mode, q->option[i]));
        const bool selected = !revealed && run->selected == i;
        if (!revealed) {
            style_row(row, selected ? KO_COL_RED : KO_COL_LINE, selected ? 0xFFFFFF : KO_COL_CARD,
                      KO_COL_INK, "", KO_COL_OK);
        } else if (i == q->correct) {
            style_row(row, KO_COL_OK, KO_COL_OK_BG, KO_COL_INK, KO_S_SYM_CHECK, KO_COL_OK);
        } else if (i == run->chosen) {
            style_row(row, KO_COL_BAD, KO_COL_BAD_BG, KO_COL_INK, KO_S_SYM_CROSS, KO_COL_BAD);
        } else {
            style_row(row, KO_COL_LINE, KO_COL_CARD, KO_COL_GRAY, "", KO_COL_OK);
        }
    }

    // 页脚。
    if (revealed) {
        const bool last = run->index + 1 >= run->count;
        ko_ui_set_hints("", last ? KO_S_SYM_OK " " KO_S_HINT_RESULT : KO_S_SYM_OK " " KO_S_HINT_NEXT_Q,
                        KO_S_HINT_BACK);
    } else {
        const bool on_replay = has_replay && run->selected < 0;
        ko_ui_set_hints(KO_S_SYM_UP " " KO_S_HINT_UP,
                        on_replay ? KO_S_SYM_OK " " KO_S_QZ_REPLAY : KO_S_SYM_OK " " KO_S_HINT_CONFIRM,
                        KO_S_SYM_DOWN " " KO_S_HINT_DOWN);
    }
}

// ---------------------------------------------------------------------------
// 结果页
// ---------------------------------------------------------------------------

void ko_ui_show_quiz_result(const ko_quiz_result_view_t *view) {
    ko_ui_begin_page(KO_S_QZ_RESULT);
    lv_obj_t *page = ko_ui_content;

    lv_obj_t *card = ko_ui_box(page, KO_MARGIN, 22, KO_CONTENT_W, 178, KO_COL_CARD, 16);
    ko_ui_set_frame(card, KO_COL_LINE);

    // 大号得分：ASCII 数字用 48 号韩文字体（含全部 ASCII），满分时换成绿色。
    char score[16];
    snprintf(score, sizeof(score), "%d/%d", view->score, view->total);
    lv_obj_t *big = ko_ui_label(card, ko_font_hangul(48),
                                view->score == view->total ? KO_COL_OK : KO_COL_RED, score);
    ko_ui_label_box(big, 0, 14, KO_CONTENT_W - 4, LV_TEXT_ALIGN_CENTER);

    static const char *const GRADES[4] = {
        KO_S_GRADE_PERFECT, KO_S_GRADE_GOOD, KO_S_GRADE_OK, KO_S_GRADE_LOW,
    };
    lv_obj_t *grade = ko_ui_label(card, KO_FONT_ZH_M, KO_COL_INK,
                                  GRADES[ko_quiz_grade(view->score, view->total)]);
    ko_ui_label_box(grade, 0, 92, KO_CONTENT_W - 4, LV_TEXT_ALIGN_CENTER);

    ko_ui_box(card, (KO_CONTENT_W - 4 - 28) / 2, 132, 28, 3, KO_COL_RED, 1);

    char total[48];
    snprintf(total, sizeof(total), KO_S_SUB_QUIZ, (int)view->correct_total);
    lv_obj_t *cumulative = ko_ui_label(card, KO_FONT_ZH_S, KO_COL_SUB, total);
    ko_ui_label_box(cumulative, 0, 144, KO_CONTENT_W - 4, LV_TEXT_ALIGN_CENTER);

    ko_ui_set_hints("", KO_S_SYM_OK " " KO_S_HINT_AGAIN, KO_S_HINT_BACK);
}
