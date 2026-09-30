// main/ko_fonts.h —— 应用用到的 LVGL 字体。字体本身由 tools/gen_korean_assets.py 用
// lv_font_conv 从 Source Han Sans（SIL OFL）裁剪生成，放在 assets/fonts/ko_*.c，
// 字符集由 assets/korean/content.json 精确推导（见 assets/README.md）。
//
// 一个字体只负责一种文字：中文字体不含谚文，韩文字体不含汉字。
// 所以界面里任何一个标签都只能放单一文字，混排要拆成两个标签。
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(ko_zh14);
LV_FONT_DECLARE(ko_zh20);
LV_FONT_DECLARE(ko_ko24);
LV_FONT_DECLARE(ko_ko32);
LV_FONT_DECLARE(ko_ko48);
LV_FONT_DECLARE(ko_jamo96);

#define KO_FONT_ZH_S (&ko_zh14)
#define KO_FONT_ZH_M (&ko_zh20)
#define KO_FONT_JAMO (&ko_jamo96)

// 韩文字体：px 取 24 / 32 / 48（ko_layout 的选字号函数返回的就是这三个值）。
const lv_font_t *ko_font_hangul(int px);
