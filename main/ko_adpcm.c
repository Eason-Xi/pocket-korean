// main/ko_adpcm.c —— IMA-ADPCM 解码。
//
// 差值用 ((2·delta + 1) · step) >> 3 一次算出，与 ffmpeg 的 adpcm_ima_wav（也是 WAV 文件里
// IMA ADPCM 的通行算法）逐点一致，所以发音包里的音频可以直接用 ffmpeg 独立验证。
// 注意它与教科书里"step>>3 加上按位的 step、step>>1、step>>2"的写法舍入不同，
// 相差 1~3 个最低位；编码器（tools/korean_audio.py）必须用同一个公式重建。
#include "ko_adpcm.h"

static const int16_t STEP_TABLE[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253,
    279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
    1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289,
    16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

static const int8_t INDEX_TABLE[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

int16_t ko_adpcm_init(ko_adpcm_state_t *state, const uint8_t header[4]) {
    state->predictor = (int16_t)((uint16_t)header[0] | ((uint16_t)header[1] << 8));
    state->index = header[2] > 88 ? 88 : header[2];
    return state->predictor;
}

int16_t ko_adpcm_step(ko_adpcm_state_t *state, uint8_t nibble) {
    const int step = STEP_TABLE[state->index];

    const int diff = ((2 * (nibble & 7) + 1) * step) >> 3;

    int predictor = state->predictor;
    predictor += (nibble & 8) ? -diff : diff;
    if (predictor > 32767) predictor = 32767;
    if (predictor < -32768) predictor = -32768;
    state->predictor = (int16_t)predictor;

    int index = state->index + INDEX_TABLE[nibble & 0x0F];
    if (index < 0) index = 0;
    if (index > 88) index = 88;
    state->index = (uint8_t)index;
    return state->predictor;
}

size_t ko_adpcm_decode(ko_adpcm_state_t *state, const uint8_t *in, size_t codes, int16_t *out) {
    for (size_t i = 0; i < codes; i++) {
        const uint8_t byte = in[i >> 1];
        const uint8_t nibble = (i & 1) ? (uint8_t)(byte >> 4) : (uint8_t)(byte & 0x0F);
        out[i] = ko_adpcm_step(state, nibble);
    }
    return codes;
}
