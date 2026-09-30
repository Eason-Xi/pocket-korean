// main/ko_ui_card.c —— 词汇 / 短语卡片，以及"本轮完成"页。
#include <stdio.h>

#include "ko_ui.h"
#include "ko_ui_internal.h"

#define CARD_Y 22
#define CARD_H 214
#define CARD_INNER_W (KO_CONTENT_W - 4 - 16)   // 卡片去掉描边后再留 8 px 内边距
#define CARD_INNER_H (CARD_H - 4)
#define GAP 6

static struct {
    lv_obj_t *progress;
    lv_obj_t *card;
    lv_obj_t *ko;
    lv_obj_t *rom;
    lv_obj_t *divider;
    lv_obj_t *zh;
    lv_obj_t *speaker;
} s;

// 多行韩文的行距收紧：字体的行盒比字形高很多（含很大的上下留白），
// 两行叠起来会显得松散。把行距收到字号的 1.15 倍。
static void tighten_lines(lv_obj_t *label, const lv_font_t *font, int px) {
    const int natural = lv_font_get_line_height(font);
    lv_obj_set_style_text_line_space(label, px * 115 / 100 - natural, 0);   // 整数运算：C3 没有 FPU
}

// 竖直方向依次摆放：韩文、罗马音、（翻面后）分隔线与中文释义。
// 高度按"翻面后的整体"来算，这样翻面前后韩文的位置不变，不会跳。
static void layout_card(const ko_card_view_t *view) {
    const ko_item_t *item = view->item;
    const int px = view->is_phrase ? ko_phrase_font_px(item->ko) : ko_word_font_px(item->ko);
    const lv_font_t *font = ko_font_hangul(px);

    // 内容表里的字符串常驻 Flash：用 static 文本，不在 LVGL 内存池里再拷一份。
    lv_label_set_text_static(s.ko, item->ko);
    lv_obj_set_style_text_font(s.ko, font, 0);
    tighten_lines(s.ko, font, px);
    lv_obj_set_width(s.ko, CARD_INNER_W);
    lv_label_set_text_static(s.rom, item->rom);
    lv_obj_set_width(s.rom, CARD_INNER_W);
    lv_label_set_text_static(s.zh, item->zh);
    lv_obj_set_width(s.zh, CARD_INNER_W);
    lv_obj_update_layout(s.card);

    const int ko_h = lv_obj_get_height(s.ko);
    const int rom_h = lv_obj_get_height(s.rom);
    const int zh_h = lv_obj_get_height(s.zh);
    const int divider_h = 3;
    const int total = ko_h + GAP + rom_h + GAP + divider_h + GAP + zh_h;
    int y = (CARD_INNER_H - total) / 2;
    if (y < 4) y = 4;

    lv_obj_set_pos(s.ko, 8, y);
    y += ko_h + GAP;
    lv_obj_set_pos(s.rom, 8, y);
    y += rom_h + GAP;
    lv_obj_set_pos(s.divider, (CARD_INNER_W + 16 - 28) / 2, y);
    y += divider_h + GAP;
    lv_obj_set_pos(s.zh, 8, y);
}

void ko_ui_show_card(const ko_card_view_t *view) {
    ko_ui_begin_page(view->deck_title);
    lv_obj_t *page = ko_ui_content;

    s.progress = ko_ui_label(page, KO_FONT_ZH_S, KO_COL_SUB, "");
    ko_ui_label_box(s.progress, KO_SCREEN_W - KO_MARGIN - 80, 1, 80, LV_TEXT_ALIGN_RIGHT);

    s.card = ko_ui_box(page, KO_MARGIN, CARD_Y, KO_CONTENT_W, CARD_H, KO_COL_CARD, 16);
    ko_ui_set_frame(s.card, KO_COL_LINE);

    s.ko = ko_ui_label(s.card, ko_font_hangul(48), KO_COL_INK, "");
    lv_obj_set_style_text_align(s.ko, LV_TEXT_ALIGN_CENTER, 0);
    s.rom = ko_ui_label(s.card, KO_FONT_ZH_S, KO_COL_SUB, "");
    lv_obj_set_style_text_align(s.rom, LV_TEXT_ALIGN_CENTER, 0);
    s.divider = ko_ui_box(s.card, 0, 0, 28, 3, KO_COL_RED, 1);
    s.zh = ko_ui_label(s.card, KO_FONT_ZH_M, KO_COL_RED, "");
    lv_obj_set_style_text_align(s.zh, LV_TEXT_ALIGN_CENTER, 0);

    s.speaker = ko_ui_label(s.card, KO_FONT_ZH_S, KO_COL_SUB, KO_S_SYM_NOTE);
    lv_obj_align(s.speaker, LV_ALIGN_TOP_RIGHT, -2, -2);
    ko_ui_register_speaker(s.speaker);

    ko_ui_update_card(view);
}

void ko_ui_update_card(const ko_card_view_t *view) {
    lv_label_set_text_fmt(s.progress, "%d/%d", view->done, view->total);
    layout_card(view);

    if (view->back) {
        lv_obj_remove_flag(s.divider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s.zh, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s.divider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s.zh, LV_OBJ_FLAG_HIDDEN);
    }
    if (view->has_audio) lv_obj_remove_flag(s.speaker, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s.speaker, LV_OBJ_FLAG_HIDDEN);

    if (view->back) {
        ko_ui_set_hints(KO_S_SYM_UP " " KO_S_HINT_UNKNOWN,
                        view->has_audio ? KO_S_SYM_OK " " KO_S_HINT_REPLAY : "",
                        KO_S_SYM_DOWN " " KO_S_HINT_KNOWN);
    } else {
        ko_ui_set_hints(view->has_audio ? KO_S_SYM_UP " " KO_S_HINT_PLAY : "",
                        KO_S_SYM_OK " " KO_S_HINT_FLIP, KO_S_HINT_BACK);
    }
}

// ---------------------------------------------------------------------------
// 本轮完成
// ---------------------------------------------------------------------------

void ko_ui_show_done(const ko_done_view_t *view) {
    ko_ui_begin_page(view->deck_title);
    lv_obj_t *page = ko_ui_content;

    lv_obj_t *card = ko_ui_box(page, KO_MARGIN, 22, KO_CONTENT_W, 178, KO_COL_CARD, 16);
    ko_ui_set_frame(card, KO_COL_LINE);

    lv_obj_t *mark = ko_ui_box(card, (KO_CONTENT_W - 4 - 44) / 2, 12, 44, 44, KO_COL_OK, 22);
    lv_obj_t *check = ko_ui_label(mark, KO_FONT_ZH_M, 0xFFFFFF, KO_S_SYM_CHECK);
    lv_obj_center(check);

    lv_obj_t *title = ko_ui_label(card, KO_FONT_ZH_M, KO_COL_INK, KO_S_SESSION_DONE);
    ko_ui_label_box(title, 0, 62, KO_CONTENT_W - 4, LV_TEXT_ALIGN_CENTER);

    char line[48];
    snprintf(line, sizeof(line), KO_S_SESSION_SUMMARY, view->known, view->unknown);
    lv_obj_t *summary = ko_ui_label(card, KO_FONT_ZH_S, KO_COL_SUB, line);
    ko_ui_label_box(summary, 0, 96, KO_CONTENT_W - 4, LV_TEXT_ALIGN_CENTER);

    // 该主题的掌握进度条。
    const int bar_w = KO_CONTENT_W - 4 - 32;
    lv_obj_t *track = ko_ui_box(card, 16, 136, bar_w, 8, KO_COL_LINE, 4);
    const int fill = view->deck_total ? bar_w * view->mastered / view->deck_total : 0;
    if (fill > 0) ko_ui_box(track, 0, 0, fill < 8 ? 8 : fill, 8, KO_COL_OK, 4);
    snprintf(line, sizeof(line), KO_S_SUB_MASTERED, view->mastered, view->deck_total);
    lv_obj_t *mastered = ko_ui_label(card, KO_FONT_ZH_S, KO_COL_SUB, line);
    ko_ui_label_box(mastered, 0, 148, KO_CONTENT_W - 4, LV_TEXT_ALIGN_CENTER);

    ko_ui_set_hints("", KO_S_SYM_OK " " KO_S_HINT_AGAIN, KO_S_HINT_BACK);
}
