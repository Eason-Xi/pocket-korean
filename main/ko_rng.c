// main/ko_rng.c —— xorshift32。
#include "ko_rng.h"

uint32_t ko_rng_next(uint32_t *state) {
    uint32_t x = *state ? *state : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

uint32_t ko_rng_below(uint32_t *state, uint32_t n) {
    if (n == 0) return 0;
    // 取高位再取模：低位的周期性比高位明显。取模偏差对 n <= 几百可以忽略。
    return (ko_rng_next(state) >> 8) % n;
}
