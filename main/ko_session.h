// main/ko_session.h —— 一轮卡片学习的队列逻辑（"Leitner 简化版"）。
// 纯逻辑，不依赖 ESP-IDF / LVGL。
//
// 规则：
//   * 一轮最多 KO_SESSION_MAX 张，优先挑熟练度盒子低、没看过的；
//   * "没记住"的卡隔 KO_REINSERT_GAP 张后再出现；同一张卡在一轮里最多被评
//     "没记住" KO_REPEAT_LIMIT 次，到了上限就放弃（计入"再看"），
//     避免一张难卡把整轮拖成死循环；
//   * 队列空了这一轮就结束。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KO_SESSION_MAX 10
#define KO_REPEAT_LIMIT 2
#define KO_REINSERT_GAP 3

typedef struct {
    uint16_t item[KO_SESSION_MAX];   // 条目在其类别数组中的序号；队首 = 当前卡
    uint8_t again[KO_SESSION_MAX];   // 与 item 平行：这张卡本轮已"没记住"几次
    uint8_t len;                     // 队列里还剩几张（含当前）
    uint8_t total;                   // 本轮总卡数
    uint8_t known;                   // 本轮已记住
    uint8_t unknown;                 // 本轮以"再看"收场（达到重复上限）
} ko_session_t;

// 从区间 [first, first+count) 建队列。states 是该类别的状态字节数组。
// rotate 用来轮转同优先级条目的起点（传学习轮次数），让每轮从不同的卡开始。
void ko_session_begin(ko_session_t *session, const uint8_t *states, uint16_t first,
                      uint16_t count, uint32_t rotate);

bool ko_session_active(const ko_session_t *session);
// 当前卡；队列为空时返回 0（调用前先看 ko_session_active）。
uint16_t ko_session_current(const ko_session_t *session);
// 已经终结的卡数（用于 "3/10" 这样的进度）。
uint8_t ko_session_done(const ko_session_t *session);

// 对当前卡评级。返回 true 表示这张卡本轮已终结（记住了，或达到重复上限）。
bool ko_session_rate(ko_session_t *session, bool known);
