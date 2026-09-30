// main/ko_ui_alpha.c —— 字母卡片：一个字母一页，大字母 + 名称 / 读音 / 拼读 / 例词。
#include <stdio.h>

#include "ko_ui.h"
#include "ko_ui_internal.h"

#define CARD_Y 20
#define CARD_H 96   // ㅐ 这类最高的字母墨迹约 85 px，卡片内高 92 才留得出上下边距
#define CARD_INNER_H (CARD_H - 4)
#define GLYPH_W 100

static struct {
    lv_obj_t *group;      // 组名
    lv_obj_t *position;   // "3/10"
    lv_obj_t *glyph;
    lv_obj_t *speaker;
    lv_obj_t *name;
    lv_obj_t *name_rom;
    lv_obj_t *sound;
    lv_obj_t *hint;
    lv_obj_t *combo;
    lv_obj_t *combo_rom;
    lv_obj_t *ex;
    lv_obj_t *ex_zh;
    lv_obj_t *ex_rom;
} s;

// 静态文字：内容表里的字符串常驻 Flash，直接引用，不在 LVGL 内存池里再拷一份。
static void set_static(lv_obj_t *label, const char *text) {
    lv_label_set_text_static(label, text);
}

void ko_ui_show_alpha(const ko_alpha_view_t *view) {
    ko_ui_begin_page(KO_S_HOME_ALPHA);
    lv_obj_t *page = ko_ui_content;

    s.group = ko_ui_label(page, KO_FONT_ZH_S, KO_COL_SUB, "");
    ko_ui_label_box(s.group, KO_MARGIN, 0, 110, LV_TEXT_ALIGN_LEFT);
    s.position = ko_ui_label(page, KO_FONT_ZH_S, KO_COL_SUB, "");
    ko_ui_label_box(s.position, KO_SCREEN_W - KO_MARGIN - 80, 0, 80, LV_TEXT_ALIGN_RIGHT);

    // 主卡片：左边大字母，右边名称与读音。
    lv_obj_t *card = ko_ui_box(page, KO_MARGIN, CARD_Y, KO_CONTENT_W, CARD_H, KO_COL_CARD, 14);
    ko_ui_set_frame(card, KO_COL_LINE);
    s.glyph = ko_ui_label(card, KO_FONT_JAMO, KO_COL_RED, "");

    s.name = ko_ui_label(card, ko_font_hangul(24), KO_COL_INK, "");
    ko_ui_label_box(s.name, GLYPH_W + 2, 2, 96, LV_TEXT_ALIGN_LEFT);
    s.name_rom = ko_ui_label(card, KO_FONT_ZH_S, KO_COL_SUB, "");
    ko_ui_label_box(s.name_rom, GLYPH_W + 2, 36, 96, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *sound_tag = ko_ui_label(card, KO_FONT_ZH_S, KO_COL_SUB, KO_S_LAB_SOUND);
    ko_ui_label_box(sound_tag, GLYPH_W + 2, 64, 40, LV_TEXT_ALIGN_LEFT);
    s.sound = ko_ui_label(card, KO_FONT_ZH_M, KO_COL_RED, "");
    ko_ui_label_box(s.sound, GLYPH_W + 38, 58, 60, LV_TEXT_ALIGN_LEFT);

    s.speaker = ko_ui_label(card, KO_FONT_ZH_S, KO_COL_SUB, KO_S_SYM_NOTE);
    lv_obj_align(s.speaker, LV_ALIGN_TOP_RIGHT, -4, 0);
    ko_ui_register_speaker(s.speaker);

    // 近似汉语读音提示：最多两行。
    const int hint_y = CARD_Y + CARD_H + 1;
    s.hint = ko_ui_label(page, KO_FONT_ZH_S, KO_COL_INK, "");
    ko_ui_label_box(s.hint, KO_MARGIN + 4, hint_y, KO_CONTENT_W - 8, LV_TEXT_ALIGN_LEFT);

    // 拼读：辅音 + ㅏ、ㅇ + 元音。
    const int combo_y = hint_y + 42;
    lv_obj_t *combo_tag = ko_ui_label(page, KO_FONT_ZH_S, KO_COL_SUB, KO_S_LAB_COMBO);
    ko_ui_label_box(combo_tag, KO_MARGIN + 4, combo_y + 8, 40, LV_TEXT_ALIGN_LEFT);
    s.combo = ko_ui_label(page, ko_font_hangul(24), KO_COL_INK, "");
    ko_ui_label_box(s.combo, KO_MARGIN + 46, combo_y, 120, LV_TEXT_ALIGN_LEFT);
    s.combo_rom = ko_ui_label(page, KO_FONT_ZH_S, KO_COL_SUB, "");
    ko_ui_label_box(s.combo_rom, KO_SCREEN_W - KO_MARGIN - 50, combo_y + 8, 46, LV_TEXT_ALIGN_RIGHT);

    // 例词：韩文 + 中文释义，罗马音放在下一行。
    const int ex_y = combo_y + 33;
    lv_obj_t *ex_tag = ko_ui_label(page, KO_FONT_ZH_S, KO_COL_SUB, KO_S_LAB_EXAMPLE);
    ko_ui_label_box(ex_tag, KO_MARGIN + 4, ex_y + 8, 40, LV_TEXT_ALIGN_LEFT);
    s.ex = ko_ui_label(page, ko_font_hangul(24), KO_COL_INK, "");
    ko_ui_label_box(s.ex, KO_MARGIN + 46, ex_y, 96, LV_TEXT_ALIGN_LEFT);
    s.ex_zh = ko_ui_label(page, KO_FONT_ZH_M, KO_COL_INK, "");
    ko_ui_label_box(s.ex_zh, KO_SCREEN_W - KO_MARGIN - 84, ex_y + 3, 80, LV_TEXT_ALIGN_RIGHT);
    s.ex_rom = ko_ui_label(page, KO_FONT_ZH_S, KO_COL_SUB, "");
    ko_ui_label_box(s.ex_rom, KO_MARGIN + 46, ex_y + 30, 160, LV_TEXT_ALIGN_LEFT);

    ko_ui_update_alpha(view);
}

void ko_ui_update_alpha(const ko_alpha_view_t *view) {
    const ko_letter_t *letter = &ko_letters[view->letter];
    const int group_index = ko_letter_group_of(view->letter);
    const ko_letter_group_t *group = &ko_letter_groups[group_index];
    // 元音用蓝、辅音用红：一眼看出当前在学哪一类。
    const bool vowel = group_index == 0 || group_index == KO_LETTER_GROUP_COUNT - 1;

    set_static(s.group, group->zh);
    lv_label_set_text_fmt(s.position, "%d/%d", view->letter - group->first + 1, group->count);

    set_static(s.glyph, letter->ch);
    lv_obj_set_style_text_color(s.glyph, lv_color_hex(vowel ? KO_COL_BLUE : KO_COL_RED), 0);
    ko_ui_center_glyph(s.glyph, KO_FONT_JAMO, letter->ch, 0, GLYPH_W, CARD_INNER_H);
    set_static(s.name, letter->name);
    set_static(s.name_rom, letter->name_rom);
    set_static(s.sound, letter->sound);
    set_static(s.hint, letter->hint);

    char combo[48];
    snprintf(combo, sizeof(combo), "%s = %s", letter->pair, letter->syl);
    lv_label_set_text(s.combo, combo);
    set_static(s.combo_rom, letter->syl_rom);
    set_static(s.ex, letter->ex);
    set_static(s.ex_zh, letter->ex_zh);
    set_static(s.ex_rom, letter->ex_rom);

    if (view->has_audio) lv_obj_remove_flag(s.speaker, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s.speaker, LV_OBJ_FLAG_HIDDEN);

    ko_ui_set_hints(KO_S_SYM_UP " " KO_S_HINT_PREV,
                    view->has_audio ? KO_S_SYM_OK " " KO_S_HINT_PLAY : "",
                    KO_S_SYM_DOWN " " KO_S_HINT_NEXT);
}
