// main/ko_crc32.c —— 逐位计算的 CRC-32。
// 数据量很小（存档 ~260 字节，发音包索引 ~3 KB，只在开机/保存时算一次），
// 不值得为它放一张 1 KB 的查表。
#include "ko_crc32.h"

uint32_t ko_crc32_update(uint32_t crc, const void *data, size_t len) {
    const uint8_t *bytes = data;
    for (size_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return crc;
}

uint32_t ko_crc32(const void *data, size_t len) {
    return ~ko_crc32_update(0xFFFFFFFFu, data, len);
}
