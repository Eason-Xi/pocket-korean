// tools/korean_preview/preview_main.c —— 主机上渲染韩语学习应用各页面的程序。
// 由 tools/render_korean_preview.py 编译并运行：链接真实的 LVGL 与固件里的界面代码，
// 输出每个页面的 PPM，并报告每个页面创建后 LVGL 内存池的占用。
//
// 同时用 lv_font_get_glyph_dsc() 检查内容里每个码点在"实际用来显示它的字体"里都有字形
// （LVGL 文档要求的覆盖检查；占位字形不算覆盖），并带一个已知缺失的反例，
// 保证这个检查本身不会无条件通过。
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "ko_content.h"
#include "ko_fonts.h"
#include "ko_layout.h"
#include "ko_progress.h"
#include "ko_quiz.h"
#include "ko_strings.h"
#include "ko_theme.h"
#include "ko_ui.h"
#include "ko_view.h"

static uint16_t s_frame[KO_SCREEN_W * KO_SCREEN_H];
static uint8_t s_render_buf[KO_SCREEN_W * KO_SCREEN_H * 2];
static uint32_t s_tick_ms;
static const char *s_out_dir = ".";
static size_t s_peak_used;

static uint32_t tick_cb(void) {
    return s_tick_ms;
}

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    const uint16_t *src = (const uint16_t *)px_map;
    const int w = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; y++) {
        memcpy(&s_frame[y * KO_SCREEN_W + area->x1], src, (size_t)w * 2);
        src += w;
    }
    lv_display_flush_ready(display);
}

static size_t mem_used(void) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    return mon.total_size - mon.free_size;
}

static void settle(void) {
    // 20 ms 的刷新周期：每次推进 30 ms，多跑几次让所有待刷新区域画完。
    for (int i = 0; i < 6; i++) {
        s_tick_ms += 30;
        lv_timer_handler();
    }
}

static void capture(const char *name) {
    settle();
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", s_out_dir, name);
    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(1);
    }
    fprintf(file, "P6\n%d %d\n255\n", KO_SCREEN_W, KO_SCREEN_H);
    for (int i = 0; i < KO_SCREEN_W * KO_SCREEN_H; i++) {
        const uint16_t c = s_frame[i];
        // RGB565 → RGB888（低位用高位补齐，白色才是 255）。
        const int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
        fputc((r << 3) | (r >> 2), file);
        fputc((g << 2) | (g >> 4), file);
        fputc((b << 3) | (b >> 2), file);
    }
    fclose(file);

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    const size_t used = mon.total_size - mon.free_size;
    if (used > s_peak_used) s_peak_used = used;
    // max_used 是历史峰值：包含渲染过程中的临时分配（圆角遮罩等），比"渲染完之后的已用"更能说明
    // 内存池够不够。池被耗尽时 LVGL 的断言处理是死循环，板上表现为卡死 / 白屏。
    if (mon.max_used > s_peak_used) s_peak_used = mon.max_used;
    printf("MEM %-28s used=%5zu peak=%5zu free=%5zu largest_free=%5zu frag=%u%%\n", name, used,
           (size_t)mon.max_used, (size_t)mon.free_size, (size_t)mon.free_biggest_size,
           (unsigned)mon.frag_pct);
}

// ---------------------------------------------------------------------------
// 字形覆盖检查
// ---------------------------------------------------------------------------

static uint32_t next_codepoint(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t cp;
    int len;
    if (s[0] < 0x80) { cp = s[0]; len = 1; }
    else if (s[0] < 0xE0) { cp = s[0] & 0x1F; len = 2; }
    else if (s[0] < 0xF0) { cp = s[0] & 0x0F; len = 3; }
    else { cp = s[0] & 0x07; len = 4; }
    for (int i = 1; i < len; i++) cp = (cp << 6) | (s[i] & 0x3F);
    *p += len;
    return cp;
}

static bool font_has(const lv_font_t *font, uint32_t cp) {
    lv_font_glyph_dsc_t dsc;
    memset(&dsc, 0, sizeof(dsc));
    return lv_font_get_glyph_dsc(font, &dsc, cp, 0) && !dsc.is_placeholder;
}

static int s_font_failures;

static void check_text(const char *what, const char *text, const lv_font_t *font) {
    for (const char *p = text; *p;) {
        const uint32_t cp = next_codepoint(&p);
        if (cp == '\n') continue;
        if (!font_has(font, cp)) {
            printf("FONT-MISSING %s U+%04X in \"%s\"\n", what, (unsigned)cp, text);
            s_font_failures++;
        }
    }
}

static void check_fonts(void) {
    // 反例：这些码点故意不在对应字体里，必须被报告为缺失，否则检查是假的。
    if (font_has(&ko_zh20, 0x9F98) || font_has(&ko_ko24, 0xD7A3) || font_has(&ko_zh14, 0xAC00)) {
        printf("FONT-CHECK-BROKEN: negative case was reported as covered\n");
        s_font_failures++;
    }

    for (int i = 0; i < KO_LETTER_GROUP_COUNT; i++) {
        check_text("group.zh", ko_letter_groups[i].zh, &ko_zh20);
        check_text("group.zh", ko_letter_groups[i].zh, &ko_zh14);
        check_text("group.ko", ko_letter_groups[i].ko, &ko_ko24);
    }
    for (int i = 0; i < KO_LETTER_COUNT; i++) {
        const ko_letter_t *l = &ko_letters[i];
        check_text("letter.ch", l->ch, KO_FONT_JAMO);
        check_text("letter.ch", l->ch, &ko_ko48);
        check_text("letter.name", l->name, &ko_ko24);
        check_text("letter.pair", l->pair, &ko_ko24);
        check_text("letter.syl", l->syl, &ko_ko24);
        check_text("letter.ex", l->ex, &ko_ko24);
        check_text("letter.hint", l->hint, &ko_zh14);
        check_text("letter.ex_zh", l->ex_zh, &ko_zh20);
        check_text("letter.rom", l->name_rom, &ko_zh14);
        check_text("letter.sound", l->sound, &ko_zh20);
        check_text("letter.sound", l->sound, &ko_ko24);
    }
    for (int i = 0; i < KO_TOPIC_COUNT; i++) {
        check_text("topic.zh", ko_topics[i].zh, &ko_zh20);
        check_text("topic.zh", ko_topics[i].zh, &ko_zh14);
    }
    for (int i = 0; i < KO_PHRASE_GROUP_COUNT; i++) {
        check_text("group.zh", ko_phrase_groups[i].zh, &ko_zh20);
    }
    for (int i = 0; i < KO_WORD_COUNT; i++) {
        check_text("word.ko", ko_words[i].ko, &ko_ko48);
        check_text("word.ko", ko_words[i].ko, &ko_ko32);
        check_text("word.ko", ko_words[i].ko, &ko_ko24);
        check_text("word.zh", ko_words[i].zh, &ko_zh20);
        check_text("word.rom", ko_words[i].rom, &ko_zh14);
    }
    for (int i = 0; i < KO_PHRASE_COUNT; i++) {
        check_text("phrase.ko", ko_phrases[i].ko, &ko_ko32);
        check_text("phrase.ko", ko_phrases[i].ko, &ko_ko24);
        check_text("phrase.zh", ko_phrases[i].zh, &ko_zh20);
        check_text("phrase.rom", ko_phrases[i].rom, &ko_zh14);
    }
    // 界面里出现的符号与固定文字（抽取几个有代表性的）。
    check_text("ui.sym", KO_S_SYM_UP KO_S_SYM_DOWN KO_S_SYM_OK KO_S_SYM_CHECK KO_S_SYM_CROSS KO_S_SYM_NOTE,
               &ko_zh14);
    check_text("ui.sym", KO_S_SYM_CHECK KO_S_SYM_CROSS KO_S_SYM_NOTE, &ko_zh20);
    check_text("ui.title", KO_S_APP_TITLE KO_S_QZ_RESULT, &ko_zh20);
    check_text("ui.emblem", KO_K_EMBLEM_ALPHA KO_K_EMBLEM_VOCAB KO_K_EMBLEM_PHRASE KO_K_EMBLEM_QUIZ
               KO_K_EMBLEM_SETTINGS, &ko_ko24);
}

// ---------------------------------------------------------------------------
// 场景：全部通过 ko_view（与固件共用）构建视图，预览到的就是固件真正会显示的内容。
// ---------------------------------------------------------------------------

static ko_progress_t s_progress;
static ko_view_env_t s_env;

static bool preview_has_clip(uint16_t clip) {
    (void)clip;
    return true;
}

// 造一份有代表性的学习进度：几个字母学过，各主题掌握程度不同。
static void make_sample_state(void) {
    memset(&s_progress, 0, sizeof(s_progress));
    for (int i = 0; i < 12; i++) s_progress.letters[i] = ko_state_mark_seen(0);
    for (int i = 0; i < KO_WORD_COUNT; i++) {
        uint8_t st = 0;
        const int level = (i * 7) % 6;
        for (int k = 0; k < level; k++) st = ko_state_rate(st, true);
        if (i % 4 != 3) s_progress.words[i] = st;
    }
    for (int i = 0; i < 6; i++) {
        s_progress.phrases[i] = ko_state_rate(ko_state_rate(ko_state_rate(0, true), true), true);
    }
    s_progress.quiz_correct_total = 87;
    s_env = (ko_view_env_t){
        .progress = &s_progress, .volume = 60, .brightness = 100, .pack = KO_VIEW_PACK_ABSENT,
        .pack_slots = 0, .storage_ok = true, .has_clip = preview_has_clip,
    };
}

static void show_list_scene(const char *name, ko_screen_id_t id, uint16_t cursor, uint16_t first,
                            bool rebuild) {
    ko_route_t route = { .id = id, .arg0 = 0, .arg1 = first, .cursor = cursor };
    ko_view_ctx_t ctx;
    const ko_list_view_t view = ko_view_list(&ctx, &s_env, &route);
    if (rebuild) ko_ui_show_list(&view); else ko_ui_update_list(&view);
    capture(name);
}

static void show_lists(void) {
    show_list_scene("01_home", KO_SCR_HOME, 1, 0, true);
    show_list_scene("02_alpha_groups", KO_SCR_ALPHA_GROUPS, 2, 0, true);
    show_list_scene("03_topics_scrolled", KO_SCR_TOPICS, 7, 3, true);
    show_list_scene("03b_topics_top", KO_SCR_TOPICS, 0, 0, false);
    show_list_scene("03c_phrase_groups", KO_SCR_PHRASE_GROUPS, 1, 0, true);

    // 测验菜单：没有发音包时"听音选词"置灰，并提示需要发音包。
    show_list_scene("04_quiz_menu_no_pack", KO_SCR_QUIZ_MENU, 0, 0, true);
    s_env.pack = KO_VIEW_PACK_READY;
    s_env.pack_slots = KO_CLIP_COUNT;
    show_list_scene("04b_quiz_menu_with_pack", KO_SCR_QUIZ_MENU, 3, 0, true);
    s_env.pack = KO_VIEW_PACK_ABSENT;
    s_env.pack_slots = 0;

    show_list_scene("05_settings", KO_SCR_SETTINGS, 0, 0, true);
    s_env.reset_armed = true;
    show_list_scene("05b_settings_reset_armed", KO_SCR_SETTINGS, 2, 0, false);
    s_env.reset_armed = false;
    s_env.pack = KO_VIEW_PACK_READY;
    s_env.pack_slots = KO_CLIP_COUNT;
    show_list_scene("05c_settings_pack_ready", KO_SCR_SETTINGS, 1, 0, false);
    s_env.storage_ok = false;
    s_env.pack = KO_VIEW_PACK_BROKEN;
    show_list_scene("05d_settings_degraded", KO_SCR_SETTINGS, 0, 0, false);
    s_env.storage_ok = true;
    s_env.pack = KO_VIEW_PACK_ABSENT;
}

static void show_alpha(void) {
    // 代表性的字母：最简单的、辅音、双辅音、复合元音、例词最长的。
    static const struct { int index; const char *name; } picks[] = {
        { 0, "10_alpha_a" }, { 10, "11_alpha_g" }, { 24, "12_alpha_kk" }, { 29, "13_alpha_ae" },
        { 18, "14_alpha_j" }, { 39, "15_alpha_ui" },
    };
    for (unsigned i = 0; i < sizeof(picks) / sizeof(picks[0]); i++) {
        const int group = ko_letter_group_of((uint16_t)picks[i].index);
        ko_route_t route = { .id = KO_SCR_ALPHA_DETAIL, .arg0 = (uint16_t)group,
                             .cursor = (uint16_t)(picks[i].index - ko_letter_groups[group].first) };
        const ko_alpha_view_t view = ko_view_alpha(&s_env, &route);
        if (i == 0) ko_ui_show_alpha(&view); else ko_ui_update_alpha(&view);
        capture(picks[i].name);
    }
}

static int find_item(const ko_item_t *items, int count, const char *ko) {
    for (int i = 0; i < count; i++) {
        if (strcmp(items[i].ko, ko) == 0) return i;
    }
    fprintf(stderr, "not found: %s\n", ko);
    exit(1);
}

// 让会话的当前卡片正好是 ko 这一条，并返回对应的路由。
static ko_route_t card_route_for(ko_kind_t kind, const char *ko, ko_session_t *session) {
    const int index = kind == KO_KIND_WORD ? find_item(ko_words, KO_WORD_COUNT, ko)
                                           : find_item(ko_phrases, KO_PHRASE_COUNT, ko);
    const int deck = kind == KO_KIND_WORD ? ko_word_topic((uint16_t)index) : ko_phrase_group_of((uint16_t)index);
    ko_route_t route = { .id = KO_SCR_CARD, .arg0 = kind, .arg1 = (uint16_t)deck };
    memset(session, 0, sizeof(*session));
    session->item[0] = (uint16_t)index;
    session->len = 10;
    session->total = 10;
    return route;
}

static void show_cards(void) {
    static const char *const words[] = { "하나", "사랑하다", "안녕하세요", "안녕히 가세요", "떡볶이" };
    ko_session_t session;
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        const ko_route_t route = card_route_for(KO_KIND_WORD, words[i], &session);
        session.len = (uint8_t)(10 - i);
        char name[48];
        ko_card_view_t view = ko_view_card(&s_env, &route, &session, false);
        // 每张卡都来自不同主题：重建页面，页眉才会显示这张卡自己的主题名。
        ko_ui_show_card(&view);
        snprintf(name, sizeof(name), "20_word%u_front", i);
        capture(name);
        view = ko_view_card(&s_env, &route, &session, true);
        ko_ui_update_card(&view);
        snprintf(name, sizeof(name), "21_word%u_back", i);
        capture(name);
    }

    static const char *const phrases[] = {
        "얼마예요?", "버스 정류장이 어디예요?", "한국어를 잘 못해요.", "택시 좀 불러 주세요.",
        "병원에 가고 싶어요.",
    };
    for (unsigned i = 0; i < sizeof(phrases) / sizeof(phrases[0]); i++) {
        const ko_route_t route = card_route_for(KO_KIND_PHRASE, phrases[i], &session);
        const ko_card_view_t view = ko_view_card(&s_env, &route, &session, true);
        ko_ui_show_card(&view);
        char name[48];
        snprintf(name, sizeof(name), "22_phrase%u_back", i);
        capture(name);
    }

    // 没有发音时：不画喇叭图标、页脚也没有"发音"按键提示。
    s_env.has_clip = NULL;
    const ko_route_t route = card_route_for(KO_KIND_WORD, "빨간색", &session);
    ko_card_view_t no_audio = ko_view_card(&s_env, &route, &session, false);
    ko_ui_show_card(&no_audio);
    capture("23_word_no_audio_front");
    s_env.has_clip = preview_has_clip;

    session.known = 8;
    session.unknown = 2;
    const ko_route_t done_route = { .id = KO_SCR_SESSION_DONE, .arg0 = KO_KIND_WORD,
                                    .arg1 = (uint16_t)ko_word_topic((uint16_t)find_item(ko_words, KO_WORD_COUNT, "빨간색")) };
    const ko_done_view_t done = ko_view_done(&s_env, &done_route, &session);
    ko_ui_show_done(&done);
    capture("24_session_done");
}

static void show_quiz(void) {
    static const char *const names[KO_QUIZ_MODE_COUNT] = { "30_quiz_ko2zh", "31_quiz_zh2ko",
                                                           "32_quiz_letter", "33_quiz_listen" };
    for (int mode = 0; mode < KO_QUIZ_MODE_COUNT; mode++) {
        uint32_t rng = 20260930u + (uint32_t)mode;
        ko_quiz_run_t run;
        ko_quiz_run_begin(&run, (ko_quiz_mode_t)mode, &s_progress, &rng);
        run.index = 2;
        const ko_quiz_view_t view = { .run = &run };

        ko_ui_show_quiz(&view);
        capture(names[mode]);

        // 选中另一项，作答错误，再展示对错。
        ko_quiz_run_move(&run, 1);
        char name[64];
        snprintf(name, sizeof(name), "%s_moved", names[mode]);
        ko_ui_update_quiz(&view);
        capture(name);

        run.selected = (int8_t)((ko_quiz_run_question(&run)->correct + 1) % KO_QUIZ_OPTIONS);
        ko_quiz_run_confirm(&run);
        snprintf(name, sizeof(name), "%s_wrong", names[mode]);
        ko_ui_update_quiz(&view);
        capture(name);

        run.phase = KO_QUIZ_PHASE_ASK;
        run.selected = (int8_t)ko_quiz_run_question(&run)->correct;
        ko_quiz_run_confirm(&run);
        snprintf(name, sizeof(name), "%s_right", names[mode]);
        ko_ui_update_quiz(&view);
        capture(name);
    }

    ko_quiz_run_t result_run;
    memset(&result_run, 0, sizeof(result_run));
    result_run.count = KO_QUIZ_ROUND;
    result_run.score = 10;
    ko_quiz_result_view_t good = ko_view_quiz_result(&s_env, &result_run);
    ko_ui_show_quiz_result(&good);
    capture("34_quiz_result_perfect");
    result_run.score = 6;
    good = ko_view_quiz_result(&s_env, &result_run);
    ko_ui_show_quiz_result(&good);
    capture("35_quiz_result_mid");
}

static void show_battery(void) {
    static const int levels[] = { 100, 57, 15, 3, -1 };
    for (unsigned i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        ko_ui_set_battery(levels[i]);
        char name[32];
        snprintf(name, sizeof(name), "40_battery_%d", levels[i]);
        // 只截页眉附近：直接用整屏，看电量图标与数字。
        capture(name);
    }
    ko_ui_set_battery(72);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);   // 出问题被杀掉时，已有的输出不能丢
    if (argc > 1) s_out_dir = argv[1];

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *display = lv_display_create(KO_SCREEN_W, KO_SCREEN_H);
    lv_display_set_buffers(display, s_render_buf, NULL, sizeof(s_render_buf),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush_cb);

    printf("MEM %-28s used=%5zu\n", "lvgl_init", mem_used());
    ko_ui_init();
    ko_ui_set_battery(72);
    printf("MEM %-28s used=%5zu\n", "chrome_only", mem_used());

    check_fonts();
    printf("FONT-CHECK %s (%d problem(s))\n", s_font_failures ? "FAIL" : "PASS", s_font_failures);

    make_sample_state();
    show_lists();
    show_alpha();
    show_cards();
    show_quiz();
    show_battery();
    printf("PEAK used=%zu of %u\n", s_peak_used, (unsigned)LV_MEM_SIZE);
    return s_font_failures ? 2 : 0;
}
