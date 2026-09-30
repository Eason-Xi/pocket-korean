// main/ko_input.c —— 按键事件 → 逻辑按键。
#include "ko_input.h"

#include <string.h>

void ko_input_init(ko_input_t *input) {
    memset(input, 0, sizeof(*input));
}

ko_key_t ko_input_map(ko_input_t *input, ko_btn_t btn, ko_ev_t ev, uint32_t now_ms,
                      bool screen_awake, bool *wake) {
    if (wake) *wake = false;

    if (input->swallowing) {
        // 无符号减法：毫秒计数回绕后仍然正确。
        const bool expired = (uint32_t)(now_ms - input->swallow_since_ms) >= KO_WAKE_SWALLOW_MS;
        if (expired || (btn == input->swallow_btn && ev == KO_EV_PRESS)) {
            // 窗口过期，或同一个键又被按下 = 新的一次按压，不再吞。
            input->swallowing = false;
        } else if (btn == input->swallow_btn) {
            return KO_KEY_NONE;   // 唤醒那次按压的 CLICK / DOUBLE / LONG
        }
    }

    if (!screen_awake) {
        if (wake) *wake = true;
        input->swallowing = true;
        input->swallow_btn = btn;
        input->swallow_since_ms = now_ms;
        return KO_KEY_NONE;
    }

    switch (btn) {
    case KO_BTN_UP:
        return ev == KO_EV_PRESS ? KO_KEY_UP : KO_KEY_NONE;
    case KO_BTN_DOWN:
        return ev == KO_EV_PRESS ? KO_KEY_DOWN : KO_KEY_NONE;
    case KO_BTN_OK:
        if (ev == KO_EV_CLICK || ev == KO_EV_DOUBLE) return KO_KEY_OK;
        if (ev == KO_EV_LONG) return KO_KEY_BACK;
        return KO_KEY_NONE;
    }
    return KO_KEY_NONE;
}
