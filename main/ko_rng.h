// main/ko_rng.h —— 可注入种子的伪随机数（xorshift32），让测验出题在主机测试里可复现。
// 不是密码学随机数；只用来打乱选项、挑选题目。
#pragma once

#include <stdint.h>

// state 不能为 0；传 0 会被替换成固定的非零种子。
uint32_t ko_rng_next(uint32_t *state);

// [0, n) 内的整数；n 为 0 时返回 0。
uint32_t ko_rng_below(uint32_t *state, uint32_t n);
