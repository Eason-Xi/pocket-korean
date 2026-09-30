// tests/test_ko_audio.c —— ADPCM 解码、发音包解析、提示音合成。
//
// 夹具 ko_pack_fixture.h 由 tools/korean_audio.py 里的 Python 编码器 / 解码器生成，
// 这里用固件的 C 解码器去解并逐条比对 CRC：两个独立实现互相印证，
// 任何一边的算法或格式改动都会让这个测试失败。
#include <string.h>

#include "ko_adpcm.h"
#include "ko_crc32.h"
#include "ko_pack.h"
#include "ko_test.h"
#include "ko_tone.h"

#include "ko_pack_fixture.h"

typedef struct {
    const uint8_t *data;
    uint32_t size;
} mem_t;

static bool mem_read(void *ctx, uint32_t offset, void *dst, uint32_t len) {
    const mem_t *m = ctx;
    if (offset > m->size || len > m->size - offset) return false;
    memcpy(dst, m->data + offset, len);
    return true;
}

static ko_pack_reader_t reader_for(mem_t *m, uint32_t capacity) {
    ko_pack_reader_t r = { .read = mem_read, .ctx = m, .capacity = capacity };
    return r;
}

// 像播放任务那样分块解码：每块 8 字节 = 16 个码，最后一块可能是奇数个码。
static uint32_t decode_clip_crc(const mem_t *pack, const ko_pack_clip_t *clip, int16_t head[4]) {
    const uint8_t *stream = pack->data + clip->offset;
    ko_adpcm_state_t state;
    int16_t pcm[16 * 4];
    uint32_t crc = 0xFFFFFFFFu;

    size_t produced = 0;
    int16_t first = ko_adpcm_init(&state, stream);
    for (int i = 0; i < 4; i++) head[i] = 0;
    uint8_t le[2] = { (uint8_t)first, (uint8_t)((uint16_t)first >> 8) };
    crc = ko_crc32_update(crc, le, 2);
    head[0] = first;
    produced = 1;

    size_t codes_left = clip->samples - 1;
    const uint8_t *p = stream + 4;
    while (codes_left > 0) {
        const size_t codes = codes_left < 16 ? codes_left : 16;
        CHECK(ko_adpcm_decode(&state, p, codes, pcm) == codes);
        for (size_t i = 0; i < codes; i++) {
            uint8_t b[2] = { (uint8_t)pcm[i], (uint8_t)((uint16_t)pcm[i] >> 8) };
            crc = ko_crc32_update(crc, b, 2);
            if (produced + i < 4) head[produced + i] = pcm[i];
        }
        produced += codes;
        codes_left -= codes;
        p += (codes + 1) / 2;
    }
    CHECK(produced == clip->samples);
    return ~crc;
}

static void test_fixture_decodes_like_python(void) {
    mem_t pack = { KO_FIXTURE_PACK, (uint32_t)sizeof(KO_FIXTURE_PACK) };
    ko_pack_reader_t r = reader_for(&pack, pack.size);
    ko_pack_info_t info;
    CHECK(ko_pack_open(&r, &info) == KO_PACK_OK);
    CHECK(info.clip_count == KO_FIXTURE_CLIPS);
    CHECK(info.sample_rate == KO_PACK_SAMPLE_RATE);
    CHECK(info.total_size == pack.size);

    for (uint32_t id = 0; id < KO_FIXTURE_CLIPS; id++) {
        ko_pack_clip_t clip;
        const ko_fixture_expect_t *want = &KO_FIXTURE_EXPECT[id];
        if (want->samples == 0) {
            CHECK(!ko_pack_clip(&r, &info, id, &clip));   // 缺失项按"没有发音"处理
            continue;
        }
        CHECK(ko_pack_clip(&r, &info, id, &clip));
        CHECK(clip.samples == want->samples);
        int16_t head[4];
        CHECK(decode_clip_crc(&pack, &clip, head) == want->pcm_crc32);
        for (int i = 0; i < 4; i++) CHECK(head[i] == want->head[i]);
    }
    CHECK(!ko_pack_clip(&r, &info, KO_FIXTURE_CLIPS, &(ko_pack_clip_t){ 0 }));
    CHECK(!ko_pack_clip(&r, &info, 0xFFFFFFFFu, &(ko_pack_clip_t){ 0 }));
}

static void test_adpcm_robustness(void) {
    // 头里的步长索引越界：夹到 88，不能让后续查表越界（UBSan / ASan 会抓）。
    const uint8_t bad_header[4] = { 0x00, 0x10, 0xFF, 0x00 };
    ko_adpcm_state_t state;
    CHECK(ko_adpcm_init(&state, bad_header) == 0x1000);
    CHECK(state.index == 88);
    int16_t last = 0;
    for (int i = 0; i < 64; i++) last = ko_adpcm_step(&state, (uint8_t)(i & 0x0F));
    CHECK(state.index <= 88);
    (void)last;

    // 满幅方向来回翻转不会溢出 int16。
    const uint8_t loud[4] = { 0xFF, 0x7F, 88, 0 };   // predictor = 32767, index 最大
    ko_adpcm_init(&state, loud);
    for (int i = 0; i < 64; i++) {
        const int16_t v = ko_adpcm_step(&state, (i & 1) ? 0x0F : 0x07);
        CHECK(v >= -32768 && v <= 32767);
    }
    // 负方向的头。
    const uint8_t neg[4] = { 0x00, 0x80, 0, 0 };
    CHECK(ko_adpcm_init(&state, neg) == -32768);
}

// 按格式手工造一个包，索引由调用方给出；CRC 都是正确的，用来测"CRC 对但内容不合法"。
static uint32_t build_pack(uint8_t *out, size_t cap, uint32_t clip_count, uint32_t total_size,
                           const uint32_t (*entries)[3]) {
    memset(out, 0, cap);
    const uint32_t data_offset = KO_PACK_HEADER_BYTES + clip_count * KO_PACK_ENTRY_BYTES;
    for (uint32_t i = 0; i < clip_count; i++) {
        for (int f = 0; f < 3; f++) {
            for (int b = 0; b < 4; b++) {
                out[KO_PACK_HEADER_BYTES + i * KO_PACK_ENTRY_BYTES + f * 4 + b] =
                    (uint8_t)(entries[i][f] >> (8 * b));
            }
        }
    }
    const uint32_t index_crc = ko_crc32(out + KO_PACK_HEADER_BYTES, clip_count * KO_PACK_ENTRY_BYTES);
    memcpy(out, "KOPK", 4);
    out[4] = KO_PACK_VERSION;
    out[6] = KO_PACK_HEADER_BYTES;
#define PUT32(off, v) do { for (int b = 0; b < 4; b++) out[(off) + b] = (uint8_t)((v) >> (8 * b)); } while (0)
    PUT32(8, clip_count);
    PUT32(12, KO_PACK_SAMPLE_RATE);
    PUT32(16, KO_PACK_HEADER_BYTES);
    PUT32(20, data_offset);
    PUT32(24, total_size);
    PUT32(28, index_crc);
    const uint32_t header_crc = ko_crc32(out, 36);
    PUT32(36, header_crc);
#undef PUT32
    return data_offset;
}

static void test_pack_validation(void) {
    static uint8_t blob[256];
    mem_t pack = { blob, sizeof(blob) };
    ko_pack_reader_t r = reader_for(&pack, sizeof(blob));
    ko_pack_info_t info;

    // 合法的最小包：一条 10 字节的发音，数据在 40+12 = 52 起。
    const uint32_t good[1][3] = { { 0, 10, 13 } };   // 10 字节 = 4 头 + 6 数据 → 最多 1+12 = 13 个采样
    uint32_t data_offset = build_pack(blob, sizeof(blob), 1, 52 + 12, good);
    CHECK(data_offset == 52);
    CHECK(ko_pack_open(&r, &info) == KO_PACK_OK);
    ko_pack_clip_t clip;
    CHECK(ko_pack_clip(&r, &info, 0, &clip) && clip.offset == 52 && clip.bytes == 10 &&
          clip.samples == 13);

    // 采样数比字节数能编出的多：不合法。
    const uint32_t too_many[1][3] = { { 0, 10, 14 } };
    build_pack(blob, sizeof(blob), 1, 52 + 12, too_many);
    CHECK(ko_pack_open(&r, &info) == KO_PACK_BAD);
    // 条目越过数据区。
    const uint32_t overflow[1][3] = { { 8, 10, 13 } };
    build_pack(blob, sizeof(blob), 1, 52 + 12, overflow);
    CHECK(ko_pack_open(&r, &info) == KO_PACK_BAD);
    // 偏移 + 长度在 32 位上回绕：不能被绕过检查。
    const uint32_t wrap[1][3] = { { 0xFFFFFFF0u, 0x20, 13 } };
    build_pack(blob, sizeof(blob), 1, 52 + 12, wrap);
    CHECK(ko_pack_open(&r, &info) == KO_PACK_BAD);
    // 太短的条目（连头都装不下）。
    const uint32_t tiny[1][3] = { { 0, 3, 1 } };
    build_pack(blob, sizeof(blob), 1, 52 + 12, tiny);
    CHECK(ko_pack_open(&r, &info) == KO_PACK_BAD);
    // 缺失项（bytes = 0）合法，但只有 samples 也为 0 才行。
    const uint32_t missing[1][3] = { { 0, 0, 0 } };
    build_pack(blob, sizeof(blob), 1, 52, missing);
    CHECK(ko_pack_open(&r, &info) == KO_PACK_OK);
    CHECK(!ko_pack_clip(&r, &info, 0, &clip));
    const uint32_t ghost[1][3] = { { 0, 0, 5 } };
    build_pack(blob, sizeof(blob), 1, 52, ghost);
    CHECK(ko_pack_open(&r, &info) == KO_PACK_BAD);
    // total_size 大于分区容量：不能读越界。
    build_pack(blob, sizeof(blob), 1, 52 + 12, good);
    ko_pack_reader_t small = reader_for(&pack, 60);
    CHECK(ko_pack_open(&small, &info) == KO_PACK_BAD);
    // total_size 小于数据起点。
    build_pack(blob, sizeof(blob), 1, 40, good);
    CHECK(ko_pack_open(&r, &info) == KO_PACK_BAD);
}

static void test_pack_header_corruption(void) {
    static uint8_t blob[sizeof(KO_FIXTURE_PACK)];
    memcpy(blob, KO_FIXTURE_PACK, sizeof(blob));
    mem_t pack = { blob, sizeof(blob) };
    ko_pack_reader_t r = reader_for(&pack, sizeof(blob));
    ko_pack_info_t info;
    CHECK(ko_pack_open(&r, &info) == KO_PACK_OK);

    // 头里任何一个字节被改都必须被发现（魔数被改成非 0xFF 也是"损坏"而不是"未安装"）。
    for (int i = 0; i < KO_PACK_HEADER_BYTES; i++) {
        blob[i] ^= 0x01;
        CHECK(ko_pack_open(&r, &info) == KO_PACK_BAD);
        blob[i] ^= 0x01;
    }
    // 索引区任何一个字节被改都必须被发现。
    for (uint32_t i = KO_PACK_HEADER_BYTES; i < KO_PACK_HEADER_BYTES + KO_FIXTURE_CLIPS * KO_PACK_ENTRY_BYTES; i++) {
        blob[i] ^= 0x01;
        CHECK(ko_pack_open(&r, &info) == KO_PACK_BAD);
        blob[i] ^= 0x01;
    }
    // 数据区不做整包校验（开机要读 3 MB 太慢），改动不影响 open；文档里写明了这一点。
    blob[sizeof(blob) - 1] ^= 0xFF;
    CHECK(ko_pack_open(&r, &info) == KO_PACK_OK);

    // 分区是擦除态：属于正常的"未安装"。
    memset(blob, 0xFF, sizeof(blob));
    CHECK(ko_pack_open(&r, &info) == KO_PACK_EMPTY);
    // 全 0 或随机内容：不是擦除态，也不是合法包，是"损坏"。
    memset(blob, 0x00, sizeof(blob));
    CHECK(ko_pack_open(&r, &info) == KO_PACK_BAD);
    // 容量比头还小 / 读回调失败。
    ko_pack_reader_t tiny = reader_for(&pack, 8);
    CHECK(ko_pack_open(&tiny, &info) == KO_PACK_BAD);
    mem_t none = { blob, 0 };
    ko_pack_reader_t failing = reader_for(&none, 4096);
    CHECK(ko_pack_open(&failing, &info) == KO_PACK_BAD);
}

static void test_tone(void) {
    const uint32_t rate = 16000;
    CHECK(ko_tone_total_samples(&KO_TONE_RIGHT, rate) == (90 + 150) * 16);
    CHECK(ko_tone_total_samples(&KO_TONE_WRONG, rate) == 260 * 16);

    // 一次性渲染 vs 小块渲染：结果必须完全一致（播放任务分块喂 I2S）。
    static int16_t whole[8000], parts[8000];
    ko_tone_state_t st;
    ko_tone_start(&st, &KO_TONE_RIGHT);
    const size_t total = ko_tone_render(&st, rate, 12000, whole, 8000);
    CHECK(total == 3840);
    CHECK(ko_tone_render(&st, rate, 12000, whole, 8000) == 0);   // 播完后返回 0

    ko_tone_start(&st, &KO_TONE_RIGHT);
    size_t got = 0;
    for (;;) {
        const size_t n = ko_tone_render(&st, rate, 12000, parts + got, 37);
        if (n == 0) break;
        got += n;
    }
    CHECK(got == total);
    CHECK(memcmp(whole, parts, total * sizeof(int16_t)) == 0);

    // 幅度不超过设定，且起音 / 收音从 0 开始 0 结束（没有爆音）。
    int peak = 0;
    for (size_t i = 0; i < total; i++) {
        const int v = whole[i] < 0 ? -whole[i] : whole[i];
        if (v > peak) peak = v;
    }
    CHECK(peak <= 12000 && peak > 11000);
    CHECK(whole[0] == 0);
    CHECK(whole[total - 1] > -400 && whole[total - 1] < 400);

    // 第一个音符 880 Hz、90 ms：过零次数约 2 * 880 * 0.09 ≈ 158。
    int crossings = 0;
    for (size_t i = 1; i < 90 * 16; i++) crossings += (whole[i - 1] < 0) != (whole[i] < 0);
    CHECK(crossings > 140 && crossings < 175);

    // 幅度参数缩放输出。
    ko_tone_start(&st, &KO_TONE_TICK);
    static int16_t quiet[2000];
    const size_t n = ko_tone_render(&st, rate, 1000, quiet, 2000);
    int quiet_peak = 0;
    for (size_t i = 0; i < n; i++) {
        const int v = quiet[i] < 0 ? -quiet[i] : quiet[i];
        if (v > quiet_peak) quiet_peak = v;
    }
    CHECK(quiet_peak <= 1000 && quiet_peak > 900);

    // 静音音符输出全 0。
    static const ko_tone_note_t gap[] = { { 0, 10 } };
    const ko_tone_seq_t silent = { gap, 1 };
    ko_tone_start(&st, &silent);
    int16_t z[400];
    CHECK(ko_tone_render(&st, rate, 12000, z, 400) == 160);
    for (int i = 0; i < 160; i++) CHECK(z[i] == 0);
}

int main(void) {
    test_fixture_decodes_like_python();
    test_adpcm_robustness();
    test_pack_validation();
    test_pack_header_corruption();
    test_tone();
    puts("test_ko_audio: PASS");
    return 0;
}
