// main/ko_app.c —— 韩语学习应用的控制器：按键 → 页面状态 → 界面 / 音频 / 存储。
#include "ko_app.h"

#include <stdio.h>
#include <string.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "ko_audio.h"
#include "ko_content.h"
#include "ko_input.h"
#include "ko_layout.h"
#include "ko_nav.h"
#include "ko_power.h"
#include "ko_progress.h"
#include "ko_quiz.h"
#include "ko_rng.h"
#include "ko_session.h"
#include "ko_store.h"
#include "ko_ui.h"
#include "ko_view.h"

static const char *TAG = "ko_app";

// ko_layout.h 里的屏幕尺寸是纯逻辑层自己的一份；引脚 / 面板参数的唯一来源是 bsp_pins.h，
// 这里在编译期核对二者一致，改了面板尺寸而忘了同步布局会直接编译失败。
_Static_assert(KO_SCREEN_W == BSP_LCD_W && KO_SCREEN_H == BSP_LCD_H,
               "ko_layout.h screen size must match bsp_pins.h");

// 按键回调里直接把 BSP 枚举强转成 ko_input 的枚举：数值必须一一对应，否则按键会错位。
_Static_assert((int)BSP_BTN_UP == (int)KO_BTN_UP && (int)BSP_BTN_DOWN == (int)KO_BTN_DOWN &&
                   (int)BSP_BTN_OK == (int)KO_BTN_OK,
               "bsp_btn_t and ko_btn_t must agree");
_Static_assert((int)BSP_BTN_PRESS == (int)KO_EV_PRESS && (int)BSP_BTN_CLICK == (int)KO_EV_CLICK &&
                   (int)BSP_BTN_DOUBLE == (int)KO_EV_DOUBLE && (int)BSP_BTN_LONG == (int)KO_EV_LONG,
               "bsp_btn_ev_t and ko_ev_t must agree");

#define INPUT_QUEUE_DEPTH 8
#define INPUT_TASK_STACK 4096
#define INPUT_TASK_PRIORITY 5
#define STATUS_TASK_STACK 3072
#define STATUS_TASK_PRIORITY 2
#define STATUS_PERIOD_MS 250
#define BATTERY_PERIOD_MS 30000
#define LVGL_LOCK_MS 500
#define PAIR_GAP_MS 350   // 字母名与例词之间的停顿

static const uint8_t VOLUME_STEPS[] = { 0, 20, 40, 60, 80, 100 };
static const uint8_t BRIGHTNESS_STEPS[] = { 20, 40, 60, 80, 100 };
#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    ko_btn_t btn;
    ko_ev_t ev;
} key_msg_t;

static struct {
    ko_nav_t nav;
    ko_input_t input;
    ko_power_t power;
    ko_session_t session;
    ko_quiz_run_t quiz;
    uint32_t rng;
    bool card_back;       // 当前卡片是否已翻面
    bool reset_armed;     // 设置页"清除进度"已进入二次确认
    bool reset_done;
    bool audio_ok;
    bool battery_ok;
} A;

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_power_lock;   // 输入任务与后台任务都会改 A.power
static volatile bool s_input_ready;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---------------------------------------------------------------------------
// 屏幕亮度
// ---------------------------------------------------------------------------

static void apply_backlight(void) {
    const uint8_t percent =
        ko_power_backlight_percent(A.power.level, ko_store_settings().brightness);
    bsp_display_backlight(percent);
}

// 有操作时调用：回到全亮，必要时恢复背光。
static void note_activity(void) {
    xSemaphoreTake(s_power_lock, portMAX_DELAY);
    const bool restore = ko_power_activity(&A.power, now_ms());
    if (restore) apply_backlight();
    xSemaphoreGive(s_power_lock);
}

static bool screen_is_on(void) {
    xSemaphoreTake(s_power_lock, portMAX_DELAY);
    const bool on = A.power.level != KO_SCREEN_OFF;
    xSemaphoreGive(s_power_lock);
    return on;
}

// ---------------------------------------------------------------------------
// 进度读写（只有输入任务修改；改动放在互斥锁内，后台任务写盘时才能拿到一致的快照）
// ---------------------------------------------------------------------------

static ko_progress_t *progress(void) {
    return ko_store_progress();
}

static void rate_item(ko_kind_t kind, uint16_t index, bool known) {
    ko_store_lock();
    uint8_t *states = ko_progress_states(progress(), kind);
    states[index] = ko_state_rate(states[index], known);
    ko_store_unlock();
    ko_store_mark_dirty(now_ms());
}

static void mark_seen(ko_kind_t kind, uint16_t index) {
    ko_store_lock();
    uint8_t *states = ko_progress_states(progress(), kind);
    const bool fresh = !ko_state_seen(states[index]);
    if (fresh) states[index] = ko_state_mark_seen(states[index]);
    ko_store_unlock();
    if (fresh) ko_store_mark_dirty(now_ms());
}

// ---------------------------------------------------------------------------
// 界面刷新
// ---------------------------------------------------------------------------

// 从真实的存储 / 音频状态填出视图需要的环境。
static ko_view_env_t make_env(void) {
    const ko_settings_t settings = ko_store_settings();
    const ko_audio_pack_state_t pack = ko_audio_pack_state();
    const ko_view_env_t env = {
        .progress = progress(),
        .volume = settings.volume,
        .brightness = settings.brightness,
        .pack = pack == KO_AUDIO_PACK_READY    ? KO_VIEW_PACK_READY
                : pack == KO_AUDIO_PACK_BROKEN ? KO_VIEW_PACK_BROKEN
                                               : KO_VIEW_PACK_ABSENT,
        .pack_slots = ko_audio_pack_slots(),
        .storage_ok = ko_store_ok(),
        .reset_armed = A.reset_armed,
        .reset_done = A.reset_done,
        .has_clip = ko_audio_has_clip,
    };
    return env;
}

static void log_lvgl_memory(const char *what) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "%s：LVGL 池已用 %u（历史峰值 %u）/ %u 字节，最大空闲块 %u；系统堆空闲 %u（最大块 %u）",
             what, (unsigned)(mon.total_size - mon.free_size), (unsigned)mon.max_used,
             (unsigned)mon.total_size, (unsigned)mon.free_biggest_size, (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

// rebuild = true：切换到新页面，重建内容区；false：只刷新当前页面。
static void render(bool rebuild) {
    const ko_route_t *route = ko_nav_top(&A.nav);
    const ko_view_env_t env = make_env();

    if (ko_view_is_list(route->id)) {
        ko_view_ctx_t ctx;
        const ko_list_view_t v = ko_view_list(&ctx, &env, route);
        if (rebuild) ko_ui_show_list(&v); else ko_ui_update_list(&v);
    } else {
        switch (route->id) {
        case KO_SCR_ALPHA_DETAIL: {
            const ko_alpha_view_t v = ko_view_alpha(&env, route);
            if (rebuild) ko_ui_show_alpha(&v); else ko_ui_update_alpha(&v);
            break;
        }
        case KO_SCR_CARD: {
            const ko_card_view_t v = ko_view_card(&env, route, &A.session, A.card_back);
            if (rebuild) ko_ui_show_card(&v); else ko_ui_update_card(&v);
            break;
        }
        case KO_SCR_SESSION_DONE: {
            const ko_done_view_t v = ko_view_done(&env, route, &A.session);
            ko_ui_show_done(&v);
            break;
        }
        case KO_SCR_QUIZ: {
            const ko_quiz_view_t v = { .run = &A.quiz };
            if (rebuild) ko_ui_show_quiz(&v); else ko_ui_update_quiz(&v);
            break;
        }
        case KO_SCR_QUIZ_RESULT: {
            const ko_quiz_result_view_t v = ko_view_quiz_result(&env, &A.quiz);
            ko_ui_show_quiz_result(&v);
            break;
        }
        default:
            break;
        }
    }
    if (rebuild) log_lvgl_memory("page switch");
}

// 持锁刷新界面；拿不到锁只记一条日志，状态已更新，下一次事件会把界面带回一致。
static void render_locked(bool rebuild) {
    if (!bsp_lvgl_lock(LVGL_LOCK_MS)) {
        ESP_LOGW(TAG, "LVGL 锁超时，本次界面刷新被跳过");
        return;
    }
    render(rebuild);
    bsp_lvgl_unlock();
}

// ---------------------------------------------------------------------------
// 页面动作
// ---------------------------------------------------------------------------

static void begin_session(const ko_route_t *route) {
    const ko_deck_t *deck = ko_view_card_deck(route);
    const uint8_t *states = ko_progress_states_const(progress(), ko_view_card_kind(route));
    ko_session_begin(&A.session, states, deck->first, deck->count, progress()->sessions);
    ko_store_lock();
    progress()->sessions++;
    ko_store_unlock();
    ko_store_mark_dirty(now_ms());
    A.card_back = false;
}

static void begin_quiz(ko_quiz_mode_t mode) {
    A.rng = (uint32_t)esp_timer_get_time() | 1u;   // 每次开始取不同的种子
    ko_quiz_run_begin(&A.quiz, mode, progress(), &A.rng);
}

static void play_quiz_target(void) {
    const ko_question_t *q = ko_quiz_run_question(&A.quiz);
    if (q && A.quiz.mode == KO_QUIZ_LISTEN) ko_audio_play_clip(ko_words[q->target].clip);
}

static void move_cursor(int direction) {
    ko_route_t *route = ko_nav_top_mut(&A.nav);
    const ko_view_env_t env = make_env();
    const uint16_t count = ko_view_list_count(route->id);
    uint16_t next = route->cursor;
    for (uint16_t i = 0; i < count; i++) {
        next = direction > 0 ? (uint16_t)((next + 1) % count) : (uint16_t)((next + count - 1) % count);
        if (ko_view_row_enabled(&env, route->id, next)) break;
    }
    route->cursor = next;
    route->arg1 = ko_list_first(next, count, KO_LIST_VISIBLE, route->arg1);
    A.reset_armed = false;
    A.reset_done = false;
}

static uint8_t next_step(const uint8_t *steps, size_t count, uint8_t current) {
    for (size_t i = 0; i < count; i++) {
        if (steps[i] > current) return steps[i];
    }
    return steps[0];   // 走到头回到最小值
}

static void activate_settings_row(void) {
    const ko_route_t *route = ko_nav_top(&A.nav);
    ko_settings_t settings = ko_store_settings();
    switch (route->cursor) {
    case KO_SET_VOLUME:
        settings.volume = next_step(VOLUME_STEPS, COUNT_OF(VOLUME_STEPS), settings.volume);
        ko_audio_set_volume(settings.volume);
        ko_store_set_settings(settings, now_ms());
        ko_audio_play_tone(KO_TONE_KIND_TICK);   // 调音量后立刻能听到新音量
        break;
    case KO_SET_BRIGHTNESS:
        settings.brightness = next_step(BRIGHTNESS_STEPS, COUNT_OF(BRIGHTNESS_STEPS), settings.brightness);
        ko_store_set_settings(settings, now_ms());
        xSemaphoreTake(s_power_lock, portMAX_DELAY);
        apply_backlight();
        xSemaphoreGive(s_power_lock);
        break;
    case KO_SET_RESET:
        if (!A.reset_armed) {
            A.reset_armed = true;
        } else {
            ko_store_reset_progress(now_ms());
            A.reset_armed = false;
            A.reset_done = true;
        }
        break;
    default:
        break;
    }
}

static void open_list_item(ko_route_t *route) {
    switch (route->id) {
    case KO_SCR_HOME:
        switch (route->cursor) {
        case KO_HOME_ALPHA:    ko_nav_push(&A.nav, KO_SCR_ALPHA_GROUPS, 0, 0); break;
        case KO_HOME_VOCAB:    ko_nav_push(&A.nav, KO_SCR_TOPICS, 0, 0); break;
        case KO_HOME_PHRASE:   ko_nav_push(&A.nav, KO_SCR_PHRASE_GROUPS, 0, 0); break;
        case KO_HOME_QUIZ:     ko_nav_push(&A.nav, KO_SCR_QUIZ_MENU, 0, 0); break;
        default:               ko_nav_push(&A.nav, KO_SCR_SETTINGS, 0, 0); break;
        }
        render_locked(true);
        break;
    case KO_SCR_ALPHA_GROUPS:
        ko_nav_push(&A.nav, KO_SCR_ALPHA_DETAIL, route->cursor, 0);
        mark_seen(KO_KIND_LETTER, ko_letter_groups[route->cursor].first);
        render_locked(true);
        break;
    case KO_SCR_TOPICS:
    case KO_SCR_PHRASE_GROUPS: {
        const ko_kind_t kind = route->id == KO_SCR_TOPICS ? KO_KIND_WORD : KO_KIND_PHRASE;
        ko_nav_push(&A.nav, KO_SCR_CARD, kind, route->cursor);
        begin_session(ko_nav_top(&A.nav));
        render_locked(true);
        break;
    }
    case KO_SCR_QUIZ_MENU:
        begin_quiz((ko_quiz_mode_t)route->cursor);
        if (A.quiz.count == 0) break;
        ko_nav_push(&A.nav, KO_SCR_QUIZ, route->cursor, 0);
        render_locked(true);
        play_quiz_target();
        break;
    default:   // 设置页
        activate_settings_row();
        render_locked(false);
        break;
    }
}

static void handle_alpha_key(ko_key_t key, ko_route_t *route) {
    const ko_letter_group_t *group = &ko_letter_groups[route->arg0];
    if (key == KO_KEY_OK) {
        const ko_letter_t *letter = &ko_letters[group->first + route->cursor];
        ko_audio_play_pair(letter->name_clip, letter->ex_clip, PAIR_GAP_MS);
        return;
    }
    // 上 / 下：在本组里前后翻，首尾循环。
    route->cursor = key == KO_KEY_DOWN ? (uint16_t)((route->cursor + 1) % group->count)
                                       : (uint16_t)((route->cursor + group->count - 1) % group->count);
    mark_seen(KO_KIND_LETTER, (uint16_t)(group->first + route->cursor));
    ko_audio_stop();
    render_locked(false);
}

static void finish_card(ko_route_t *route, bool known) {
    const uint16_t item = ko_session_current(&A.session);
    rate_item(ko_view_card_kind(route), item, known);
    ko_session_rate(&A.session, known);
    A.card_back = false;
    if (!ko_session_active(&A.session)) {
        ko_nav_replace(&A.nav, KO_SCR_SESSION_DONE, route->arg0, route->arg1);
        render_locked(true);
    } else {
        render_locked(false);
    }
}

static void handle_card_key(ko_key_t key, ko_route_t *route) {
    const ko_item_t *item = ko_view_card_item(route, &A.session);
    if (!A.card_back) {
        if (key == KO_KEY_OK) {
            A.card_back = true;
            mark_seen(ko_view_card_kind(route), ko_session_current(&A.session));
            ko_audio_play_clip(item->clip);   // 翻面时读一遍
            render_locked(false);
        } else if (key == KO_KEY_UP) {
            ko_audio_play_clip(item->clip);
        }
        return;
    }
    if (key == KO_KEY_OK) ko_audio_play_clip(item->clip);
    else if (key == KO_KEY_UP) finish_card(route, false);
    else if (key == KO_KEY_DOWN) finish_card(route, true);
}

static void handle_quiz_key(ko_key_t key, ko_route_t *route) {
    if (key == KO_KEY_UP || key == KO_KEY_DOWN) {
        if (ko_quiz_run_move(&A.quiz, key == KO_KEY_DOWN ? 1 : -1) == KO_QUIZ_EVENT_MOVED) {
            render_locked(false);
        }
        return;
    }
    // OK
    switch (ko_quiz_run_confirm(&A.quiz)) {
    case KO_QUIZ_EVENT_REPLAY:
        play_quiz_target();
        break;
    case KO_QUIZ_EVENT_ANSWERED: {
        const bool correct = ko_quiz_run_last_correct(&A.quiz);
        const ko_question_t *q = &A.quiz.question[A.quiz.index];
        ko_store_lock();
        progress()->quiz_answered_total++;
        if (correct) progress()->quiz_correct_total++;
        ko_store_unlock();
        rate_item(ko_quiz_kind(A.quiz.mode), q->target, correct);
        ko_audio_play_tone(correct ? KO_TONE_KIND_RIGHT : KO_TONE_KIND_WRONG);
        render_locked(false);
        break;
    }
    case KO_QUIZ_EVENT_NEXT:
        render_locked(false);
        play_quiz_target();
        break;
    case KO_QUIZ_EVENT_FINISHED:
        ko_nav_replace(&A.nav, KO_SCR_QUIZ_RESULT, route->arg0, 0);
        ko_store_flush();
        render_locked(true);
        break;
    default:
        break;
    }
}

static void go_back(void) {
    ko_audio_stop();
    A.reset_armed = false;
    A.reset_done = false;
    if (!ko_nav_pop(&A.nav)) return;
    ko_store_flush();   // 离开学习 / 测验页面：把这一轮的进度落盘
    render_locked(true);
}

static void handle_key(ko_key_t key) {
    ko_route_t *route = ko_nav_top_mut(&A.nav);

    if (key == KO_KEY_BACK) {
        go_back();
        return;
    }
    if (ko_view_is_list(route->id)) {
        if (key == KO_KEY_OK) {
            open_list_item(route);
        } else {
            move_cursor(key == KO_KEY_DOWN ? 1 : -1);
            render_locked(false);
        }
        return;
    }
    switch (route->id) {
    case KO_SCR_ALPHA_DETAIL:
        handle_alpha_key(key, route);
        break;
    case KO_SCR_CARD:
        handle_card_key(key, route);
        break;
    case KO_SCR_SESSION_DONE:
        if (key == KO_KEY_OK) {
            // 再来一轮：同一主题，回到卡片页。
            ko_nav_replace(&A.nav, KO_SCR_CARD, route->arg0, route->arg1);
            begin_session(ko_nav_top(&A.nav));
            render_locked(true);
        }
        break;
    case KO_SCR_QUIZ:
        handle_quiz_key(key, route);
        break;
    case KO_SCR_QUIZ_RESULT:
        if (key == KO_KEY_OK) {
            const ko_quiz_mode_t mode = (ko_quiz_mode_t)route->arg0;
            begin_quiz(mode);
            ko_nav_replace(&A.nav, KO_SCR_QUIZ, mode, 0);
            render_locked(true);
            play_quiz_target();
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// 任务
// ---------------------------------------------------------------------------

static void on_button(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    // 运行在 button 组件的 esp_timer 任务里：只入队，绝不阻塞、不碰 LVGL。
    if (!s_input_ready || !s_queue) return;
    const key_msg_t msg = { .btn = (ko_btn_t)btn, .ev = (ko_ev_t)ev };
    (void)xQueueSend(s_queue, &msg, 0);
}

static void input_task(void *arg) {
    (void)arg;
    key_msg_t msg;
    for (;;) {
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) != pdTRUE) continue;

        bool wake = false;
        const ko_key_t key = ko_input_map(&A.input, msg.btn, msg.ev, now_ms(), screen_is_on(), &wake);
        note_activity();   // 任何按键（含只用来唤醒屏幕的）都算操作
        if (key != KO_KEY_NONE) handle_key(key);
    }
}

static void status_task(void *arg) {
    (void)arg;
    bool playing = false;
    uint32_t last_battery_ms = 0;
    bool first = true;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(STATUS_PERIOD_MS));
        const uint32_t now = now_ms();

        // 屏幕休眠。
        bool changed = false;
        xSemaphoreTake(s_power_lock, portMAX_DELAY);
        ko_power_tick(&A.power, now, &changed);
        if (changed) apply_backlight();
        xSemaphoreGive(s_power_lock);

        // 电量：I2C 读取不放进 LVGL 任务，也不在持锁时做。
        int soc = -2;   // -2 = 这一轮不更新
        if (first || (uint32_t)(now - last_battery_ms) >= BATTERY_PERIOD_MS) {
            soc = A.battery_ok ? bsp_battery_soc() : -1;
            last_battery_ms = now;
            first = false;
        }

        // 界面上的小改动（喇叭图标、电量）合并成一次加锁。
        const bool now_playing = ko_audio_is_playing();
        if ((now_playing != playing || soc != -2) && screen_is_on()) {
            if (bsp_lvgl_lock(100)) {
                if (now_playing != playing) ko_ui_set_playing(now_playing);
                if (soc != -2) ko_ui_set_battery(soc);
                bsp_lvgl_unlock();
                playing = now_playing;
            }
        }

        // 存档写入避开播放期间（Flash 写会让 I2S 断粮）。
        ko_store_tick(now, now_playing);
    }
}

esp_err_t ko_app_start(bool audio_ok, bool battery_ok) {
    memset(&A, 0, sizeof(A));
    A.audio_ok = audio_ok;
    A.battery_ok = battery_ok;

    // 存储不可用不阻塞：进度只在本次运行有效，设置页会如实显示。
    (void)ko_store_init();
    const ko_settings_t settings = ko_store_settings();

    if (audio_ok) {
        if (ko_audio_init() == ESP_OK) ko_audio_set_volume(settings.volume);
        else ESP_LOGW(TAG, "音频任务创建失败，应用将无声运行");
    }

    s_power_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(key_msg_t));
    if (!s_power_lock || !s_queue) return ESP_ERR_NO_MEM;

    ko_nav_init(&A.nav);
    ko_input_init(&A.input);
    ko_power_init(&A.power, now_ms());

    if (!bsp_lvgl_lock(1000)) return ESP_ERR_TIMEOUT;
    ko_ui_init();
    ko_ui_set_battery(-1);
    render(true);
    bsp_lvgl_unlock();
    bsp_display_backlight(ko_power_backlight_percent(KO_SCREEN_FULL, settings.brightness));

    if (xTaskCreate(input_task, "ko_input", INPUT_TASK_STACK, NULL, INPUT_TASK_PRIORITY, NULL) != pdPASS ||
        xTaskCreate(status_task, "ko_status", STATUS_TASK_STACK, NULL, STATUS_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    // 按键最后初始化：此时界面、队列、任务都已就绪。
    const esp_err_t button_err = bsp_button_init(on_button, NULL);
    if (button_err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s（界面可显示，但无法操作）", esp_err_to_name(button_err));
        return button_err;
    }
    s_input_ready = true;
    ESP_LOGI(TAG, "就绪：音频=%d 电量=%d 存储=%d 发音包=%d", A.audio_ok, A.battery_ok, ko_store_ok(),
             (int)ko_audio_pack_state());
    return ESP_OK;
}
