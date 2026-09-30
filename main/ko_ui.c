// main/ko_ui.c —— 外壳（页眉 / 页脚 / 电量）、共用控件和通用列表页。
#include "ko_ui.h"

#include <stdio.h>
#include <string.h>

#include "ko_ui_internal.h"

// ---------------------------------------------------------------------------
// 共用控件
// ---------------------------------------------------------------------------

lv_obj_t *ko_ui_content;

static lv_obj_t *s_title;
static lv_obj_t *s_hint[3];
static lv_obj_t *s_bat_label;
static lv_obj_t *s_bat_fill;
static lv_obj_t *s_speaker;
static bool s_playing;

// 去掉主题给的默认样式（背景 / 描边 / 内边距），再关掉滚动和点击：
// 这个设备没有触摸，对象只是画面元素。
static lv_obj_t *plain(lv_obj_t *parent) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t *ko_ui_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius) {
    lv_obj_t *box = plain(parent);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, radius, 0);
    return box;
}

void ko_ui_set_frame(lv_obj_t *box, uint32_t color) {
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(box, LV_OPA_COVER, 0);
}

lv_obj_t *ko_ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

void ko_ui_label_box(lv_obj_t *label, int x, int y, int width, lv_text_align_t align) {
    lv_obj_set_pos(label, x, y);
    if (width > 0) lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, align, 0);
}

// 取 UTF-8 串的第一个码点（这里只用于单个字母，输入都是本项目自己的内容表）。
static uint32_t first_codepoint(const char *s) {
    const unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    uint32_t cp = c & (0x3F >> extra);
    for (int i = 1; i <= extra && s[i]; i++) cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
    return cp;
}

// LVGL 绘制字形的位置：墨迹顶 = 标签顶 + 行高 - base_line - box_h - ofs_y，
// 墨迹中心 = 墨迹顶 + box_h / 2。令它等于方框中心，反解标签的 y。
void ko_ui_center_glyph(lv_obj_t *label, const lv_font_t *font, const char *utf8, int x, int box_w,
                        int box_h) {
    lv_font_glyph_dsc_t dsc;
    memset(&dsc, 0, sizeof(dsc));
    int ink_h = 0, ofs_y = 0;
    if (lv_font_get_glyph_dsc(font, &dsc, first_codepoint(utf8), 0)) {
        ink_h = dsc.box_h;
        ofs_y = dsc.ofs_y;
    }
    const int y = box_h / 2 - lv_font_get_line_height(font) + font->base_line + ofs_y + ink_h / 2;
    lv_obj_set_width(label, box_w);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, x, y);
}

void ko_ui_register_speaker(lv_obj_t *speaker) {
    s_speaker = speaker;
    if (speaker) {
        lv_obj_set_style_text_color(speaker, lv_color_hex(s_playing ? KO_COL_RED : KO_COL_SUB), 0);
    }
}

void ko_ui_set_playing(bool playing) {
    if (s_playing == playing) return;
    s_playing = playing;
    if (s_speaker) {
        lv_obj_set_style_text_color(s_speaker, lv_color_hex(playing ? KO_COL_RED : KO_COL_SUB), 0);
    }
}

// ---------------------------------------------------------------------------
// 外壳
// ---------------------------------------------------------------------------

#define HEADER_H 38
#define STRIPE_H 2
#define BAT_W 24
#define BAT_H 12

void ko_ui_init(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(KO_COL_PAPER), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // 页眉：藏青底 + 下方一条红线；标题靠左，电量靠右。
    ko_ui_box(screen, 0, 0, KO_SCREEN_W, HEADER_H, KO_COL_NAVY, 0);
    ko_ui_box(screen, 0, HEADER_H, KO_SCREEN_W, STRIPE_H, KO_COL_RED, 0);
    s_title = ko_ui_label(screen, KO_FONT_ZH_M, KO_COL_NAVY_TX, "");
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_CLIP);
    ko_ui_label_box(s_title, KO_MARGIN, 4, 146, LV_TEXT_ALIGN_LEFT);

    // 电量：图标（外框 + 内衬 + 填充条 + 电池头）和百分比数字。
    const int bat_x = KO_SCREEN_W - KO_MARGIN - BAT_W - 2;
    const int bat_y = (HEADER_H - BAT_H) / 2;
    lv_obj_t *outer = ko_ui_box(screen, bat_x, bat_y, BAT_W, BAT_H, KO_COL_NAVY_TX, 2);
    ko_ui_box(outer, 1, 1, BAT_W - 2, BAT_H - 2, KO_COL_NAVY, 1);
    s_bat_fill = ko_ui_box(outer, 2, 2, BAT_W - 4, BAT_H - 4, KO_COL_NAVY_TX, 1);
    ko_ui_box(screen, bat_x + BAT_W, bat_y + 3, 2, BAT_H - 6, KO_COL_NAVY_TX, 1);
    s_bat_label = ko_ui_label(screen, KO_FONT_ZH_S, KO_COL_NAVY_TX, "--");
    ko_ui_label_box(s_bat_label, bat_x - 44, 9, 40, LV_TEXT_ALIGN_RIGHT);

    // 内容区。
    ko_ui_content = plain(screen);
    lv_obj_set_pos(ko_ui_content, 0, HEADER_H + STRIPE_H);
    lv_obj_set_size(ko_ui_content, KO_SCREEN_W, KO_SCREEN_H - HEADER_H - STRIPE_H - KO_FOOTER_H);

    // 页脚：三格按键提示，与物理按键的上 / 中 / 下一一对应。
    ko_ui_box(screen, 0, KO_SCREEN_H - KO_FOOTER_H, KO_SCREEN_W, KO_FOOTER_H, KO_COL_FOOTER, 0);
    ko_ui_box(screen, 0, KO_SCREEN_H - KO_FOOTER_H, KO_SCREEN_W, 1, KO_COL_LINE, 0);
    const int cell = KO_SCREEN_W / 3;
    for (int i = 0; i < 3; i++) {
        s_hint[i] = ko_ui_label(screen, KO_FONT_ZH_S, KO_COL_INK, "");
        ko_ui_label_box(s_hint[i], i * cell, KO_SCREEN_H - KO_FOOTER_H + 6, cell, LV_TEXT_ALIGN_CENTER);
    }

    lv_screen_load(screen);
}

void ko_ui_set_title(const char *title) {
    lv_label_set_text(s_title, title);
}

void ko_ui_set_hints(const char *left, const char *center, const char *right) {
    const char *text[3] = { left, center, right };
    for (int i = 0; i < 3; i++) {
        if (text[i] && text[i][0]) {
            lv_label_set_text(s_hint[i], text[i]);
            lv_obj_remove_flag(s_hint[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_hint[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ko_ui_begin_page(const char *title) {
    lv_obj_clean(ko_ui_content);
    s_speaker = NULL;
    ko_ui_set_title(title);
}

void ko_ui_set_battery(int percent) {
    if (percent < 0) {
        lv_label_set_text(s_bat_label, "--");
        lv_obj_add_flag(s_bat_fill, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (percent > 100) percent = 100;
    lv_label_set_text_fmt(s_bat_label, "%d%%", percent);
    lv_obj_remove_flag(s_bat_fill, LV_OBJ_FLAG_HIDDEN);
    int width = (BAT_W - 4) * percent / 100;
    if (width < 2) width = 2;   // 再低也留一小条，看得出"还有电但很少"
    lv_obj_set_width(s_bat_fill, width);
    // 15% 以下变红，提醒该充电了。
    lv_obj_set_style_bg_color(s_bat_fill,
                              lv_color_hex(percent <= 15 ? KO_COL_RED : KO_COL_NAVY_TX), 0);
}

// ---------------------------------------------------------------------------
// 通用列表页
//
// 内存：LVGL 内置池只有 24 KB，列表页是对象最多的页面（5 行 × 若干对象）。
// 所以行的外观全部用"共享样式"，选中态用 LV_STATE_CHECKED 切换，而不是给每个对象
// 挂一份本地样式；没有徽标 / 数值的列表不创建对应的对象；固定文字用 static 文本，
// 不在池里再拷一份。
// ---------------------------------------------------------------------------

#define ROW_H 46
#define ROW_GAP 2
#define ROW_TOP 4
#define EMBLEM 32
#define EMBLEM_COLORS 8

static lv_style_t st_row, st_row_selected, st_emblem, st_emblem_text;
static lv_style_t st_title, st_title_off, st_sub, st_value;
// 徽标颜色是有限的几种（每个模块一个色）：按颜色缓存共享样式。
static struct {
    uint32_t color;
    lv_style_t style;
    bool used;
} s_emblem_color[EMBLEM_COLORS];
static bool s_styles_ready;

static void init_list_styles(void) {
    if (s_styles_ready) return;
    s_styles_ready = true;

    lv_style_init(&st_row);
    lv_style_set_bg_color(&st_row, lv_color_hex(KO_COL_CARD));
    lv_style_set_bg_opa(&st_row, LV_OPA_COVER);
    lv_style_set_radius(&st_row, 10);
    lv_style_set_border_width(&st_row, 2);
    lv_style_set_border_color(&st_row, lv_color_hex(KO_COL_LINE));

    // 选中：换成白底 + 红边，同一棵样式树里靠状态切换。
    lv_style_init(&st_row_selected);
    lv_style_set_bg_color(&st_row_selected, lv_color_hex(0xFFFFFF));
    lv_style_set_border_color(&st_row_selected, lv_color_hex(KO_COL_RED));

    lv_style_init(&st_emblem);
    lv_style_set_bg_opa(&st_emblem, LV_OPA_COVER);
    lv_style_set_radius(&st_emblem, 8);
    lv_style_init(&st_emblem_text);
    lv_style_set_text_font(&st_emblem_text, ko_font_hangul(24));
    lv_style_set_text_color(&st_emblem_text, lv_color_hex(0xFFFFFF));

    lv_style_init(&st_title);
    lv_style_set_text_font(&st_title, KO_FONT_ZH_M);
    lv_style_set_text_color(&st_title, lv_color_hex(KO_COL_INK));
    lv_style_init(&st_title_off);
    lv_style_set_text_font(&st_title_off, KO_FONT_ZH_M);
    lv_style_set_text_color(&st_title_off, lv_color_hex(KO_COL_GRAY));
    lv_style_init(&st_sub);
    lv_style_set_text_font(&st_sub, KO_FONT_ZH_S);
    lv_style_set_text_color(&st_sub, lv_color_hex(KO_COL_SUB));
    lv_style_init(&st_value);
    lv_style_set_text_font(&st_value, KO_FONT_ZH_M);
    lv_style_set_text_color(&st_value, lv_color_hex(KO_COL_RED));
}

static lv_style_t *emblem_color_style(uint32_t color) {
    for (int i = 0; i < EMBLEM_COLORS; i++) {
        if (s_emblem_color[i].used && s_emblem_color[i].color == color) return &s_emblem_color[i].style;
    }
    for (int i = 0; i < EMBLEM_COLORS; i++) {
        if (!s_emblem_color[i].used) {
            s_emblem_color[i].used = true;
            s_emblem_color[i].color = color;
            lv_style_init(&s_emblem_color[i].style);
            lv_style_set_bg_color(&s_emblem_color[i].style, lv_color_hex(color));
            return &s_emblem_color[i].style;
        }
    }
    return &s_emblem_color[0].style;   // 颜色种类超出预期：用第一种兜底，不崩
}

typedef struct {
    lv_obj_t *box;
    lv_obj_t *emblem;
    lv_obj_t *emblem_text;
    lv_obj_t *title;
    lv_obj_t *sub;
    lv_obj_t *value;
    lv_style_t *emblem_style;   // 当前挂在徽标上的颜色样式
    bool title_off;
} list_row_t;

static struct {
    list_row_t row[KO_LIST_VISIBLE];
    lv_obj_t *track;
    lv_obj_t *thumb;
    bool has_emblem;
    bool has_value;
} s_list;

void ko_ui_show_list(const ko_list_view_t *view) {
    init_list_styles();
    ko_ui_begin_page(view->title);
    memset(&s_list, 0, sizeof(s_list));
    s_list.has_emblem = view->row_emblem != NULL;
    s_list.has_value = view->row_value != NULL;

    for (int r = 0; r < KO_LIST_VISIBLE; r++) {
        list_row_t *row = &s_list.row[r];
        row->box = plain(ko_ui_content);
        lv_obj_set_pos(row->box, KO_MARGIN, ROW_TOP + r * (ROW_H + ROW_GAP));
        lv_obj_set_size(row->box, KO_CONTENT_W, ROW_H);
        lv_obj_add_style(row->box, &st_row, 0);
        lv_obj_add_style(row->box, &st_row_selected, LV_STATE_CHECKED);

        if (s_list.has_emblem) {
            row->emblem = plain(row->box);
            lv_obj_set_pos(row->emblem, 4, 4);
            lv_obj_set_size(row->emblem, EMBLEM, EMBLEM);
            lv_obj_add_style(row->emblem, &st_emblem, 0);
            row->emblem_text = lv_label_create(row->emblem);
            lv_obj_add_style(row->emblem_text, &st_emblem_text, 0);
        }
        row->title = lv_label_create(row->box);
        lv_obj_add_style(row->title, &st_title, 0);
        lv_label_set_long_mode(row->title, LV_LABEL_LONG_CLIP);
        row->sub = lv_label_create(row->box);
        lv_obj_add_style(row->sub, &st_sub, 0);
        lv_label_set_long_mode(row->sub, LV_LABEL_LONG_CLIP);
        if (s_list.has_value) {
            row->value = lv_label_create(row->box);
            lv_obj_add_style(row->value, &st_value, 0);
            lv_obj_align(row->value, LV_ALIGN_RIGHT_MID, -4, 0);
        }
    }

    // 右侧滚动条：只在条目多于一屏时显示。
    const int track_h = KO_LIST_VISIBLE * ROW_H + (KO_LIST_VISIBLE - 1) * ROW_GAP;
    s_list.track = ko_ui_box(ko_ui_content, KO_SCREEN_W - 9, ROW_TOP, 3, track_h, KO_COL_LINE, 1);
    s_list.thumb = ko_ui_box(s_list.track, 0, 0, 3, track_h, KO_COL_GRAY, 1);

    ko_ui_update_list(view);
}

void ko_ui_update_list(const ko_list_view_t *view) {
    ko_ui_set_hints(view->hint_left, view->hint_center, view->hint_right);

    for (int r = 0; r < KO_LIST_VISIBLE; r++) {
        list_row_t *row = &s_list.row[r];
        const uint16_t index = (uint16_t)(view->first + r);
        if (index >= view->count) {
            lv_obj_add_flag(row->box, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(row->box, LV_OBJ_FLAG_HIDDEN);

        const bool enabled = !view->row_enabled || view->row_enabled(index, view->ctx);
        if (index == view->selected) lv_obj_add_state(row->box, LV_STATE_CHECKED);
        else lv_obj_remove_state(row->box, LV_STATE_CHECKED);

        // 徽标：颜色样式随行内容更换（共享样式，不分配新内存）。
        int text_x = 8;
        if (row->emblem) {
            const char *emblem = view->row_emblem(index, view->ctx);
            if (emblem && emblem[0]) {
                uint32_t color = view->row_color ? view->row_color(index, view->ctx) : KO_COL_BLUE;
                if (!enabled) color = KO_COL_GRAY;
                lv_style_t *style = emblem_color_style(color);
                if (row->emblem_style != style) {
                    if (row->emblem_style) lv_obj_remove_style(row->emblem, row->emblem_style, 0);
                    lv_obj_add_style(row->emblem, style, 0);
                    row->emblem_style = style;
                }
                lv_obj_remove_flag(row->emblem, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(row->emblem_text, emblem);
                lv_obj_center(row->emblem_text);
                text_x = 4 + EMBLEM + 8;
            } else {
                lv_obj_add_flag(row->emblem, LV_OBJ_FLAG_HIDDEN);
            }
        }

        // 右侧数值（设置页的音量 / 亮度）。
        char value[24] = "";
        if (row->value) {
            if (view->row_value) view->row_value(index, value, sizeof(value), view->ctx);
            lv_label_set_text(row->value, value);
        }
        const int text_w = KO_CONTENT_W - 4 - text_x - (value[0] ? 64 : 8);

        // 有副标题时两行叠放；没有就让标题在行内垂直居中。
        char sub[64] = "";
        if (view->row_sub) view->row_sub(index, sub, sizeof(sub), view->ctx);
        lv_label_set_text_static(row->title, view->row_title(index, view->ctx));
        // title_off 记录当前挂的是灰色样式；与 enabled 不一致时才换（共享样式，换起来不分配内存）。
        if (row->title_off == enabled) {
            lv_obj_remove_style(row->title, enabled ? &st_title_off : &st_title, 0);
            lv_obj_add_style(row->title, enabled ? &st_title : &st_title_off, 0);
            row->title_off = !enabled;
        }
        // 无副标题时与数值标签（RIGHT_MID，同为 20 号字）同一高度：(42 - 29) / 2 ≈ 6。
        lv_obj_set_pos(row->title, text_x, sub[0] ? -2 : 6);
        lv_obj_set_width(row->title, text_w);
        if (sub[0]) {
            lv_label_set_text(row->sub, sub);
            lv_obj_remove_flag(row->sub, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(row->sub, text_x, 20);
            lv_obj_set_width(row->sub, text_w);
        } else {
            lv_obj_add_flag(row->sub, LV_OBJ_FLAG_HIDDEN);
        }
    }

    const bool scrollable = view->count > KO_LIST_VISIBLE;
    if (scrollable) {
        const int track_h = KO_LIST_VISIBLE * ROW_H + (KO_LIST_VISIBLE - 1) * ROW_GAP;
        int thumb_h = track_h * KO_LIST_VISIBLE / view->count;
        if (thumb_h < 8) thumb_h = 8;
        const int travel = track_h - thumb_h;
        const int max_first = view->count - KO_LIST_VISIBLE;
        lv_obj_remove_flag(s_list.track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(s_list.thumb, thumb_h);
        lv_obj_set_y(s_list.thumb, max_first > 0 ? travel * view->first / max_first : 0);
    } else {
        lv_obj_add_flag(s_list.track, LV_OBJ_FLAG_HIDDEN);
    }
}
