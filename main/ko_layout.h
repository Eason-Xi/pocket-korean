// main/ko_layout.h —— 屏幕几何与文字排版估算。纯逻辑，可在主机上测试。
//
// 屏幕四个角被 BSP 以半径 30 px 涂黑（BSP_LVGL_SCREEN_RADIUS），
// 所以页眉 / 页脚的文字要离开角落一段距离；ko_app.c 里有静态断言核对本文件的
// 尺寸与 bsp_pins.h 一致，引脚与面板参数的唯一来源仍然是 bsp_pins.h。
#pragma once

#include <stdint.h>

#define KO_SCREEN_W 240
#define KO_SCREEN_H 320
#define KO_HEADER_H 40
#define KO_FOOTER_H 32
#define KO_MARGIN 14
#define KO_CONTENT_W (KO_SCREEN_W - 2 * KO_MARGIN)
#define KO_CONTENT_H (KO_SCREEN_H - KO_HEADER_H - KO_FOOTER_H)

// UTF-8 码点个数（遇到非法字节按一个码点计，保证不会死循环）。
int ko_utf8_codepoints(const char *utf8);

// 文字宽度估算，单位 1/100 em：谚文 / 汉字按全角 100，ASCII 字母数字约 56，
// 空格 22，其余 ASCII 标点约 35。只用来挑字号，真实换行由 LVGL 按字形宽度完成。
int ko_text_width_units(const char *utf8);

// 在 width_px 宽度内、以 font_px 字号按空格换行，估算需要几行（至少 1）。
int ko_text_lines(const char *utf8, int font_px, int width_px);

// 最长的一个词（按空格分隔）在该字号下的估算宽度（px）。
int ko_text_longest_token_px(const char *utf8, int font_px);

// 词汇卡片主体：在 48 / 32 / 24 里选最大的、最长词放得进一行且不超过 2 行的字号。
int ko_word_font_px(const char *utf8);
// 短语卡片：优先 32（最长词放得进一行且不超过 3 行），否则 24。
int ko_phrase_font_px(const char *utf8);

// 列表滚动：让选中项保持在可见窗口里，只在必要时滚动。返回窗口第一行的序号。
uint16_t ko_list_first(uint16_t selected, uint16_t count, uint16_t visible, uint16_t prev_first);
