// main/ko_view.c —— 应用状态 → 界面视图。
#include "ko_view.h"

#include <stdio.h>

#include "ko_strings.h"
#include "ko_theme.h"

bool ko_view_is_list(ko_screen_id_t id) {
    return id == KO_SCR_HOME || id == KO_SCR_ALPHA_GROUPS || id == KO_SCR_TOPICS ||
           id == KO_SCR_PHRASE_GROUPS || id == KO_SCR_QUIZ_MENU || id == KO_SCR_SETTINGS;
}

uint16_t ko_view_list_count(ko_screen_id_t id) {
    switch (id) {
    case KO_SCR_HOME:          return KO_HOME_COUNT;
    case KO_SCR_ALPHA_GROUPS:  return KO_LETTER_GROUP_COUNT;
    case KO_SCR_TOPICS:        return KO_TOPIC_COUNT;
    case KO_SCR_PHRASE_GROUPS: return KO_PHRASE_GROUP_COUNT;
    case KO_SCR_QUIZ_MENU:     return KO_QUIZ_MODE_COUNT;
    case KO_SCR_SETTINGS:      return KO_SET_COUNT;
    default:                   return 0;
    }
}

bool ko_view_row_enabled(const ko_view_env_t *env, ko_screen_id_t id, uint16_t index) {
    switch (id) {
    case KO_SCR_QUIZ_MENU: return index != KO_QUIZ_LISTEN || env->pack == KO_VIEW_PACK_READY;
    case KO_SCR_SETTINGS:  return index <= KO_SET_RESET;   // 后两行只是状态显示
    default:               return true;
    }
}

// ---------------------------------------------------------------------------
// 列表回调
// ---------------------------------------------------------------------------

static bool cb_enabled(uint16_t index, void *raw) {
    const ko_view_ctx_t *c = raw;
    return ko_view_row_enabled(c->env, c->id, index);
}

static const char *cb_title(uint16_t index, void *raw) {
    const ko_view_ctx_t *c = raw;
    static const char *const HOME[KO_HOME_COUNT] = {
        KO_S_HOME_ALPHA, KO_S_HOME_VOCAB, KO_S_HOME_PHRASE, KO_S_HOME_QUIZ, KO_S_HOME_SETTINGS,
    };
    static const char *const QUIZ[KO_QUIZ_MODE_COUNT] = {
        KO_S_QZ_KO2ZH, KO_S_QZ_ZH2KO, KO_S_QZ_LETTER, KO_S_QZ_LISTEN,
    };
    static const char *const SETTINGS[KO_SET_COUNT] = {
        KO_S_SET_VOLUME, KO_S_SET_BRIGHTNESS, KO_S_SET_RESET, KO_S_SET_PACK, KO_S_SET_STORAGE,
    };
    switch (c->id) {
    case KO_SCR_HOME:          return HOME[index];
    case KO_SCR_ALPHA_GROUPS:  return ko_letter_groups[index].zh;
    case KO_SCR_TOPICS:        return ko_topics[index].zh;
    case KO_SCR_PHRASE_GROUPS: return ko_phrase_groups[index].zh;
    case KO_SCR_QUIZ_MENU:     return QUIZ[index];
    default:                   return SETTINGS[index];
    }
}

static const char *cb_emblem(uint16_t index, void *raw) {
    const ko_view_ctx_t *c = raw;
    static const char *const HOME[KO_HOME_COUNT] = {
        KO_K_EMBLEM_ALPHA, KO_K_EMBLEM_VOCAB, KO_K_EMBLEM_PHRASE, KO_K_EMBLEM_QUIZ,
        KO_K_EMBLEM_SETTINGS,
    };
    static char number[8];   // 只在本次界面刷新里使用（界面会拷贝文字）
    switch (c->id) {
    case KO_SCR_HOME:          return HOME[index];
    case KO_SCR_ALPHA_GROUPS:  return ko_letters[ko_letter_groups[index].first].ch;
    case KO_SCR_QUIZ_MENU:     return KO_K_EMBLEM_QUIZ;
    default:
        snprintf(number, sizeof(number), "%02u", (unsigned)index + 1);
        return number;
    }
}

static uint32_t cb_color(uint16_t index, void *raw) {
    const ko_view_ctx_t *c = raw;
    static const uint32_t HOME[KO_HOME_COUNT] = { KO_COL_RED, KO_COL_BLUE, KO_COL_TEAL, KO_COL_GOLD,
                                                  KO_COL_GRAY };
    switch (c->id) {
    case KO_SCR_HOME:          return HOME[index];
    case KO_SCR_ALPHA_GROUPS:  return KO_COL_RED;
    case KO_SCR_PHRASE_GROUPS: return KO_COL_TEAL;
    case KO_SCR_QUIZ_MENU:     return KO_COL_GOLD;
    default:                   return KO_COL_BLUE;
    }
}

static void sub_home(const ko_view_env_t *env, uint16_t index, char *buf, size_t cap) {
    const ko_progress_t *p = env->progress;
    switch (index) {
    case KO_HOME_ALPHA:
        snprintf(buf, cap, KO_S_SUB_ALPHA, ko_progress_seen(p->letters, 0, KO_LETTER_COUNT),
                 KO_LETTER_COUNT);
        break;
    case KO_HOME_VOCAB:
        snprintf(buf, cap, KO_S_SUB_MASTERED, ko_progress_mastered(p->words, 0, KO_WORD_COUNT),
                 KO_WORD_COUNT);
        break;
    case KO_HOME_PHRASE:
        snprintf(buf, cap, KO_S_SUB_MASTERED, ko_progress_mastered(p->phrases, 0, KO_PHRASE_COUNT),
                 KO_PHRASE_COUNT);
        break;
    case KO_HOME_QUIZ:
        snprintf(buf, cap, KO_S_SUB_QUIZ, (int)p->quiz_correct_total);
        break;
    default:
        snprintf(buf, cap, "%s", KO_S_SUB_SETTINGS);
        break;
    }
}

static void sub_settings(const ko_view_env_t *env, uint16_t index, char *buf, size_t cap) {
    switch (index) {
    case KO_SET_RESET:
        if (env->reset_armed) snprintf(buf, cap, "%s", KO_S_SET_RESET_CONFIRM);
        else if (env->reset_done) snprintf(buf, cap, "%s", KO_S_SET_RESET_DONE);
        break;
    case KO_SET_PACK:
        switch (env->pack) {
        case KO_VIEW_PACK_READY:  snprintf(buf, cap, KO_S_PACK_READY, (int)env->pack_slots); break;
        case KO_VIEW_PACK_BROKEN: snprintf(buf, cap, "%s", KO_S_PACK_BAD); break;
        default:                  snprintf(buf, cap, "%s", KO_S_PACK_NONE); break;
        }
        break;
    case KO_SET_STORAGE:
        snprintf(buf, cap, "%s", env->storage_ok ? KO_S_STORAGE_OK : KO_S_STORAGE_OFF);
        break;
    default:
        break;
    }
}

static void cb_sub(uint16_t index, char *buf, size_t cap, void *raw) {
    const ko_view_ctx_t *c = raw;
    const ko_progress_t *p = c->env->progress;
    buf[0] = '\0';
    switch (c->id) {
    case KO_SCR_HOME:
        sub_home(c->env, index, buf, cap);
        break;
    case KO_SCR_ALPHA_GROUPS:
        snprintf(buf, cap, KO_S_SUB_ALPHA,
                 ko_progress_seen(p->letters, ko_letter_groups[index].first, ko_letter_groups[index].count),
                 ko_letter_groups[index].count);
        break;
    case KO_SCR_TOPICS:
        snprintf(buf, cap, KO_S_SUB_MASTERED,
                 ko_progress_mastered(p->words, ko_topics[index].first, ko_topics[index].count),
                 ko_topics[index].count);
        break;
    case KO_SCR_PHRASE_GROUPS:
        snprintf(buf, cap, KO_S_SUB_MASTERED,
                 ko_progress_mastered(p->phrases, ko_phrase_groups[index].first,
                                      ko_phrase_groups[index].count),
                 ko_phrase_groups[index].count);
        break;
    case KO_SCR_QUIZ_MENU:
        if (!ko_view_row_enabled(c->env, c->id, index)) snprintf(buf, cap, "%s", KO_S_QZ_LISTEN_OFF);
        else snprintf(buf, cap, KO_S_QZ_ROUNDS, KO_QUIZ_ROUND);
        break;
    default:
        sub_settings(c->env, index, buf, cap);
        break;
    }
}

static void cb_value(uint16_t index, char *buf, size_t cap, void *raw) {
    const ko_view_ctx_t *c = raw;
    if (index == KO_SET_VOLUME) snprintf(buf, cap, "%d%%", c->env->volume);
    else if (index == KO_SET_BRIGHTNESS) snprintf(buf, cap, "%d%%", c->env->brightness);
}

ko_list_view_t ko_view_list(ko_view_ctx_t *ctx, const ko_view_env_t *env, const ko_route_t *route) {
    ctx->env = env;
    ctx->id = route->id;
    ctx->cursor = route->cursor;

    ko_list_view_t v = {
        .count = ko_view_list_count(route->id),
        .selected = route->cursor,
        .first = route->arg1,
        .row_title = cb_title,
        .row_emblem = cb_emblem,
        .row_color = cb_color,
        .row_sub = cb_sub,
        .row_enabled = cb_enabled,
        .hint_left = KO_S_SYM_UP " " KO_S_HINT_UP,
        .hint_center = KO_S_SYM_OK " " KO_S_HINT_OPEN,
        .hint_right = KO_S_SYM_DOWN " " KO_S_HINT_DOWN,
        .ctx = ctx,
    };
    switch (route->id) {
    case KO_SCR_HOME:          v.title = KO_S_APP_TITLE; break;
    case KO_SCR_ALPHA_GROUPS:  v.title = KO_S_HOME_ALPHA; break;
    case KO_SCR_TOPICS:        v.title = KO_S_HOME_VOCAB; break;
    case KO_SCR_PHRASE_GROUPS: v.title = KO_S_HOME_PHRASE; break;
    case KO_SCR_QUIZ_MENU:     v.title = KO_S_HOME_QUIZ; break;
    default:
        v.title = KO_S_HOME_SETTINGS;
        v.row_emblem = NULL;
        v.row_value = cb_value;
        // 设置页的中间键随所选行变化：调节数值、或清除进度（二次确认）。
        v.hint_center = route->cursor == KO_SET_RESET
                            ? (env->reset_armed ? KO_S_SYM_OK " " KO_S_HINT_CONFIRM
                                                : KO_S_SYM_OK " " KO_S_HINT_CLEAR)
                            : KO_S_SYM_OK " " KO_S_HINT_CHANGE;
        break;
    }
    return v;
}

// ---------------------------------------------------------------------------
// 其他页面
// ---------------------------------------------------------------------------

static bool clip_available(const ko_view_env_t *env, uint16_t clip) {
    return env->has_clip && env->has_clip(clip);
}

ko_alpha_view_t ko_view_alpha(const ko_view_env_t *env, const ko_route_t *route) {
    const uint8_t letter = (uint8_t)(ko_letter_groups[route->arg0].first + route->cursor);
    const ko_alpha_view_t v = {
        .letter = letter,
        .has_audio = clip_available(env, ko_letters[letter].name_clip) ||
                     clip_available(env, ko_letters[letter].ex_clip),
    };
    return v;
}

ko_kind_t ko_view_card_kind(const ko_route_t *route) {
    return (ko_kind_t)route->arg0;
}

const ko_deck_t *ko_view_card_deck(const ko_route_t *route) {
    return ko_view_card_kind(route) == KO_KIND_WORD ? &ko_topics[route->arg1]
                                                    : &ko_phrase_groups[route->arg1];
}

const ko_item_t *ko_view_card_item(const ko_route_t *route, const ko_session_t *session) {
    const uint16_t index = ko_session_current(session);
    return ko_view_card_kind(route) == KO_KIND_WORD ? &ko_words[index] : &ko_phrases[index];
}

ko_card_view_t ko_view_card(const ko_view_env_t *env, const ko_route_t *route,
                            const ko_session_t *session, bool back) {
    const ko_item_t *item = ko_view_card_item(route, session);
    const ko_card_view_t v = {
        .deck_title = ko_view_card_deck(route)->zh,
        .item = item,
        .is_phrase = ko_view_card_kind(route) == KO_KIND_PHRASE,
        .back = back,
        .done = ko_session_done(session),
        .total = session->total,
        .has_audio = clip_available(env, item->clip),
    };
    return v;
}

ko_done_view_t ko_view_done(const ko_view_env_t *env, const ko_route_t *route,
                            const ko_session_t *session) {
    const ko_deck_t *deck = ko_view_card_deck(route);
    const uint8_t *states = ko_progress_states_const(env->progress, ko_view_card_kind(route));
    const ko_done_view_t v = {
        .deck_title = deck->zh,
        .known = session->known,
        .unknown = session->unknown,
        .mastered = ko_progress_mastered(states, deck->first, deck->count),
        .deck_total = deck->count,
    };
    return v;
}

ko_quiz_result_view_t ko_view_quiz_result(const ko_view_env_t *env, const ko_quiz_run_t *quiz) {
    const ko_quiz_result_view_t v = {
        .score = quiz->score,
        .total = quiz->count,
        .correct_total = env->progress->quiz_correct_total,
    };
    return v;
}
