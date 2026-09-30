// main/ko_fonts.c —— 韩文字号 → 字体。
#include "ko_fonts.h"

const lv_font_t *ko_font_hangul(int px) {
    if (px >= 48) return &ko_ko48;
    if (px >= 32) return &ko_ko32;
    return &ko_ko24;
}
