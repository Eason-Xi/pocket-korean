// tests/test_ko_input.c —— 按键映射、屏幕休眠策略、页面栈。
#include "ko_input.h"
#include "ko_nav.h"
#include "ko_power.h"
#include "ko_test.h"

static void test_awake_mapping(void) {
    ko_input_t in;
    ko_input_init(&in);
    bool wake = true;

    // 上 / 下键只认 PRESS：每次物理按下恰好一次，不受 BSP 单击 / 双击判定延迟影响。
    CHECK(ko_input_map(&in, KO_BTN_UP, KO_EV_PRESS, 0, true, &wake) == KO_KEY_UP && !wake);
    CHECK(ko_input_map(&in, KO_BTN_DOWN, KO_EV_PRESS, 0, true, &wake) == KO_KEY_DOWN);
    for (int ev = KO_EV_CLICK; ev <= KO_EV_LONG; ev++) {
        CHECK(ko_input_map(&in, KO_BTN_UP, (ko_ev_t)ev, 0, true, NULL) == KO_KEY_NONE);
        CHECK(ko_input_map(&in, KO_BTN_DOWN, (ko_ev_t)ev, 0, true, NULL) == KO_KEY_NONE);
    }

    // OK 键：单击 / 双击 = 确认，长按 = 返回，PRESS 不触发（要和长按区分）。
    CHECK(ko_input_map(&in, KO_BTN_OK, KO_EV_PRESS, 0, true, NULL) == KO_KEY_NONE);
    CHECK(ko_input_map(&in, KO_BTN_OK, KO_EV_CLICK, 0, true, NULL) == KO_KEY_OK);
    CHECK(ko_input_map(&in, KO_BTN_OK, KO_EV_DOUBLE, 0, true, NULL) == KO_KEY_OK);
    CHECK(ko_input_map(&in, KO_BTN_OK, KO_EV_LONG, 0, true, NULL) == KO_KEY_BACK);
}

static void test_wake_swallows_follow_up(void) {
    ko_input_t in;
    ko_input_init(&in);
    bool wake = false;

    // 灭屏：第一下 OK 只负责唤醒。
    CHECK(ko_input_map(&in, KO_BTN_OK, KO_EV_PRESS, 1000, false, &wake) == KO_KEY_NONE);
    CHECK(wake);
    // 唤醒后（screen_awake = true）同一次按压晚到的 CLICK / LONG 被吞掉。
    CHECK(ko_input_map(&in, KO_BTN_OK, KO_EV_CLICK, 1200, true, &wake) == KO_KEY_NONE && !wake);
    CHECK(ko_input_map(&in, KO_BTN_OK, KO_EV_LONG, 1500, true, NULL) == KO_KEY_NONE);
    // 另一个键不受影响。
    CHECK(ko_input_map(&in, KO_BTN_DOWN, KO_EV_PRESS, 1600, true, NULL) == KO_KEY_DOWN);
    // 同一个键再次按下 = 新的一次按压：结束吞键，后续 CLICK 正常。
    CHECK(ko_input_map(&in, KO_BTN_OK, KO_EV_PRESS, 1700, true, NULL) == KO_KEY_NONE);
    CHECK(ko_input_map(&in, KO_BTN_OK, KO_EV_CLICK, 1900, true, NULL) == KO_KEY_OK);

    // 窗口过期后不再吞。
    ko_input_init(&in);
    CHECK(ko_input_map(&in, KO_BTN_OK, KO_EV_PRESS, 0, false, &wake) == KO_KEY_NONE && wake);
    CHECK(ko_input_map(&in, KO_BTN_OK, KO_EV_CLICK, KO_WAKE_SWALLOW_MS - 1, true, NULL) == KO_KEY_NONE);
    ko_input_init(&in);
    CHECK(ko_input_map(&in, KO_BTN_OK, KO_EV_PRESS, 0, false, &wake) == KO_KEY_NONE);
    CHECK(ko_input_map(&in, KO_BTN_OK, KO_EV_CLICK, KO_WAKE_SWALLOW_MS, true, NULL) == KO_KEY_OK);

    // 毫秒计数回绕：窗口判断用无符号减法，回绕点附近仍然正确。
    ko_input_init(&in);
    const uint32_t near_wrap = 0xFFFFFF00u;
    CHECK(ko_input_map(&in, KO_BTN_UP, KO_EV_PRESS, near_wrap, false, &wake) == KO_KEY_NONE);
    CHECK(ko_input_map(&in, KO_BTN_UP, KO_EV_CLICK, near_wrap + 0x200u, true, NULL) == KO_KEY_NONE);
    CHECK(ko_input_map(&in, KO_BTN_OK, KO_EV_CLICK, near_wrap + 0x200u + KO_WAKE_SWALLOW_MS, true,
                       NULL) == KO_KEY_OK);

    // wake 参数可以为 NULL。
    ko_input_init(&in);
    CHECK(ko_input_map(&in, KO_BTN_DOWN, KO_EV_PRESS, 5, false, NULL) == KO_KEY_NONE);
}

static void test_power_policy(void) {
    ko_power_t p;
    bool changed;
    ko_power_init(&p, 1000);

    CHECK(ko_power_tick(&p, 1000 + KO_DIM_AFTER_MS - 1, &changed) == KO_SCREEN_FULL && !changed);
    CHECK(ko_power_tick(&p, 1000 + KO_DIM_AFTER_MS, &changed) == KO_SCREEN_DIM && changed);
    CHECK(ko_power_tick(&p, 1000 + KO_DIM_AFTER_MS + 5, &changed) == KO_SCREEN_DIM && !changed);
    CHECK(ko_power_tick(&p, 1000 + KO_OFF_AFTER_MS, &changed) == KO_SCREEN_OFF && changed);

    // 操作后立刻回到全亮，并告诉调用方需要恢复亮度。
    CHECK(ko_power_activity(&p, 1000 + KO_OFF_AFTER_MS + 10));
    CHECK(p.level == KO_SCREEN_FULL);
    CHECK(!ko_power_activity(&p, 1000 + KO_OFF_AFTER_MS + 20));   // 已经全亮，无需恢复
    CHECK(ko_power_tick(&p, 1000 + KO_OFF_AFTER_MS + 30, &changed) == KO_SCREEN_FULL && !changed);

    // 计数回绕。
    ko_power_init(&p, 0xFFFFFFF0u);
    CHECK(ko_power_tick(&p, 0xFFFFFFF0u + KO_DIM_AFTER_MS, &changed) == KO_SCREEN_DIM && changed);
    CHECK(ko_power_tick(&p, 5, &changed) == KO_SCREEN_FULL);   // 回绕后才过了 21 ms

    // 亮度：调暗不能比用户设定更亮；熄屏为 0；越界值夹到 100。
    CHECK(ko_power_backlight_percent(KO_SCREEN_FULL, 80) == 80);
    CHECK(ko_power_backlight_percent(KO_SCREEN_DIM, 80) == KO_DIM_PERCENT_MAX);
    CHECK(ko_power_backlight_percent(KO_SCREEN_DIM, 10) == 10);
    CHECK(ko_power_backlight_percent(KO_SCREEN_OFF, 80) == 0);
    CHECK(ko_power_backlight_percent(KO_SCREEN_FULL, 250) == 100);
}

static void test_nav_stack(void) {
    ko_nav_t nav;
    ko_nav_init(&nav);
    CHECK(nav.depth == 1 && ko_nav_top(&nav)->id == KO_SCR_HOME);
    CHECK(!ko_nav_pop(&nav));   // 首页不能再退

    // 进入子页面，离开时记住光标，返回后恢复。
    ko_nav_top_mut(&nav)->cursor = 3;
    CHECK(ko_nav_push(&nav, KO_SCR_TOPICS, 0, 0));
    ko_nav_top_mut(&nav)->cursor = 7;
    CHECK(ko_nav_push(&nav, KO_SCR_CARD, 1, 7));
    CHECK(ko_nav_top(&nav)->id == KO_SCR_CARD && ko_nav_top(&nav)->arg0 == 1 &&
          ko_nav_top(&nav)->arg1 == 7 && ko_nav_top(&nav)->cursor == 0);

    // 卡片 → 本轮完成：替换栈顶，返回时直接回到主题列表。
    ko_nav_replace(&nav, KO_SCR_SESSION_DONE, 0, 0);
    CHECK(nav.depth == 3);
    CHECK(ko_nav_pop(&nav));
    CHECK(ko_nav_top(&nav)->id == KO_SCR_TOPICS && ko_nav_top(&nav)->cursor == 7);
    CHECK(ko_nav_pop(&nav));
    CHECK(ko_nav_top(&nav)->id == KO_SCR_HOME && ko_nav_top(&nav)->cursor == 3);

    // 栈满时拒绝压栈且不破坏栈。
    for (int i = 1; i < KO_NAV_DEPTH; i++) CHECK(ko_nav_push(&nav, KO_SCR_TOPICS, 0, 0));
    CHECK(!ko_nav_push(&nav, KO_SCR_CARD, 0, 0));
    CHECK(nav.depth == KO_NAV_DEPTH && ko_nav_top(&nav)->id == KO_SCR_TOPICS);
}

int main(void) {
    test_awake_mapping();
    test_wake_swallows_follow_up();
    test_power_policy();
    test_nav_stack();
    puts("test_ko_input: PASS");
    return 0;
}
