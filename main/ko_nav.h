// main/ko_nav.h —— 页面栈：进入子页面压栈，长按 OK 出栈返回，并记住每一级的光标位置。
// 纯逻辑，不依赖 ESP-IDF / LVGL。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    KO_SCR_HOME = 0,
    KO_SCR_ALPHA_GROUPS,    // 字母分组列表
    KO_SCR_ALPHA_DETAIL,    // 单个字母卡片   arg0 = 字母组序号
    KO_SCR_TOPICS,          // 词汇主题列表
    KO_SCR_PHRASE_GROUPS,   // 短语场景列表
    KO_SCR_CARD,            // 词汇 / 短语卡片  arg0 = 类别(ko_kind_t) arg1 = 主题序号
    KO_SCR_SESSION_DONE,    // 一轮学习完成
    KO_SCR_QUIZ_MENU,       // 测验模式列表
    KO_SCR_QUIZ,            // 测验答题        arg0 = 模式
    KO_SCR_QUIZ_RESULT,     // 测验结果
    KO_SCR_SETTINGS,
} ko_screen_id_t;

// 栈里每一级：页面 + 该页的两个参数 + 离开它时的光标位置（回来时恢复）。
typedef struct {
    ko_screen_id_t id;
    uint16_t arg0;
    uint16_t arg1;
    uint16_t cursor;
} ko_route_t;

#define KO_NAV_DEPTH 6

typedef struct {
    ko_route_t stack[KO_NAV_DEPTH];
    uint8_t depth;   // 至少为 1（首页）
} ko_nav_t;

void ko_nav_init(ko_nav_t *nav);
const ko_route_t *ko_nav_top(const ko_nav_t *nav);
ko_route_t *ko_nav_top_mut(ko_nav_t *nav);

// 压栈；栈满返回 false（此时不改变栈）。
bool ko_nav_push(ko_nav_t *nav, ko_screen_id_t id, uint16_t arg0, uint16_t arg1);
// 出栈；已在首页返回 false。
bool ko_nav_pop(ko_nav_t *nav);
// 替换栈顶（例如卡片 → 本轮完成，返回时应直接回到主题列表）。
void ko_nav_replace(ko_nav_t *nav, ko_screen_id_t id, uint16_t arg0, uint16_t arg1);
