// main/ko_view.h —— 把应用状态翻译成界面视图（每行显示什么、页脚提示什么）。
//
// 不依赖 ESP-IDF：固件里的 ko_app.c、主机预览（tools/render_korean_preview.py）
// 和主机测试（tests/test_ko_view.c）用的是同一份代码，所以预览图和测试看到的就是
// 固件真正会显示的内容，不会出现"预览和固件悄悄不一致"。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ko_content.h"
#include "ko_nav.h"
#include "ko_progress.h"
#include "ko_quiz.h"
#include "ko_session.h"
#include "ko_ui.h"

typedef enum {
    KO_VIEW_PACK_ABSENT = 0,   // 没有安装发音包
    KO_VIEW_PACK_READY,
    KO_VIEW_PACK_BROKEN,
} ko_view_pack_t;

// 视图需要的外部状态。ko_app.c 从真实的存储 / 音频 / 电源填，预览与测试填样例数据。
typedef struct {
    const ko_progress_t *progress;
    uint8_t volume;
    uint8_t brightness;
    ko_view_pack_t pack;
    uint32_t pack_slots;
    bool storage_ok;
    bool reset_armed;                    // 设置页"清除进度"处于二次确认
    bool reset_done;
    bool (*has_clip)(uint16_t clip);     // NULL = 没有任何发音可播
} ko_view_env_t;

// 列表页各行的序号（首页 / 设置页）。
enum { KO_HOME_ALPHA = 0, KO_HOME_VOCAB, KO_HOME_PHRASE, KO_HOME_QUIZ, KO_HOME_SETTINGS, KO_HOME_COUNT };
enum { KO_SET_VOLUME = 0, KO_SET_BRIGHTNESS, KO_SET_RESET, KO_SET_PACK, KO_SET_STORAGE, KO_SET_COUNT };

// 列表回调的上下文：由调用方持有（放栈上即可，只在 show / update 调用期间使用）。
typedef struct {
    const ko_view_env_t *env;
    ko_screen_id_t id;
    uint16_t cursor;
} ko_view_ctx_t;

bool ko_view_is_list(ko_screen_id_t id);
uint16_t ko_view_list_count(ko_screen_id_t id);
// 该行能不能被选中（听音测验没有发音包时不可选；设置页后两行只是状态显示）。
bool ko_view_row_enabled(const ko_view_env_t *env, ko_screen_id_t id, uint16_t index);

// 组装列表视图。ctx 由调用方提供存储，并且在使用返回的视图期间必须保持有效。
ko_list_view_t ko_view_list(ko_view_ctx_t *ctx, const ko_view_env_t *env, const ko_route_t *route);

ko_alpha_view_t ko_view_alpha(const ko_view_env_t *env, const ko_route_t *route);

// 卡片路由的辅助：类别、所属主题 / 场景、当前卡片条目。
ko_kind_t ko_view_card_kind(const ko_route_t *route);
const ko_deck_t *ko_view_card_deck(const ko_route_t *route);
const ko_item_t *ko_view_card_item(const ko_route_t *route, const ko_session_t *session);

ko_card_view_t ko_view_card(const ko_view_env_t *env, const ko_route_t *route,
                            const ko_session_t *session, bool back);
ko_done_view_t ko_view_done(const ko_view_env_t *env, const ko_route_t *route,
                            const ko_session_t *session);
ko_quiz_result_view_t ko_view_quiz_result(const ko_view_env_t *env, const ko_quiz_run_t *quiz);
