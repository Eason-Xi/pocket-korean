// main/ko_layout.c —— 文字排版估算与列表滚动。
#include "ko_layout.h"

#include <stdbool.h>

// UTF-8 首字节 → 该码点占几个字节。非法首字节按 1 字节前进。
static int utf8_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if (lead >= 0xF0) return 4;
    if (lead >= 0xE0) return 3;
    if (lead >= 0xC0) return 2;
    return 1;
}

static int utf8_advance(const char *s, int length) {
    int taken = 0;
    while (taken < length && s[taken] != '\0') taken++;
    return taken > 0 ? taken : 1;
}

int ko_utf8_codepoints(const char *utf8) {
    int count = 0;
    while (*utf8) {
        utf8 += utf8_advance(utf8, utf8_length((unsigned char)*utf8));
        count++;
    }
    return count;
}

static int ascii_units(char c) {
    if (c == ' ') return 22;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return 56;
    return 35;
}

int ko_text_width_units(const char *utf8) {
    int units = 0;
    while (*utf8) {
        const unsigned char lead = (unsigned char)*utf8;
        units += lead < 0x80 ? ascii_units((char)lead) : 100;
        utf8 += utf8_advance(utf8, utf8_length(lead));
    }
    return units;
}

// 一个不含空格的词占多宽（单位：1/100 px = units * font_px）。
static long token_width(const char *begin, const char *end, int font_px) {
    long units = 0;
    for (const char *p = begin; p < end;) {
        const unsigned char lead = (unsigned char)*p;
        units += lead < 0x80 ? ascii_units((char)lead) : 100;
        p += utf8_advance(p, utf8_length(lead));
    }
    return units * font_px;
}

int ko_text_lines(const char *utf8, int font_px, int width_px) {
    const long line_width = (long)width_px * 100;
    const long space = 22L * font_px;
    int lines = 1;
    long current = 0;   // 当前行已占宽度，0 表示这一行还没有内容

    const char *p = utf8;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *end = p;
        while (*end && *end != ' ') end++;
        const long width = token_width(p, end, font_px);
        p = end;

        if (width > line_width) {
            // 单个词比一整行还宽：LVGL 会在字符间断开。先另起一行放它。
            if (current > 0) lines++;
            lines += (int)((width - 1) / line_width);   // 除去第一行，还要几行
            current = width % line_width;
            if (current == 0) current = line_width;
        } else if (current == 0) {
            current = width;
        } else if (current + space + width <= line_width) {
            current += space + width;
        } else {
            lines++;
            current = width;
        }
    }
    return lines;
}

int ko_text_longest_token_px(const char *utf8, int font_px) {
    long longest = 0;
    const char *p = utf8;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *end = p;
        while (*end && *end != ' ') end++;
        const long width = token_width(p, end, font_px);
        if (width > longest) longest = width;
        p = end;
    }
    return (int)(longest / 100);
}

// 一个字号可用的条件：最长的词能放进一行（否则 LVGL 会把词从中间劈开，很难看），
// 并且总行数不超过上限。
static bool font_fits(const char *utf8, int font_px, int max_lines) {
    return ko_text_longest_token_px(utf8, font_px) <= KO_CONTENT_W &&
           ko_text_lines(utf8, font_px, KO_CONTENT_W) <= max_lines;
}

int ko_word_font_px(const char *utf8) {
    if (font_fits(utf8, 48, 2)) return 48;
    if (font_fits(utf8, 32, 2)) return 32;
    return 24;
}

int ko_phrase_font_px(const char *utf8) {
    return font_fits(utf8, 32, 3) ? 32 : 24;
}

uint16_t ko_list_first(uint16_t selected, uint16_t count, uint16_t visible, uint16_t prev_first) {
    if (visible == 0 || count <= visible) return 0;
    uint16_t first = prev_first;
    if (selected < first) first = selected;
    if (selected >= (uint16_t)(first + visible)) first = (uint16_t)(selected - visible + 1);
    const uint16_t max_first = (uint16_t)(count - visible);
    return first > max_first ? max_first : first;
}
