// main/ko_input.h —— 把 BSP 按键事件映射成应用的"逻辑按键"。纯逻辑，可在主机上测试。
//
// 为什么不直接用 CLICK：BSP 的 button 组件在松开后要等 180 ms 才能确定是单击还是
// 双击；这 180 ms 内再按一次，两次快按会被合并成一个 DOUBLE，且不会产生任何 CLICK。
// 所以翻页用的 上 / 下 键改用 PRESS（每次物理按下恰好一次、零延迟），
// 确认键仍用 CLICK（要和长按区分），并把 DOUBLE 也当作一次确认，避免快按被吞掉。
//
// 灭屏时按键的第一下只负责点亮屏幕，不能同时触发页面动作；否则用户在黑屏里按一下，
// 醒来后页面已经悄悄翻了一张卡。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { KO_BTN_UP = 0, KO_BTN_DOWN, KO_BTN_OK } ko_btn_t;
typedef enum { KO_EV_PRESS = 0, KO_EV_CLICK, KO_EV_DOUBLE, KO_EV_LONG } ko_ev_t;

typedef enum {
    KO_KEY_NONE = 0,
    KO_KEY_UP,     // 上移 / 上一个
    KO_KEY_DOWN,   // 下移 / 下一个
    KO_KEY_OK,     // 确认 / 翻面 / 发音
    KO_KEY_BACK,   // 长按 OK：返回上一级
} ko_key_t;

// 唤醒那一下按键，其后续的 CLICK / DOUBLE / LONG 在这段时间内都被吞掉。
#define KO_WAKE_SWALLOW_MS 1500

typedef struct {
    bool swallowing;
    ko_btn_t swallow_btn;
    uint32_t swallow_since_ms;
} ko_input_t;

void ko_input_init(ko_input_t *input);

// screen_awake=false 表示屏幕已灭：本次事件只唤醒（*wake=true），返回 KO_KEY_NONE。
// wake 可以传 NULL。now_ms 用单调毫秒计数，回绕安全。
ko_key_t ko_input_map(ko_input_t *input, ko_btn_t btn, ko_ev_t ev, uint32_t now_ms,
                      bool screen_awake, bool *wake);
