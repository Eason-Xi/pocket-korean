// main/ko_adpcm.h —— IMA-ADPCM（4 bit / 采样）解码。纯逻辑，可在主机上测试。
//
// 为什么用 ADPCM：16 kHz 单声道 16 bit PCM 每秒 32 KB，250 多条发音要十几 MB，
// 放不进 8 MB Flash；4 bit ADPCM 每秒约 8 KB，整套只要 3 MB 左右，
// 解码几乎不占 CPU 和内存（参见 docs/reference/shinku-chen/audio-compression-trade-offs.md）。
//
// 流格式：每条发音以 4 字节头开始（预测值 int16 小端 + 步长索引 uint8 + 保留 1 字节），
// 头里的预测值就是第 0 个采样；后面每字节含两个 4 bit 码，低半字节在前。
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t predictor;
    uint8_t index;   // 0..88
} ko_adpcm_state_t;

// 从 4 字节头初始化解码状态；返回第 0 个采样（即预测值）。
// 步长索引越界时夹到合法范围，脏数据不会让后续解码读表越界。
int16_t ko_adpcm_init(ko_adpcm_state_t *state, const uint8_t header[4]);

// 解一个 4 bit 码，返回对应采样。
int16_t ko_adpcm_step(ko_adpcm_state_t *state, uint8_t nibble);

// 把 `codes` 个 4 bit 码（每字节两个，低半字节在前）解成采样写入 out，返回写出的采样数。
// in 至少要有 (codes + 1) / 2 字节。
size_t ko_adpcm_decode(ko_adpcm_state_t *state, const uint8_t *in, size_t codes, int16_t *out);
