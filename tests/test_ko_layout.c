// tests/test_ko_layout.c —— 文字排版估算与列表滚动，并用真实内容检查版面预算。
#include <string.h>

#include "ko_content.h"
#include "ko_layout.h"
#include "ko_test.h"

static void test_utf8_and_width(void) {
    CHECK(ko_utf8_codepoints("") == 0);
    CHECK(ko_utf8_codepoints("abc") == 3);
    CHECK(ko_utf8_codepoints("안녕") == 2);
    CHECK(ko_utf8_codepoints("a가b") == 3);
    // 孤立的多字节首字节（截断的 UTF-8）：按一个码点计，不能死循环或越过结尾。
    CHECK(ko_utf8_codepoints("\xE2") == 1);
    CHECK(ko_utf8_codepoints("a\xE2\x82") == 2);
    CHECK(ko_utf8_codepoints("\x80\x80") == 2);

    CHECK(ko_text_width_units("가") == 100);
    CHECK(ko_text_width_units("ab") == 112);
    CHECK(ko_text_width_units("a b") == 56 + 22 + 56);
    CHECK(ko_text_width_units("?") == 35);
}

static void test_lines(void) {
    // 单词放得进一行。
    CHECK(ko_text_lines("하나", 48, KO_CONTENT_W) == 1);
    // 两个词一行放不下：换行。
    CHECK(ko_text_lines("안녕히 가세요", 48, KO_CONTENT_W) == 2);
    // 同样的文字字号小了可以合成一行。
    CHECK(ko_text_lines("안녕히 가세요", 24, KO_CONTENT_W) == 1);
    // 单个词比一行还宽：LVGL 会在字符间断开，估算要算上多出来的行。
    CHECK(ko_text_lines("안녕하세요", 48, KO_CONTENT_W) == 2);
    CHECK(ko_text_lines("", 24, KO_CONTENT_W) == 1);
    CHECK(ko_text_lines("   ", 24, KO_CONTENT_W) == 1);
    // 多余空格不产生空行。
    CHECK(ko_text_lines("가   나", 24, KO_CONTENT_W) == 1);

    CHECK(ko_text_longest_token_px("안녕히 가세요", 48) == 144);
    CHECK(ko_text_longest_token_px("", 48) == 0);
}

static void test_font_choice(void) {
    CHECK(ko_word_font_px("하나") == 48);
    CHECK(ko_word_font_px("사랑하다") == 48);          // 4 个音节，48 号刚好一行
    CHECK(ko_word_font_px("안녕히 가세요") == 48);      // 两个词各 3 个音节，两行
    // 5 个音节的词在 48 号放不进一行（会被从中间劈开），必须降到 32 号。
    CHECK(ko_word_font_px("안녕하세요") == 32);
    CHECK(ko_word_font_px("죄송합니다") == 32);
    // 再长的词退到 24。
    CHECK(ko_word_font_px("가나다라마바사아자") == 24);
    CHECK(ko_phrase_font_px("얼마예요?") == 32);
    CHECK(ko_phrase_font_px("가나다라마바사아자차카타파하 가나다라마바사아자차카타파하 "
                            "가나다라마바사아자차카타파하 가나다라마바사아자차카타파하") == 24);
}

// 用真实内容检查：词汇卡片主体最多两行、短语最多三行，并且没有词被劈开。
static void test_real_content_budget(void) {
    for (int i = 0; i < KO_WORD_COUNT; i++) {
        const char *ko = ko_words[i].ko;
        const int font = ko_word_font_px(ko);
        CHECK(font == 48 || font == 32 || font == 24);
        CHECK(ko_text_lines(ko, font, KO_CONTENT_W) <= 2);
        CHECK(ko_text_longest_token_px(ko, font) <= KO_CONTENT_W);
    }
    int phrase_lines_max = 0;
    for (int i = 0; i < KO_PHRASE_COUNT; i++) {
        const char *ko = ko_phrases[i].ko;
        const int font = ko_phrase_font_px(ko);
        CHECK(font == 32);   // 当前内容里所有短语都能以 32 号显示
        const int lines = ko_text_lines(ko, font, KO_CONTENT_W);
        CHECK(lines <= 3);
        CHECK(ko_text_longest_token_px(ko, font) <= KO_CONTENT_W);
        if (lines > phrase_lines_max) phrase_lines_max = lines;
    }
    CHECK(phrase_lines_max >= 1);

    // 字母卡片的例词以 24 号显示，必须一行放得下。
    for (int i = 0; i < KO_LETTER_COUNT; i++) {
        CHECK(ko_text_lines(ko_letters[i].ex, 24, KO_CONTENT_W) == 1);
        CHECK(ko_text_lines(ko_letters[i].name, 24, KO_CONTENT_W) == 1);
    }
}

static void test_list_first(void) {
    // 不足一屏：不滚动。
    CHECK(ko_list_first(3, 4, 5, 0) == 0);
    CHECK(ko_list_first(0, 5, 5, 0) == 0);
    // 选中项在窗口内：窗口不动。
    CHECK(ko_list_first(3, 20, 5, 2) == 2);
    // 向下越过窗口底：刚好把选中项露出来。
    CHECK(ko_list_first(7, 20, 5, 2) == 3);
    // 向上越过窗口顶：窗口顶对齐选中项。
    CHECK(ko_list_first(1, 20, 5, 2) == 1);
    // 到末尾：窗口最多滚到最后一屏。
    CHECK(ko_list_first(19, 20, 5, 0) == 15);
    // 循环到开头（从最后一项回到 0）。
    CHECK(ko_list_first(0, 20, 5, 15) == 0);
    // 越界的 prev_first 被夹住。
    CHECK(ko_list_first(2, 20, 5, 60) <= 15);
    CHECK(ko_list_first(2, 20, 0, 0) == 0);
}

int main(void) {
    test_utf8_and_width();
    test_lines();
    test_font_choice();
    test_real_content_budget();
    test_list_first();
    puts("test_ko_layout: PASS");
    return 0;
}
