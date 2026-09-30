// main/ko_ui.h —— 韩语学习应用的界面（LVGL）。
//
// 结构：一个常驻的"外壳"（页眉：标题 + 电量；页脚：三键提示）加一个内容区。
// 切换页面只重建内容区，外壳不动：LVGL 内置内存池只有 24 KB，
// 同一时刻只能有一页的对象，也就不会出现"新旧两页同时存在"的峰值。
//
// 本文件不依赖 ESP-IDF / BSP，只依赖 LVGL 和纯逻辑模块，因此可以在主机上编译，
// 用 tools/render_korean_preview.py 渲染成 PNG 检查版面。
// 线程：所有函数都必须在 LVGL 任务里调用，或持有 bsp_lvgl_lock()。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ko_content.h"
#include "ko_quiz.h"

// 创建外壳并加载屏幕。之后才能调用 show_* / update_*。
void ko_ui_init(void);
// 电量百分比；-1 表示读不到（显示 "--"，不画假数字）。
void ko_ui_set_battery(int percent);
// 当前页面上的发音图标是否处于"正在播放"状态。
void ko_ui_set_playing(bool playing);

// ---------------------------------------------------------------------------
// 通用列表页：首页、字母分组、词汇主题、短语场景、测验模式、设置都用它。
// ---------------------------------------------------------------------------
#define KO_LIST_VISIBLE 5

typedef struct {
    const char *title;
    uint16_t count;
    uint16_t selected;
    uint16_t first;   // 窗口第一行的序号，由调用方用 ko_list_first() 维护
    const char *(*row_title)(uint16_t index, void *ctx);
    const char *(*row_emblem)(uint16_t index, void *ctx);            // 可为 NULL：不画徽标
    uint32_t (*row_color)(uint16_t index, void *ctx);                // 可为 NULL：默认蓝色
    void (*row_sub)(uint16_t index, char *buf, size_t cap, void *ctx);     // 可为 NULL
    void (*row_value)(uint16_t index, char *buf, size_t cap, void *ctx);   // 可为 NULL
    bool (*row_enabled)(uint16_t index, void *ctx);                  // 可为 NULL：全部可用
    const char *hint_left;
    const char *hint_center;
    const char *hint_right;
    void *ctx;
} ko_list_view_t;

void ko_ui_show_list(const ko_list_view_t *view);
void ko_ui_update_list(const ko_list_view_t *view);   // 只刷新内容，不重建对象

// ---------------------------------------------------------------------------
// 字母卡片
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t letter;     // ko_letters 里的序号
    bool has_audio;
} ko_alpha_view_t;

void ko_ui_show_alpha(const ko_alpha_view_t *view);
void ko_ui_update_alpha(const ko_alpha_view_t *view);

// ---------------------------------------------------------------------------
// 词汇 / 短语卡片
// ---------------------------------------------------------------------------
typedef struct {
    const char *deck_title;   // 主题 / 场景名，显示在页眉
    const ko_item_t *item;
    bool is_phrase;
    bool back;                // false = 只看到韩文；true = 已翻面，显示中文
    uint8_t done;             // 本轮已完成张数
    uint8_t total;
    bool has_audio;
} ko_card_view_t;

void ko_ui_show_card(const ko_card_view_t *view);
void ko_ui_update_card(const ko_card_view_t *view);

typedef struct {
    const char *deck_title;
    uint8_t known;
    uint8_t unknown;
    uint16_t mastered;        // 该主题里已掌握的条目数
    uint16_t deck_total;
} ko_done_view_t;

void ko_ui_show_done(const ko_done_view_t *view);

// ---------------------------------------------------------------------------
// 测验
// ---------------------------------------------------------------------------
typedef struct {
    const ko_quiz_run_t *run;
} ko_quiz_view_t;

void ko_ui_show_quiz(const ko_quiz_view_t *view);
void ko_ui_update_quiz(const ko_quiz_view_t *view);

typedef struct {
    uint8_t score;
    uint8_t total;
    uint32_t correct_total;   // 累计答对
} ko_quiz_result_view_t;

void ko_ui_show_quiz_result(const ko_quiz_result_view_t *view);
