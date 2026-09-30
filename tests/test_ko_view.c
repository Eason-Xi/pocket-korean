// tests/test_ko_view.c —— 应用状态 → 界面视图（每行显示什么、页脚提示什么）。
//
// ko_view.c 与固件、主机预览共用，所以这里断言的就是板上会显示的内容；
// 另外用真实内容逐行估算文字宽度，防止标题 / 副标题溢出行宽或页眉。
#include <stdio.h>
#include <string.h>

#include "ko_layout.h"
#include "ko_strings.h"
#include "ko_test.h"
#include "ko_view.h"

static bool all_clips(uint16_t clip) {
    (void)clip;
    return true;
}

static bool only_letter_names(uint16_t clip) {
    // 只有字母名（前 40 个 id）有发音，例词没有。
    return clip < KO_LETTER_COUNT;
}

static ko_progress_t s_progress;

static ko_view_env_t env(void) {
    ko_view_env_t e = {
        .progress = &s_progress, .volume = 60, .brightness = 80, .pack = KO_VIEW_PACK_ABSENT,
        .pack_slots = 0, .storage_ok = true, .has_clip = NULL,
    };
    return e;
}

static void row_text(const ko_list_view_t *v, uint16_t index, char *title, char *sub, char *value,
                     size_t cap) {
    snprintf(title, cap, "%s", v->row_title(index, v->ctx));
    sub[0] = value[0] = '\0';
    if (v->row_sub) v->row_sub(index, sub, cap, v->ctx);
    if (v->row_value) v->row_value(index, value, cap, v->ctx);
}

static void test_counts_and_enabled_rules(void) {
    CHECK(ko_view_is_list(KO_SCR_HOME) && ko_view_is_list(KO_SCR_SETTINGS));
    CHECK(!ko_view_is_list(KO_SCR_CARD) && !ko_view_is_list(KO_SCR_QUIZ));
    CHECK(ko_view_list_count(KO_SCR_HOME) == KO_HOME_COUNT);
    CHECK(ko_view_list_count(KO_SCR_TOPICS) == KO_TOPIC_COUNT);
    CHECK(ko_view_list_count(KO_SCR_CARD) == 0);

    ko_view_env_t e = env();
    // 没有发音包：听音选词不可选；有了才可选。
    for (int mode = 0; mode < KO_QUIZ_MODE_COUNT; mode++) {
        CHECK(ko_view_row_enabled(&e, KO_SCR_QUIZ_MENU, (uint16_t)mode) == (mode != KO_QUIZ_LISTEN));
    }
    e.pack = KO_VIEW_PACK_READY;
    CHECK(ko_view_row_enabled(&e, KO_SCR_QUIZ_MENU, KO_QUIZ_LISTEN));
    e.pack = KO_VIEW_PACK_BROKEN;   // 包损坏也不能选
    CHECK(!ko_view_row_enabled(&e, KO_SCR_QUIZ_MENU, KO_QUIZ_LISTEN));
    // 设置页：只有前三行可调，后两行是状态显示。
    for (uint16_t i = 0; i < KO_SET_COUNT; i++) {
        CHECK(ko_view_row_enabled(&e, KO_SCR_SETTINGS, i) == (i <= KO_SET_RESET));
    }
    CHECK(ko_view_row_enabled(&e, KO_SCR_HOME, 0));
}

static void test_home_and_group_lists(void) {
    memset(&s_progress, 0, sizeof(s_progress));
    for (int i = 0; i < 12; i++) s_progress.letters[i] = ko_state_mark_seen(0);
    s_progress.quiz_correct_total = 87;
    for (int i = 0; i < 5; i++) {
        uint8_t st = 0;
        for (int k = 0; k < KO_MASTERED_BOX; k++) st = ko_state_rate(st, true);
        s_progress.words[i] = st;   // 前 5 个词已掌握，都在第一个主题里
    }

    ko_view_env_t e = env();
    ko_route_t route = { .id = KO_SCR_HOME, .cursor = 2, .arg1 = 0 };
    ko_view_ctx_t ctx;
    ko_list_view_t v = ko_view_list(&ctx, &e, &route);
    CHECK(strcmp(v.title, KO_S_APP_TITLE) == 0);
    CHECK(v.count == KO_HOME_COUNT && v.selected == 2 && v.first == 0);
    CHECK(strcmp(v.hint_left, KO_S_SYM_UP " " KO_S_HINT_UP) == 0);
    CHECK(strcmp(v.hint_center, KO_S_SYM_OK " " KO_S_HINT_OPEN) == 0);
    CHECK(strcmp(v.hint_right, KO_S_SYM_DOWN " " KO_S_HINT_DOWN) == 0);
    CHECK(v.row_emblem != NULL && v.row_value == NULL);

    char title[64], sub[64], value[64], expect[64];
    row_text(&v, KO_HOME_ALPHA, title, sub, value, sizeof(title));
    CHECK(strcmp(title, KO_S_HOME_ALPHA) == 0);
    snprintf(expect, sizeof(expect), KO_S_SUB_ALPHA, 12, KO_LETTER_COUNT);
    CHECK(strcmp(sub, expect) == 0);
    row_text(&v, KO_HOME_VOCAB, title, sub, value, sizeof(title));
    snprintf(expect, sizeof(expect), KO_S_SUB_MASTERED, 5, KO_WORD_COUNT);
    CHECK(strcmp(sub, expect) == 0);
    row_text(&v, KO_HOME_QUIZ, title, sub, value, sizeof(title));
    snprintf(expect, sizeof(expect), KO_S_SUB_QUIZ, 87);
    CHECK(strcmp(sub, expect) == 0);

    // 词汇主题列表：第一个主题 5 个已掌握，其余为 0。
    route.id = KO_SCR_TOPICS;
    v = ko_view_list(&ctx, &e, &route);
    CHECK(v.count == KO_TOPIC_COUNT);
    row_text(&v, 0, title, sub, value, sizeof(title));
    snprintf(expect, sizeof(expect), KO_S_SUB_MASTERED, 5, ko_topics[0].count);
    CHECK(strcmp(sub, expect) == 0);
    CHECK(strcmp(title, ko_topics[0].zh) == 0);
    row_text(&v, 1, title, sub, value, sizeof(title));
    snprintf(expect, sizeof(expect), KO_S_SUB_MASTERED, 0, ko_topics[1].count);
    CHECK(strcmp(sub, expect) == 0);
    CHECK(strcmp(v.row_emblem(2, v.ctx), "03") == 0);   // 主题徽标是两位序号

    // 字母分组：徽标是该组第一个字母。
    route.id = KO_SCR_ALPHA_GROUPS;
    v = ko_view_list(&ctx, &e, &route);
    CHECK(strcmp(v.row_emblem(0, v.ctx), ko_letters[ko_letter_groups[0].first].ch) == 0);
    row_text(&v, 0, title, sub, value, sizeof(title));
    snprintf(expect, sizeof(expect), KO_S_SUB_ALPHA, 10, ko_letter_groups[0].count);   // 前 10 个都学过
    CHECK(strcmp(sub, expect) == 0);
}

static void test_quiz_menu_and_settings(void) {
    ko_view_env_t e = env();
    ko_route_t route = { .id = KO_SCR_QUIZ_MENU };
    ko_view_ctx_t ctx;
    char title[64], sub[64], value[64], expect[64];

    ko_list_view_t v = ko_view_list(&ctx, &e, &route);
    row_text(&v, KO_QUIZ_KO2ZH, title, sub, value, sizeof(title));
    snprintf(expect, sizeof(expect), KO_S_QZ_ROUNDS, KO_QUIZ_ROUND);
    CHECK(strcmp(sub, expect) == 0);
    row_text(&v, KO_QUIZ_LISTEN, title, sub, value, sizeof(title));
    CHECK(strcmp(sub, KO_S_QZ_LISTEN_OFF) == 0);   // 没发音包：提示原因，而不是只置灰
    CHECK(!v.row_enabled(KO_QUIZ_LISTEN, v.ctx));

    // 设置页。
    route.id = KO_SCR_SETTINGS;
    route.cursor = KO_SET_VOLUME;
    v = ko_view_list(&ctx, &e, &route);
    CHECK(strcmp(v.title, KO_S_HOME_SETTINGS) == 0);
    CHECK(v.row_emblem == NULL && v.row_value != NULL);
    row_text(&v, KO_SET_VOLUME, title, sub, value, sizeof(title));
    CHECK(strcmp(value, "60%") == 0 && sub[0] == '\0');
    row_text(&v, KO_SET_BRIGHTNESS, title, sub, value, sizeof(title));
    CHECK(strcmp(value, "80%") == 0);
    CHECK(strcmp(v.hint_center, KO_S_SYM_OK " " KO_S_HINT_CHANGE) == 0);

    // 清除进度：中间键提示先是"清除"，二次确认后变"确认"，副标题给出说明。
    route.cursor = KO_SET_RESET;
    v = ko_view_list(&ctx, &e, &route);
    CHECK(strcmp(v.hint_center, KO_S_SYM_OK " " KO_S_HINT_CLEAR) == 0);
    row_text(&v, KO_SET_RESET, title, sub, value, sizeof(title));
    CHECK(sub[0] == '\0');
    e.reset_armed = true;
    v = ko_view_list(&ctx, &e, &route);
    CHECK(strcmp(v.hint_center, KO_S_SYM_OK " " KO_S_HINT_CONFIRM) == 0);
    row_text(&v, KO_SET_RESET, title, sub, value, sizeof(title));
    CHECK(strcmp(sub, KO_S_SET_RESET_CONFIRM) == 0);
    e.reset_armed = false;
    e.reset_done = true;
    v = ko_view_list(&ctx, &e, &route);
    row_text(&v, KO_SET_RESET, title, sub, value, sizeof(title));
    CHECK(strcmp(sub, KO_S_SET_RESET_DONE) == 0);

    // 发音包与存储状态如实显示，包括降级情况。
    row_text(&v, KO_SET_PACK, title, sub, value, sizeof(title));
    CHECK(strcmp(sub, KO_S_PACK_NONE) == 0);
    e.pack = KO_VIEW_PACK_READY;
    e.pack_slots = 256;
    v = ko_view_list(&ctx, &e, &route);
    row_text(&v, KO_SET_PACK, title, sub, value, sizeof(title));
    snprintf(expect, sizeof(expect), KO_S_PACK_READY, 256);
    CHECK(strcmp(sub, expect) == 0);
    e.pack = KO_VIEW_PACK_BROKEN;
    v = ko_view_list(&ctx, &e, &route);
    row_text(&v, KO_SET_PACK, title, sub, value, sizeof(title));
    CHECK(strcmp(sub, KO_S_PACK_BAD) == 0);
    row_text(&v, KO_SET_STORAGE, title, sub, value, sizeof(title));
    CHECK(strcmp(sub, KO_S_STORAGE_OK) == 0);
    e.storage_ok = false;
    v = ko_view_list(&ctx, &e, &route);
    row_text(&v, KO_SET_STORAGE, title, sub, value, sizeof(title));
    CHECK(strcmp(sub, KO_S_STORAGE_OFF) == 0);
}

// 用真实内容逐行估算宽度：标题（20 号）与副标题（14 号）必须放得进行内文字区，
// 页眉标题（20 号）必须放得进标题区。估算与 ko_ui.c 里的版式常量一致。
static void test_text_fits_in_rows(void) {
    ko_view_env_t e = env();
    e.pack = KO_VIEW_PACK_READY;
    e.pack_slots = 1234;
    static const ko_screen_id_t LISTS[] = { KO_SCR_HOME, KO_SCR_ALPHA_GROUPS, KO_SCR_TOPICS,
                                            KO_SCR_PHRASE_GROUPS, KO_SCR_QUIZ_MENU, KO_SCR_SETTINGS };
    const int header_title_w = 146;   // ko_ui.c 页眉标题区宽度

    for (unsigned l = 0; l < sizeof(LISTS) / sizeof(LISTS[0]); l++) {
        ko_route_t route = { .id = LISTS[l] };
        ko_view_ctx_t ctx;
        ko_list_view_t v = ko_view_list(&ctx, &e, &route);
        CHECK(ko_text_width_units(v.title) * 20 / 100 <= header_title_w);

        for (uint16_t i = 0; i < v.count; i++) {
            char title[64], sub[64], value[64];
            row_text(&v, i, title, sub, value, sizeof(title));
            CHECK(title[0] != '\0');
            CHECK(strlen(sub) < sizeof(sub) - 1);

            // ko_ui.c：有徽标时文字从 x=44 起，右侧留 8（有数值时留 64）。
            const int text_x = v.row_emblem ? 44 : 8;
            const int right_room = value[0] ? 64 : 8;
            const int text_w = KO_CONTENT_W - 4 - text_x - right_room;
            CHECK(ko_text_width_units(title) * 20 / 100 <= text_w);
            CHECK(ko_text_width_units(sub) * 14 / 100 <= text_w);
        }
    }

    // 卡片页眉：所有主题 / 场景名都要放得进标题区。
    for (int t = 0; t < KO_TOPIC_COUNT; t++) CHECK(ko_text_width_units(ko_topics[t].zh) * 20 / 100 <= header_title_w);
    for (int g = 0; g < KO_PHRASE_GROUP_COUNT; g++) {
        CHECK(ko_text_width_units(ko_phrase_groups[g].zh) * 20 / 100 <= header_title_w);
    }
    // 卡片背面的中文释义（20 号）最多一行：宽度不超过卡片内文区。
    for (int i = 0; i < KO_WORD_COUNT; i++) CHECK(ko_text_width_units(ko_words[i].zh) * 20 / 100 <= KO_CONTENT_W - 4 - 16);
    // 测验选项：中文选项（20 号）单行放得下，韩文选项（24 号）单行放得下。
    const int option_w = KO_CONTENT_W - 4 - 16 - 24;
    for (int i = 0; i < KO_WORD_COUNT; i++) {
        CHECK(ko_text_width_units(ko_words[i].zh) * 20 / 100 <= option_w);
        CHECK(ko_text_width_units(ko_words[i].ko) * 24 / 100 <= option_w);
    }
}

static void test_card_alpha_done_views(void) {
    memset(&s_progress, 0, sizeof(s_progress));
    ko_view_env_t e = env();

    // 字母页：字母序号 = 组首 + 组内位置；只要名称或例词有一个能播就显示发音。
    ko_route_t alpha = { .id = KO_SCR_ALPHA_DETAIL, .arg0 = 1, .cursor = 3 };
    ko_alpha_view_t av = ko_view_alpha(&e, &alpha);
    CHECK(av.letter == ko_letter_groups[1].first + 3 && !av.has_audio);   // has_clip 为 NULL
    e.has_clip = only_letter_names;
    av = ko_view_alpha(&e, &alpha);
    CHECK(av.has_audio);
    e.has_clip = all_clips;

    // 词汇卡片。
    ko_session_t session;
    const uint8_t *states = s_progress.words;
    ko_session_begin(&session, states, ko_topics[2].first, ko_topics[2].count, 0);
    ko_route_t card = { .id = KO_SCR_CARD, .arg0 = KO_KIND_WORD, .arg1 = 2 };
    ko_card_view_t cv = ko_view_card(&e, &card, &session, false);
    CHECK(strcmp(cv.deck_title, ko_topics[2].zh) == 0);
    CHECK(cv.item == &ko_words[ko_topics[2].first] && !cv.is_phrase && !cv.back);
    CHECK(cv.done == 0 && cv.total == session.total && cv.has_audio);
    ko_session_rate(&session, true);
    cv = ko_view_card(&e, &card, &session, true);
    CHECK(cv.done == 1 && cv.back && cv.item == &ko_words[ko_topics[2].first + 1]);

    // 短语卡片走另一张表。
    ko_session_begin(&session, s_progress.phrases, ko_phrase_groups[1].first, ko_phrase_groups[1].count, 0);
    ko_route_t phrase = { .id = KO_SCR_CARD, .arg0 = KO_KIND_PHRASE, .arg1 = 1 };
    cv = ko_view_card(&e, &phrase, &session, false);
    CHECK(cv.is_phrase && cv.item == &ko_phrases[ko_phrase_groups[1].first]);
    CHECK(strcmp(cv.deck_title, ko_phrase_groups[1].zh) == 0);

    // 完成页：掌握数取自进度。
    for (int i = 0; i < 3; i++) {
        uint8_t st = 0;
        for (int k = 0; k < KO_MASTERED_BOX; k++) st = ko_state_rate(st, true);
        s_progress.words[ko_topics[2].first + i] = st;
    }
    session.known = 7;
    session.unknown = 3;
    const ko_route_t done_route = { .id = KO_SCR_SESSION_DONE, .arg0 = KO_KIND_WORD, .arg1 = 2 };
    const ko_done_view_t dv = ko_view_done(&e, &done_route, &session);
    CHECK(dv.known == 7 && dv.unknown == 3 && dv.mastered == 3 && dv.deck_total == ko_topics[2].count);

    // 测验结果页。
    ko_quiz_run_t run;
    memset(&run, 0, sizeof(run));
    run.score = 8;
    run.count = 10;
    s_progress.quiz_correct_total = 55;
    const ko_quiz_result_view_t rv = ko_view_quiz_result(&e, &run);
    CHECK(rv.score == 8 && rv.total == 10 && rv.correct_total == 55);
}

int main(void) {
    test_counts_and_enabled_rules();
    test_home_and_group_lists();
    test_quiz_menu_and_settings();
    test_text_fits_in_rows();
    test_card_alpha_done_views();
    puts("test_ko_view: PASS");
    return 0;
}
