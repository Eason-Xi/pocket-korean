// main/ko_ui_internal.h —— 界面各页面共用的内部函数（不是公共接口）。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#include "ko_fonts.h"
#include "ko_layout.h"
#include "ko_strings.h"
#include "ko_theme.h"

// 内容区容器：外壳里页眉与页脚之间的区域，页面的对象都建在它下面。
extern lv_obj_t *ko_ui_content;

// 清空内容区并设置标题；每个 show_* 先调它。
void ko_ui_begin_page(const char *title);
void ko_ui_set_title(const char *title);
// NULL 或空串表示隐藏该格。
void ko_ui_set_hints(const char *left, const char *center, const char *right);

// 页面登记自己的发音图标，ko_ui_set_playing() 会给它上色。不用时传 NULL。
void ko_ui_register_speaker(lv_obj_t *speaker);

// 纯色圆角块（无边框、无内边距、不可滚动）。
lv_obj_t *ko_ui_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius);
// 单一文字的标签。width > 0 时固定宽度并换行；align 为 LV_TEXT_ALIGN_*。
lv_obj_t *ko_ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
void ko_ui_label_box(lv_obj_t *label, int x, int y, int width, lv_text_align_t align);

// 给块加 / 改描边（宽度固定为 2，只改颜色，这样子对象的位置不会因选中而抖动）。
void ko_ui_set_frame(lv_obj_t *box, uint32_t color);

// 把单个大字（如字母）的"字形墨迹"在 (x, 0, box_w, box_h) 这个方框里居中。
// 字体的行盒比字形高得多（含上下留白），直接把标签居中会让字偏上或偏下；
// 这里按该字形的实际度量（box_h / ofs_y）和字体的 base_line 反推标签位置。
void ko_ui_center_glyph(lv_obj_t *label, const lv_font_t *font, const char *utf8, int x, int box_w,
                        int box_h);
