// main/ko_session.c —— 一轮卡片学习的队列。
#include "ko_session.h"

#include <string.h>

#include "ko_progress.h"

// 排序键：盒子低的优先，其次没看过的，最后按"轮转后的位置"。
// 三段拼进一个 uint32：轮转位置在同一 (盒子, 是否看过) 内唯一，所以键整体唯一，
// 选择时才能用"比上一个大的最小键"逐个取出，而不需要额外的标记数组。
static uint32_t candidate_key(uint8_t state, uint16_t index, uint16_t count, uint32_t rotate) {
    const uint16_t offset = (uint16_t)(rotate % count);
    const uint16_t rotated = (uint16_t)((index + count - offset) % count);
    return ((uint32_t)ko_state_box(state) << 24) |
           ((uint32_t)(ko_state_seen(state) ? 1u : 0u) << 16) | rotated;
}

void ko_session_begin(ko_session_t *session, const uint8_t *states, uint16_t first,
                      uint16_t count, uint32_t rotate) {
    memset(session, 0, sizeof(*session));
    if (count == 0) return;

    const uint16_t wanted = count < KO_SESSION_MAX ? count : KO_SESSION_MAX;
    bool have_previous = false;
    uint32_t previous = 0;

    for (uint16_t n = 0; n < wanted; n++) {
        uint32_t best_key = UINT32_MAX;
        uint16_t best_index = 0;
        for (uint16_t i = 0; i < count; i++) {
            const uint32_t key = candidate_key(states[first + i], i, count, rotate);
            if (have_previous && key <= previous) continue;
            if (key < best_key) {
                best_key = key;
                best_index = i;
            }
        }
        session->item[n] = (uint16_t)(first + best_index);
        previous = best_key;
        have_previous = true;
    }
    session->len = (uint8_t)wanted;
    session->total = (uint8_t)wanted;
}

bool ko_session_active(const ko_session_t *session) {
    return session->len > 0;
}

uint16_t ko_session_current(const ko_session_t *session) {
    return session->len > 0 ? session->item[0] : 0;
}

uint8_t ko_session_done(const ko_session_t *session) {
    return (uint8_t)(session->total - session->len);
}

bool ko_session_rate(ko_session_t *session, bool known) {
    if (session->len == 0) return false;

    const uint16_t item = session->item[0];
    uint8_t again = session->again[0];

    // 取出队首。
    for (uint8_t i = 1; i < session->len; i++) {
        session->item[i - 1] = session->item[i];
        session->again[i - 1] = session->again[i];
    }
    session->len--;

    if (known) {
        session->known++;
        return true;
    }
    again++;
    if (again >= KO_REPEAT_LIMIT) {
        session->unknown++;
        return true;
    }

    // 隔几张后再出现；队列不够长就排到队尾。
    const uint8_t pos = session->len < KO_REINSERT_GAP ? session->len : KO_REINSERT_GAP;
    for (uint8_t i = session->len; i > pos; i--) {
        session->item[i] = session->item[i - 1];
        session->again[i] = session->again[i - 1];
    }
    session->item[pos] = item;
    session->again[pos] = again;
    session->len++;
    return false;
}
