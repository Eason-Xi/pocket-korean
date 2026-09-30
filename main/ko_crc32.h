// main/ko_crc32.h —— CRC-32（IEEE 802.3，多项式 0xEDB88320，与 zlib 一致）。
// 进度存档和发音包头共用；纯逻辑，可在主机上测试。
#pragma once

#include <stddef.h>
#include <stdint.h>

// 一次性计算。ko_crc32("123456789", 9) == 0xCBF43926。
uint32_t ko_crc32(const void *data, size_t len);

// 增量计算：初值传 0xFFFFFFFF，逐块调用 update，最后对结果按位取反。
// （update 返回的是未取反的中间值，这样可以直接把上一次的返回值传回去。）
uint32_t ko_crc32_update(uint32_t crc, const void *data, size_t len);
